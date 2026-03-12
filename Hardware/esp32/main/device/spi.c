/* spi.c - 32바이트 패킷 (CMD+LEN+DATA30) */

#include "spi.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"

#define PIN_SCK      4
#define PIN_MISO     5
#define PIN_MOSI     6
#define PIN_CS       7
#define SPI_CLOCK_HZ (1000000U)

#define CMD_WRITE    0x01U
#define CMD_READ     0x02U
#define STATUS_OK    0x00U

static spi_device_handle_t s_spi = NULL;
QueueHandle_t spi_cmd_queue = NULL;

esp_err_t spiMasterInit(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num   = PIN_MOSI,
        .miso_io_num   = PIN_MISO,
        .sclk_io_num   = PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = (int32_t)SPI_PKT_SIZE,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .mode           = 0U,
        .clock_speed_hz = (int32_t)SPI_CLOCK_HZ,
        .spics_io_num   = PIN_CS,
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi));

    spi_cmd_queue = xQueueCreate(10U, sizeof(spi_cmd_t));
    if (spi_cmd_queue == NULL) 
    {
        (void)printf("SPI queue create failed\n");
    }

    (void)printf("SPI Master init OK\n");
    return ESP_OK;
}

static esp_err_t spiTransfer(const uint8_t *tx, uint8_t *rx)
{
    spi_transaction_t t = {
        .length    = (size_t)SPI_PKT_SIZE * 8U,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(s_spi, &t);
}

esp_err_t spiWrite(const uint8_t *data, uint8_t len)
{
    if ((data == NULL) || (len == 0U) || (len > (uint8_t)MAX_SPI_DATA_LEN)) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[SPI_PKT_SIZE] = {0U};
    uint8_t rx[SPI_PKT_SIZE] = {0U};

    tx[0] = CMD_WRITE;
    tx[1] = len;
    (void)memcpy(&tx[2], data, (size_t)len);

    esp_err_t ret = spiTransfer(tx, rx);
    if (ret != ESP_OK) 
    {
        (void)printf("SPI transfer failed: %d\n", (int)ret);
        return ret;
    }

    (void)printf("WRITE '%.*s' → STATUS=0x%02X\n", (int)len, (const char *)data, (unsigned int)rx[0]);
    return ESP_OK;
}

esp_err_t spiRead(uint8_t *out_buf, uint8_t *out_len)
{
    if ((out_buf == NULL) || (out_len == NULL)) 
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[SPI_PKT_SIZE] = {0U};
    uint8_t rx[SPI_PKT_SIZE] = {0U};

    tx[0] = CMD_READ;
    tx[1] = 0U;

    esp_err_t ret = spiTransfer(tx, rx);
    if (ret != ESP_OK) 
    {
        return ret;
    }

    *out_len = rx[1];
    if (*out_len > (uint8_t)MAX_SPI_DATA_LEN) 
    {
        *out_len = (uint8_t)MAX_SPI_DATA_LEN;
    }
    (void)memcpy(out_buf, &rx[2], (size_t)*out_len);

    (void)printf("READ → STATUS=0x%02X LEN=%d\n", (unsigned int)rx[0], (int)*out_len);
    return (rx[0] == STATUS_OK) ? ESP_OK : ESP_FAIL;
}

/* MQTT → SPI 큐에서 꺼내서 즉시 STM32로 전송 */
void spiTask(void *arg)
{
    spi_cmd_t cmd;
    (void)arg;

    for (;;) 
    {
        if (xQueueReceive(spi_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) 
        {
            (void)printf("SPI sending: '%.*s' (%d bytes)\n", (int)cmd.len, (const char *)cmd.data, (int)cmd.len);
            (void)spiWrite(cmd.data, cmd.len);
        }
    }
}