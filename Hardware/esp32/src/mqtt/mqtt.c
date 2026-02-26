#include "mqtt.h"

esp_mqtt_client_handle_t client;

void mqttEventHandler(void *handler_args,esp_event_base_t base, int32_t event_id, void *event_data){
    switch ((esp_mqtt_event_id_t)event_id) {

        case MQTT_EVENT_CONNECTED:
			printf("MQTT Connected\n");

			esp_mqtt_client_subscribe(client, "test/topic", 0);

			esp_mqtt_client_publish(
				client,
				"test/topic",
				"Hello from ESP32-C3",
				0,      // length (0이면 strlen 자동 계산)
				1,      // QoS
				0       // retain
			);

			break;
			
		case MQTT_EVENT_DATA: {
            esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
            printf("Topic: %.*s\n", event->topic_len, event->topic);
            printf("Data: %.*s\n", event->data_len, event->data);
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

//mqtt 연결 wrapper 함수
esp_mqtt_client_handle_t mqttClient(){

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

    esp_mqtt_client_register_event(
        client,
        ESP_EVENT_ANY_ID,
        mqttEventHandler,
        NULL
    );

    esp_mqtt_client_start(client);

    return client;
}