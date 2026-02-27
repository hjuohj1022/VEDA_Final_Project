#ifndef SPI_H
#define SPI_H
#include "esp_err.h"
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// SPI 패킷의 최대 데이터 길이 (현재 코드 기준 5바이트)
#define MAX_SPI_CMD_LEN 5

// 큐로 전달할 명령어 구조체
typedef struct {
    uint8_t data[MAX_SPI_CMD_LEN];
    uint8_t len;
} spi_cmd_t;

extern QueueHandle_t spi_cmd_queue;

esp_err_t spiMasterInit(void);
esp_err_t spiPing(void);
esp_err_t spiWrite(const uint8_t *data, uint8_t len);
esp_err_t spiRead(uint8_t *out_buf, uint8_t *out_len);
void      spiTask(void *arg);

#endif