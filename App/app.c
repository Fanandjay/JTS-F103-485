/**
 ******************************************************************************
 * @file    app.c
 * @brief   应用层实现：ADS1220 上电自检 + 串口命令 + 主循环调度。
 ******************************************************************************
 */

#include "app.h"
#include "app_config.h"
#include "app_measure.h"
#include "app_report.h"
#include "app_shell.h"
#include "ADS1220.H"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* 应用级上下文。原本散落在 main.c 的全局变量集中到这里。 */
static ADS1220_Handle_t s_hads;
static App_Measure_t    s_measure;

/*==============================================================================
 * 串口命令处理
 *============================================================================*/

static void App_Cmd_StartOutput(void)
{
    App_Report_Printf("RX OK: 0\r\n");
    App_Measure_SetOutput(&s_measure, 1U);
    App_Report_Printf("Conversion output started\r\n");
}

static void App_Cmd_StopOutput(void)
{
    App_Report_Printf("RX OK: 1\r\n");
    App_Measure_SetOutput(&s_measure, 0U);
    App_Report_Printf("Conversion output stopped\r\n");
}

static void App_Cmd_StartTare(void)
{
    App_Report_Printf("RX OK: 2\r\n");
    App_Measure_StartTare(&s_measure);
    App_Report_Printf("Tare started: keep the sensor unloaded\r\n");
}

/*==============================================================================
 * ADS1220 上电自检
 *
 * 配置参数全部取自 App/app_config.h —— 打印、写入、校验三者同源，
 * 不会再出现"打印 64x 却写 128x"这类不一致。
 *============================================================================*/

static HAL_StatusTypeDef App_SelfTest(void)
{
    uint8_t expected[4];
    uint8_t actual[4];

    App_Report_Printf("\r\n");
    App_Report_Printf("========================================\r\n");
    App_Report_Printf("  ADS1220 Initialization\r\n");
    App_Report_Printf("========================================\r\n\r\n");

    /* 1. 初始化 */
    App_Report_Printf("[1] Initializing ADS1220...\r\n");
    if (ADS1220_Init(&s_hads, &hspi2,
                     APP_ADC_CS_PORT, APP_ADC_CS_PIN,
                     APP_ADC_DRDY_PORT, APP_ADC_DRDY_PIN) != HAL_OK)
    {
        App_Report_Printf("     [ERROR] ADS1220_Init failed!\r\n");
        return HAL_ERROR;
    }
    App_Report_Printf("     [OK] ADS1220_Init success\r\n\r\n");

    /* 2. 发送 RESET */
    App_Report_Printf("[2] Sending RESET command...\r\n");
    if (ADS1220_Reset(&s_hads) != HAL_OK)
    {
        App_Report_Printf("     [ERROR] RESET failed!\r\n");
        return HAL_ERROR;
    }
    App_Report_Printf("     [OK] RESET success\r\n\r\n");

    /* 3. 读复位默认值：ADS1220 复位后 CONFIG0..3 全为 0x00。
     *    若读到 FF FF FF FF，说明 DOUT/MISO 恒高，芯片没有响应
     *    （未焊接 / 未供电 / 片选或时钟异常）。 */
    App_Report_Printf("[3] Reset defaults...\r\n");
    if (ADS1220_ReadAllRegs(&s_hads) != HAL_OK)
    {
        App_Report_Printf("     [ERROR] Failed to read registers!\r\n");
        return HAL_ERROR;
    }
    App_Report_Printf("     CONFIG0..3 = %02X %02X %02X %02X\r\n",
                      s_hads.config[0], s_hads.config[1],
                      s_hads.config[2], s_hads.config[3]);
    if (s_hads.config[0] == 0xFFU && s_hads.config[1] == 0xFFU &&
        s_hads.config[2] == 0xFFU && s_hads.config[3] == 0xFFU)
    {
        App_Report_Printf("     [ERROR] All registers read 0xFF - ADS1220 not responding\r\n");
        return HAL_ERROR;
    }
    App_Report_Printf("     [OK]\r\n\r\n");

    /* 4. 连通性自检：确认 SPI 写通路真正生效。
     *    先写一个不同的值再回读，可覆盖"目标配置恰好等于芯片当前值"这种
     *    仅靠回读比对无法发现的写通路故障。 */
    App_Report_Printf("[4] Connection test...\r\n");
    if (ADS1220_ConnectionTest(&s_hads) != HAL_OK)
    {
        App_Report_Printf("     [ERROR] Write/read probe failed - SPI write path broken\r\n");
        return HAL_ERROR;
    }
    App_Report_Printf("     [OK] Write/read probe passed\r\n\r\n");

    /* 5. 按 App/app_config.h 配置（写入与打印同源） */
    App_Report_Printf("[5] Configuring from App/app_config.h...\r\n");
    if (ADS1220_Config(&s_hads,
                       APP_ADC_CHANNEL,
                       APP_ADC_GAIN,
                       APP_ADC_DR,
                       APP_ADC_MODE,
                       APP_ADC_CONV,
                       APP_ADC_VREF) != HAL_OK)
    {
        App_Report_Printf("     [ERROR] ADS1220_Config failed!\r\n");
        return HAL_ERROR;
    }
    App_Report_Printf("     Channel    : %s\r\n",
                      ADS1220_ChannelName(APP_ADC_CHANNEL));
    App_Report_Printf("     Gain       : %u\r\n",
                      (unsigned)ADS1220_GetGainValue(&s_hads));
    App_Report_Printf("     Data rate  : %u SPS\r\n",
                      (unsigned)ADS1220_DataRateSps(APP_ADC_DR));
    App_Report_Printf("     Mode       : %s\r\n",
                      (APP_ADC_MODE == ADS1220_MOD_NORM)  ? "normal" :
                      (APP_ADC_MODE == ADS1220_MOD_TURBO) ? "turbo" : "duty-cycle");
    App_Report_Printf("     Conversion : %s\r\n",
                      (APP_ADC_CONV == ADS1220_CONV_CONT) ? "continuous" : "single-shot");
    App_Report_Printf("     VREF       : %s\r\n",
                      (APP_ADC_VREF == ADS1220_VREF_INT)  ? "internal 2.048V" :
                      (APP_ADC_VREF == ADS1220_VREF_EXT)  ? "external REFP0/REFN0" :
                      (APP_ADC_VREF == ADS1220_VREF_AVDD) ? "AVDD" : "external REFP1/REFN1");
    App_Report_Printf("\r\n");

    /* 6. 回读校验：期望值取自驱动内部记录，此处不再硬编码寄存器常量 */
    App_Report_Printf("[6] Verifying registers...\r\n");
    if (ADS1220_VerifyConfig(&s_hads, expected, actual) == HAL_OK)
    {
        App_Report_Printf("     CONFIG0 = 0x%02X\r\n", actual[0]);
        App_Report_Printf("     CONFIG1 = 0x%02X\r\n", actual[1]);
        App_Report_Printf("     CONFIG2 = 0x%02X\r\n", actual[2]);
        App_Report_Printf("     CONFIG3 = 0x%02X\r\n", actual[3]);
        App_Report_Printf("     [OK] Registers verified\r\n\r\n");
    }
    else
    {
        App_Report_Printf("     CONFIG0: expect 0x%02X, actual 0x%02X\r\n",
                          expected[0], actual[0]);
        App_Report_Printf("     CONFIG1: expect 0x%02X, actual 0x%02X\r\n",
                          expected[1], actual[1]);
        App_Report_Printf("     CONFIG2: expect 0x%02X, actual 0x%02X\r\n",
                          expected[2], actual[2]);
        App_Report_Printf("     CONFIG3: expect 0x%02X, actual 0x%02X\r\n",
                          expected[3], actual[3]);
        App_Report_Printf("     [ERROR] Register verification failed\r\n");
        return HAL_ERROR;
    }

    /* 7. 启动连续转换，但先不输出样本，等用户发 0 */
    App_Report_Printf("[7] Starting continuous conversion...\r\n");
    if (ADS1220_Start(&s_hads) != HAL_OK)
    {
        App_Report_Printf("     [ERROR] Failed to start continuous conversion!\r\n");
        return HAL_ERROR;
    }
    App_Report_Printf("     [OK] Continuous conversion started\r\n");

    return HAL_OK;
}

/*==============================================================================
 * 对外接口
 *============================================================================*/

void App_Init(void)
{
    App_Report_Init(&huart1);
    App_Measure_Init(&s_measure, &s_hads, &huart1);

    if (App_Shell_Init(&huart1) != HAL_OK)
    {
        App_Report_Printf("ERROR: UART receive interrupt failed to start\r\n");
        Error_Handler();
    }

    /* 先注册命令，让单字符命令在自检期间也能立即生效。 */
    (void)App_Shell_Register('0', App_Cmd_StartOutput);
    (void)App_Shell_Register('1', App_Cmd_StopOutput);
    (void)App_Shell_Register('2', App_Cmd_StartTare);

    App_Report_Banner();

    if (App_SelfTest() != HAL_OK)
    {
        App_Report_Printf("\r\nSelf-test FAILED - execution halted.\r\n");
        Error_Handler();
    }

    App_Report_Printf("\r\nReady. Send 0 to start output, 1 to stop output.\r\n");
}

void App_Loop(void)
{
    App_Shell_Poll();
    App_Measure_Poll(&s_measure);
    HAL_Delay(1);
}
