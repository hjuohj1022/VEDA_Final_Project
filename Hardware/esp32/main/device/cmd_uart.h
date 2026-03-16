#ifndef CMD_UART_H
#define CMD_UART_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>

#define CMD_UART_NUM       UART_NUM_1
#define CMD_UART_TX_PIN    11
#define CMD_UART_RX_PIN    12
#define CMD_UART_BAUD      115200
#define CMD_UART_RX_BUF    256
#define CMD_MAX_LEN        64

typedef struct {
    uint8_t len;
    char data[CMD_MAX_LEN];
} cmd_uart_msg_t;

extern QueueHandle_t g_cmd_uart_queue;

esp_err_t cmdUartInit(void);
void cmdUartTask(void *arg);
void cmdUartFlushInput(void);
uint32_t cmdUartGetQueueDepth(void);

#endif
