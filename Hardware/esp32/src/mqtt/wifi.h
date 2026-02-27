#ifndef WIFI_H
#define WIFI_H

#include "esp_wifi.h"      // wifi_config_t, esp_err_t
#include "esp_event.h"     // esp_event_base_t
#include "esp_netif.h"

void wifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
esp_err_t wifiConnect(wifi_config_t *conf);

#endif