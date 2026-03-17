#ifndef WIFI_H
#define WIFI_H

#include "esp_wifi.h"      // wifi_config_t, esp_err_t
#include "esp_event.h"     // esp_event_base_t
#include "esp_netif.h"

esp_err_t wifiConnect(const wifi_config_t *conf);
bool wifiIsConnected(void);

#endif
