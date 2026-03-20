#include "mqtt.h"
#include "udp_stream.h"
#include "wifi.h"
#include "../device/frame_link.h"
#include "../device/cmd_uart.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if defined(__has_include)
#if __has_include("../app_secrets.h")
#include "../app_secrets.h"
#else
#include "../app_secrets.defaults.h"
#endif
#else
#include "../app_secrets.defaults.h"
#endif

static esp_mqtt_client_handle_t s_client          = NULL;
static bool                     s_mqtt_connected  = false;
static int64_t                  s_last_connect_us = 0;
static int64_t                  s_next_health_publish_us = 0;

#define MQTT_FRAME_CHUNK_RETRY_DELAY_MS  25U
#define MQTT_FRAME_CHUNK_DELAY_MS        2U
#define MQTT_FRAME_BACKOFF_DELAY_MS      0U
#define MQTT_FRAME_IDLE_DELAY_MS         10U
#define UDP_FAST_FRAME_IDLE_DELAY_MS     1U
#define UDP_CONGESTED_FRAME_DELAY_MS     12U
#define UDP_FAST_FRAME_YIELD_TICKS       1U
#define UDP_FAST_CHUNK_YIELD_INTERVAL    0U
#define UDP_CONGESTED_CHUNK_YIELD_INTERVAL 4U
#define MQTT_HEALTH_TASK_POLL_MS         250U
#define MQTT_HEALTH_PERIOD_US            (60000000LL)
#define FRAME_PIXEL_COUNT                (FRAME_BYTES / 2U)
#define THERMAL_VALID_MIN_RAW            1000U
#define THERMAL_VALID_MAX_RAW            30000U
#define THERMAL_MIN_SPAN_RAW             100U

static TickType_t delayTicksAtLeast1(uint32_t delay_ms)
{
    const TickType_t ticks = pdMS_TO_TICKS(delay_ms);
    return (ticks > 0U) ? ticks : 1U;
}

static bool frameStreamUseMqtt(void)
{
    return (APP_FRAME_STREAM_MODE == FRAME_STREAM_MODE_MQTT_ONLY) ||
           (APP_FRAME_STREAM_MODE == FRAME_STREAM_MODE_BOTH);
}

static bool frameStreamUseUdp(void)
{
    return ((APP_FRAME_STREAM_MODE == FRAME_STREAM_MODE_UDP_ONLY) ||
            (APP_FRAME_STREAM_MODE == FRAME_STREAM_MODE_BOTH) ||
            (APP_FRAME_STREAM_MODE == FRAME_STREAM_MODE_UDP_FRAME_MQTT_CONTROL)) &&
           udpStreamIsEnabled();
}

static bool frameStreamUseUdpFast8(void)
{
    return ((APP_FRAME_STREAM_MODE == FRAME_STREAM_MODE_UDP_ONLY) ||
            (APP_FRAME_STREAM_MODE == FRAME_STREAM_MODE_UDP_FRAME_MQTT_CONTROL)) &&
           (APP_UDP_FRAME_8BIT != 0) &&
           udpStreamIsEnabled();
}

static uint16_t frameReadPixelBe(const uint8_t *frame_buf, size_t pixel_idx)
{
    const size_t offset = pixel_idx * 2U;

    return (uint16_t)(((uint16_t)frame_buf[offset] << 8U) | (uint16_t)frame_buf[offset + 1U]);
}

static void frameComputeRange(const uint8_t *frame_buf, uint16_t *min_val, uint16_t *max_val)
{
    uint16_t min_pixel = 0xFFFFU;
    uint16_t max_pixel = 0U;
    bool has_valid_pixel = false;

    for (size_t pixel_idx = 0U; pixel_idx < FRAME_PIXEL_COUNT; pixel_idx++) {
        const uint16_t pixel = frameReadPixelBe(frame_buf, pixel_idx);

        if ((pixel <= THERMAL_VALID_MIN_RAW) || (pixel >= THERMAL_VALID_MAX_RAW)) {
            continue;
        }

        has_valid_pixel = true;
        if (pixel < min_pixel) {
            min_pixel = pixel;
        }
        if (pixel > max_pixel) {
            max_pixel = pixel;
        }
    }

    if (!has_valid_pixel) {
        min_pixel = 7000U;
        max_pixel = 10000U;
    } else if ((max_pixel - min_pixel) < THERMAL_MIN_SPAN_RAW) {
        max_pixel = min_pixel + THERMAL_MIN_SPAN_RAW;
    }

    *min_val = min_pixel;
    *max_val = max_pixel;
}

static void framePack8BitChunk(uint8_t *dst,
                               const uint8_t *frame_buf,
                               size_t pixel_offset,
                               size_t pixel_count,
                               uint16_t min_val,
                               uint16_t max_val)
{
    const uint32_t range = (max_val > min_val) ? (uint32_t)(max_val - min_val) : 1U;

    for (size_t i = 0U; i < pixel_count; i++) {
        uint16_t pixel = frameReadPixelBe(frame_buf, pixel_offset + i);

        if (pixel < min_val) {
            pixel = min_val;
        } else if (pixel > max_val) {
            pixel = max_val;
        }

        dst[i] = (uint8_t)((((uint32_t)(pixel - min_val)) * 255U) / range);
    }
}

static bool mqttTopicEquals(const esp_mqtt_event_handle_t event, const char *topic)
{
    const size_t topic_len = strlen(topic);

    return (event->topic_len == (int)topic_len) &&
           (strncmp(event->topic, topic, topic_len) == 0);
}

static bool mqttPayloadEquals(const esp_mqtt_event_handle_t event, const char *payload)
{
    const size_t payload_len = strlen(payload);

    return (event->data_len == (int)payload_len) &&
           (strncmp(event->data, payload, payload_len) == 0);
}

static bool mqttQueueStmCommand(const char *command, size_t command_len)
{
    cmd_uart_msg_t cmd = {0};

    if ((command == NULL) || (command_len == 0U)) {
        return false;
    }

    if (g_cmd_uart_queue == NULL) {
        (void)printf("STM32 UART bridge unavailable, command ignored\n");
        return false;
    }

    cmd.len = (command_len > (size_t)(CMD_MAX_LEN - 1))
              ? (uint8_t)(CMD_MAX_LEN - 1)
              : (uint8_t)command_len;
    (void)memcpy(cmd.data, command, (size_t)cmd.len);
    cmd.data[cmd.len] = '\0';

    if (xQueueSend(g_cmd_uart_queue, &cmd, pdMS_TO_TICKS(100U)) != pdPASS) {
        (void)printf("STM32 UART queue full, command dropped: '%.*s'\n",
                     (int)cmd.len,
                     (const char *)cmd.data);
        return false;
    }

    (void)printf("Queued to STM32 UART: '%.*s'\n",
                 (int)cmd.len,
                 (const char *)cmd.data);
    return true;
}

static const char *mqttResolveLaserCommand(const esp_mqtt_event_handle_t event)
{
    if (mqttPayloadEquals(event, "laser on") ||
        mqttPayloadEquals(event, "LASER ON") ||
        mqttPayloadEquals(event, "on") ||
        mqttPayloadEquals(event, "ON")) {
        return "LASER ON";
    }

    if (mqttPayloadEquals(event, "laser off") ||
        mqttPayloadEquals(event, "LASER OFF") ||
        mqttPayloadEquals(event, "off") ||
        mqttPayloadEquals(event, "OFF")) {
        return "LASER OFF";
    }

    return NULL;
}

static bool mqttBuildHealthPayload(char *payload, size_t payload_size)
{
    frame_link_stats_t stats = {0};
    wifi_ap_record_t ap_info = {0};
    const bool wifi_ok = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    const int rssi = wifi_ok ? (int)ap_info.rssi : -127;
    const uint32_t cmd_depth = cmdUartGetQueueDepth();

    if ((payload == NULL) || (payload_size == 0U)) {
        return false;
    }

    frameLinkGetStats(&stats);
    (void)snprintf(payload,
                   payload_size,
                   "{\"uptime_sec\":%lu,\"wifi_connected\":%s,\"wifi_rssi\":%d,"
                   "\"mqtt_connected\":%s,\"free_heap\":%lu,\"min_heap\":%lu,"
                   "\"cmd_queue_depth\":%lu,\"frame_packets\":%lu,\"frame_completed\":%lu,"
                   "\"frame_timeouts\":%lu,\"frame_errors\":%lu,\"bad_magic\":%lu,"
                   "\"bad_checksum\":%lu,\"bad_len\":%lu,\"seq_errors\":%lu,"
                   "\"queue_full_drops\":%lu,\"stale_frame_drops\":%lu,\"frame_ready\":%u}",
                   (unsigned long)(esp_timer_get_time() / 1000000ULL),
                   wifi_ok ? "true" : "false",
                   rssi,
                   s_mqtt_connected ? "true" : "false",
                   (unsigned long)esp_get_free_heap_size(),
                   (unsigned long)esp_get_minimum_free_heap_size(),
                   (unsigned long)cmd_depth,
                   (unsigned long)stats.total_packets,
                   (unsigned long)stats.completed_frames,
                   (unsigned long)stats.spi_timeouts,
                   (unsigned long)stats.spi_errors,
                   (unsigned long)stats.bad_magic,
                   (unsigned long)stats.bad_checksum,
                   (unsigned long)stats.bad_payload_len,
                   (unsigned long)stats.seq_errors,
                   (unsigned long)stats.queue_full_drops,
                   (unsigned long)stats.stale_frame_drops,
                   (unsigned int)stats.frame_ready);
    return true;
}

static void mqttPublishHealthSnapshot(const char *reason)
{
    char payload[320];

    if (!mqttBuildHealthPayload(payload, sizeof(payload))) {
        return;
    }

    mqttPublishText(HEALTH_TOPIC, payload, 0);
    if (reason != NULL) {
        (void)printf("Health publish (%s): %s\n", reason, payload);
    } else {
        (void)printf("Health publish: %s\n", payload);
    }
}

static void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    const esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)handler_args;
    (void)base;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        s_mqtt_connected = true;
        s_last_connect_us = esp_timer_get_time();
        s_next_health_publish_us = s_last_connect_us + MQTT_HEALTH_PERIOD_US;
        (void)esp_mqtt_client_subscribe(event->client, CMD_TOPIC, 1);
        (void)esp_mqtt_client_subscribe(event->client, LASER_CMD_TOPIC, 1);
        (void)esp_mqtt_client_subscribe(event->client, HEALTH_CONTROL_TOPIC, 1);
        (void)esp_mqtt_client_publish(s_client, "lepton/status", "Lepton ready", 12, 1, 0);
        (void)printf("MQTT connected: SPI frame-link mode, frame topic qos=0, cmd/control topic qos=1\n");
        (void)printf("Heap after MQTT connect: free=%lu min=%lu\n",
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)esp_get_minimum_free_heap_size());
        break;
    }

    case MQTT_EVENT_DATA: {
        (void)printf("Topic: %.*s\n", (int)event->topic_len, event->topic);
        (void)printf("Data: %.*s\n", (int)event->data_len, event->data);

        if (mqttTopicEquals(event, HEALTH_CONTROL_TOPIC)) {
            if (mqttPayloadEquals(event, HEALTH_CONTROL_CMD) ||
                mqttPayloadEquals(event, "cmd=publish_status_now")) {
                mqttPublishHealthSnapshot("request");
            } else {
                (void)printf("Ignoring unsupported system/control command\n");
            }
            break;
        }

        if (mqttTopicEquals(event, LASER_CMD_TOPIC)) {
            const char *laser_cmd = mqttResolveLaserCommand(event);

            if (laser_cmd == NULL) {
                (void)printf("Ignoring unsupported laser/control command\n");
            } else {
                (void)mqttQueueStmCommand(laser_cmd, strlen(laser_cmd));
            }
            break;
        }

        if (!mqttTopicEquals(event, CMD_TOPIC)) {
            break;
        }

        (void)mqttQueueStmCommand(event->data, (size_t)event->data_len);
        break;
    }

    case MQTT_EVENT_DISCONNECTED: {
        s_mqtt_connected = false;
        s_next_health_publish_us = 0;
        (void)printf("MQTT disconnected\n");
        break;
    }

    case MQTT_EVENT_ERROR: {
        (void)printf("MQTT error occurred\n");
        if (event->error_handle != NULL) {
            (void)printf("Error type: %d\n", (int)event->error_handle->error_type);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                (void)printf("TLS last esp err=0x%x tls_stack_err=0x%x sock_errno=%d\n",
                             (unsigned int)event->error_handle->esp_tls_last_esp_err,
                             (unsigned int)event->error_handle->esp_tls_stack_err,
                             (int)event->error_handle->esp_transport_sock_errno);
            }
        }
        break;
    }

    default: {
        break;
    }
    }
}

bool mqttIsConnected(void)
{
    return s_mqtt_connected;
}

void mqttPublishText(const char *topic, const char *payload, int qos)
{
    if ((s_client != NULL) && s_mqtt_connected && (topic != NULL) && (payload != NULL)) {
        (void)esp_mqtt_client_publish(s_client, topic, payload, 0, qos, 0);
    }
}

esp_mqtt_client_handle_t mqttClient(void)
{
    const int mqtt_buffer_size = frameStreamUseMqtt() ? 4096 : 1024;
    const int mqtt_out_buffer_size = frameStreamUseMqtt() ? 4096 : 1024;

    if (s_client != NULL) {
        return s_client;
    }

    if (strlen(APP_MQTT_BROKER_URI) == 0U) {
        (void)printf("MQTT broker URI missing. Update main/app_secrets.h\n");
        return NULL;
    }

    (void)printf("MQTT client init: free=%lu min=%lu\n",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
    (void)printf("MQTT client init: largest_8bit=%lu buffer=%d out_buffer=%d\n",
                 (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                 mqtt_buffer_size,
                 mqtt_out_buffer_size);

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri                               = APP_MQTT_BROKER_URI,
        .buffer.size                                      = mqtt_buffer_size,
        .buffer.out_size                                  = mqtt_out_buffer_size,
        .network.timeout_ms                               = 15000,
        .network.reconnect_timeout_ms                     = 8000,
        .session.keepalive                                = 90,
        .broker.verification.certificate                  = ca_cert_pem,
        .broker.verification.skip_cert_common_name_check  = true,
        .credentials.authentication.certificate           = client_cert_pem,
        .credentials.authentication.key                   = client_key_pem,
    };
    s_client = esp_mqtt_client_init(&cfg);
    (void)esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqttEventHandler, NULL);
    (void)esp_mqtt_client_start(s_client);
    return s_client;
}

void mqttFrameTask(void *arg)
{
    uint8_t msg[CHUNK_MSG_SIZE];
    (void)arg;

    for (;;) {
        const uint8_t *fb = NULL;
        uint16_t frame_id = 0U;
        int buffer_idx = -1;

        const bool use_mqtt = frameStreamUseMqtt();
        const bool use_udp = frameStreamUseUdp();
        const bool use_udp_fast8 = frameStreamUseUdpFast8();
        const bool udp_congested = use_udp && udpStreamIsCongested();
        const uint16_t udp_chunk_yield_interval = udp_congested
                                                  ? UDP_CONGESTED_CHUNK_YIELD_INTERVAL
                                                  : UDP_FAST_CHUNK_YIELD_INTERVAL;
        const size_t chunk_payload_size = use_udp ? (size_t)UDP_CHUNK_PAYLOAD_SIZE
                                                  : (size_t)CHUNK_PAYLOAD_SIZE;
        const bool drop_stale_frames = use_udp_fast8;
        const size_t frame_payload_bytes = use_udp_fast8 ? FRAME_PIXEL_COUNT : FRAME_BYTES;
        const uint16_t total_chunks =
            (uint16_t)((frame_payload_bytes + chunk_payload_size - 1U) / chunk_payload_size);
        const bool verbose_frame_log = use_mqtt;
        const TickType_t idle_delay_ticks = delayTicksAtLeast1(use_udp_fast8
                                                               ? (udp_congested
                                                                      ? UDP_CONGESTED_FRAME_DELAY_MS
                                                                      : UDP_FAST_FRAME_IDLE_DELAY_MS)
                                                               : MQTT_FRAME_IDLE_DELAY_MS);

        if (!use_mqtt && !use_udp) {
            vTaskDelay(idle_delay_ticks);
            continue;
        }

        if (use_udp && !wifiIsConnected()) {
            vTaskDelay(idle_delay_ticks);
            continue;
        }

        if (use_udp_fast8 && udp_congested) {
            vTaskDelay(idle_delay_ticks);
            continue;
        }

        if (use_mqtt && ((s_mqtt_connected == false) || (s_client == NULL))) {
            vTaskDelay(idle_delay_ticks);
            continue;
        }

        if (use_mqtt && ((esp_timer_get_time() - s_last_connect_us) < 1000000LL)) {
            vTaskDelay(delayTicksAtLeast1(MQTT_FRAME_IDLE_DELAY_MS));
            continue;
        }

        if (!frameLinkAcquireReadyFrame(&fb, &frame_id, &buffer_idx, drop_stale_frames)) {
            vTaskDelay(idle_delay_ticks);
            continue;
        }

        uint16_t min_val = 7000U;
        uint16_t max_val = 10000U;
        bool send_ok = true;
        int failed_chunk = -1;

        if (use_udp_fast8) {
            frameComputeRange(fb, &min_val, &max_val);
        }

        if (verbose_frame_log) {
            (void)printf("Sending frame id=%u from buffer=%d\n", (unsigned int)frame_id, buffer_idx);
        }

        for (uint16_t i = 0U; i < total_chunks; i++) {
            if (use_mqtt && (s_mqtt_connected == false)) {
                send_ok = false;
                failed_chunk = (int)i;
                break;
            }

            const size_t offset = (size_t)i * chunk_payload_size;
            const size_t bytes_remaining = frame_payload_bytes - offset;
            const size_t data_size = (bytes_remaining > chunk_payload_size)
                                   ? chunk_payload_size
                                   : bytes_remaining;

            msg[0] = (uint8_t)((frame_id >> 8) & 0xFFU);
            msg[1] = (uint8_t)(frame_id & 0xFFU);
            msg[2] = (uint8_t)((i >> 8) & 0xFFU);
            msg[3] = (uint8_t)(i & 0xFFU);
            msg[4] = (uint8_t)((total_chunks >> 8) & 0xFFU);
            msg[5] = (uint8_t)(total_chunks & 0xFFU);
            msg[6] = (uint8_t)((min_val >> 8) & 0xFFU);
            msg[7] = (uint8_t)(min_val & 0xFFU);
            msg[8] = (uint8_t)((max_val >> 8) & 0xFFU);
            msg[9] = (uint8_t)(max_val & 0xFFU);
            if (use_udp_fast8) {
                framePack8BitChunk(&msg[CHUNK_HEADER_SIZE], fb, offset, data_size, min_val, max_val);
            } else {
                (void)memcpy(&msg[CHUNK_HEADER_SIZE], &fb[offset], data_size);
            }

            if (use_mqtt) {
                int32_t retry_count = 0;
                int32_t ret = -1;
                while (retry_count < 3) {
                    ret = (int32_t)esp_mqtt_client_publish(s_client,
                                                           CHUNK_TOPIC,
                                                           (const char *)msg,
                                                           (int)(CHUNK_HEADER_SIZE + data_size),
                                                           0,
                                                           0);
                    if (ret >= 0) {
                        break;
                    }

                    (void)printf("Chunk send failed: frame=%u chunk=%u ret=%d retry=%d/3\n",
                                 (unsigned int)frame_id,
                                 (unsigned int)i,
                                 (int)ret,
                                 (int)(retry_count + 1));
                    vTaskDelay(delayTicksAtLeast1(MQTT_FRAME_CHUNK_RETRY_DELAY_MS));
                    retry_count++;
                }

                if (ret < 0) {
                    send_ok = false;
                    failed_chunk = (int)i;
                    break;
                }
            }

            if (use_udp) {
                const bool dtls_connected_once = udpStreamHasConnectedOnce();
                const int udp_sent = udpStreamSend(msg, CHUNK_HEADER_SIZE + data_size);
                if ((udp_sent < 0) && dtls_connected_once) {
                    (void)printf("UDP send failed: frame=%u chunk=%u len=%u\n",
                                 (unsigned int)frame_id,
                                 (unsigned int)i,
                                 (unsigned int)(CHUNK_HEADER_SIZE + data_size));
                    send_ok = false;
                    failed_chunk = (int)i;
                    break;
                }
                if (use_udp_fast8 &&
                    (udp_chunk_yield_interval > 0U) &&
                    ((((uint16_t)(i + 1U)) % udp_chunk_yield_interval) == 0U)) {
                    vTaskDelay(UDP_FAST_FRAME_YIELD_TICKS);
                }
            }

            if (verbose_frame_log && ((i == 0U) || ((i + 1U) == total_chunks))) {
                (void)printf("Frame chunk progress: id=%u chunk=%u/%u\n",
                             (unsigned int)frame_id,
                             (unsigned int)(i + 1U),
                             (unsigned int)total_chunks);
            }

            if ((MQTT_FRAME_CHUNK_DELAY_MS > 0U) && !use_udp_fast8) {
                vTaskDelay(delayTicksAtLeast1(MQTT_FRAME_CHUNK_DELAY_MS));
            }
        }

        if (send_ok && verbose_frame_log) {
            (void)printf("Frame sent OK: id=%u\n", (unsigned int)frame_id);
        } else if (!send_ok) {
            (void)printf("Frame send incomplete: id=%u failed_chunk=%d/%u\n",
                         (unsigned int)frame_id,
                         failed_chunk + 1,
                         (unsigned int)total_chunks);
        }

        frameLinkReleaseReadyFrame(buffer_idx);
        if (use_udp_fast8) {
            // Let IDLE run often enough to satisfy the task watchdog under sustained UDP load.
            vTaskDelay(udpStreamIsCongested()
                           ? delayTicksAtLeast1(UDP_CONGESTED_FRAME_DELAY_MS)
                           : UDP_FAST_FRAME_YIELD_TICKS);
        } else if (MQTT_FRAME_BACKOFF_DELAY_MS > 0U) {
            vTaskDelay(delayTicksAtLeast1(MQTT_FRAME_BACKOFF_DELAY_MS));
        }
    }
}

void mqttHealthTask(void *arg)
{
    (void)arg;

    for (;;) {
        if (s_mqtt_connected &&
            (s_next_health_publish_us != 0) &&
            (esp_timer_get_time() >= s_next_health_publish_us)) {
            mqttPublishHealthSnapshot("periodic");
            s_next_health_publish_us += MQTT_HEALTH_PERIOD_US;
        }

        vTaskDelay(pdMS_TO_TICKS(MQTT_HEALTH_TASK_POLL_MS));
    }
}
