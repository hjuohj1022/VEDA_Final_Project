/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : USART1 command interface + PCA9685 3-channel servo control
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"

/* USER CODE BEGIN Includes */
#include <stdbool.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "motor.h"
/* USER CODE END Includes */

SPI_HandleTypeDef  hspi1;
DMA_HandleTypeDef  hdma_spi1_rx;
DMA_HandleTypeDef  hdma_spi1_tx;
TIM_HandleTypeDef  htim2;
UART_HandleTypeDef huart2;
I2C_HandleTypeDef  hi2c1;

/* USER CODE BEGIN PV */
#define UART_RX_BUF_SIZE  64U

static uint8_t uart_rx_byte;
static char uart_cmd_buf[UART_RX_BUF_SIZE];
static uint8_t uart_cmd_len;
static volatile bool uart_cmd_ready;
/* USER CODE END PV */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM2_Init(void);
static void MX_I2C1_Init(void);

/* USER CODE BEGIN PFP */
static void processUartCommand(void);
static void uartStartReceiveIT(void);
static void uartSendText(const char *text);
static void uartSendAngles(void);
static void uartSendBootDiagnostics(void);
static void uartSendOkWithAngles(void);
static char *trimCommand(char *text);
/* USER CODE END PFP */

/* USER CODE BEGIN 0 */
int32_t __io_putchar(int32_t ch)
{
    (void)ch;
    return ch;
}
/* USER CODE END 0 */

int main(void)
{
    (void)HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART2_UART_Init();
    MX_TIM2_Init();
    MX_I2C1_Init();

    /* USER CODE BEGIN 2 */
    uartSendBootDiagnostics();

    {
        const int32_t init_result = Motor_Init(&hi2c1);
        if (init_result != 0)
        {
            char response[48];
            (void)snprintf(response, sizeof(response), "ERR PCA9685 %ld\r\n", (long)init_result);
            uartSendText(response);
        }
        else
        {
            uartSendText("READY\r\n");
        }
    }

    uartStartReceiveIT();
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN WHILE */
        Motor_Update();

        if (uart_cmd_ready)
        {
            processUartCommand();
        }
        /* USER CODE END WHILE */
    }
}

/* USER CODE BEGIN 4 */
static void uartSendText(const char *text)
{
    if (text != NULL)
    {
        const size_t len = strlen(text);
        if (len > 0U)
        {
            (void)HAL_UART_Transmit(&huart2, (uint8_t *)text, (uint16_t)len, HAL_MAX_DELAY);
        }
    }
}

static void uartSendAngles(void)
{
    char response[48];

    (void)snprintf(response, sizeof(response), "ANGLES %d %d %d\r\n",
                   (int)Motor_GetAngle(0U),
                   (int)Motor_GetAngle(1U),
                   (int)Motor_GetAngle(2U));
    uartSendText(response);
}

static void uartSendOkWithAngles(void)
{
    char response[56];

    (void)snprintf(response, sizeof(response), "OK %d %d %d\r\n",
                   (int)Motor_GetAngle(0U),
                   (int)Motor_GetAngle(1U),
                   (int)Motor_GetAngle(2U));
    uartSendText(response);
}

static char *trimCommand(char *text)
{
    char *start = text;
    char *end;

    while ((*start != '\0') && (isspace((unsigned char)*start) != 0))
    {
        start++;
    }

    end = start + strlen(start);
    while ((end > start) && (isspace((unsigned char)end[-1]) != 0))
    {
        end--;
    }
    *end = '\0';

    return start;
}

static void uartSendBootDiagnostics(void)
{
    char response[48];
    uint8_t found = 0U;

    uartSendText("BOOT USART1 PA9/PA10\r\n");
    uartSendText("I2C scan start\r\n");

    for (uint8_t addr = 0x01U; addr < 0x7FU; addr++)
    {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)((uint16_t)addr << 1U), 1U, 10U) == HAL_OK)
        {
            (void)snprintf(response, sizeof(response), "I2C 0x%02X\r\n", (unsigned int)addr);
            uartSendText(response);
            found = 1U;
        }
    }

    if (found == 0U)
    {
        uartSendText("I2C none\r\n");
    }
}

static void uartStartReceiveIT(void)
{
    (void)HAL_UART_Receive_IT(&huart2, &uart_rx_byte, 1U);
}

static void processUartCommand(void)
{
    char *cmd;

    uart_cmd_ready = false;

    if (uart_cmd_len == 0U)
    {
        uartStartReceiveIT();
        return;
    }

    cmd = trimCommand(uart_cmd_buf);

    if ((strcmp(cmd, "read") == 0) || (strcmp(cmd, "READ") == 0))
    {
        uartSendAngles();
    }
    else if ((strcmp(cmd, "ping") == 0) || (strcmp(cmd, "PING") == 0))
    {
        uartSendText("PONG\r\n");
    }
    else if ((strcmp(cmd, "stopall") == 0) || (strcmp(cmd, "STOPALL") == 0))
    {
        Motor_StopAll();
        uartSendOkWithAngles();
    }
    else if ((strcmp(cmd, "help") == 0) || (strcmp(cmd, "HELP") == 0))
    {
        uartSendText("CMD motor<N> left press\r\n");
        uartSendText("CMD motor<N> right press\r\n");
        uartSendText("CMD motor<N> release\r\n");
        uartSendText("CMD motor<N> set <deg>\r\n");
        uartSendText("CMD read | ping | stopall\r\n");
    }
    else
    {
        const int32_t result = Motor_ParseAndRun(cmd);
        if (result == 0)
        {
            uartSendOkWithAngles();
        }
        else
        {
            uartSendText("ERR\r\n");
        }
    }

    uart_cmd_len = 0U;
    (void)memset(uart_cmd_buf, 0, sizeof(uart_cmd_buf));
    uartStartReceiveIT();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        if ((uart_rx_byte == '\r') || (uart_rx_byte == '\n'))
        {
            if (uart_cmd_len > 0U)
            {
                uart_cmd_buf[uart_cmd_len] = '\0';
                uart_cmd_ready = true;
                return;
            }
        }
        else if (uart_cmd_len < (UART_RX_BUF_SIZE - 1U))
        {
            uart_cmd_buf[uart_cmd_len] = (char)uart_rx_byte;
            uart_cmd_len++;
        }
        else
        {
            uart_cmd_len = 0U;
            (void)memset(uart_cmd_buf, 0, sizeof(uart_cmd_buf));
            uartSendText("ERR overflow\r\n");
        }

        uartStartReceiveIT();
    }
}
/* USER CODE END 4 */

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    RCC_OscInitStruct.PLL.PLLM = 16;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 400000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }
}

static void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_SLAVE;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_HARD_INPUT;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 10;
}

static void MX_TIM2_Init(void)
{
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 83;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = 3029;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
    {
        Error_Handler();
    }

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
    {
        Error_Handler();
    }

    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 1520;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_TIM_MspPostInit(&htim2);
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART1;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        Error_Handler();
    }
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

    GPIO_InitStruct.Pin = B1_Pin;
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
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
