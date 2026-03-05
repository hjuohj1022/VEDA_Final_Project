#ifndef MQTT_H
#define MQTT_H

#include "mqtt_client.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "certs/cert.h"
#include <stdbool.h>

#define CHUNK_PAYLOAD_SIZE  2048                              // 청크 데이터 크기 축소 (안정성 향상)
#define CHUNK_HEADER_SIZE   8                                 // index(2) + total(2) + min(2) + max(2)
#define CHUNK_MSG_SIZE      (CHUNK_HEADER_SIZE + CHUNK_PAYLOAD_SIZE)  // 전체 메시지 크기
#define CHUNK_TOPIC         "lepton/frame/chunk"

extern esp_mqtt_client_handle_t client;
extern bool mqtt_connected;

void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
esp_mqtt_client_handle_t mqttClient(void);
void mqttFrameTask(void *arg);

#endif