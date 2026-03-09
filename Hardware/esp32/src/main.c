#include "mqtt/wifi.h"
#include "mqtt/mqtt.h"
#include "device/uart.h"
#include "device/spi.h"

esp_err_t systemInit(void)
{
    esp_err_t ret = nvs_flash_init();
    if ((ret == (esp_err_t)ESP_ERR_NVS_NO_FREE_PAGES) ||
        (ret == (esp_err_t)ESP_ERR_NVS_NEW_VERSION_FOUND)) 
    {
        (void)nvs_flash_erase();
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    return ESP_OK;
}

void app_main(void)
{
    (void)systemInit();

    uartInit();
    /* UART와 MQTT 우선순위를 5로 동일하게 설정하여 CPU 시간을 공평하게 분배 */
    (void)xTaskCreate(uartTask,      "uart_task",  8192U, NULL, 5U, NULL);
    (void)xTaskCreate(mqttFrameTask, "mqtt_frame", 8192U, NULL, 5U, NULL);

    (void)spiMasterInit();
    (void)xTaskCreate(spiTask, "spi_task", 4096U, NULL, 4U, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    (void)wifiConnect(&wifi_config);
}