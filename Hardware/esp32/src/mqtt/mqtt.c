#include "mqtt.h"
#include "../device/spi.h"
#include "../device/uart.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_mqtt_client_handle_t s_client        = NULL;
static bool                     s_mqtt_connected = false;

static void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    const esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void)handler_args;
    (void)base;

    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED: {
        s_mqtt_connected = true;
        /* Subscribe with QoS 1 to ensure commands are not missed */
        (void)esp_mqtt_client_subscribe(event->client, "test/topic", 1);
        (void)esp_mqtt_client_publish(s_client, "lepton/status", "Lepton ready", 12, 1, 0);
        (void)printf("MQTT Connected & Subscribed (QoS 1)\n");
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

            /* Wait up to 100ms if the queue is full, instead of dropping the command immediately */
            if (xQueueSend(spi_cmd_queue, &cmd, pdMS_TO_TICKS(100U)) != pdPASS) {
                (void)printf("SPI queue full! Command dropped: '%.*s'\n", (int)cmd.len, (const char *)cmd.data);
            } else {
                (void)printf("Queued to SPI: '%.*s'\n", (int)cmd.len, (const char *)cmd.data);
            }
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED: {
        s_mqtt_connected = false;
        (void)printf("MQTT Disconnected! Attempting to reconnect...\n");
        (void)esp_mqtt_client_reconnect(s_client);
        break;
    }

    case MQTT_EVENT_ERROR: {
        (void)printf("MQTT Error Occurred\n");
        if (event->error_handle != NULL) {
            (void)printf("Error Type: %d\n", (int)event->error_handle->error_type);
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
        .broker.address.uri              = "mqtts://192.168.55.200:8883",
        .buffer.size                     = 4096,   
        .buffer.out_size                 = 16384,  /* 버퍼를 16KB로 확장하여 안정성 확보 */
        .network.timeout_ms              = 10000,
        .network.reconnect_timeout_ms    = 5000,   
        .session.keepalive               = 60,
        .broker.verification.certificate                 = ca_cert_pem,
        .broker.verification.skip_cert_common_name_check = true,
        .credentials.authentication.certificate          = client_cert_pem,
        .credentials.authentication.key                  = client_key_pem,
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
        if ((s_mqtt_connected == false) || (s_client == NULL) || (g_frame_ready == 0U)) {
            vTaskDelay(pdMS_TO_TICKS(10U));
            continue;
        }

        /* g_frame_ready를 여기서 끄지 않고, 전송이 끝난 후에 끔 (버퍼 보호) */
        uint16_t min_val = 0xFFFFU;
        uint16_t max_val = 0x0000U;
        const uint8_t *fb = g_frame_bufs[0]; /* 싱글 버퍼 사용 */

        for (size_t p = 0U; p < (size_t)FRAME_BYTES; p += 16U) {
            const uint16_t pixel = (uint16_t)(((uint16_t)fb[p] << 8) | (uint16_t)fb[p+1U]);
            if ((pixel > 1000U) && (pixel < 30000U)) { 
                if (pixel < min_val) {
                    min_val = pixel;
                }
                if (pixel > max_val) {
                    max_val = pixel;
                }
            }
        }
        if ((min_val == 0xFFFFU) || (min_val >= max_val)) {
            min_val = 7000U;
            max_val = 9000U;
        }

        (void)printf("Sending Frame (Min:%d Max:%d)\n", (int)min_val, (int)max_val);
        bool send_ok = true;

        for (uint16_t i = 0U; i < total_chunks; i++) {
            if (s_mqtt_connected == false) {
                send_ok = false;
                break;
            }

            const size_t offset = (size_t)i * (size_t)CHUNK_PAYLOAD_SIZE;
            const size_t data_size = ((FRAME_BYTES - offset) > (size_t)CHUNK_PAYLOAD_SIZE) ? (size_t)CHUNK_PAYLOAD_SIZE : (FRAME_BYTES - offset);

            msg[0] = (uint8_t)((i >> 8) & 0xFFU);
            msg[1] = (uint8_t)(i & 0xFFU);
            msg[2] = (uint8_t)((total_chunks >> 8) & 0xFFU);
            msg[3] = (uint8_t)(total_chunks & 0xFFU);
            msg[4] = (uint8_t)((min_val >> 8) & 0xFFU);
            msg[5] = (uint8_t)(min_val & 0xFFU);
            msg[6] = (uint8_t)((max_val >> 8) & 0xFFU);
            msg[7] = (uint8_t)(max_val & 0xFFU);
            (void)memcpy(&msg[CHUNK_HEADER_SIZE], &fb[offset], data_size);

            int32_t retry_count = 0;
            int32_t ret = -1;
            while (retry_count < 3) {
                ret = (int32_t)esp_mqtt_client_publish(s_client, CHUNK_TOPIC, (const char *)msg, 
                                                (int)(CHUNK_HEADER_SIZE + data_size), 0, 0);
                if (ret >= 0) {
                    break;
                }
                
                /* 전송 실패 시 잠시 대기 후 재시도 (버퍼 비우기 시간 확보) */
                (void)printf("⚠️ Chunk %d send failed (ret:%d), retry %d/3...\n", (int)i, (int)ret, (int)(retry_count + 1));
                vTaskDelay(pdMS_TO_TICKS(100U));
                retry_count++;
            }

            if (ret < 0) {
                send_ok = false; 
                break; 
            }
            vTaskDelay(pdMS_TO_TICKS(10U)); /* 안정적인 전송을 위한 딜레이 */
        }

        if (send_ok == true) {
            (void)printf("Frame sent OK\n");
        }
        
        /* 전송이 완전히 끝난 후 새 프레임을 받을 준비를 함 */
        g_read_idx = -1;
        g_frame_ready = 0U; 
        
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
}
