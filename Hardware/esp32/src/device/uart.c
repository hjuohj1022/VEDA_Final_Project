#include "uart.h"
#include "mqtt.h"
#include <stdio.h>
#include <stdlib.h>

volatile uint8_t   g_frame_ready   = 0;
uint8_t           *g_frame_buf     = NULL;
uint8_t            g_offset_packet = 0;
SemaphoreHandle_t  g_frame_mutex   = NULL;

static const uint8_t HEADER[HEADER_LEN] = {'F','S','T','A','R','T'};

void uartInit(void) {
    g_frame_buf = malloc(FRAME_BYTES);
    if (!g_frame_buf) { printf("g_frame_buf malloc failed\n"); return; }

    g_frame_mutex = xSemaphoreCreateMutex();

    uart_config_t cfg = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // 드라이버 버퍼 4KB만 사용
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_RX_BUF, 0, 0, NULL, 0));
    printf("UART init OK (%d baud, rx_buf=%d)\n", UART_BAUD, UART_RX_BUF);
}

void uartTask(void *arg) {
    uint8_t *chunk = malloc(CHUNK_SIZE);
    uint8_t *work  = malloc(HEADER_LEN + PROTO_EXTRA + CHUNK_SIZE); // 헤더 탐색용 여유
    if (!chunk || !work) {
        printf("uartTask malloc failed\n");
        vTaskDelete(NULL);
        return;
    }

    // 상태 머신
    typedef enum { ST_FIND_HEADER, ST_READ_OFFSET, ST_READ_FRAME } State;
    State   state       = ST_FIND_HEADER;
    uint8_t hdr_buf[HEADER_LEN];
    int     hdr_pos     = 0;
    size_t  frame_pos   = 0;
    uint8_t offset_byte = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1));
        switch (state) {
        case ST_FIND_HEADER: {
            uint8_t b;
            int n = uart_read_bytes(UART_NUM, &b, 1, pdMS_TO_TICKS(100));
            if (n <= 0) break;

            hdr_buf[hdr_pos++] = b;
            if (hdr_pos < HEADER_LEN) {
                // 부분 매칭 확인
                if (b != HEADER[hdr_pos - 1]) hdr_pos = 0;
            } else {
                if (memcmp(hdr_buf, HEADER, HEADER_LEN) == 0) {
                    state   = ST_READ_OFFSET;
                    hdr_pos = 0;
                } else {
                    // 슬라이딩: 1바이트씩 버림
                    memmove(hdr_buf, hdr_buf + 1, HEADER_LEN - 1);
                    hdr_pos = HEADER_LEN - 1;
                }
            }
            break;
        }

        case ST_READ_OFFSET: {
            int n = uart_read_bytes(UART_NUM, &offset_byte, 1, pdMS_TO_TICKS(100));
            if (n <= 0) break;
            frame_pos = 0;
            state = ST_READ_FRAME;
            break;
        }

        case ST_READ_FRAME: {
            size_t remain = FRAME_BYTES - frame_pos;
            size_t to_read = remain < CHUNK_SIZE ? remain : CHUNK_SIZE;
            int n = uart_read_bytes(UART_NUM, chunk, to_read, pdMS_TO_TICKS(200));
            if (n <= 0) break;

            memcpy(g_frame_buf + frame_pos, chunk, n);
            frame_pos += n;

            if (frame_pos >= FRAME_BYTES) {
                // 프레임 완성
                g_offset_packet = offset_byte;
                g_frame_ready   = 1;
                printf("Frame OK offset_pkt=%d\n", offset_byte);

                // MQTT 전송
                if (client != NULL) {
                    // offset(1) + frame(38400) 순서로 publish
                    // mqtt_payload를 동적 할당 없이 두 번 나눠 보내는 대신
                    // QoS0으로 단순 전송
                    uint8_t hdr_byte = offset_byte;
                    // ESP-IDF MQTT는 단일 publish만 지원 → 임시 버퍼 필요
                    // 단, 38401B를 한 번에 할당하면 또 메모리 부족
                    // → offset을 topic에 인코딩하는 방식으로 우회
                    char topic[32];
                    snprintf(topic, sizeof(topic), "lepton/frame/%d", offset_byte);
                    esp_mqtt_client_publish(
                        client, topic,
                        (const char *)g_frame_buf,
                        FRAME_BYTES,
                        0, 0
                    );
                }
                state = ST_FIND_HEADER;
            }
            break;
        }
        } // switch
    }

    free(chunk);
    free(work);
}