#include "mqtt/wifi.h"
#include "mqtt/mqtt.h"
#include "device/frame_link.h"
#include "device/cmd_uart.h"
#include <string.h>

#if defined(__has_include)
#if __has_include("app_secrets.h")
#include "app_secrets.h"
#else
#include "app_secrets.defaults.h"
#endif
#else
#include "app_secrets.defaults.h"
#endif

#define TASK_PRIO_FRAME_LINK   6U
#define TASK_PRIO_FRAME_SEND   4U
#define TASK_PRIO_CMD_UART     4U
#define TASK_PRIO_HEALTH       2U

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
    BaseType_t task_ok;
    esp_err_t ret;
    bool cmd_uart_enabled = false;

    (void)systemInit();
    (void)printf("APP mode=spi_frame_link build=20260310\n");

    if ((strlen(APP_WIFI_SSID) == 0U) || (strlen(APP_WIFI_PASS) == 0U))
    {
        (void)printf("Wi-Fi secrets missing. Create main/app_secrets.h from main/app_secrets.example.h\n");
        return;
    }

    (void)printf("Init: frame link\n");
    frameLinkInit();

    (void)printf("Init: STM32 UART bridge\n");
    ret = cmdUartInit();
    if (ret == ESP_OK) {
        cmd_uart_enabled = true;
    } else {
        (void)printf("STM32 command bridge disabled: %s\n", esp_err_to_name(ret));
    }

    task_ok = xTaskCreate(frameLinkTask, "frame_link", 8192U, NULL, TASK_PRIO_FRAME_LINK, NULL);
    (void)printf("Task create frame_link: %s\n", (task_ok == pdPASS) ? "OK" : "FAIL");

    task_ok = xTaskCreate(mqttFrameTask, "mqtt_frame", 8192U, NULL, TASK_PRIO_FRAME_SEND, NULL);
    (void)printf("Task create mqtt_frame: %s\n", (task_ok == pdPASS) ? "OK" : "FAIL");

    task_ok = xTaskCreate(mqttHealthTask, "mqtt_health", 4096U, NULL, TASK_PRIO_HEALTH, NULL);
    (void)printf("Task create mqtt_health: %s\n", (task_ok == pdPASS) ? "OK" : "FAIL");

    if (cmd_uart_enabled) {
        task_ok = xTaskCreate(cmdUartTask, "stm32_uart", 4096U, NULL, TASK_PRIO_CMD_UART, NULL);
        (void)printf("Task create stm32_uart: %s\n", (task_ok == pdPASS) ? "OK" : "FAIL");
        (void)printf("STM32 command bridge enabled: UART1(GPIO8/9) -> USART1(PA9/PA10)\n");
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = APP_WIFI_SSID,
            .password = APP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    (void)printf("Init: Wi-Fi connect\n");
    ret = wifiConnect(&wifi_config);
    if (ret != ESP_OK) {
        (void)printf("Wi-Fi init failed: %s\n", esp_err_to_name(ret));
    }
}
