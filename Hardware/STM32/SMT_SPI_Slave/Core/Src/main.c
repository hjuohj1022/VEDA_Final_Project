/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : SPI Slave 32바이트 패킷 + PCA9685 3채널 서보 제어
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"

/* USER CODE BEGIN Includes */
#include <string.h>
#include <stdio.h>
#include "motor.h"
/* USER CODE END Includes */

SPI_HandleTypeDef  hspi1;
DMA_HandleTypeDef  hdma_spi1_rx;
DMA_HandleTypeDef  hdma_spi1_tx;
TIM_HandleTypeDef  htim2;       /* CubeMX 생성 유지 (미사용이어도 무방) */
UART_HandleTypeDef huart2;
I2C_HandleTypeDef  hi2c1;       /* ← PCA9685용 I2C1 */

/* USER CODE BEGIN PV */
#define SPI_PKT_SIZE   32
#define MAX_DATA_LEN   30
#define CMD_WRITE      0x01
#define CMD_READ       0x02
#define STATUS_OK      0x00
#define STATUS_ERROR   0xFF

static uint8_t spi_rx_buf[SPI_PKT_SIZE];
static uint8_t spi_tx_buf[SPI_PKT_SIZE];
/* USER CODE END PV */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_I2C1_Init(void);   /* ← 추가 */

/* USER CODE BEGIN PFP */
static void processPacket(void);
static void prepareResponse(uint8_t status, const uint8_t *data, uint8_t len);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
int __io_putchar(int ch) {
    HAL_UART_Transmit(&huart2, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}
/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_SPI1_Init();
    MX_USART2_UART_Init();
    MX_TIM2_Init();
    MX_I2C1_Init();   /* ← 추가 */

    /* USER CODE BEGIN 2 */
    /* USER CODE BEGIN 2 */
    printf("I2C scan...\r\n");
    for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, addr << 1, 1, 10) == HAL_OK) {
            printf("Found device at 0x%02X\r\n", addr);
        }
    }
    if (Motor_Init(&hi2c1) != 0) {
        printf("PCA9685 init FAIL\r\n");
    } else {
        printf("PCA9685 init OK - 3 servos ready\r\n");
    }
    prepareResponse(STATUS_OK, NULL, 0);
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN WHILE */
        Motor_Update();

        HAL_StatusTypeDef ret =
            HAL_SPI_TransmitReceive(&hspi1, spi_tx_buf, spi_rx_buf,
                                    SPI_PKT_SIZE, 10);
        if (ret == HAL_OK) {
            processPacket();
            prepareResponse(STATUS_OK, NULL, 0);
        } else if (ret == HAL_TIMEOUT) {
            /* No data received within 10ms, just continue */
        }
        /* USER CODE END WHILE */
    }
}

/* USER CODE BEGIN 4 */
static void processPacket(void)
{
    uint8_t  cmd  = spi_rx_buf[0];
    uint8_t  len  = spi_rx_buf[1];
    uint8_t *data = &spi_rx_buf[2];

    if (cmd != CMD_WRITE && cmd != CMD_READ) {
        prepareResponse(STATUS_OK, NULL, 0);
        return;
    }
    if (len > MAX_DATA_LEN) len = MAX_DATA_LEN;

    switch (cmd)
    {
    case CMD_WRITE:
    {
        char cmd_str[MAX_DATA_LEN + 1];
        memcpy(cmd_str, data, len);
        cmd_str[len] = '\0';

        printf("RX: '%s'\r\n", cmd_str);

        /* "motor1/2/3 left/right/set <deg>" */
        int result = Motor_ParseAndRun(cmd_str);
        if (result == 0) {
            /* 명령에서 모터 번호 추출 (로그용) */
            int mid_log = 0;
            sscanf(cmd_str, " motor%d", &mid_log);
            if (mid_log >= 1 && mid_log <= MOTOR_NUM) {
                printf("motor%d -> %d deg\r\n", mid_log, Motor_GetAngle((uint8_t)(mid_log - 1)));
            }
            prepareResponse(STATUS_OK, NULL, 0);
        } else {
            printf("Unknown cmd\r\n");
            prepareResponse(STATUS_ERROR, NULL, 0);
        }
        break;
    }

    case CMD_READ:
    {
        /* 3개 모터 각도를 한 번에 반환: [m1_hi, m1_lo, m2_hi, m2_lo, m3_hi, m3_lo] */
        uint8_t payload[6];
        for (uint8_t i = 0; i < MOTOR_NUM; i++) {
            int16_t a = Motor_GetAngle(i);
            payload[i * 2]     = (uint8_t)(a >> 8);
            payload[i * 2 + 1] = (uint8_t)(a & 0xFF);
        }
        prepareResponse(STATUS_OK, payload, 6);
        break;
    }

    default:
        prepareResponse(STATUS_ERROR, NULL, 0);
        break;
    }
}

static void prepareResponse(uint8_t status, const uint8_t *data, uint8_t len)
{
    memset(spi_tx_buf, 0, SPI_PKT_SIZE);
    spi_tx_buf[0] = status;
    spi_tx_buf[1] = len;
    if (data && len > 0 && len <= MAX_DATA_LEN)
        memcpy(&spi_tx_buf[2], data, len);
}
/* USER CODE END 4 */

/* ── 주변장치 초기화 ─────────────────────────── */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM           = 16;
    RCC_OscInitStruct.PLL.PLLN           = 336;
    RCC_OscInitStruct.PLL.PLLP           = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ           = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) Error_Handler();
}

/**
 * @brief I2C1 초기화 (PCA9685용)
 *        PB6 = SCL, PB7 = SDA  (CubeMX로 생성 시 자동 처리됨)
 *        400kHz Fast Mode
 */
static void MX_I2C1_Init(void)
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 400000;
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2     = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) Error_Handler();
}

static void MX_SPI1_Init(void)
{
    hspi1.Instance            = SPI1;
    hspi1.Init.Mode           = SPI_MODE_SLAVE;
    hspi1.Init.Direction      = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize       = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity    = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase       = SPI_PHASE_1EDGE;
    hspi1.Init.NSS            = SPI_NSS_HARD_INPUT;
    hspi1.Init.FirstBit       = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode         = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial  = 10;
    if (HAL_SPI_Init(&hspi1) != HAL_OK) Error_Handler();
}

static void MX_TIM2_Init(void)
{
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};
    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 83;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 3029;   /* 330 Hz */
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) Error_Handler();
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK) Error_Handler();
    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = 1520;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK) Error_Handler();
    HAL_TIM_MspPostInit(&htim2);
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
}

static void MX_DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitStruct.Pin  = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
