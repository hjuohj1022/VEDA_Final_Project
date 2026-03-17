#include "cmd_uart.h"
#include "../mqtt/mqtt.h"
#include "driver/uart.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

QueueHandle_t g_cmd_uart_queue = NULL;

#define CMD_UART_LINE_BUF 128U

esp_err_t cmdUartInit(void)
{
    esp_err_t ret;

    const uart_config_t cfg = {
        .baud_rate = CMD_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ret = uart_param_config(CMD_UART_NUM, &cfg);
    if (ret != ESP_OK) {
        (void)printf("STM32 UART param config failed: %s\n", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(CMD_UART_NUM,
                       CMD_UART_TX_PIN,
                       CMD_UART_RX_PIN,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        (void)printf("STM32 UART set pins failed (tx=%d rx=%d): %s\n",
                     (int)CMD_UART_TX_PIN,
                     (int)CMD_UART_RX_PIN,
                     esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(CMD_UART_NUM, CMD_UART_RX_BUF, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        (void)printf("STM32 UART driver install failed: %s\n", esp_err_to_name(ret));
        return ret;
    }

    g_cmd_uart_queue = xQueueCreate(16U, sizeof(cmd_uart_msg_t));
    if (g_cmd_uart_queue == NULL) {
        (void)printf("STM32 UART queue create failed\n");
        (void)uart_driver_delete(CMD_UART_NUM);
        return ESP_FAIL;
    }

    (void)printf("STM32 UART bridge init OK (uart=%d tx=%d rx=%d baud=%d)\n",
                 (int)CMD_UART_NUM,
                 (int)CMD_UART_TX_PIN,
                 (int)CMD_UART_RX_PIN,
                 (int)CMD_UART_BAUD);
    cmdUartFlushInput();
    return ESP_OK;
}

void cmdUartFlushInput(void)
{
    (void)uart_flush_input(CMD_UART_NUM);
}

uint32_t cmdUartGetQueueDepth(void)
{
    if (g_cmd_uart_queue == NULL) {
        return 0U;
    }

    return (uint32_t)uxQueueMessagesWaiting(g_cmd_uart_queue);
}

void cmdUartTask(void *arg)
{
    cmd_uart_msg_t msg;
    uint8_t rx_buf[128];
    char line_buf[CMD_UART_LINE_BUF];
    size_t line_len = 0U;
    (void)arg;

    for (;;) {
        if ((g_cmd_uart_queue != NULL) &&
            (xQueueReceive(g_cmd_uart_queue, &msg, pdMS_TO_TICKS(20U)) == pdTRUE)) {
            if (msg.len > 0U) {
                (void)uart_write_bytes(CMD_UART_NUM, msg.data, (uint32_t)msg.len);
                (void)uart_write_bytes(CMD_UART_NUM, "\r\n", 2U);
                (void)printf("STM32 UART TX: '%.*s'\n", (int)msg.len, msg.data);
            }
        }

        const int32_t n = (int32_t)uart_read_bytes(CMD_UART_NUM,
                                                   rx_buf,
                                                   sizeof(rx_buf) - 1U,
                                                   pdMS_TO_TICKS(10U));
        if (n > 0) {
            for (int32_t i = 0; i < n; i++) {
                const char ch = (char)rx_buf[i];

                if ((ch == '\r') || (ch == '\n')) {
                    if (line_len > 0U) {
                        line_buf[line_len] = '\0';
                        (void)printf("STM32 UART RX: %s\n", line_buf);
                        // Control/ack traffic should be delivered reliably even under frame load.
                        mqttPublishText(STM32_RESP_TOPIC, line_buf, 1);
                        line_len = 0U;
                    }
                    continue;
                }

                if (line_len < (sizeof(line_buf) - 1U)) {
                    line_buf[line_len] = ch;
                    line_len++;
                } else {
                    line_buf[line_len] = '\0';
                    (void)printf("STM32 UART RX overflow, dropping partial line: %s\n", line_buf);
                    line_len = 0U;
                }
            }
        }
    }
}
