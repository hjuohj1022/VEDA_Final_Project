#include "uart.h"
#include "mqtt.h"
#include <stdio.h>
#include <stdlib.h>

volatile uint8_t  g_frame_ready             = 0;
uint8_t          *g_frame_bufs[NUM_BUFFERS] = {NULL, NULL};
volatile int      g_write_idx               = 0;
volatile int      g_read_idx                = -1;
uint8_t           g_offset_packet           = 0;
SemaphoreHandle_t g_frame_mutex             = NULL;

static const uint8_t HEADER[HEADER_LEN] = {'F','S','T','A','R','T'};

void uartInit(void) {
    g_frame_bufs[0] = malloc(FRAME_BYTES);
    g_frame_bufs[1] = malloc(FRAME_BYTES);
    if (!g_frame_bufs[0] || !g_frame_bufs[1]) {
        printf("g_frame_bufs malloc failed\n");
        return;
    }

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
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, UART_RX_BUF, 0, 0, NULL, 0));
    printf("UART init OK (%d baud, rx_buf=%d)\n", UART_BAUD, UART_RX_BUF);
}

void uartTask(void *arg) {
    uint8_t *chunk = malloc(CHUNK_SIZE);
    uint8_t *work  = malloc(HEADER_LEN + PROTO_EXTRA + CHUNK_SIZE);
    if (!chunk || !work) {
        printf("uartTask malloc failed\n");
        vTaskDelete(NULL);
        return;
    }

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
                if (b != HEADER[hdr_pos - 1]) hdr_pos = 0;
            } else {
                if (memcmp(hdr_buf, HEADER, HEADER_LEN) == 0) {
                    state   = ST_READ_OFFSET;
                    hdr_pos = 0;
                } else {
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
            size_t remain  = FRAME_BYTES - frame_pos;
            size_t to_read = remain < CHUNK_SIZE ? remain : CHUNK_SIZE;

            int n = uart_read_bytes(UART_NUM,
                                    g_frame_bufs[g_write_idx] + frame_pos,
                                    to_read,
                                    pdMS_TO_TICKS(200));
            if (n <= 0) break;

            frame_pos += n;

            if (frame_pos >= FRAME_BYTES) {
                g_offset_packet = offset_byte;
                g_read_idx      = g_write_idx;
                g_write_idx     = (g_write_idx + 1) % NUM_BUFFERS;
                g_frame_ready   = 1;

                if (mqtt_connected) {
                    printf("Frame OK offset_pkt=%d\n", offset_byte);
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