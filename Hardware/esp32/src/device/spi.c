/* spi_master.c - STM32와 8바이트 패킷 통일 */

#include "spi.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"

/* ── 핀 (핀맵 기준) ── */
#define PIN_SCK      4
#define PIN_MISO     5
#define PIN_MOSI     6
#define PIN_CS       7
#define SPI_CLOCK_HZ (1 * 1000 * 1000)   /* 1MHz */

/*
 * 패킷 구조 (8바이트 고정, STM32와 동일):
 *   [0]     CMD
 *   [1]     LEN  (데이터 길이, 최대 5)
 *   [2..6]  DATA (5바이트)
 *   [7]     dummy
 */
#define PKT_SIZE     8
#define CMD_WRITE    0x01
#define CMD_READ     0x02
#define CMD_PING     0xAA
#define STATUS_OK    0x00

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
        .max_transfer_sz = PKT_SIZE,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {
        .mode           = 0,              /* CPOL=0, CPHA=0 */
        .clock_speed_hz = SPI_CLOCK_HZ,
        .spics_io_num   = PIN_CS,
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev_cfg, &s_spi));

    spi_cmd_queue = xQueueCreate(10, sizeof(spi_cmd_t));
    if (spi_cmd_queue == NULL) {
        printf("Failed to create SPI command queue\n");
    }

    printf("SPI Master init OK (SCK=%d MOSI=%d MISO=%d CS=%d)\n",
           PIN_SCK, PIN_MOSI, PIN_MISO, PIN_CS);
    return ESP_OK;
}

static esp_err_t spiTransfer(const uint8_t *tx, uint8_t *rx)
{
    spi_transaction_t t = {
        .length    = PKT_SIZE * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(s_spi, &t);
}

esp_err_t spiWrite(const uint8_t *data, uint8_t len)
{
    if (!data || len == 0 || len > 5) return ESP_ERR_INVALID_ARG;

    uint8_t tx[PKT_SIZE] = {0};
    uint8_t rx[PKT_SIZE] = {0};

    tx[0] = CMD_WRITE;
    tx[1] = len;
    memcpy(&tx[2], data, len);

    esp_err_t ret = spiTransfer(tx, rx);
    if (ret != ESP_OK) {
        printf("SPI transfer failed: %d\n", ret);
        return ret;
    }

    /* rx[0]은 이전 트랜잭션의 STM32 응답 (동시 송수신) */
    printf("WRITE → STATUS=0x%02X\n", rx[0]);
    return ESP_OK;
}

esp_err_t spiRead(uint8_t *out_buf, uint8_t *out_len)
{
    if (!out_buf || !out_len) return ESP_ERR_INVALID_ARG;

    uint8_t tx[PKT_SIZE] = {0};
    uint8_t rx[PKT_SIZE] = {0};

    tx[0] = CMD_READ;
    tx[1] = 0;

    esp_err_t ret = spiTransfer(tx, rx);
    if (ret != ESP_OK) return ret;

    *out_len = rx[1];
    memcpy(out_buf, &rx[2], *out_len);

    printf("READ → STATUS=0x%02X LEN=%d\n", rx[0], *out_len);
    return (rx[0] == STATUS_OK) ? ESP_OK : ESP_FAIL;
}

void spiTask(void *arg)
{
    spi_cmd_t cmd;

    while (1) {
        // 큐에 데이터가 들어올 때까지 무한 대기
        if (xQueueReceive(spi_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            printf("SPI Task: Received command from MQTT. Length: %d\n", cmd.len);
            
            // STM32로 SPI 전송
            esp_err_t ret = spiWrite(cmd.data, cmd.len);
            if (ret == ESP_OK) {
                printf("SPI Task: Command sent successfully.\n");
            } else {
                printf("SPI Task: Command send failed.\n");
            }
        }
    }
}