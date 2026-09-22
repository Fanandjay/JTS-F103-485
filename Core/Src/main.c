/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : ADS1220 Wheatstone Bridge ADC Reader
  *
  * @note           本文件只保留 HAL 初始化、时钟配置与错误处理。
  *                  业务逻辑位于 App/ 目录：
  *                    app.c         应用初始化与主循环调度
  *                    app_config.h  唯一配置源（增益/速率/滤波/去皮等）
  *                    app_measure.c 采集状态机
  *                    app_shell.c   串口命令解析
  *                    app_report.c  调试输出
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include "app.h"
/* USER CODE END Includes */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);

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

    /* 串口绑定、命令注册、ADS1220 上电自检都在这里完成；
     * 自检失败会直接停机，不会带病进入主循环。 */
    App_Init();

    while (1)
    {
        App_Loop();
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
