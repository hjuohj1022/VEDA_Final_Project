#ifndef MQTT_H
#define MQTT_H

#include "mqtt_client.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "certs/cert.h" 

extern esp_mqtt_client_handle_t client;

//extern const uint8_t ca_cert_pem_start[]     asm("_binary_ca_cert_pem_start");
//extern const uint8_t ca_cert_pem_end[]       asm("_binary_ca_cert_pem_end");

//extern const uint8_t client_cert_pem_start[] asm("_binary_client_cert_pem_start");
//extern const uint8_t client_cert_pem_end[]   asm("_binary_client_cert_pem_end");

//extern const uint8_t client_key_pem_start[]  asm("_binary_client_key_pem_start");
//extern const uint8_t client_key_pem_end[]    asm("_binary_client_key_pem_end");

void mqttEventHandler( void *handler_args,esp_event_base_t base, int32_t event_id, void *event_data);
esp_mqtt_client_handle_t mqttClient();

#endif