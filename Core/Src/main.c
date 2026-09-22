/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : ADS1220 Wheatstone Bridge ADC Reader
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include "app_config.h"
#include "ADS1220.H"
#include "Filter.h"
#include "VOFA.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
/* USER CODE END Includes */

/* Private define ------------------------------------------------------------*/
/* 可调参数一律集中在 App/app_config.h，本文件只保留内部使用的宏。 */
#define BRIDGE_EXCITATION_V    5.0f   /* 电桥激励电压(V)，比例式接法下等于基准 */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADS1220_Handle_t hads;

static uint8_t uart_rx_byte;
static uint8_t uart_command_buffer[APP_CMD_BUFFER_SIZE];
static volatile uint8_t uart_command_length;
static volatile uint8_t uart_pending_command_length;
static volatile uint8_t uart_command_ready;
static volatile uint8_t uart_command_overflow;
volatile uint8_t conversion_output_enabled;
volatile uint8_t uart_command_received;
volatile uint8_t uart_received_command;
uint32_t adc_invalid_count;
static MovingAverage5_t adc_moving_average;
static FirstOrderLowPass_t adc_lowpass;
static uint8_t adc_filter_started;
static volatile uint8_t zero_calibrating;
static uint8_t zero_valid;
static int64_t zero_sum;
static uint32_t zero_sample_count;
static int32_t zero_offset;

/* USER CODE BEGIN PV */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void UART_Printf(const char *fmt, ...);
HAL_StatusTypeDef ADS1220_FullTest(void);
static void UART_ProcessCommand(void);
static void UART_CommandReceivedCallback(uint8_t command);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);

/* USER CODE BEGIN PFP */
void UART_Printf(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, strlen(buf), HAL_MAX_DELAY);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        if (uart_rx_byte == '\r' || uart_rx_byte == '\n')
        {
            if (uart_command_length > 0U && !uart_command_overflow &&
                !uart_command_ready)
            {
                uart_command_buffer[uart_command_length] = '\0';
                uart_pending_command_length = uart_command_length;
                uart_command_ready = 1U;
            }

            uart_command_length = 0U;
            uart_command_overflow = 0U;
        }
        else if (!uart_command_ready && !uart_command_overflow)
        {
            /* Accept a standalone ASCII 0/1/2 immediately, with or without CR/LF. */
            if (uart_command_length == 0U &&
                (uart_rx_byte == '0' || uart_rx_byte == '1' ||
                 uart_rx_byte == '2'))
            {
                uart_command_buffer[0] = uart_rx_byte;
                uart_command_buffer[1] = '\0';
                uart_pending_command_length = 1U;
                uart_command_ready = 1U;
            }
            else if (uart_command_length < (APP_CMD_BUFFER_SIZE - 1U))
            {
                uart_command_buffer[uart_command_length++] = uart_rx_byte;
            }
            else
            {
                uart_command_length = 0U;
                uart_command_overflow = 1U;
            }
        }

        /* Rearm reception for the next byte. */
        HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1U);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        /* Recover reception after an overrun, noise, or framing error. */
        HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1U);
    }
}

static void UART_ProcessCommand(void)
{
    uint8_t command[APP_CMD_BUFFER_SIZE];
    uint8_t command_length;

    if (!uart_command_ready)
        return;

    /* Copy and release the pending command atomically with respect to RX ISR. */
    __disable_irq();
    command_length = uart_pending_command_length;
    memcpy(command, uart_command_buffer, command_length + 1U);
    uart_command_ready = 0U;
    uart_command_length = 0U;
    __enable_irq();

    if (command_length == 1U && command[0] == '0')
    {
        UART_CommandReceivedCallback('0');
        adc_filter_started = 0U;
        conversion_output_enabled = 1U;
        UART_Printf("RX OK: 0\r\n");
        UART_Printf("Conversion output started\r\n");
    }
    else if (command_length == 1U && command[0] == '1')
    {
        UART_CommandReceivedCallback('1');
        conversion_output_enabled = 0U;
        UART_Printf("RX OK: 1\r\n");
        UART_Printf("Conversion output stopped\r\n");
    }
    else if (command_length == 1U && command[0] == '2')
    {
        UART_CommandReceivedCallback('2');

        /* Keep ADS1220 running, but temporarily stop sample output while
         * collecting an unloaded zero reference. */
        conversion_output_enabled = 0U;
        zero_calibrating = 1U;
        zero_sum = 0;
        zero_sample_count = 0U;
        UART_Printf("RX OK: 2\r\n");
        UART_Printf("Tare started: keep the sensor unloaded\r\n");
    }
    else
    {
        UART_Printf("Unknown command\r\n");
    }
}

/* Called after a complete, valid command has been received and parsed. */
static void UART_CommandReceivedCallback(uint8_t command)
{
    uart_received_command = command;
    uart_command_received = 1U;
}
/* USER CODE END PFP */

/**
  * @brief  ADS1220 上电自检：初始化、复位、配置、回读校验、启动连续转换。
  * @note   配置参数全部取自 App/app_config.h —— 打印、写入、校验三者同源，
  *         不会再出现"打印 64x 却写 128x"这类不一致。
  */
HAL_StatusTypeDef ADS1220_FullTest(void)
{
    uint8_t expected[4];
    uint8_t actual[4];

    UART_Printf("\r\n");
    UART_Printf("========================================\r\n");
    UART_Printf("  ADS1220 Initialization\r\n");
    UART_Printf("========================================\r\n\r\n");

    /* 1. 初始化 */
    UART_Printf("[1] Initializing ADS1220...\r\n");
    if (ADS1220_Init(&hads, &hspi2, ADS1220_CS_GPIO_Port, ADS1220_CS_Pin,
                     GPIOB, GPIO_PIN_14) != HAL_OK)
    {
        UART_Printf("     [ERROR] ADS1220_Init failed!\r\n");
        return HAL_ERROR;
    }
    UART_Printf("     [OK] ADS1220_Init success\r\n\r\n");

    /* 2. 发送 RESET */
    UART_Printf("[2] Sending RESET command...\r\n");
    if (ADS1220_Reset(&hads) != HAL_OK)
    {
        UART_Printf("     [ERROR] RESET failed!\r\n");
        return HAL_ERROR;
    }
    UART_Printf("     [OK] RESET success\r\n\r\n");

    /* 3. 读复位默认值：ADS1220 复位后 CONFIG0..3 全为 0x00。
     *    若读到 FF FF FF FF，说明 DOUT/MISO 恒高，芯片没有响应
     *    （未焊接 / 未供电 / 片选或时钟异常）。 */
    UART_Printf("[3] Reset defaults...\r\n");
    if (ADS1220_ReadAllRegs(&hads) != HAL_OK)
    {
        UART_Printf("     [ERROR] Failed to read registers!\r\n");
        return HAL_ERROR;
    }
    UART_Printf("     CONFIG0..3 = %02X %02X %02X %02X\r\n",
                hads.config[0], hads.config[1], hads.config[2], hads.config[3]);
    if (hads.config[0] == 0xFFU && hads.config[1] == 0xFFU &&
        hads.config[2] == 0xFFU && hads.config[3] == 0xFFU)
    {
        UART_Printf("     [ERROR] All registers read 0xFF - ADS1220 not responding\r\n");
        return HAL_ERROR;
    }
    UART_Printf("     [OK]\r\n\r\n");

    /* 4. 连通性自检：确认 SPI 写通路真正生效。
     *    先写一个不同的值再回读，可覆盖"目标配置恰好等于芯片当前值"这种
     *    仅靠回读比对无法发现的写通路故障。 */
    UART_Printf("[4] Connection test...\r\n");
    if (ADS1220_ConnectionTest(&hads) != HAL_OK)
    {
        UART_Printf("     [ERROR] Write/read probe failed - SPI write path broken\r\n");
        return HAL_ERROR;
    }
    UART_Printf("     [OK] Write/read probe passed\r\n\r\n");

    /* 5. 按 App/app_config.h 配置（写入与打印同源） */
    UART_Printf("[5] Configuring from App/app_config.h...\r\n");
    if (ADS1220_Config(&hads,
                       APP_ADC_CHANNEL,
                       APP_ADC_GAIN,
                       APP_ADC_DR,
                       APP_ADC_MODE,
                       APP_ADC_CONV,
                       APP_ADC_VREF) != HAL_OK)
    {
        UART_Printf("     [ERROR] ADS1220_Config failed!\r\n");
        return HAL_ERROR;
    }
    UART_Printf("     Channel    : %s\r\n", ADS1220_ChannelName(APP_ADC_CHANNEL));
    UART_Printf("     Gain       : %u\r\n", (unsigned)ADS1220_GetGainValue(&hads));
    UART_Printf("     Data rate  : %u SPS\r\n", (unsigned)ADS1220_DataRateSps(APP_ADC_DR));
    UART_Printf("     Mode       : %s\r\n",
                (APP_ADC_MODE == ADS1220_MOD_NORM)  ? "normal" :
                (APP_ADC_MODE == ADS1220_MOD_TURBO) ? "turbo" : "duty-cycle");
    UART_Printf("     Conversion : %s\r\n",
                (APP_ADC_CONV == ADS1220_CONV_CONT) ? "continuous" : "single-shot");
    UART_Printf("     VREF       : %s\r\n",
                (APP_ADC_VREF == ADS1220_VREF_INT)  ? "internal 2.048V" :
                (APP_ADC_VREF == ADS1220_VREF_EXT)  ? "external REFP0/REFN0" :
                (APP_ADC_VREF == ADS1220_VREF_AVDD) ? "AVDD" : "external REFP1/REFN1");
    UART_Printf("\r\n");

    /* 6. 回读校验：期望值取自驱动内部记录，此处不再硬编码寄存器常量 */
    UART_Printf("[6] Verifying registers...\r\n");
    if (ADS1220_VerifyConfig(&hads, expected, actual) == HAL_OK)
    {
        UART_Printf("     CONFIG0 = 0x%02X\r\n", actual[0]);
        UART_Printf("     CONFIG1 = 0x%02X\r\n", actual[1]);
        UART_Printf("     CONFIG2 = 0x%02X\r\n", actual[2]);
        UART_Printf("     CONFIG3 = 0x%02X\r\n", actual[3]);
        UART_Printf("     [OK] Registers verified\r\n\r\n");
    }
    else
    {
        UART_Printf("     CONFIG0: expect 0x%02X, actual 0x%02X\r\n", expected[0], actual[0]);
        UART_Printf("     CONFIG1: expect 0x%02X, actual 0x%02X\r\n", expected[1], actual[1]);
        UART_Printf("     CONFIG2: expect 0x%02X, actual 0x%02X\r\n", expected[2], actual[2]);
        UART_Printf("     CONFIG3: expect 0x%02X, actual 0x%02X\r\n", expected[3], actual[3]);
        UART_Printf("     [ERROR] Register verification failed\r\n");
        return HAL_ERROR;
    }

    /* 7. Start continuous conversion, but do not output samples yet. */
    UART_Printf("[7] Starting continuous conversion...\r\n");
    if (ADS1220_Start(&hads) != HAL_OK)
    {
        UART_Printf("     [ERROR] Failed to start continuous conversion!\r\n");
        return HAL_ERROR;
    }
    UART_Printf("     [OK] Continuous conversion started\r\n");
    UART_Printf("\r\nReady. Send 0 to start output, 1 to stop output.\r\n");

    return HAL_OK;
}

/**
  * @brief  The application entry point.
  */
int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();
    MX_SPI2_Init();

    if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1U) != HAL_OK)
    {
        Error_Handler();
    }

    UART_Printf("\r\n");
    UART_Printf("========================================\r\n");
    UART_Printf("  ADS1220 Wheatstone Bridge ADC Reader\r\n");
    UART_Printf("  STM32F103C8\r\n");
    UART_Printf("========================================\r\n");
    UART_Printf("  SPI2: PB13=SCK, PB14=MISO, PB15=MOSI\r\n");
    UART_Printf("  CS  : PB12\r\n");
    UART_Printf("  Command: 0=start, 1=stop, 2=tare\r\n");
    UART_Printf("========================================\r\n\r\n");

    /* ADC 配置在自检段中配置完成后再打印，保证描述与实际寄存器一致。 */
    if (ADS1220_FullTest() != HAL_OK)
    {
        UART_Printf("\r\nSelf-test FAILED - execution halted.\r\n");
        Error_Handler();
    }

    /* 主循环：轮询 DRDY，取数、去皮、滤波并输出 */
    while (1)
    {
        UART_ProcessCommand();

        static uint8_t drdy_low_count = 0U;
        static uint8_t drdy_read_locked = 0U;
        static uint32_t drdy_lock_tick = 0U;
        int32_t raw;
        uint8_t adc_data_ready = ADS1220_IsDataReady(&hads);

        /* ADC must also be read while a zero calibration is in progress. */
        if (!conversion_output_enabled && !zero_calibrating)
        {
            drdy_low_count = 0U;
            drdy_read_locked = 0U;
        }
        else if (drdy_read_locked)
        {
            /* Normal path: unlock when DOUT/DRDY returns high. */
            if (!adc_data_ready ||
                (HAL_GetTick() - drdy_lock_tick) >= APP_DRDY_TIMEOUT_MS)
            {
                drdy_read_locked = 0U;
                drdy_low_count = 0U;
            }
        }
        else if (adc_data_ready)
        {
            /* Require two consecutive low-level observations before reading. */
            if (drdy_low_count < APP_DRDY_CONFIRM)
                drdy_low_count++;

            if (drdy_low_count >= APP_DRDY_CONFIRM)
            {
                drdy_read_locked = 1U;
                drdy_lock_tick = HAL_GetTick();
                drdy_low_count = 0U;

                if (ADS1220_ReadDataContinuous(&hads, &raw) == HAL_OK)
                {
                    if ((uint32_t)raw == 0xFFFFFFFFUL)
                    {
                        adc_invalid_count++;
                        if ((adc_invalid_count % 20U) == 0U)
                        {
                            UART_Printf("ADC invalid sample count=%lu\r\n",
                                        (unsigned long)adc_invalid_count);
                        }
                    }
                    else if (zero_calibrating)
                    {
                        /* Collect only valid raw samples for the zero point.
                         * The ADC remains in continuous-conversion mode. */
                        zero_sum += (int64_t)raw;
                        zero_sample_count++;

                        if (zero_sample_count >= APP_TARE_SAMPLES)
                        {
                            zero_offset = (int32_t)(zero_sum /
                                                     (int64_t)zero_sample_count);
                            zero_valid = 1U;
                            zero_calibrating = 0U;
                            conversion_output_enabled = 0U;

                            /* Do not carry pre-tare samples into the output. */
                            adc_filter_started = 0U;

                            UART_Printf("Tare completed\r\n");
                            UART_Printf("Zero offset = %ld\r\n",
                                        (long)zero_offset);
                            UART_Printf("Send 0 to start zero-corrected output\r\n");
                        }
                    }
                    else if (conversion_output_enabled)
                    {
                        int32_t corrected_raw;
                        float average_value;
                        float filtered_value;
                        int32_t filtered_raw;
                        float vofa_values[3];

                        /* Apply tare before filtering. If no tare has been
                         * completed, preserve the original ADC code. */
                        corrected_raw = raw;
                        if (zero_valid != 0U)
                            corrected_raw = raw - zero_offset;

                        if (adc_filter_started == 0U)
                        {
                            /* Initialize both filters at the first valid code
                             * to avoid a startup transient from zero. */
                            MovingAverage5_Init(&adc_moving_average);
                            FirstOrderLowPass_Init(&adc_lowpass, APP_LPF_ALPHA);
                            adc_filter_started = 1U;
                        }

                        /* Filter only valid ADC samples:
                         * raw -> five-point average -> first-order LPF. */
                        average_value = MovingAverage5_Update(&adc_moving_average,
                                                              (float)corrected_raw);
                        filtered_value = FirstOrderLowPass_Update(&adc_lowpass,
                                                                  average_value);

                        /* Round the floating-point result to an ADC code. */
                        if (filtered_value >= 0.0f)
                        {
                            filtered_raw = (int32_t)(filtered_value + 0.5f);
                        }
                        else
                        {
                            filtered_raw = (int32_t)(filtered_value - 0.5f);
                        }

                        /* Send three curves to VOFA+:
                         * CH1: raw ADC code before tare;
                         * CH2: ADC code after tare;
                         * CH3: tare-corrected and filtered ADC code. */
                        vofa_values[0] = (float)raw;
                        vofa_values[1] = (float)corrected_raw;
                        vofa_values[2] = (float)filtered_raw;
                        VOFA_JustFloat_Send(&huart1, vofa_values, 3U);
                    }
                }
            }
        }
        else
        {
            drdy_low_count = 0U;
        }

        HAL_Delay(1);
    }
}

/**
  * @brief System Clock Configuration
  */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) { Error_Handler(); }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }
}

/**
  * @brief  This function is executed in case of error occurrence.
  */
void Error_Handler(void)
{
    __disable_irq();
    while (1) { }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) { }
#endif
