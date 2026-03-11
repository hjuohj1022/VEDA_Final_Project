#include "mqtt.h"
#include "../device/spi.h"
#include "../device/frame_link.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_mqtt_client_handle_t s_client         = NULL;
static bool                     s_mqtt_connected = false;

static void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    const esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)handler_args;
    (void)base;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        s_mqtt_connected = true;
        (void)esp_mqtt_client_subscribe(event->client, "test/topic", 1);
        (void)esp_mqtt_client_publish(s_client, "lepton/status", "Lepton ready", 12, 1, 0);
        (void)printf("MQTT connected: SPI frame-link mode, frame topic qos=0, cmd topic qos=1\n");
        (void)printf("Heap after MQTT connect: free=%lu min=%lu\n",
                     (unsigned long)esp_get_free_heap_size(),
                     (unsigned long)esp_get_minimum_free_heap_size());
        break;
    }

    case MQTT_EVENT_DATA: {
        (void)printf("Topic: %.*s\n", (int)event->topic_len, event->topic);
        (void)printf("Data: %.*s\n", (int)event->data_len, event->data);

        if (spi_cmd_queue != NULL) {
            spi_cmd_t cmd = {0};
            cmd.len = (event->data_len > (int)MAX_SPI_DATA_LEN)
                      ? (uint8_t)MAX_SPI_DATA_LEN
                      : (uint8_t)event->data_len;
            (void)memcpy(cmd.data, event->data, (size_t)cmd.len);

            if (xQueueSend(spi_cmd_queue, &cmd, pdMS_TO_TICKS(100U)) != pdPASS) {
                (void)printf("SPI queue full, command dropped: '%.*s'\n", (int)cmd.len, (const char *)cmd.data);
            } else {
                (void)printf("Queued to SPI: '%.*s'\n", (int)cmd.len, (const char *)cmd.data);
            }
        } else {
            (void)printf("SPI command bridge disabled, command ignored\n");
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED: {
        s_mqtt_connected = false;
        (void)printf("MQTT disconnected, reconnecting...\n");
        (void)esp_mqtt_client_reconnect(s_client);
        break;
    }

    case MQTT_EVENT_ERROR: {
        (void)printf("MQTT error occurred\n");
        if (event->error_handle != NULL) {
            (void)printf("Error type: %d\n", (int)event->error_handle->error_type);
        }
        break;
    }

    default: {
        break;
    }
    }
}

esp_mqtt_client_handle_t mqttClient(void)
{
    if (s_client != NULL) {
        return s_client;
    }

    const esp_mqtt_client_config_t cfg = {
        .broker.address.uri                               = "mqtts://192.168.55.200:8883",
        .buffer.size                                      = 4096,
        .buffer.out_size                                  = 4096,
        .network.timeout_ms                               = 10000,
        .network.reconnect_timeout_ms                     = 5000,
        .session.keepalive                                = 60,
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
    const uint16_t total_chunks = (uint16_t)((FRAME_BYTES + CHUNK_PAYLOAD_SIZE - 1U) / CHUNK_PAYLOAD_SIZE);
    (void)arg;

    for (;;) {
        const uint8_t *fb = NULL;
        uint16_t frame_id = 0U;
        int buffer_idx = -1;

        if ((s_mqtt_connected == false) || (s_client == NULL)) {
            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }

        if (!frameLinkAcquireReadyFrame(&fb, &frame_id, &buffer_idx)) {
            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }

        const uint16_t min_val = 7000U;
        const uint16_t max_val = 10000U;
        bool send_ok = true;
        int failed_chunk = -1;

        (void)printf("Sending frame id=%u from buffer=%d\n", (unsigned int)frame_id, buffer_idx);

        for (uint16_t i = 0U; i < total_chunks; i++) {
            if (s_mqtt_connected == false) {
                send_ok = false;
                failed_chunk = (int)i;
                break;
            }

            const size_t offset = (size_t)i * (size_t)CHUNK_PAYLOAD_SIZE;
            const size_t data_size = ((FRAME_BYTES - offset) > (size_t)CHUNK_PAYLOAD_SIZE)
                                   ? (size_t)CHUNK_PAYLOAD_SIZE
                                   : (FRAME_BYTES - offset);

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
            (void)memcpy(&msg[CHUNK_HEADER_SIZE], &fb[offset], data_size);

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
                vTaskDelay(pdMS_TO_TICKS(10U));
                retry_count++;
            }

            if (ret < 0) {
                send_ok = false;
                failed_chunk = (int)i;
                break;
            }

            if ((i == 0U) || (((i + 1U) % 8U) == 0U) || ((i + 1U) == total_chunks)) {
                (void)printf("Frame chunk progress: id=%u chunk=%u/%u\n",
                             (unsigned int)frame_id,
                             (unsigned int)(i + 1U),
                             (unsigned int)total_chunks);
            }

            vTaskDelay(pdMS_TO_TICKS(10U));
        }

        if (send_ok) {
            (void)printf("Frame sent OK: id=%u\n", (unsigned int)frame_id);
        } else {
            (void)printf("Frame send incomplete: id=%u failed_chunk=%d/%u\n",
                         (unsigned int)frame_id,
                         failed_chunk + 1,
                         (unsigned int)total_chunks);
        }

        frameLinkReleaseReadyFrame(buffer_idx);
        vTaskDelay(pdMS_TO_TICKS(20U));
    }
}
