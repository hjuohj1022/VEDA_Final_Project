#include <zephyr.h>
#include <device.h>
#include <drivers/i2c.h>
#include <drivers/spi.h>
#include <drivers/uart.h>
#include <drivers/gpio.h>
#include <usb/usb_device.h>
#include <sys/printk.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <fsl_iomuxc.h>
#include <fsl_gpio.h>
#include <soc.h>

#define LEP_CCI_ADDRESS  (0x2AU)
#define PACKET_SIZE      (164U)
#define PACKETS_PER_SEG  (60U)
#define PIXELS_PER_PKT   (80U)
#define NUM_SEGMENTS     (4U)
#define FRAME_SIZE       (160U * 120U)

#define LEP_REG_STATUS   (0x0002U)
#define LEP_REG_COMMAND  (0x0004U)
#define LEP_REG_LENGTH   (0x0006U)
#define LEP_REG_DATA0    (0x0008U)
#define LEP_CMD_AGC_ENABLE  (0x0101U)
#define LEP_CMD_VID_OUTPUT  (0x0204U)

static uint16_t frameBuffer[FRAME_SIZE];
static uint8_t  rawPacket[PACKET_SIZE];

/* Device nodes */
#define I2C_DEV_NODE  DT_NODELABEL(lpi2c1)
#define SPI_DEV_NODE  DT_NODELABEL(lpspi4)
#define UART_DEV_NODE DT_NODELABEL(lpuart6)
#define CS_GPIO_NODE  DT_NODELABEL(gpio2)
#define CS_PIN        (0U)

static const struct device *i2c_dev;
static const struct device *spi_dev;
static const struct device *uart_dev;
static const struct device *cs_gpio;

static struct spi_config spi_cfg = {
	.frequency = 18000000U,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8U) | SPI_MODE_CPOL | SPI_MODE_CPHA, /* SPI_MODE3 */
	.slave = 0,
};

static void setup_pinmux(void) {
	CLOCK_EnableClock(kCLOCK_Iomuxc);

	/* LPI2C1 SCL, SDA on Teensy-Pins 19/18 */
	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_00_LPI2C1_SCL, 1);
	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_01_LPI2C1_SDA, 1);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_00_LPI2C1_SCL,
			    0x10B0u); /* Basic config similar to pinmux.c */

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_01_LPI2C1_SDA,
			    0x10B0u);

	/* LPSPI4 MISO, MOSI, SCK, CS on Teensy-Pins 12/11/13/10 */
	IOMUXC_SetPinMux(IOMUXC_GPIO_B0_00_LPSPI4_PCS0, 0);
	IOMUXC_SetPinMux(IOMUXC_GPIO_B0_01_LPSPI4_SDI, 0);
	IOMUXC_SetPinMux(IOMUXC_GPIO_B0_02_LPSPI4_SDO, 0);
	IOMUXC_SetPinMux(IOMUXC_GPIO_B0_03_LPSPI4_SCK, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_B0_00_LPSPI4_PCS0, 0x10B0u);
	IOMUXC_SetPinConfig(IOMUXC_GPIO_B0_01_LPSPI4_SDI, 0x10B0u);
	IOMUXC_SetPinConfig(IOMUXC_GPIO_B0_02_LPSPI4_SDO, 0x10B0u);
	IOMUXC_SetPinConfig(IOMUXC_GPIO_B0_03_LPSPI4_SCK, 0x10B0u);
}

static void cciWriteReg(uint16_t reg, uint16_t val) {
	uint8_t data[4];
	data[0] = (uint8_t)(reg >> 8U);
	data[1] = (uint8_t)(reg & 0xFFU);
	data[2] = (uint8_t)(val >> 8U);
	data[3] = (uint8_t)(val & 0xFFU);
	(void)i2c_write(i2c_dev, data, 4U, LEP_CCI_ADDRESS);
	(void)k_msleep(5U);
}

static uint16_t cciReadReg(uint16_t reg) {
	uint8_t reg_addr[2] = { (uint8_t)(reg >> 8U), (uint8_t)(reg & 0xFFU) };
	uint8_t data[2] = {0U, 0U};
	(void)i2c_write_read(i2c_dev, LEP_CCI_ADDRESS, reg_addr, 2U, data, 2U);
	return (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
}

static bool cciWaitBusy(uint32_t ms) {
	bool is_ready = false;
	uint32_t start = k_uptime_get_32();
	while ((k_uptime_get_32() - start) < ms) {
		if ((cciReadReg(LEP_REG_STATUS) & 0x0001U) == 0U) {
			is_ready = true;
			break;
		}
		(void)k_msleep(10U);
	}
	return is_ready;
}

static bool cciSet(uint16_t cmdId, uint16_t value) {
	bool success = false;
	if (cciWaitBusy(2000U)) {
		cciWriteReg(LEP_REG_DATA0, value);
		cciWriteReg(LEP_REG_LENGTH, 1U);
		cciWriteReg(LEP_REG_COMMAND, (uint16_t)(cmdId | 0x02U));
		success = cciWaitBusy(2000U);
	}
	return success;
}

static void resetLepton(void) {
	(void)gpio_pin_set(cs_gpio, CS_PIN, 1);
	(void)k_msleep(200U);
}

static void uart_send(const struct device *dev, const uint8_t *data, size_t len) {
	for (size_t i = 0U; i < len; i++) {
		uart_poll_out(dev, data[i]);
	}
}

void main(void) {
	setup_pinmux();
	(void)usb_enable(NULL);

	i2c_dev = DEVICE_DT_GET(I2C_DEV_NODE);
	spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);
	uart_dev = DEVICE_DT_GET(UART_DEV_NODE);
	cs_gpio = DEVICE_DT_GET(CS_GPIO_NODE);

	if ((!device_is_ready(i2c_dev)) || (!device_is_ready(spi_dev)) || 
	    (!device_is_ready(uart_dev)) || (!device_is_ready(cs_gpio))) {
		printk("Devices not ready\n");
	} else {
		(void)gpio_pin_configure(cs_gpio, CS_PIN, GPIO_OUTPUT_ACTIVE);
		(void)gpio_pin_set(cs_gpio, CS_PIN, 1);

		(void)k_msleep(1000U);

		(void)cciSet(LEP_CMD_AGC_ENABLE, 0x0000U);
		(void)cciSet(LEP_CMD_VID_OUTPUT, 0x0007U);
		printk("Lepton init complete\n");

		while (true) {
			uint32_t startTime = k_uptime_get_32();
			uint8_t currentSeg = 0U;
			bool frameComplete = false;

			while (!frameComplete) {
				if ((k_uptime_get_32() - startTime) > 3000U) {
					resetLepton();
					break;
				}

				(void)gpio_pin_set(cs_gpio, CS_PIN, 0);
				struct spi_buf rx_buf = { .buf = rawPacket, .len = PACKET_SIZE };
				struct spi_buf_set rx_bufs = { .buffers = &rx_buf, .count = 1U };
				(void)spi_read(spi_dev, &spi_cfg, &rx_bufs);
				(void)gpio_pin_set(cs_gpio, CS_PIN, 1);

				if ((rawPacket[0] & 0x0FU) == 0x0FU) {
					(void)k_usleep(30U);
					continue;
				}

				uint8_t packetId = rawPacket[1];
				if (packetId >= PACKETS_PER_SEG) {
					continue;
				}

				if (packetId == 20U) {
					uint8_t segBits = (uint8_t)((rawPacket[0] >> 4U) & 0x07U);
					if ((segBits > 0U) && (segBits <= 4U)) {
						currentSeg = segBits;
					}
				}

				if (currentSeg > 0U) {
					uint32_t baseIdx = (uint32_t)(((uint32_t)(currentSeg - 1U) * (uint32_t)PACKETS_PER_SEG * (uint32_t)PIXELS_PER_PKT) + ((uint32_t)packetId * (uint32_t)PIXELS_PER_PKT));

					if ((baseIdx + PIXELS_PER_PKT) <= (uint32_t)FRAME_SIZE) {
						for (uint32_t i = 0U; i < PIXELS_PER_PKT; i++) {
							uint8_t msb = rawPacket[4U + (i * 2U)];
							uint8_t lsb = rawPacket[5U + (i * 2U)];
							frameBuffer[baseIdx + i] = (uint16_t)(((uint16_t)lsb << 8U) | (uint16_t)msb);
						}
					}

					if (packetId == 59U) {
						if (currentSeg == 4U) {
							frameComplete = true;
						} else {
							currentSeg++;
						}
					}
				}
			}

			if (frameComplete) {
				printk("Frame complete - sending\n");
				uart_send(uart_dev, (const uint8_t *)"FSTART", 6U);
				uint8_t zero = 0U;
				uart_send(uart_dev, &zero, 1U);

				const uint8_t *ptr = (const uint8_t *)frameBuffer;
				size_t totalBytes = (size_t)FRAME_SIZE * 2U;
				size_t sentBytes = 0U;

				while (sentBytes < totalBytes) {
					size_t remaining = totalBytes - sentBytes;
					size_t len = (remaining > 1024U) ? 1024U : remaining;
					uart_send(uart_dev, &ptr[sentBytes], len);
					sentBytes += len;
					(void)k_usleep(200U);
				}
				printk("Sent OK\n");
				(void)k_msleep(400U);
			}
		}
	}
}
