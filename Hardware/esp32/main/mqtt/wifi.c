#include "wifi.h"
#include "mqtt.h"
#include "udp_stream.h"
#include "../device/cmd_uart.h"
#if defined(__has_include)
#if __has_include("../app_secrets.h")
#include "../app_secrets.h"
#else
#include "../app_secrets.defaults.h"
#endif
#else
#include "../app_secrets.defaults.h"
#endif

static volatile bool s_wifi_connected = false;

static void wifiConfigureBandMode(void)
{
#if CONFIG_SOC_WIFI_SUPPORT_5G
    wifi_protocols_t protocols = {
        .ghz_2g = 0U,
        .ghz_5g = WIFI_PROTOCOL_11A | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_11AC | WIFI_PROTOCOL_11AX,
    };
    wifi_bandwidths_t bandwidths = {
        .ghz_2g = WIFI_BW_HT20,
        .ghz_5g = WIFI_BW_HT20,
    };

    ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_5G_ONLY));
    ESP_ERROR_CHECK(esp_wifi_set_protocols(WIFI_IF_STA, &protocols));
    ESP_ERROR_CHECK(esp_wifi_set_bandwidths(WIFI_IF_STA, &bandwidths));
    (void)printf("[WiFi] 5 GHz only mode enabled\n");
    (void)printf("[WiFi] 5 GHz protocols: 11a/11n/11ac/11ax, bandwidth: HT20\n");
#else
    (void)printf("[WiFi] 5 GHz not supported on this target, using default band mode\n");
#endif
}

static void wifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT)
    {
        if (event_id == (int32_t)WIFI_EVENT_STA_START)
        {
            (void)printf("WiFi STA started\n");
        }
        else if (event_id == (int32_t)WIFI_EVENT_STA_DISCONNECTED)
        {
            s_wifi_connected = false;
            (void)printf("WiFi disconnected\n");
            udpStreamReset();
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
            s_wifi_connected = true;
            (void)printf("WiFi connected, got IP\n");
            (void)printf("Free heap: %lu\n", (uint32_t)esp_get_free_heap_size());
            cmdUartFlushInput();
            if (APP_FRAME_STREAM_MODE != FRAME_STREAM_MODE_UDP_ONLY) {
                (void)mqttClient();
            } else {
                (void)printf("MQTT client disabled by APP_FRAME_STREAM_MODE=UDP_ONLY\n");
            }
            if ((APP_FRAME_STREAM_MODE == FRAME_STREAM_MODE_BOTH) ||
                (APP_FRAME_STREAM_MODE == FRAME_STREAM_MODE_UDP_FRAME_MQTT_CONTROL)) {
                udpStreamDeferInit(3000U);
                (void)printf("DTLS init deferred for 3000 ms to let MQTT/TLS start first\n");
            }
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

bool wifiIsConnected(void)
{
    return s_wifi_connected;
}

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
    wifiConfigureBandMode();
    ESP_ERROR_CHECK(esp_wifi_connect());

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
