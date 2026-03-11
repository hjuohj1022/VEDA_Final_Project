#include "cmd_uart.h"
#include "../mqtt/mqtt.h"
#include "driver/uart.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

QueueHandle_t g_cmd_uart_queue = NULL;

esp_err_t cmdUartInit(void)
{
    const uart_config_t cfg = {
        .baud_rate = CMD_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(CMD_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(CMD_UART_NUM,
                                 CMD_UART_TX_PIN,
                                 CMD_UART_RX_PIN,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(CMD_UART_NUM, CMD_UART_RX_BUF, 0, 0, NULL, 0));

    g_cmd_uart_queue = xQueueCreate(10U, sizeof(cmd_uart_msg_t));
    if (g_cmd_uart_queue == NULL) {
        (void)printf("STM32 UART queue create failed\n");
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

void cmdUartTask(void *arg)
{
    cmd_uart_msg_t msg;
    uint8_t rx_buf[128];
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
            rx_buf[n] = '\0';
            (void)printf("STM32 UART RX: %s", (const char *)rx_buf);
            if (rx_buf[n - 1] != '\n') {
                (void)printf("\n");
            }
            mqttPublishText(STM32_RESP_TOPIC, (const char *)rx_buf, 0);
        }
    }
}
