#ifndef MQTT_H
#define MQTT_H

#include "mqtt_client.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "certs/cert.h"
#include <stdbool.h>

#define FRAME_STREAM_MODE_MQTT_ONLY  0
#define FRAME_STREAM_MODE_UDP_ONLY   1
#define FRAME_STREAM_MODE_BOTH       2
#define FRAME_STREAM_MODE_UDP_FRAME_MQTT_CONTROL  3

#define CHUNK_PAYLOAD_SIZE  1200
#define CHUNK_HEADER_SIZE   10
#define CHUNK_MSG_SIZE      (CHUNK_HEADER_SIZE + CHUNK_PAYLOAD_SIZE)
#define CHUNK_TOPIC         "lepton/frame/chunk"
#define CMD_TOPIC           "motor/control"
#define LASER_CMD_TOPIC     "laser/control"
#define STM32_RESP_TOPIC    "motor/response"
#define HEALTH_TOPIC        "system/status"
#define HEALTH_CONTROL_TOPIC "system/control"
#define HEALTH_CONTROL_CMD   "publish_status_now"

extern esp_mqtt_client_handle_t client;
extern bool mqtt_connected;

esp_mqtt_client_handle_t mqttClient(void);
void mqttFrameTask(void *arg);
void mqttHealthTask(void *arg);
bool mqttIsConnected(void);
void mqttPublishText(const char *topic, const char *payload, int qos);

#endif
