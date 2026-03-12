#ifndef UART_H
#define UART_H

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define UART_NUM        UART_NUM_1
#define UART_TX_PIN     21
#define UART_RX_PIN     20
#define UART_BAUD       4000000

#define FRAME_BYTES     38400
#define HEADER_LEN      6
#define PROTO_EXTRA     1
#define TOTAL_PKT       (HEADER_LEN + PROTO_EXTRA + FRAME_BYTES)

#define UART_RX_BUF     8192
#define CHUNK_SIZE      1024

#define NUM_BUFFERS     2
extern uint8_t          *g_frame_bufs[NUM_BUFFERS];
extern volatile int      g_write_idx;
extern volatile int      g_read_idx;

extern volatile uint8_t  g_frame_ready;
extern uint8_t           g_offset_packet;
extern SemaphoreHandle_t g_frame_mutex;

void uartInit(void);
void uartTask(void *arg);
bool uartAcquireReadyFrame(const uint8_t **frame_buf, uint16_t *frame_id, uint8_t *offset, int *buffer_idx);
void uartReleaseReadyFrame(int buffer_idx);
uint8_t uartGetAllocatedBufferCount(void);

#endif


