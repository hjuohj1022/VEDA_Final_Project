#ifndef UART_H
#define UART_H

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdint.h>

#define UART_NUM        UART_NUM_1
#define UART_TX_PIN     21
#define UART_RX_PIN     20
#define UART_BAUD       921600

#define FRAME_BYTES     38400
#define HEADER_LEN      6
#define PROTO_EXTRA     1
#define TOTAL_PKT       (HEADER_LEN + PROTO_EXTRA + FRAME_BYTES)

#define UART_RX_BUF     4096
#define CHUNK_SIZE      1024

// 더블 버퍼
#define NUM_BUFFERS     2
extern uint8_t          *g_frame_bufs[NUM_BUFFERS];
extern volatile int      g_write_idx;
extern volatile int      g_read_idx;

extern volatile uint8_t  g_frame_ready;
extern uint8_t           g_offset_packet;
extern SemaphoreHandle_t g_frame_mutex;

void uartInit(void);
void uartTask(void *arg);

#endif