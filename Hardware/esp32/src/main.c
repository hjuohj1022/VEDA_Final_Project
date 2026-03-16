#include "mqtt/wifi.h"
#include "mqtt/mqtt.h"
#include "device/frame_link.h"
#include "device/cmd_uart.h"

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
    (void)printf("APP mode=spi_frame_link build=20260310\n");

    frameLinkInit();
    ESP_ERROR_CHECK(cmdUartInit());
    (void)xTaskCreate(frameLinkTask, "frame_link", 8192U, NULL, 5U, NULL);
    (void)xTaskCreate(mqttFrameTask, "mqtt_frame", 8192U, NULL, 5U, NULL);
    (void)xTaskCreate(mqttHealthTask, "mqtt_health", 4096U, NULL, 4U, NULL);
    (void)xTaskCreate(cmdUartTask, "stm32_uart", 4096U, NULL, 5U, NULL);

    (void)printf("STM32 command bridge enabled: UART1(GPIO21/20) -> USART1(PA9/PA10)\n");

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    (void)wifiConnect(&wifi_config);
}
