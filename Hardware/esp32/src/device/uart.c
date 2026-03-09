#include "uart.h"
#include "mqtt.h"
#include <stdio.h>
#include <stdlib.h>

volatile uint8_t  g_frame_ready             = 0;
uint8_t          *g_frame_bufs[NUM_BUFFERS] = {NULL};
volatile int      g_write_idx               = 0;
volatile int      g_read_idx                = -1;
uint8_t           g_offset_packet           = 0;
SemaphoreHandle_t g_frame_mutex             = NULL;

static const uint8_t HEADER[HEADER_LEN] = {'F','S','T','A','R','T'};

void uartInit(void) 
{
    g_frame_bufs[0] = (uint8_t *)malloc((size_t)FRAME_BYTES);
    if (NULL == g_frame_bufs[0]) 
    {
        (void)printf("g_frame_bufs malloc failed\n");
        return;
    }

    g_frame_mutex = xSemaphoreCreateMutex();

    const uart_config_t cfg = {
        .baud_rate = (int)UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM, (int)UART_TX_PIN, (int)UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM, (int)UART_RX_BUF, 0, 0, NULL, 0));
    (void)printf("UART init OK (%d baud, rx_buf=%d)\n", (int)UART_BAUD, (int)UART_RX_BUF);
}

void uartTask(void *arg) 
{
    typedef enum { ST_FIND_HEADER, ST_READ_OFFSET, ST_READ_FRAME } State;
    State   state       = ST_FIND_HEADER;
    int32_t hdr_pos     = 0;
    size_t  frame_pos   = 0U;
    uint8_t offset_byte = 0U;
    (void)arg;

    for (;;) 
    {
        /* 이전 프레임이 전송 완료될 때까지 대기 (싱글 버퍼 보호) */
        if (0U != g_frame_ready) 
        {
            /* MQTT 전송 중에는 버퍼를 완전히 비우고 충분히 쉼 (100ms) */
            (void)uart_flush_input(UART_NUM);
            vTaskDelay(pdMS_TO_TICKS(100U)); 
            continue;
        }

        switch (state) 
        {
        case ST_FIND_HEADER: {
            uint8_t b = 0U;
            /* 1바이트 읽기 시 타임아웃을 짧게 주어 자주 양보하게 함 */
            const int32_t n = (int32_t)uart_read_bytes(UART_NUM, &b, 1U, pdMS_TO_TICKS(10U));
            if (n <= 0) 
            {
                break;
            }

            if (b == HEADER[hdr_pos]) 
            {
                hdr_pos++;
                if (hdr_pos == (int32_t)HEADER_LEN) 
                {
                    state   = ST_READ_OFFSET;
                    hdr_pos = 0;
                }
            } 
            else 
            {
                hdr_pos = (b == HEADER[0]) ? 1 : 0;
            }
            break;
        }

        case ST_READ_OFFSET: {
            const int32_t n = (int32_t)uart_read_bytes(UART_NUM, &offset_byte, 1U, pdMS_TO_TICKS(50U));
            if (n <= 0) 
            {
                break;
            }
            frame_pos = 0U;
            state = ST_READ_FRAME;
            break;
        }

        case ST_READ_FRAME: {
            const size_t remain = (size_t)FRAME_BYTES - frame_pos;
            size_t available = 0U;
            (void)uart_get_buffered_data_len(UART_NUM, &available);

            size_t to_read = (available > remain) ? remain : available;
            if (to_read == 0U) 
            {
                to_read = 1U; 
            }

            const int32_t n = (int32_t)uart_read_bytes(UART_NUM,
                                    &g_frame_bufs[0][frame_pos],
                                    to_read,
                                    pdMS_TO_TICKS(50U));
            if (n <= 0) 
            {
                /* 수신 중단 시 헤더 찾기로 복귀하여 동기화 유지 */
                state = ST_FIND_HEADER;
                break;
            }

            frame_pos += (size_t)n;

            if (frame_pos >= (size_t)FRAME_BYTES) 
            {
                g_offset_packet = offset_byte;
                g_read_idx      = 0;
                g_frame_ready   = 1U;
                state = ST_FIND_HEADER;
                (void)printf(">> Frame Captured\n");
            }
            break;
        }
        
        default: {
            state = ST_FIND_HEADER;
            break;
        }
        } /* switch */
    }
}