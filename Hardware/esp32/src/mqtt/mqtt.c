#include "mqtt.h"
#include "../device/spi.h"
#include <string.h>

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

            // 특정 토픽("test/topic")으로 들어온 명령만 처리하도록 필터링 (선택 사항)
            if (strncmp(event->topic, "test/topic", event->topic_len) == 0) {
                if (spi_cmd_queue != NULL) {
                    spi_cmd_t cmd = {0};
                    
                    // SPI 패킷 데이터 영역이 최대 5바이트이므로 길이 제한
                    cmd.len = (event->data_len > MAX_SPI_CMD_LEN) ? MAX_SPI_CMD_LEN : event->data_len;
                    memcpy(cmd.data, event->data, cmd.len);

                    // 큐에 명령어 삽입 (대기 시간 0)
                    if (xQueueSend(spi_cmd_queue, &cmd, 0) != pdPASS) {
                        printf("MQTT Event: SPI command queue is full!\n");
                    }
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