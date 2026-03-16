#ifndef MQTT_H
#define MQTT_H

#include "mqtt_client.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "certs/cert.h"
#include <stdbool.h>

#define CHUNK_PAYLOAD_SIZE  1024
#define CHUNK_HEADER_SIZE   10
#define CHUNK_MSG_SIZE      (CHUNK_HEADER_SIZE + CHUNK_PAYLOAD_SIZE)
#define CHUNK_TOPIC         "lepton/frame/chunk"
#define CMD_TOPIC           "motor/control"
#define STM32_RESP_TOPIC    "motor/response"
#define HEALTH_TOPIC        "system/status"
#define HEALTH_REQ_TOPIC    "system/request"

extern esp_mqtt_client_handle_t client;
extern bool mqtt_connected;

esp_mqtt_client_handle_t mqttClient(void);
void mqttFrameTask(void *arg);
void mqttHealthTask(void *arg);
bool mqttIsConnected(void);
void mqttPublishText(const char *topic, const char *payload, int qos);

#endif
