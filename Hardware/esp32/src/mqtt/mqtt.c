#include "mqtt.h"
#include "../device/spi.h"
#include "../device/uart.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_mqtt_client_handle_t client        = NULL;
bool                     mqtt_connected = false;

void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED: {
        mqtt_connected = true;
        esp_mqtt_client_subscribe(event->client, "test/topic", 0);
        esp_mqtt_client_publish(client, "lepton/status", "Lepton ready", 12, 0, 0);
        printf("MQTT Connected & Subscribed\n");
        break;
    }

    case MQTT_EVENT_DATA: {
        printf("Topic: %.*s\n", event->topic_len, event->topic);
        printf("Data: %.*s\n", event->data_len, event->data);

        if (spi_cmd_queue != NULL) {
            spi_cmd_t cmd = {0};
            cmd.len = (event->data_len > MAX_SPI_DATA_LEN)
                      ? MAX_SPI_DATA_LEN
                      : (uint8_t)event->data_len;
            memcpy(cmd.data, event->data, cmd.len);

            if (xQueueSend(spi_cmd_queue, &cmd, 0) != pdPASS) {
                printf("SPI queue full!\n");
            } else {
                printf("Queued to SPI: '%.*s'\n", cmd.len, cmd.data);
            }
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        mqtt_connected = false;
        printf("MQTT Disconnected! Attempting to reconnect...\n");
        esp_mqtt_client_reconnect(client);
        break;

    case MQTT_EVENT_ERROR:
        printf("MQTT Error Occurred\n");
        if (event->error_handle != NULL) {
            printf("Error Type: %d\n", event->error_handle->error_type);
        }
        break;

    default:
        break;
    }
}

esp_mqtt_client_handle_t mqttClient(void)
{
    if (client != NULL) return client;

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri              = "mqtts://192.168.55.200:8883",
        .buffer.size                     = 4096,   
        .buffer.out_size                 = 16384,  // 버퍼를 16KB로 확장하여 안정성 확보
        .network.timeout_ms              = 10000,
        .network.reconnect_timeout_ms    = 5000,   
        .session.keepalive               = 60,
        .broker.verification.certificate                 = ca_cert_pem,
        .broker.verification.skip_cert_common_name_check = true,
        .credentials.authentication.certificate          = client_cert_pem,
        .credentials.authentication.key                  = client_key_pem,
    };
    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqttEventHandler, NULL);
    esp_mqtt_client_start(client);
    return client;
}

void mqttFrameTask(void *arg)
{
    uint8_t msg[CHUNK_MSG_SIZE];
    const uint16_t total_chunks = (FRAME_BYTES + CHUNK_PAYLOAD_SIZE - 1) / CHUNK_PAYLOAD_SIZE;

    while (1) {
        if (!mqtt_connected || client == NULL || !g_frame_ready) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // g_frame_ready를 여기서 끄지 않고, 전송이 끝난 후에 끔 (버퍼 보호)
        uint16_t min_val = 0xFFFF;
        uint16_t max_val = 0x0000;
        uint8_t *fb = g_frame_bufs[0]; // 싱글 버퍼 사용

        for (size_t p = 0; p < FRAME_BYTES; p += 16) {
            uint16_t pixel = (uint16_t)((fb[p] << 8) | fb[p+1]);
            if (pixel > 1000 && pixel < 30000) { 
                if (pixel < min_val) min_val = pixel;
                if (pixel > max_val) max_val = pixel;
            }
        }
        if (min_val == 0xFFFF || min_val >= max_val) { min_val = 7000; max_val = 9000; }

        printf("Sending Frame (Min:%d Max:%d)\n", min_val, max_val);
        bool send_ok = true;

        for (uint16_t i = 0; i < total_chunks; i++) {
            if (!mqtt_connected) { send_ok = false; break; }

            size_t offset = (size_t)i * CHUNK_PAYLOAD_SIZE;
            size_t data_size = (FRAME_BYTES - offset > CHUNK_PAYLOAD_SIZE) ? CHUNK_PAYLOAD_SIZE : (FRAME_BYTES - offset);

            msg[0] = (i >> 8) & 0xFF; msg[1] = i & 0xFF;
            msg[2] = (total_chunks >> 8) & 0xFF; msg[3] = total_chunks & 0xFF;
            msg[4] = (min_val >> 8) & 0xFF; msg[5] = min_val & 0xFF;
            msg[6] = (max_val >> 8) & 0xFF; msg[7] = max_val & 0xFF;
            memcpy(msg + CHUNK_HEADER_SIZE, fb + offset, data_size);

            int retry_count = 0;
            int ret = -1;
            while (retry_count < 3) {
                ret = esp_mqtt_client_publish(client, CHUNK_TOPIC, (const char *)msg, 
                                                CHUNK_HEADER_SIZE + data_size, 0, 0);
                if (ret >= 0) break;
                
                // 전송 실패 시 잠시 대기 후 재시도 (버퍼 비우기 시간 확보)
                printf("⚠️ Chunk %d send failed (ret:%d), retry %d/3...\n", i, ret, retry_count + 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                retry_count++;
            }

            if (ret < 0) {
                send_ok = false; 
                break; 
            }
            vTaskDelay(pdMS_TO_TICKS(10)); // 안정적인 전송을 위한 딜레이
        }

        if (send_ok) printf("Frame sent OK\n");
        
        // 전송이 완전히 끝난 후 새 프레임을 받을 준비를 함
        g_read_idx = -1;
        g_frame_ready = 0; 
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
