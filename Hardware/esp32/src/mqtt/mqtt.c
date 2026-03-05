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
    if (client != NULL) {
        return client;
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri              = "mqtts://192.168.55.200:8883",
        .buffer.size                     = 2048,   
        .buffer.out_size                 = 8192,   
        .network.timeout_ms              = 10000,
        .network.reconnect_timeout_ms    = 5000,   
        .session.keepalive               = 30,     
        .broker.verification.certificate                 = ca_cert_pem,
        .broker.verification.skip_cert_common_name_check = true,
        .credentials.authentication.certificate          = client_cert_pem,
        .credentials.authentication.key                  = client_key_pem,
    };
    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqttEventHandler, NULL);
    esp_mqtt_client_start(client);

    // 태스크 스택을 다시 4KB로 축소 (발열 및 메모리 절약)
    xTaskCreate(mqttFrameTask, "mqttFrameTask", 4096, NULL, 5, NULL);

    return client;
}

// 38KB 프레임을 청크로 나눠 전송
void mqttFrameTask(void *arg)
{
    uint8_t msg[CHUNK_MSG_SIZE];
    const uint16_t total_chunks = (FRAME_BYTES + CHUNK_PAYLOAD_SIZE - 1) / CHUNK_PAYLOAD_SIZE;

    while (1) {
        // 연결 및 데이터 대기
        if (!mqtt_connected || client == NULL || g_read_idx < 0 || !g_frame_ready) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        int    idx   = g_read_idx;
        g_frame_ready = 0;
        g_read_idx    = -1;

        printf("Publishing frame from buffer index %d...\n", idx);
        bool send_ok = true;

        for (uint16_t i = 0; i < total_chunks; i++) {
            if (!mqtt_connected) {
                send_ok = false;
                break;
            }

            size_t offset    = (size_t)i * CHUNK_PAYLOAD_SIZE;
            size_t data_size = FRAME_BYTES - offset;
            if (data_size > CHUNK_PAYLOAD_SIZE) data_size = CHUNK_PAYLOAD_SIZE;

            msg[0] = (i >> 8) & 0xFF;
            msg[1] =  i       & 0xFF;
            msg[2] = (total_chunks >> 8) & 0xFF;
            msg[3] =  total_chunks       & 0xFF;
            memcpy(msg + CHUNK_HEADER_SIZE, g_frame_bufs[idx] + offset, data_size);

            int ret = -1;
            for (int retry = 0; retry < 3; retry++) {
                ret = esp_mqtt_client_publish(
                    client,
                    CHUNK_TOPIC,
                    (const char *)msg,
                    CHUNK_HEADER_SIZE + data_size,
                    0, 0
                );
                if (ret >= 0) break;
                vTaskDelay(pdMS_TO_TICKS(50));
            }

            if (ret < 0) {
                send_ok = false;
                break;
            }

            // 발열 완화를 위해 청크 간 딜레이를 20ms로 소폭 조정
            vTaskDelay(pdMS_TO_TICKS(20));
        }

        if (send_ok) {
            printf("Frame sent OK: %d chunks\n", total_chunks);
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}