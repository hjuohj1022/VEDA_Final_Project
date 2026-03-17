#include <zephyr.h>
#include <device.h>
#include <drivers/i2c.h>
#include <drivers/spi.h>
#include <drivers/gpio.h>
#include <usb/usb_device.h>
#include <sys/printk.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <fsl_iomuxc.h>
#include <fsl_gpio.h>
#include <soc.h>

#define LEP_CCI_ADDRESS  ((uint8_t)0x2AU)
#define PACKET_SIZE      (164U)
#define PACKETS_PER_SEG  (60U)
#define PIXELS_PER_PKT   (80U)
#define NUM_SEGMENTS     (4U)
#define FRAME_SIZE       (160U * 120U)
#define FRAME_BYTES      (FRAME_SIZE * 2U)
#define FRAME_BUFFER_COUNT (3U)

#define LEP_REG_STATUS   (0x0002U)
#define LEP_REG_COMMAND  (0x0004U)
#define LEP_REG_LENGTH   (0x0006U)
#define LEP_REG_DATA0    (0x0008U)
#define LEP_CMD_AGC_ENABLE  (0x0101U)
#define LEP_CMD_VID_OUTPUT  (0x0204U)

#define FRAME_SPI_PKT_SIZE   (256U)
#define FRAME_SPI_CLOCK_HZ   (4000000U)
#define FRAME_SPI_GAP_US      (300U)
#define FRAME_SPI_CS_SETUP_US (5U)
#define FRAME_SPI_CS_HOLD_US  (5U)
#define FRAME_POST_SEND_SLEEP_MS (0U)
#define CAPTURE_TIMEOUT_RESET_THRESHOLD (3U)

#define FRAME_SPI_HEADER_SIZE   (14U)
#define FRAME_SPI_PAYLOAD_SIZE  (FRAME_SPI_PKT_SIZE - FRAME_SPI_HEADER_SIZE)

static uint8_t  rawPacket[PACKET_SIZE];
static uint8_t  frameTxPacket[FRAME_SPI_PKT_SIZE];
static uint16_t frameBuffers[FRAME_BUFFER_COUNT][FRAME_SIZE];
static uint16_t frameIds[FRAME_BUFFER_COUNT];

/* Device nodes */
#define I2C_DEV_NODE         DT_NODELABEL(lpi2c1)
#define LEPTON_SPI_DEV_NODE  DT_NODELABEL(lpspi4)
#define LEPTON_CS_GPIO_NODE  DT_NODELABEL(gpio2)
#define FRAME_CS_GPIO_NODE   DT_NODELABEL(gpio1)
#define LEPTON_CS_PIN        (0U)
#define FRAME_CS_PIN         (3U)
#define FRAME_SPI_NODE       DT_NODELABEL(lpspi3)

static const struct device *i2c_dev;
static const struct device *lepton_spi_dev;
static const struct device *frame_spi_dev;
static const struct device *lepton_cs_gpio;
static const struct device *frame_cs_gpio;
static bool frame_spi_ready = false;
static uint32_t dropped_ready_frames = 0U;

K_MSGQ_DEFINE(free_frame_queue, sizeof(uint8_t), FRAME_BUFFER_COUNT, 4U);
K_MSGQ_DEFINE(ready_frame_queue, sizeof(uint8_t), FRAME_BUFFER_COUNT, 4U);
K_THREAD_STACK_DEFINE(frame_sender_stack, 4096U);
static struct k_thread frame_sender_thread;

static void resetLepton(void);
static void frameSenderThread(void *arg1, void *arg2, void *arg3);

static struct spi_config lepton_spi_cfg = {
	.frequency = 18000000U,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8U) | SPI_MODE_CPOL | SPI_MODE_CPHA, /* SPI_MODE3 */
	.slave = 0,
};

static struct spi_config frame_spi_cfg = {
	.frequency = FRAME_SPI_CLOCK_HZ,
	.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8U),
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

static void setup_frame_spi_pinmux(void)
{
	/* SPI frame link on Teensy pins 0(CS GPIO) / 1(MISO) / 26(MOSI) / 27(SCK) */
	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_03_GPIO1_IO03, 0);
	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_02_LPSPI3_SDI, 0);
	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_14_LPSPI3_SDO, 0);
	IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_15_LPSPI3_SCK, 0);

	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_03_GPIO1_IO03, 0x10B0u);
	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_02_LPSPI3_SDI, 0x10B0u);
	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_14_LPSPI3_SDO, 0x10B0u);
	IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_15_LPSPI3_SCK, 0x10B0u);
}

static void ensureFrameSpiReady(void)
{
	if (frame_spi_ready) {
		return;
	}

	if (frame_spi_dev == NULL) {
		printk("Frame SPI device unavailable\n");
		return;
	}

	setup_frame_spi_pinmux();
	(void)gpio_pin_configure(frame_cs_gpio, FRAME_CS_PIN, GPIO_OUTPUT_ACTIVE);
	(void)gpio_pin_set(frame_cs_gpio, FRAME_CS_PIN, 1);
	frame_spi_ready = true;
	printk("Frame SPI pinmux ready\n");
}

static bool cciWriteReg(uint16_t reg, uint16_t val) {
	uint8_t data[4];

	data[0] = (uint8_t)(reg >> 8U);
	data[1] = (uint8_t)(reg & 0xFFU);
	data[2] = (uint8_t)(val >> 8U);
	data[3] = (uint8_t)(val & 0xFFU);
	if (i2c_write(i2c_dev, data, 4U, LEP_CCI_ADDRESS) != 0) {
		return false;
	}
	(void)k_msleep(5U);
	return true;
}

static bool cciReadReg(uint16_t reg, uint16_t *out_val) {
	uint8_t reg_addr[2] = { (uint8_t)(reg >> 8U), (uint8_t)(reg & 0xFFU) };
	uint8_t data[2] = {0U, 0U};

	if ((out_val == NULL) ||
	    (i2c_write_read(i2c_dev, LEP_CCI_ADDRESS, reg_addr, 2U, data, 2U) != 0)) {
		return false;
	}

	*out_val = (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
	return true;
}

static bool cciWaitBusy(uint32_t ms) {
	bool is_ready = false;
	uint32_t start = k_uptime_get_32();
	uint16_t status = 0U;
	while ((k_uptime_get_32() - start) < ms) {
		if (!cciReadReg(LEP_REG_STATUS, &status)) {
			break;
		}
		if ((status & 0x0001U) == 0U) {
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
		success = cciWriteReg(LEP_REG_DATA0, value) &&
		          cciWriteReg(LEP_REG_LENGTH, 1U) &&
		          cciWriteReg(LEP_REG_COMMAND, (uint16_t)(cmdId | 0x02U)) &&
		          cciWaitBusy(2000U);
	}
	return success;
}

static bool configureLepton(void)
{
	for (uint8_t attempt = 1U; attempt <= 3U; attempt++) {
		if (cciSet(LEP_CMD_AGC_ENABLE, 0x0000U) &&
		    cciSet(LEP_CMD_VID_OUTPUT, 0x0007U)) {
			return true;
		}

		printk("Lepton config attempt %u failed\n", attempt);
		resetLepton();
		(void)k_msleep(200U);
	}

	return false;
}

static void resetLepton(void) {
	(void)gpio_pin_set(lepton_cs_gpio, LEPTON_CS_PIN, 1);
	(void)k_msleep(200U);
}

static uint16_t frameChecksum(const uint8_t *data, uint16_t len)
{
	uint32_t sum = 0U;

	for (uint16_t i = 0U; i < len; i++) {
		sum += data[i];
	}

	return (uint16_t)(sum & 0xFFFFU);
}

static void frameWriteU16Be(uint8_t *dst, uint16_t value)
{
	dst[0] = (uint8_t)((value >> 8U) & 0xFFU);
	dst[1] = (uint8_t)(value & 0xFFU);
}

static int sendFrameOverSpi(const uint16_t *frame_data, uint16_t frame_id)
{
	const uint8_t *frame_bytes = (const uint8_t *)frame_data;
	const uint16_t total_chunks = (uint16_t)((FRAME_BYTES + FRAME_SPI_PAYLOAD_SIZE - 1U) / FRAME_SPI_PAYLOAD_SIZE);

	ensureFrameSpiReady();
	if (!frame_spi_ready) {
		return -1;
	}

	for (uint16_t chunk_idx = 0U; chunk_idx < total_chunks; chunk_idx++) {
		const size_t offset = (size_t)chunk_idx * (size_t)FRAME_SPI_PAYLOAD_SIZE;
		const uint16_t payload_len = (uint16_t)(((FRAME_BYTES - offset) > (size_t)FRAME_SPI_PAYLOAD_SIZE)
		                                      ? (size_t)FRAME_SPI_PAYLOAD_SIZE
		                                      : (FRAME_BYTES - offset));
		struct spi_buf tx_buf = {
			.buf = frameTxPacket,
			.len = FRAME_SPI_PKT_SIZE,
		};
		struct spi_buf_set tx_bufs = {
			.buffers = &tx_buf,
			.count = 1U,
		};

		(void)memset(frameTxPacket, 0, sizeof(frameTxPacket));
		frameTxPacket[0] = (uint8_t)'T';
		frameTxPacket[1] = (uint8_t)'E';
		frameTxPacket[2] = (uint8_t)'S';
		frameTxPacket[3] = (uint8_t)'T';
		frameWriteU16Be(&frameTxPacket[4], frame_id);
		frameWriteU16Be(&frameTxPacket[6], chunk_idx);
		frameWriteU16Be(&frameTxPacket[8], total_chunks);
		frameWriteU16Be(&frameTxPacket[10], payload_len);
		(void)memcpy(&frameTxPacket[FRAME_SPI_HEADER_SIZE], &frame_bytes[offset], payload_len);
		frameWriteU16Be(&frameTxPacket[12], frameChecksum(&frameTxPacket[FRAME_SPI_HEADER_SIZE], payload_len));

		(void)gpio_pin_set(frame_cs_gpio, FRAME_CS_PIN, 0);
		(void)k_busy_wait(FRAME_SPI_CS_SETUP_US);
		if (spi_write(frame_spi_dev, &frame_spi_cfg, &tx_bufs) != 0) {
			(void)gpio_pin_set(frame_cs_gpio, FRAME_CS_PIN, 1);
			printk("Frame SPI write failed: frame=%u chunk=%u/%u\n",
			       frame_id,
			       chunk_idx + 1U,
			       total_chunks);
			return -1;
		}
		(void)k_busy_wait(FRAME_SPI_CS_HOLD_US);
		(void)gpio_pin_set(frame_cs_gpio, FRAME_CS_PIN, 1);

		(void)k_usleep(FRAME_SPI_GAP_US);
	}

	return 0;
}

static void frameSenderThread(void *arg1, void *arg2, void *arg3)
{
	uint32_t sent_frames = 0U;
	(void)arg1;
	(void)arg2;
	(void)arg3;

	while (true) {
		uint8_t buffer_idx = 0U;

		if (k_msgq_get(&ready_frame_queue, &buffer_idx, K_FOREVER) != 0) {
			continue;
		}

		if (sendFrameOverSpi(frameBuffers[buffer_idx], frameIds[buffer_idx]) == 0) {
			sent_frames++;
			if ((sent_frames % 30U) == 0U) {
				printk("Frame SPI sent: id=%u total=%u dropped_ready=%u\n",
				       frameIds[buffer_idx],
				       sent_frames,
				       dropped_ready_frames);
			}
		} else {
			printk("Frame SPI send failed: id=%u\n", frameIds[buffer_idx]);
		}

		if (FRAME_POST_SEND_SLEEP_MS > 0U) {
			(void)k_msleep(FRAME_POST_SEND_SLEEP_MS);
		}

		(void)k_msgq_put(&free_frame_queue, &buffer_idx, K_FOREVER);
	}
}

static bool captureFrame(uint16_t *frame_buffer, uint32_t *discard_count)
{
	const uint32_t start_ms = k_uptime_get_32();
	uint8_t current_seg = 0U;
	uint32_t local_discards = 0U;

	if ((frame_buffer == NULL) || (discard_count == NULL)) {
		return false;
	}

	while (true) {
		int spi_ret;

		if ((k_uptime_get_32() - start_ms) > 3000U) {
			*discard_count = local_discards;
			return false;
		}

		(void)gpio_pin_set(lepton_cs_gpio, LEPTON_CS_PIN, 0);
		struct spi_buf rx_buf = { .buf = rawPacket, .len = PACKET_SIZE };
		struct spi_buf_set rx_bufs = { .buffers = &rx_buf, .count = 1U };
		spi_ret = spi_read(lepton_spi_dev, &lepton_spi_cfg, &rx_bufs);
		(void)gpio_pin_set(lepton_cs_gpio, LEPTON_CS_PIN, 1);
		if (spi_ret != 0) {
			local_discards++;
			if ((local_discards % 20U) == 0U) {
				printk("Lepton SPI read failed: ret=%d discards=%u\n", spi_ret, local_discards);
			}
			(void)k_msleep(5U);
			continue;
		}

		if ((rawPacket[0] & 0x0FU) == 0x0FU) {
			local_discards++;
			(void)k_usleep(30U);
			continue;
		}

		if (rawPacket[1] >= PACKETS_PER_SEG) {
			local_discards++;
			continue;
		}

		if (rawPacket[1] == 20U) {
			const uint8_t seg_bits = (uint8_t)((rawPacket[0] >> 4U) & 0x07U);
			if ((seg_bits > 0U) && (seg_bits <= NUM_SEGMENTS)) {
				current_seg = seg_bits;
			}
		}

		if (current_seg > 0U) {
			const uint32_t base_idx =
				(((uint32_t)(current_seg - 1U) * (uint32_t)PACKETS_PER_SEG * (uint32_t)PIXELS_PER_PKT) +
				 ((uint32_t)rawPacket[1] * (uint32_t)PIXELS_PER_PKT));

			if ((base_idx + PIXELS_PER_PKT) <= (uint32_t)FRAME_SIZE) {
				for (uint32_t i = 0U; i < PIXELS_PER_PKT; i++) {
					const uint8_t msb = rawPacket[4U + (i * 2U)];
					const uint8_t lsb = rawPacket[5U + (i * 2U)];
					frame_buffer[base_idx + i] = (uint16_t)(((uint16_t)lsb << 8U) | (uint16_t)msb);
				}
			}

			if (rawPacket[1] == (PACKETS_PER_SEG - 1U)) {
				if (current_seg == NUM_SEGMENTS) {
					*discard_count = local_discards;
					return true;
				}
				current_seg++;
			}
		}
	}
}

void main(void) {
	uint16_t frame_id = 1U;
	uint32_t consecutive_capture_timeouts = 0U;

	setup_pinmux();
	(void)usb_enable(NULL);

	i2c_dev = DEVICE_DT_GET(I2C_DEV_NODE);
	lepton_spi_dev = DEVICE_DT_GET(LEPTON_SPI_DEV_NODE);
#if DT_NODE_HAS_STATUS(FRAME_SPI_NODE, okay)
	frame_spi_dev = DEVICE_DT_GET(FRAME_SPI_NODE);
#else
	frame_spi_dev = NULL;
#endif
	lepton_cs_gpio = DEVICE_DT_GET(LEPTON_CS_GPIO_NODE);
	frame_cs_gpio = DEVICE_DT_GET(FRAME_CS_GPIO_NODE);

	if ((!device_is_ready(i2c_dev)) || (!device_is_ready(lepton_spi_dev)) ||
	    (!device_is_ready(lepton_cs_gpio)) ||
	    (!device_is_ready(frame_cs_gpio))) {
		printk("Devices not ready\n");
	} else {
		(void)gpio_pin_configure(lepton_cs_gpio, LEPTON_CS_PIN, GPIO_OUTPUT_ACTIVE);
		(void)gpio_pin_set(lepton_cs_gpio, LEPTON_CS_PIN, 1);

		(void)k_msleep(1000U);

		if (!configureLepton()) {
			printk("Lepton init failed after retries\n");
			while (true) {
				(void)k_msleep(1000U);
			}
		}
		printk("Lepton init complete, SPI frame link ready\n");
		for (uint8_t i = 0U; i < FRAME_BUFFER_COUNT; i++) {
			(void)k_msgq_put(&free_frame_queue, &i, K_NO_WAIT);
		}
		(void)k_thread_create(&frame_sender_thread,
		                      frame_sender_stack,
		                      K_THREAD_STACK_SIZEOF(frame_sender_stack),
		                      frameSenderThread,
		                      NULL,
		                      NULL,
		                      NULL,
		                      5,
		                      0,
		                      K_NO_WAIT);

		while (true) {
			uint32_t discard_count = 0U;
			uint8_t buffer_idx = 0U;

			if (k_msgq_get(&free_frame_queue, &buffer_idx, K_NO_WAIT) != 0) {
				if (k_msgq_get(&ready_frame_queue, &buffer_idx, K_NO_WAIT) == 0) {
					dropped_ready_frames++;
				} else {
					(void)k_msleep(2U);
					continue;
				}
			}

			frameIds[buffer_idx] = frame_id;
			if (!captureFrame(frameBuffers[buffer_idx], &discard_count)) {
				consecutive_capture_timeouts++;
				printk("Capture timeout: discards=%u consecutive=%u\n",
				       discard_count,
				       consecutive_capture_timeouts);
				(void)k_msgq_put(&free_frame_queue, &buffer_idx, K_NO_WAIT);
				if (consecutive_capture_timeouts >= CAPTURE_TIMEOUT_RESET_THRESHOLD) {
					printk("Capture timeout threshold reached, resetting Lepton\n");
					resetLepton();
					consecutive_capture_timeouts = 0U;
				} else {
					(void)k_msleep(20U);
				}
				continue;
			}
			consecutive_capture_timeouts = 0U;
			if (k_msgq_put(&ready_frame_queue, &buffer_idx, K_NO_WAIT) != 0) {
				dropped_ready_frames++;
				(void)k_msgq_put(&free_frame_queue, &buffer_idx, K_NO_WAIT);
			}
			frame_id++;
		}
	}
}
