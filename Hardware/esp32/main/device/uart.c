#include "uart.h"
#include "mqtt.h"
#include "esp_system.h"
#include <stdio.h>
#include <stdlib.h>

volatile uint8_t  g_frame_ready             = 0;
uint8_t          *g_frame_bufs[NUM_BUFFERS] = {NULL};
volatile int      g_write_idx               = 0;
volatile int      g_read_idx                = -1;
uint8_t           g_offset_packet           = 0;
SemaphoreHandle_t g_frame_mutex             = NULL;

static const uint8_t HEADER[HEADER_LEN] = {'F', 'S', 'T', 'A', 'R', 'T'};
static uint16_t s_frame_ids[NUM_BUFFERS] = {0U};
static uint8_t  s_offsets[NUM_BUFFERS]   = {0U};
static uint8_t  s_allocated_buffers      = 0U;
static uint16_t s_next_frame_id          = 1U;

static bool uartQueueIsFull(void)
{
    bool is_full = false;

    if ((g_frame_mutex != NULL) &&
        (xSemaphoreTake(g_frame_mutex, pdMS_TO_TICKS(20U)) == pdTRUE)) {
        is_full = (g_frame_ready >= s_allocated_buffers) && (s_allocated_buffers > 0U);
        (void)xSemaphoreGive(g_frame_mutex);
    }

    return is_full;
}

uint8_t uartGetAllocatedBufferCount(void)
{
    return s_allocated_buffers;
}

bool uartAcquireReadyFrame(const uint8_t **frame_buf, uint16_t *frame_id, uint8_t *offset, int *buffer_idx)
{
    bool ok = false;

    if ((frame_buf == NULL) || (frame_id == NULL) || (offset == NULL) || (buffer_idx == NULL)) {
        return false;
    }

    if ((g_frame_mutex != NULL) &&
        (xSemaphoreTake(g_frame_mutex, pdMS_TO_TICKS(20U)) == pdTRUE)) {
        if ((g_frame_ready > 0U) && (g_read_idx >= 0) && (g_read_idx < (int)s_allocated_buffers)) {
            *buffer_idx = g_read_idx;
            *frame_buf = g_frame_bufs[g_read_idx];
            *frame_id = s_frame_ids[g_read_idx];
            *offset = s_offsets[g_read_idx];
            ok = true;
        }
        (void)xSemaphoreGive(g_frame_mutex);
    }

    return ok;
}

void uartReleaseReadyFrame(int buffer_idx)
{
    if ((g_frame_mutex == NULL) || (buffer_idx < 0)) {
        return;
    }

    if (xSemaphoreTake(g_frame_mutex, pdMS_TO_TICKS(20U)) == pdTRUE) {
        if ((g_frame_ready > 0U) &&
            (g_read_idx == buffer_idx) &&
            (s_allocated_buffers > 0U)) {
            g_frame_ready--;
            if (g_frame_ready > 0U) {
                g_read_idx = (g_read_idx + 1) % (int)s_allocated_buffers;
            } else {
                g_read_idx = -1;
            }
        }
        (void)xSemaphoreGive(g_frame_mutex);
    }
}

void uartInit(void)
{
    for (uint8_t i = 0U; i < (uint8_t)NUM_BUFFERS; i++) {
        g_frame_bufs[i] = (uint8_t *)malloc((size_t)FRAME_BYTES);
        if (g_frame_bufs[i] == NULL) {
            break;
        }
        s_allocated_buffers++;
    }

    if (s_allocated_buffers == 0U) {
        (void)printf("frame buffer allocation failed\n");
        return;
    }

    g_frame_mutex = xSemaphoreCreateMutex();
    if (g_frame_mutex == NULL) {
        (void)printf("frame mutex create failed\n");
        return;
    }

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

    (void)printf("UART init OK (%d baud, rx_buf=%d, frame_bufs=%u)\n",
                 (int)UART_BAUD,
                 (int)UART_RX_BUF,
                 (unsigned int)s_allocated_buffers);
    (void)printf("Heap after UART init: free=%lu min=%lu\n",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
}

void uartTask(void *arg)
{
    typedef enum { ST_FIND_HEADER, ST_READ_OFFSET, ST_READ_FRAME } State;

    State   state       = ST_FIND_HEADER;
    int32_t hdr_pos     = 0;
    size_t  frame_pos   = 0U;
    uint8_t offset_byte = 0U;
    int     target_idx  = 0;
    (void)arg;

    for (;;) {
        if (s_allocated_buffers == 0U) {
            vTaskDelay(pdMS_TO_TICKS(100U));
            continue;
        }

        if ((state != ST_READ_FRAME) && uartQueueIsFull()) {
            (void)uart_flush_input(UART_NUM);
            vTaskDelay(pdMS_TO_TICKS(20U));
            continue;
        }

        switch (state) {
        case ST_FIND_HEADER: {
            uint8_t b = 0U;
            const int32_t n = (int32_t)uart_read_bytes(UART_NUM, &b, 1U, pdMS_TO_TICKS(10U));
            if (n <= 0) {
                break;
            }

            if (b == HEADER[hdr_pos]) {
                hdr_pos++;
                if (hdr_pos == (int32_t)HEADER_LEN) {
                    state = ST_READ_OFFSET;
                    hdr_pos = 0;
                }
            } else {
                hdr_pos = (b == HEADER[0]) ? 1 : 0;
            }
            break;
        }

        case ST_READ_OFFSET: {
            const int32_t n = (int32_t)uart_read_bytes(UART_NUM, &offset_byte, 1U, pdMS_TO_TICKS(50U));
            if (n <= 0) {
                state = ST_FIND_HEADER;
                break;
            }

            target_idx = g_write_idx;
            frame_pos = 0U;
            state = ST_READ_FRAME;
            break;
        }

        case ST_READ_FRAME: {
            const size_t remain = (size_t)FRAME_BYTES - frame_pos;
            size_t available = 0U;
            (void)uart_get_buffered_data_len(UART_NUM, &available);

            size_t to_read = (available > remain) ? remain : available;
            if (to_read == 0U) {
                to_read = 1U;
            }

            const int32_t n = (int32_t)uart_read_bytes(UART_NUM,
                                                       &g_frame_bufs[target_idx][frame_pos],
                                                       to_read,
                                                       pdMS_TO_TICKS(50U));
            if (n <= 0) {
                state = ST_FIND_HEADER;
                frame_pos = 0U;
                break;
            }

            frame_pos += (size_t)n;

            if (frame_pos >= (size_t)FRAME_BYTES) {
                uint16_t frame_id = 0U;

                if ((g_frame_mutex != NULL) &&
                    (xSemaphoreTake(g_frame_mutex, pdMS_TO_TICKS(20U)) == pdTRUE)) {
                    g_offset_packet = offset_byte;
                    s_offsets[target_idx] = offset_byte;
                    s_frame_ids[target_idx] = s_next_frame_id++;
                    frame_id = s_frame_ids[target_idx];

                    if (g_frame_ready == 0U) {
                        g_read_idx = target_idx;
                    }

                    if (g_frame_ready < s_allocated_buffers) {
                        g_frame_ready++;
                    }

                    g_write_idx = (target_idx + 1) % (int)s_allocated_buffers;
                    (void)xSemaphoreGive(g_frame_mutex);
                }

                state = ST_FIND_HEADER;
                frame_pos = 0U;
                (void)printf(">> Frame captured id=%u queued=%u/%u\n",
                             (unsigned int)frame_id,
                             (unsigned int)g_frame_ready,
                             (unsigned int)s_allocated_buffers);
            }
            break;
        }

        default: {
            state = ST_FIND_HEADER;
            frame_pos = 0U;
            break;
        }
        }
    }
}
