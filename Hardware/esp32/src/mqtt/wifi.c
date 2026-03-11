#include "wifi.h"
#include "mqtt.h"
#include "../device/uart.h" 
static void wifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT) 
    {
        if (event_id == (int32_t)WIFI_EVENT_STA_START) 
        {
            (void)esp_wifi_connect();
        }
        else if (event_id == (int32_t)WIFI_EVENT_STA_DISCONNECTED) 
        {
            (void)printf("WiFi disconnected\n");
            (void)esp_wifi_connect();
        }
        else 
        {
            /* Other WiFi events */
        }
    }
    else if (event_base == IP_EVENT) 
    {
        if (event_id == (int32_t)IP_EVENT_STA_GOT_IP) 
        {
            (void)printf("WiFi connected, got IP\n");
            (void)printf("Free heap: %lu\n", (uint32_t)esp_get_free_heap_size());
            (void)uart_flush(UART_NUM);  /* ← UART 버퍼 비우기 */
            (void)mqttClient();
        }
        else 
        {
            /* Other IP events */
        }
    }
    else 
    {
        /* Other event bases */
    }
}

/* wifi 연결 wrapper 함수 */
esp_err_t wifiConnect(const wifi_config_t *conf)
{
    (void)esp_netif_create_default_wifi_sta();

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifiEventHandler,
        NULL));

    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifiEventHandler,
        NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, (wifi_config_t *)conf));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* 절전 모드 상태 확인 로그 추가 */
    wifi_ps_type_t ps_type;
    if (esp_wifi_get_ps(&ps_type) == ESP_OK) {
        if (ps_type == WIFI_PS_NONE) {
            (void)printf("[WiFi] Power Save Mode: DISABLED (WIFI_PS_NONE)\n");
        } else {
            (void)printf("[WiFi] Power Save Mode: ENABLED (type: %d)\n", (int)ps_type);
        }
    }

    return ESP_OK;
}