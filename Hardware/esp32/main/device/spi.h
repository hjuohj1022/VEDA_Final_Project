#ifndef SPI_H
#define SPI_H
#include "esp_err.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/*
 * 패킷 구조 (32바이트 고정):
 *   [0]     CMD
 *   [1]     LEN  (데이터 길이, 최대 30)
 *   [2..31] DATA (30바이트)
 */
#define MAX_SPI_DATA_LEN  30
#define SPI_PKT_SIZE      32   /* CMD(1) + LEN(1) + DATA(30) */

typedef struct {
    uint8_t data[MAX_SPI_DATA_LEN];
    uint8_t len;
} spi_cmd_t;

extern QueueHandle_t spi_cmd_queue;

esp_err_t spiMasterInit(void);
esp_err_t spiWrite(const uint8_t *data, uint8_t len);
esp_err_t spiRead(uint8_t *out_buf, uint8_t *out_len);
void      spiTask(void *arg);

#endif