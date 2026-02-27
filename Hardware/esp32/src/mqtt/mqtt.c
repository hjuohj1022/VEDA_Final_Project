#include "mqtt.h"
#include "../device/spi.h"
#include <string.h>

esp_mqtt_client_handle_t client;

void mqttEventHandler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    switch ((esp_mqtt_event_id_t)event_id) {

    case MQTT_EVENT_CONNECTED:
        printf("MQTT Connected\n");
        esp_mqtt_client_subscribe(client, "test/topic", 0);
        break;

    case MQTT_EVENT_DATA: {
        esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
        printf("Topic: %.*s\n", event->topic_len, event->topic);
        printf("Data: %.*s\n", event->data_len, event->data);

        /* 수신 즉시 SPI 큐로 전달 */
        if (spi_cmd_queue != NULL) {
            spi_cmd_t cmd = {0};
            cmd.len = (event->data_len > MAX_SPI_DATA_LEN)
                      ? MAX_SPI_DATA_LEN
                      : (uint8_t)event->data_len;
            memcpy(cmd.data, event->data, cmd.len);

            if (xQueueSend(spi_cmd_queue, &cmd, 0) != pdPASS) {
                printf("SPI queue full!\n");
            } else {
                printf("Queued to SPI: '%.*s'\n", cmd.len, cmd.data);
            }
        }
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        printf("MQTT Disconnected\n");
        esp_mqtt_client_reconnect(client);
        break;

    case MQTT_EVENT_ERROR:
        printf("MQTT Error\n");
        break;

    default:
        break;
    }
}

esp_mqtt_client_handle_t mqttClient(void)
{
    if (client != NULL) {
        printf("MQTT already initialized\n");
        return client;
    }

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = "mqtts://192.168.55.200:8883",
        .buffer.out_size = 40960,
        .broker.verification.certificate = ca_cert_pem,
        .broker.verification.skip_cert_common_name_check = true,
        .credentials.authentication.certificate = client_cert_pem,
        .credentials.authentication.key = client_key_pem,
    };
    client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqttEventHandler, NULL);
    esp_mqtt_client_start(client);

    return client;
}