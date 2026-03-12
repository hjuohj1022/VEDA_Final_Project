/*
 * Copyright (c) 2013 - 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017, 2024 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "fsl_component_serial_port_usb.h"
#include "fsl_gpio.h"
#include "fsl_iomuxc.h"
#include "fsl_lpi2c.h"
#include "fsl_lpspi.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "board.h"
#include "usb_device_config.h"
#include "usb_phy.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define LEP_CCI_ADDRESS            (0x2AU)
#define USB_CONSOLE_CONTROLLER_ID  kSerialManager_UsbControllerEhci0
#define PACKET_SIZE                (164U)
#define PACKETS_PER_SEG            (60U)
#define PIXELS_PER_PKT             (80U)
#define NUM_SEGMENTS               (4U)
#define FRAME_SIZE                 (160U * 120U)
#define FRAME_BYTES                (FRAME_SIZE * 2U)

#define LEP_REG_STATUS             (0x0002U)
#define LEP_REG_COMMAND            (0x0004U)
#define LEP_REG_LENGTH             (0x0006U)
#define LEP_REG_DATA0              (0x0008U)
#define LEP_CMD_AGC_ENABLE         (0x0101U)
#define LEP_CMD_VID_OUTPUT         (0x0204U)

#define LEPTON_I2C_BAUDRATE        (400000U)
#define LEPTON_SPI_CLOCK_HZ        (18000000U)

#define FRAME_SPI_PKT_SIZE         (256U)
#define FRAME_SPI_CLOCK_HZ         (4000000U)
#define FRAME_SPI_GAP_US           (300U)
#define FRAME_SPI_CS_SETUP_US      (5U)
#define FRAME_SPI_CS_HOLD_US       (5U)
#define FRAME_POST_SEND_SLEEP_MS   (250U)
#define FRAME_SPI_HEADER_SIZE      (14U)
#define FRAME_SPI_PAYLOAD_SIZE     (FRAME_SPI_PKT_SIZE - FRAME_SPI_HEADER_SIZE)

#define FRAME_CS_GPIO              GPIO1
#define FRAME_CS_GPIO_PIN          (3U)

/*******************************************************************************
 * Variables
 ******************************************************************************/
static uint16_t s_frameBuffer[FRAME_SIZE];
static uint8_t s_rawPacket[PACKET_SIZE];
static uint8_t s_frameTxPacket[FRAME_SPI_PKT_SIZE];
static uint32_t s_ticksPerUs = 1U;
static bool s_frameSpiReady = false;

/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void timerInit(void);
static void boardConfigUsbMpu(void);
static void usbDeviceClockInit(void);
static uint32_t uptimeMs(void);
static void delayUs(uint32_t delayUsValue);
static void delayMs(uint32_t delayMsValue);
static void setupLeptonPinmux(void);
static void setupFrameSpiPinmux(void);
static void initLeptonI2c(void);
static void initLeptonSpi(void);
static void ensureFrameSpiReady(void);
static void resetLepton(void);
static bool cciWriteReg(uint16_t reg, uint16_t val);
static bool cciReadReg(uint16_t reg, uint16_t *outVal);
static bool cciWaitBusy(uint32_t timeoutMs);
static bool cciSet(uint16_t cmdId, uint16_t value);
static bool configureLepton(void);
static uint16_t frameChecksum(const uint8_t *data, uint16_t len);
static void frameWriteU16Be(uint8_t *dst, uint16_t value);
static int sendFrameOverSpi(uint16_t frameId);
static bool captureFrame(uint32_t *discardCount);

/*******************************************************************************
 * Code
 ******************************************************************************/
static void timerInit(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
#if defined(DWT_LAR)
    DWT->LAR = 0xC5ACCE55U;
#endif
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_ticksPerUs = SystemCoreClock / 1000000U;
    if (s_ticksPerUs == 0U)
    {
        s_ticksPerUs = 1U;
    }
}

static void boardConfigUsbMpu(void)
{
    SCB_DisableICache();
    SCB_DisableDCache();

    ARM_MPU_Disable();
    MPU->RBAR = ARM_MPU_RBAR(7, 0x80000000U);
    MPU->RASR = ARM_MPU_RASR(0, ARM_MPU_AP_FULL, 0, 1, 1, 1, 0, ARM_MPU_REGION_SIZE_32MB);
    ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk | MPU_CTRL_HFNMIENA_Msk);

    SCB_EnableDCache();
    SCB_EnableICache();
}

static void usbDeviceClockInit(void)
{
#if defined(USB_DEVICE_CONFIG_EHCI) && (USB_DEVICE_CONFIG_EHCI > 0U)
    usb_phy_config_struct_t phyConfig = {
        BOARD_USB_PHY_D_CAL,
        BOARD_USB_PHY_TXCAL45DP,
        BOARD_USB_PHY_TXCAL45DM,
    };

    if (USB_CONSOLE_CONTROLLER_ID == kSerialManager_UsbControllerEhci0)
    {
        CLOCK_EnableUsbhs0PhyPllClock(kCLOCK_Usbphy480M, 480000000U);
        CLOCK_EnableUsbhs0Clock(kCLOCK_Usb480M, 480000000U);
    }
    else
    {
        CLOCK_EnableUsbhs1PhyPllClock(kCLOCK_Usbphy480M, 480000000U);
        CLOCK_EnableUsbhs1Clock(kCLOCK_Usb480M, 480000000U);
    }
    USB_EhciPhyInit(USB_CONSOLE_CONTROLLER_ID, BOARD_XTAL0_CLK_HZ, &phyConfig);
#endif
}

static uint32_t uptimeMs(void)
{
    return DWT->CYCCNT / (s_ticksPerUs * 1000U);
}

static void delayUs(uint32_t delayUsValue)
{
    uint32_t startCycles = DWT->CYCCNT;
    uint32_t waitCycles  = delayUsValue * s_ticksPerUs;

    while ((DWT->CYCCNT - startCycles) < waitCycles)
    {
    }
}

static void delayMs(uint32_t delayMsValue)
{
    while (delayMsValue > 0U)
    {
        delayUs(1000U);
        delayMsValue--;
    }
}

static void setupLeptonPinmux(void)
{
    CLOCK_EnableClock(kCLOCK_Iomuxc);

    /* LPI2C1 on Teensy pins 19(SCL) / 18(SDA). */
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_00_LPI2C1_SCL, 1U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_01_LPI2C1_SDA, 1U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_00_LPI2C1_SCL, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_01_LPI2C1_SDA, 0x10B0U);

    /* LPSPI4 on Teensy pins 10(CS) / 12(MISO) / 11(MOSI) / 13(SCK). */
    IOMUXC_SetPinMux(IOMUXC_GPIO_B0_00_LPSPI4_PCS0, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_B0_01_LPSPI4_SDI, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_B0_02_LPSPI4_SDO, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_B0_03_LPSPI4_SCK, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B0_00_LPSPI4_PCS0, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B0_01_LPSPI4_SDI, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B0_02_LPSPI4_SDO, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B0_03_LPSPI4_SCK, 0x10B0U);
}

static void setupFrameSpiPinmux(void)
{
    /* SPI frame link on Teensy pins 0(CS GPIO) / 1(MISO) / 26(MOSI) / 27(SCK). */
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_03_GPIO1_IO03, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_02_LPSPI3_SDI, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_14_LPSPI3_SDO, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B1_15_LPSPI3_SCK, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_03_GPIO1_IO03, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B0_02_LPSPI3_SDI, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_14_LPSPI3_SDO, 0x10B0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_AD_B1_15_LPSPI3_SCK, 0x10B0U);
}

static void initLeptonI2c(void)
{
    lpi2c_master_config_t masterConfig;

    CLOCK_EnableClock(kCLOCK_Lpi2c1);
    CLOCK_SetMux(kCLOCK_Lpi2cMux, 0U);
    CLOCK_SetDiv(kCLOCK_Lpi2cDiv, 0U);

    LPI2C_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate_Hz = LEPTON_I2C_BAUDRATE;
    LPI2C_MasterInit(LPI2C1, &masterConfig, BOARD_BOOTCLOCKRUN_LPI2C_CLK_ROOT);
}

static void initLeptonSpi(void)
{
    lpspi_master_config_t masterConfig;

    CLOCK_EnableClock(kCLOCK_Lpspi4);

    LPSPI_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate = LEPTON_SPI_CLOCK_HZ;
    masterConfig.whichPcs = kLPSPI_Pcs0;
    masterConfig.cpol     = kLPSPI_ClockPolarityActiveLow;
    masterConfig.cpha     = kLPSPI_ClockPhaseSecondEdge;
    LPSPI_MasterInit(LPSPI4, &masterConfig, BOARD_BOOTCLOCKRUN_LPSPI_CLK_ROOT);
}

static void ensureFrameSpiReady(void)
{
    lpspi_master_config_t masterConfig;
    gpio_pin_config_t frameCsConfig = {kGPIO_DigitalOutput, 1U, kGPIO_NoIntmode};

    if (s_frameSpiReady)
    {
        return;
    }

    setupFrameSpiPinmux();
    CLOCK_EnableClock(kCLOCK_Gpio1);
    CLOCK_EnableClock(kCLOCK_Lpspi3);
    GPIO_PinInit(FRAME_CS_GPIO, FRAME_CS_GPIO_PIN, &frameCsConfig);

    LPSPI_MasterGetDefaultConfig(&masterConfig);
    masterConfig.baudRate = FRAME_SPI_CLOCK_HZ;
    masterConfig.whichPcs = kLPSPI_Pcs0;
    LPSPI_MasterInit(LPSPI3, &masterConfig, BOARD_BOOTCLOCKRUN_LPSPI_CLK_ROOT);

    s_frameSpiReady = true;
    PRINTF("Frame SPI pinmux ready\r\n");
}

static void resetLepton(void)
{
    delayMs(200U);
}

static bool cciWriteReg(uint16_t reg, uint16_t val)
{
    uint8_t data[4];
    lpi2c_master_transfer_t transfer;

    data[0] = (uint8_t)(reg >> 8U);
    data[1] = (uint8_t)(reg & 0xFFU);
    data[2] = (uint8_t)(val >> 8U);
    data[3] = (uint8_t)(val & 0xFFU);

    (void)memset(&transfer, 0, sizeof(transfer));
    transfer.flags        = kLPI2C_TransferDefaultFlag;
    transfer.slaveAddress = LEP_CCI_ADDRESS;
    transfer.direction    = kLPI2C_Write;
    transfer.data         = data;
    transfer.dataSize     = sizeof(data);

    if (LPI2C_MasterTransferBlocking(LPI2C1, &transfer) != kStatus_Success)
    {
        return false;
    }

    delayMs(5U);
    return true;
}

static bool cciReadReg(uint16_t reg, uint16_t *outVal)
{
    uint8_t data[2] = {0U, 0U};
    lpi2c_master_transfer_t transfer;

    if (outVal == NULL)
    {
        return false;
    }

    (void)memset(&transfer, 0, sizeof(transfer));
    transfer.flags          = kLPI2C_TransferDefaultFlag;
    transfer.slaveAddress   = LEP_CCI_ADDRESS;
    transfer.direction      = kLPI2C_Read;
    transfer.subaddress     = reg;
    transfer.subaddressSize = 2U;
    transfer.data           = data;
    transfer.dataSize       = sizeof(data);

    if (LPI2C_MasterTransferBlocking(LPI2C1, &transfer) != kStatus_Success)
    {
        return false;
    }

    *outVal = (uint16_t)(((uint16_t)data[0] << 8U) | (uint16_t)data[1]);
    return true;
}

static bool cciWaitBusy(uint32_t timeoutMs)
{
    uint32_t startMs = uptimeMs();
    uint16_t status = 0U;

    while ((uptimeMs() - startMs) < timeoutMs)
    {
        if (!cciReadReg(LEP_REG_STATUS, &status))
        {
            break;
        }

        if ((status & 0x0001U) == 0U)
        {
            return true;
        }

        delayMs(10U);
    }

    return false;
}

static bool cciSet(uint16_t cmdId, uint16_t value)
{
    if (!cciWaitBusy(2000U))
    {
        return false;
    }

    return cciWriteReg(LEP_REG_DATA0, value) &&
           cciWriteReg(LEP_REG_LENGTH, 1U) &&
           cciWriteReg(LEP_REG_COMMAND, (uint16_t)(cmdId | 0x0002U)) &&
           cciWaitBusy(2000U);
}

static bool configureLepton(void)
{
    uint8_t attempt;

    for (attempt = 1U; attempt <= 3U; attempt++)
    {
        if (cciSet(LEP_CMD_AGC_ENABLE, 0x0000U) &&
            cciSet(LEP_CMD_VID_OUTPUT, 0x0007U))
        {
            return true;
        }

        PRINTF("Lepton config attempt %u failed\r\n", attempt);
        resetLepton();
    }

    return false;
}

static uint16_t frameChecksum(const uint8_t *data, uint16_t len)
{
    uint32_t sum = 0U;
    uint16_t index;

    for (index = 0U; index < len; index++)
    {
        sum += data[index];
    }

    return (uint16_t)(sum & 0xFFFFU);
}

static void frameWriteU16Be(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)((value >> 8U) & 0xFFU);
    dst[1] = (uint8_t)(value & 0xFFU);
}

static int sendFrameOverSpi(uint16_t frameId)
{
    const uint8_t *frameBytes =
        (const uint8_t *)(const void *)s_frameBuffer;
    const uint16_t totalChunks =
        (uint16_t)((FRAME_BYTES + FRAME_SPI_PAYLOAD_SIZE - 1U) / FRAME_SPI_PAYLOAD_SIZE);
    uint16_t chunkIdx;

    ensureFrameSpiReady();
    if (!s_frameSpiReady)
    {
        return -1;
    }

    for (chunkIdx = 0U; chunkIdx < totalChunks; chunkIdx++)
    {
        size_t offset = (size_t)chunkIdx * (size_t)FRAME_SPI_PAYLOAD_SIZE;
        uint16_t payloadLen =
            (uint16_t)(((FRAME_BYTES - offset) > (size_t)FRAME_SPI_PAYLOAD_SIZE) ?
                           (size_t)FRAME_SPI_PAYLOAD_SIZE :
                           (FRAME_BYTES - offset));
        lpspi_transfer_t transfer;

        (void)memset(s_frameTxPacket, 0, sizeof(s_frameTxPacket));
        s_frameTxPacket[0] = (uint8_t)'T';
        s_frameTxPacket[1] = (uint8_t)'E';
        s_frameTxPacket[2] = (uint8_t)'S';
        s_frameTxPacket[3] = (uint8_t)'T';
        frameWriteU16Be(&s_frameTxPacket[4], frameId);
        frameWriteU16Be(&s_frameTxPacket[6], chunkIdx);
        frameWriteU16Be(&s_frameTxPacket[8], totalChunks);
        frameWriteU16Be(&s_frameTxPacket[10], payloadLen);
        (void)memcpy(&s_frameTxPacket[FRAME_SPI_HEADER_SIZE], &frameBytes[offset], payloadLen);
        frameWriteU16Be(&s_frameTxPacket[12],
                        frameChecksum(&s_frameTxPacket[FRAME_SPI_HEADER_SIZE], payloadLen));

        (void)memset(&transfer, 0, sizeof(transfer));
        transfer.txData      = s_frameTxPacket;
        transfer.dataSize    = FRAME_SPI_PKT_SIZE;
        transfer.configFlags = kLPSPI_MasterPcs0;

        GPIO_PinWrite(FRAME_CS_GPIO, FRAME_CS_GPIO_PIN, 0U);
        delayUs(FRAME_SPI_CS_SETUP_US);
        if (LPSPI_MasterTransferBlocking(LPSPI3, &transfer) != kStatus_Success)
        {
            GPIO_PinWrite(FRAME_CS_GPIO, FRAME_CS_GPIO_PIN, 1U);
            PRINTF("Frame SPI write failed: frame=%u chunk=%u/%u\r\n",
                   frameId,
                   (uint32_t)chunkIdx + 1U,
                   totalChunks);
            return -1;
        }
        delayUs(FRAME_SPI_CS_HOLD_US);
        GPIO_PinWrite(FRAME_CS_GPIO, FRAME_CS_GPIO_PIN, 1U);
        delayUs(FRAME_SPI_GAP_US);
    }

    return 0;
}

static bool captureFrame(uint32_t *discardCount)
{
    uint32_t startMs = uptimeMs();
    uint32_t localDiscards = 0U;
    uint8_t currentSeg = 0U;

    if (discardCount == NULL)
    {
        return false;
    }

    while (true)
    {
        lpspi_transfer_t transfer;
        status_t status;

        if ((uptimeMs() - startMs) > 3000U)
        {
            *discardCount = localDiscards;
            return false;
        }

        (void)memset(&transfer, 0, sizeof(transfer));
        transfer.rxData      = s_rawPacket;
        transfer.dataSize    = PACKET_SIZE;
        transfer.configFlags = kLPSPI_MasterPcs0;
        status = LPSPI_MasterTransferBlocking(LPSPI4, &transfer);
        if (status != kStatus_Success)
        {
            localDiscards++;
            if ((localDiscards % 20U) == 0U)
            {
                PRINTF("Lepton SPI read failed: status=%d discards=%u\r\n", status, localDiscards);
            }
            delayMs(5U);
            continue;
        }

        if ((s_rawPacket[0] & 0x0FU) == 0x0FU)
        {
            localDiscards++;
            delayUs(30U);
            continue;
        }

        if (s_rawPacket[1] >= PACKETS_PER_SEG)
        {
            localDiscards++;
            continue;
        }

        if (s_rawPacket[1] == 20U)
        {
            uint8_t segBits = (uint8_t)((s_rawPacket[0] >> 4U) & 0x07U);
            if ((segBits > 0U) && (segBits <= NUM_SEGMENTS))
            {
                currentSeg = segBits;
            }
        }

        if (currentSeg > 0U)
        {
            uint32_t baseIdx =
                (((uint32_t)(currentSeg - 1U) * (uint32_t)PACKETS_PER_SEG * (uint32_t)PIXELS_PER_PKT) +
                 ((uint32_t)s_rawPacket[1] * (uint32_t)PIXELS_PER_PKT));

            if ((baseIdx + PIXELS_PER_PKT) <= (uint32_t)FRAME_SIZE)
            {
                uint32_t pixelIndex;

                for (pixelIndex = 0U; pixelIndex < PIXELS_PER_PKT; pixelIndex++)
                {
                    uint8_t msb = s_rawPacket[4U + (pixelIndex * 2U)];
                    uint8_t lsb = s_rawPacket[5U + (pixelIndex * 2U)];
                    s_frameBuffer[baseIdx + pixelIndex] =
                        (uint16_t)(((uint16_t)lsb << 8U) | (uint16_t)msb);
                }
            }

            if (s_rawPacket[1] == 59U)
            {
                if (currentSeg == NUM_SEGMENTS)
                {
                    *discardCount = localDiscards;
                    return true;
                }
                currentSeg++;
            }
        }
    }
}

int main(void)
{
    uint16_t frameId = 1U;

    BOARD_ConfigMPU();
    boardConfigUsbMpu();
    BOARD_InitBootClocks();
    usbDeviceClockInit();
    DbgConsole_Init((uint8_t)USB_CONSOLE_CONTROLLER_ID, 0U, kSerialPort_UsbCdc, 0U);

    SystemCoreClockUpdate();
    CLOCK_EnableClock(kCLOCK_Trace);
    timerInit();

    setupLeptonPinmux();
    initLeptonI2c();
    initLeptonSpi();
    ensureFrameSpiReady();

    PRINTF("Lepton init start\r\n");
    delayMs(1000U);

    if (!configureLepton())
    {
        PRINTF("Lepton init failed after retries\r\n");
        while (true)
        {
            delayMs(1000U);
        }
    }

    PRINTF("Lepton init complete, SPI frame link ready\r\n");

    while (true)
    {
        uint32_t discardCount = 0U;

        if (!captureFrame(&discardCount))
        {
            PRINTF("Capture timeout, resetting Lepton (discards=%u)\r\n", discardCount);
            resetLepton();
            continue;
        }

        if (sendFrameOverSpi(frameId) == 0)
        {
            PRINTF("Frame sent over SPI: id=%u\r\n", frameId);
            frameId++;
        }
        else
        {
            PRINTF("Frame SPI send failed: id=%u\r\n", frameId);
        }

        delayMs(FRAME_POST_SEND_SLEEP_MS);
    }
}
