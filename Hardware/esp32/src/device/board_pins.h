#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#include "driver/spi_common.h"

/* FireBeetle 2 ESP32-C5 official SPI mapping */
#define BOARD_SPI_HOST      SPI2_HOST
#define BOARD_SPI_PIN_SCK   23
#define BOARD_SPI_PIN_MOSI  24
#define BOARD_SPI_PIN_MISO  25
#define BOARD_SPI_PIN_CS    27

#endif
