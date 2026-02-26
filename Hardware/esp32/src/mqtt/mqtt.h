#ifndef MQTT_H
#define MQTT_H

#include "mqtt_client.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "certs/cert.h" 

extern esp_mqtt_client_handle_t client;

void mqttEventHandler( void *handler_args,esp_event_base_t base, int32_t event_id, void *event_data);
esp_mqtt_client_handle_t mqttClient();

#endif