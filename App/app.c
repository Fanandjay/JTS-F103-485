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
#include "app_led.h"
#include "app_modbus.h"
#include <string.h>
#include "ADS1220.H"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* 应用级上下文。原本散落在 main.c 的全局变量集中到这里。 */
static ADS1220_Handle_t s_hads;
static App_Measure_t    s_measure;

/* 上一次 App_Loop 进入时刻，用于统计主循环周期（诊断通道 CH8） */
static uint32_t s_last_loop_tick;

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

/**
 * @brief 自检第 8 步：观测 DOUT/DRDY 波形，判定它是锁存型还是脉冲型。
 *
 * 这一步只读引脚、不发 SPI，窗口内也不读取转换结果，
 * 因此 DRDY 不会被读走，波形特征能直接暴露出来：
 *
 *   锁存型（DRDYM=0）→ 只出现 0~1 个下降沿，之后长期保持低
 *   脉冲型（DRDYM=1）→ 下降沿数量≈转换次数，低电平占空比很小
 *
 * 结论决定轮询方案是否可行：脉冲型下轮询必然大量漏检，必须改用 EXTI。
 */
static void App_SelfTest_ObserveDrdy(void)
{
    ADS1220_DrdyStats_t drdy;
    uint32_t duty_permille = 0U;
    const char *verdict;

    App_Report_Printf("[8] Observing DRDY for %u ms (read-only)...\r\n",
                      (unsigned)APP_DRDY_OBSERVE_MS);

    if (ADS1220_ObserveDrdy(&s_hads, APP_DRDY_OBSERVE_MS, &drdy) != HAL_OK)
    {
        App_Report_Printf("     [WARN] DWT cycle counter unavailable - skipped\r\n");
        return;
    }

    if (drdy.samples > 0U)
        duty_permille = (drdy.low_samples * 1000U) / drdy.samples;

    App_Report_Printf("     window     : %lu us, %lu samples\r\n",
                      (unsigned long)drdy.duration_us,
                      (unsigned long)drdy.samples);
    App_Report_Printf("     edges      : %lu falling\r\n",
                      (unsigned long)drdy.falling_edges);
    App_Report_Printf("     low duty   : %lu.%lu %%\r\n",
                      (unsigned long)(duty_permille / 10U),
                      (unsigned long)(duty_permille % 10U));
    App_Report_Printf("     longest low: %lu us\r\n",
                      (unsigned long)drdy.low_us_max);

    if (drdy.level_at_end != 0U)
    {
        App_Report_Printf("     still LOW  : %lu us at window end\r\n",
                          (unsigned long)drdy.low_us_pending);
    }

    /* 判定依据：
     *   脉冲型 -> 每次转换都有一次"低->高"的完整脉冲，故有多个下降沿
     *   锁存型 -> 拉低后不读取就不会回高，故 (longest low == 0) 且末尾仍为低
     *   恒低   -> 全程为低；恒高 -> 0 个下降沿且 duty 为 0
     */
    if (drdy.falling_edges >= 2U)
    {
        verdict = "PULSING - polling WILL miss, use EXTI";
    }
    else if ((drdy.level_at_end != 0U) && (drdy.low_us_max == 0U))
    {
        /* 有一段时间为低，但从未"结算"成一段完整低电平 —— 说明拉低后没回高 */
        verdict = "LATCHED - polling is OK";
    }
    else if ((drdy.level_at_end != 0U) &&
             (drdy.low_us_pending > (drdy.duration_us / 4U)))
    {
        verdict = "LATCHED - polling is OK";
    }
    else if ((drdy.falling_edges == 0U) && (drdy.low_samples == 0U))
    {
        verdict = "STUCK HIGH - no conversion seen";
    }
    else
    {
        verdict = "UNCLEAR - read the figures above";
    }

    App_Report_Printf("     => %s\r\n\r\n", verdict);
}

/**
 * @brief 自检第 9 步：实测"读取后 DRDY 是否回高"与真实转换周期。
 *
 * 第 [8] 步只观测不读取，能判定波形类型，但回答不了两件事：
 *   1. 读走数据后 DRDY 到底会不会回高 —— CH7 超时持续增长指向这里
 *   2. 真实转换周期是多少 —— 配的是 20 SPS(50ms)，但第 [8] 步显示首次
 *      下降沿出现在约 177 ms，需要用"两次下降沿之间"这种不受上电整定
 *      时间影响的方法核实
 */
static void App_SelfTest_ProbeConversion(void)
{
    ADS1220_ConvProbe_t probe;
    HAL_StatusTypeDef   st;
    uint32_t            sps_x10 = 0U;

    App_Report_Printf("[9] Probing conversion (%u rounds)...\r\n",
                      (unsigned)APP_CONV_PROBE_ROUNDS);

    st = ADS1220_ProbeConversion(&s_hads, APP_CONV_PROBE_ROUNDS,
                                 APP_CONV_PROBE_TIMEOUT_MS, &probe);

    if (st == HAL_ERROR)
    {
        App_Report_Printf("     [WARN] DWT cycle counter unavailable - skipped\r\n\r\n");
        return;
    }
    if (st == HAL_TIMEOUT)
    {
        App_Report_Printf("     [ERROR] timeout waiting for DRDY\r\n\r\n");
        return;
    }

    if (probe.period_us_avg > 0U)
        sps_x10 = 10000000UL / probe.period_us_avg;     /* 换算成 SPS x 10 */

    App_Report_Printf("     rounds     : %u done, %u returned HIGH\r\n",
                      (unsigned)probe.rounds_done,
                      (unsigned)probe.high_ok_rounds);
    App_Report_Printf("     high wait  : %lu us (max)\r\n",
                      (unsigned long)probe.high_wait_us_max);

    if (probe.high_ok_rounds > 0U)
    {
        App_Report_Printf("     period     : avg %lu us (min %lu, max %lu)\r\n",
                          (unsigned long)probe.period_us_avg,
                          (unsigned long)probe.period_us_min,
                          (unsigned long)probe.period_us_max);
        App_Report_Printf("     implied    : %lu.%lu SPS   (configured 20)\r\n",
                          (unsigned long)(sps_x10 / 10U),
                          (unsigned long)(sps_x10 % 10U));
    }

    if (probe.high_ok_rounds == 0U)
    {
        App_Report_Printf("     => DRDY NEVER returned HIGH after read\r\n\r\n");
    }
    else if (sps_x10 < 170U)
    {
        App_Report_Printf("     => actual rate FAR BELOW 20 SPS - check CONFIG1/clock\r\n\r\n");
    }
    else
    {
        App_Report_Printf("     => DRDY handshake OK, rate close to configured\r\n\r\n");
    }
}

/**
 * @brief 自检第 [10] 步：速率扫描 —— 直接写 CONFIG1 的 DR/MODE 组合并实测周期。
 *
 * 第 [9] 步把矛盾钉死了：CONFIG1 回读 0x04（normal / 20 SPS），
 * 但真实转换周期约 200 ms（5 SPS = 配置的 1/4）。
 * ADS1220 手册里能把速率除以 4 的机制只有 duty-cycle 模式（MODE=01），
 * 而回读说 MODE=00 —— 两个证据必有一个在撒谎。
 *
 * 本步不再纠缠"信谁"，而是做受控实验：绕开枚举 API，把若干 DR/MODE 组合
 * 直接写进 CONFIG1，逐个实测转换周期（DWT 计时，方法与第 [9] 步相同）：
 *
 *   - 若改 DR 后周期按比例变化  -> DR 位有效，问题只在个别档位/模式位
 *   - 若所有档位都 ÷4           -> 全局分频：内部时钟异常或 MODE 位实际为 duty
 *   - 若所有档位周期都不变      -> CONFIG1 运行时写入根本没生效
 *   - write 与 readback 不一致  -> SPI 读写链路至少有一方不可信
 *
 * 判读表见 REFACTOR-NOTES.md 第 15 节。扫描结束后恢复 CONFIG1=0x04。
 */
static void App_SelfTest_RateSweep(void)
{
    typedef struct
    {
        uint8_t     cfg1;
        const char *desc;
        uint32_t    expect_us;      /* 按数据手册该组合应有的周期 */
    } App_RateCase_t;

    static const App_RateCase_t cases[] = {
        { 0x04U, "DR=000 normal ( 20 SPS)",   50000U },
        { 0x0CU, "DR=000 duty  (  5 SPS)",  200000U },
        { 0x14U, "DR=000 turbo ( 40 SPS)",   25000U },
        { 0x24U, "DR=001 normal ( 45 SPS)",   22222U },
        { 0x44U, "DR=010 normal ( 90 SPS)",   11111U },
        { 0xC4U, "DR=110 normal (1000 SPS)",   1000U },
    };

    const uint8_t case_count = (uint8_t)(sizeof(cases) / sizeof(cases[0]));
    uint8_t i;

    App_Report_Printf("[10] Rate sweep: write CONFIG1, measure real period...\r\n");

    for (i = 0; i < case_count; i++)
    {
        ADS1220_ConvProbe_t probe;
        HAL_StatusTypeDef   st;
        uint8_t             readback = 0xFFU;
        uint32_t            sps_x10  = 0U;
        uint32_t            mult_x10 = 0U;

        if (ADS1220_WriteReg(&s_hads, ADS1220_REG_CONFIG1, cases[i].cfg1) != HAL_OK)
        {
            App_Report_Printf("     write 0x%02X FAILED (SPI error)\r\n",
                              cases[i].cfg1);
            continue;
        }

        /* 写完立刻回读：write 与 readback 不同 => 读写链路有一方不可信 */
        (void)ADS1220_ReadReg(&s_hads, ADS1220_REG_CONFIG1, &readback);

        st = ADS1220_ProbeConversion(&s_hads, APP_SWEEP_ROUNDS,
                                     APP_SWEEP_TIMEOUT_MS, &probe);
        if (st == HAL_TIMEOUT)
        {
            App_Report_Printf("     write 0x%02X rb 0x%02X  %s : [TIMEOUT] no DRDY\r\n",
                              cases[i].cfg1, readback, cases[i].desc);
            continue;
        }
        if (st == HAL_ERROR)
        {
            App_Report_Printf("     write 0x%02X  %s : [ERROR] DWT unavailable\r\n",
                              cases[i].cfg1, cases[i].desc);
            continue;
        }

        if (probe.period_us_min > 0U)
        {
            /* 以 min 为准：前几轮含 sinc 滤波器整定，周期会被拉长 */
            sps_x10 = 10000000UL / probe.period_us_min;
            mult_x10 = (probe.period_us_min * 10U) / cases[i].expect_us;
        }

        App_Report_Printf("     write 0x%02X rb 0x%02X  %s : expect %lu.%lu ms, min %lu us => %lu.%lu SPS (x%lu.%lu)\r\n",
                          cases[i].cfg1, readback, cases[i].desc,
                          (unsigned long)(cases[i].expect_us / 1000U),
                          (unsigned long)((cases[i].expect_us % 1000U) / 100U),
                          (unsigned long)probe.period_us_min,
                          (unsigned long)(sps_x10 / 10U),
                          (unsigned long)(sps_x10 % 10U),
                          (unsigned long)(mult_x10 / 10U),
                          (unsigned long)(mult_x10 % 10U));
    }

    /* 恢复用户配置的 0x04（20 SPS / normal / 连续），并确认恢复成功 */
    if (ADS1220_WriteReg(&s_hads, ADS1220_REG_CONFIG1,
                         (uint8_t)(ADS1220_DR_VAL_20SPS | ADS1220_CM_VAL_CONTINUOUS)) == HAL_OK)
    {
        uint8_t rb = 0xFFU;
        (void)ADS1220_ReadReg(&s_hads, ADS1220_REG_CONFIG1, &rb);
        App_Report_Printf("     restored CONFIG1=0x%02X (rb 0x%02X)\r\n\r\n",
                          (unsigned)(ADS1220_DR_VAL_20SPS | ADS1220_CM_VAL_CONTINUOUS),
                          rb);
    }
    else
    {
        App_Report_Printf("     [ERROR] failed to restore CONFIG1\r\n\r\n");
    }
}

/**
 * @brief 自检第 [11] 步：单次转换（single-shot）模式下的转换时间实测。
 *
 * 第 [10] 步的实测结果：连续转换模式下，六种 DR/MODE 组合的周期全部
 * 钉死在约 183 ms（5.4 SPS），不随 DR 变化，且回读与写入一致 ——
 * 寄存器单元正常，但速率控制逻辑对 DR/MODE 位无响应。
 *
 * 剩下最后一个未检验的分支：**坏的可能只是连续模式的自动重启机制，
 * 单次转换的速率控制未必坏**。本步在 single-shot 下分别用
 * DR=000(20 SPS) 与 DR=110(1000 SPS) 各发两次 START，实测转换耗时：
 *
 *   - 两次耗时随 DR 明显变化（约 250 ms vs 约 5 ms，含滤波器整定）
 *     => 单次转换的速率逻辑正常。**20 Hz 目标可救**：改为单次模式，
 *     由固件每 50 ms 发一次 START 驱动采样（见第 16 节方案）。
 *   - 两次耗时依旧钉死在约 183 ms 不随 DR 变化
 *     => 芯片速率控制整体失效，判定为硬件问题（芯片损伤/异常批次），
 *     换芯片验证；同时建议实测 AVDD 与检查模块来源。
 *
 * 单次转换耗时 ≈ 滤波器整定 + 1/DR，每次 START 都重新整定，
 * 因此两轮都应随 DR 变化。计时用 HAL_GetTick（ms 分辨率足够区分
 * 5 ms 与 183 ms 两档）。
 */
static void App_SelfTest_SingleShotTiming(void)
{
    typedef struct
    {
        uint8_t     cfg1;
        const char *desc;
    } App_SSCase_t;

    static const App_SSCase_t cases[] = {
        { 0x00U, "SS DR=000 normal ( 20 SPS)" },
        { 0xC0U, "SS DR=110 normal (1000 SPS)" },
    };

    const uint8_t case_count = (uint8_t)(sizeof(cases) / sizeof(cases[0]));
    uint8_t i;
    uint8_t round;

    App_Report_Printf("[11] Single-shot timing test...\r\n");

    for (i = 0; i < case_count; i++)
    {
        uint8_t readback = 0xFFU;
        uint32_t conv_ms[2] = {0xFFFFFFFFUL, 0xFFFFFFFFUL};

        if (ADS1220_WriteReg(&s_hads, ADS1220_REG_CONFIG1, cases[i].cfg1) != HAL_OK)
        {
            App_Report_Printf("     write 0x%02X FAILED (SPI error)\r\n",
                              cases[i].cfg1);
            continue;
        }
        (void)ADS1220_ReadReg(&s_hads, ADS1220_REG_CONFIG1, &readback);

        /* 手册：写任意配置寄存器会触发一次单次转换，先把它冲掉，
         * 保证后面计时的起点是我们自己发的 START。 */
        {
            uint32_t t0 = HAL_GetTick();
            while (ADS1220_IsDataReady(&s_hads) == 0U)
            {
                if ((HAL_GetTick() - t0) >= APP_SWEEP_TIMEOUT_MS)
                    break;
            }
            {
                int32_t raw;
                (void)ADS1220_ReadDataContinuous(&s_hads, &raw);
            }
        }

        App_Report_Printf("     write 0x%02X rb 0x%02X  %s\r\n",
                          cases[i].cfg1, readback, cases[i].desc);

        for (round = 0U; round < 2U; round++)
        {
            uint32_t t0;
            int32_t  raw;

            if (ADS1220_Start(&s_hads) != HAL_OK)
            {
                App_Report_Printf("       conv#%u: START failed\r\n",
                                  (unsigned)(round + 1U));
                continue;
            }

            t0 = HAL_GetTick();
            while (ADS1220_IsDataReady(&s_hads) == 0U)
            {
                if ((HAL_GetTick() - t0) >= APP_SWEEP_TIMEOUT_MS)
                    break;
            }
            conv_ms[round] = HAL_GetTick() - t0;

            /* 读走结果，让 DRDY 回高，给下一轮干净的起点 */
            (void)ADS1220_ReadDataContinuous(&s_hads, &raw);
        }

        if (conv_ms[0] == 0xFFFFFFFFUL)
        {
            App_Report_Printf("       conv#1: TIMEOUT - no DRDY\r\n");
        }
        else
        {
            App_Report_Printf("       conv#1: %lu ms   conv#2: %lu ms   (expect DR-dependent: ~250/~5 ms)\r\n",
                              (unsigned long)conv_ms[0],
                              (unsigned long)conv_ms[1]);
        }
    }

    /* 恢复 0x04 并重新发 START：WREG 只会触发单次转换，
     * 连续转换必须靠 START 重新启动，否则主循环会等不到 DRDY。 */
    if (ADS1220_WriteReg(&s_hads, ADS1220_REG_CONFIG1,
                         (uint8_t)(ADS1220_DR_VAL_20SPS | ADS1220_CM_VAL_CONTINUOUS)) == HAL_OK)
    {
        uint8_t rb = 0xFFU;
        (void)ADS1220_ReadReg(&s_hads, ADS1220_REG_CONFIG1, &rb);
        (void)ADS1220_Start(&s_hads);
        App_Report_Printf("     restored CONFIG1=0x%02X (rb 0x%02X), continuous restarted\r\n\r\n",
                          (unsigned)(ADS1220_DR_VAL_20SPS | ADS1220_CM_VAL_CONTINUOUS),
                          rb);
    }
    else
    {
        App_Report_Printf("     [ERROR] failed to restore CONFIG1\r\n\r\n");
    }
}

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
    App_Report_Printf("     [OK] Continuous conversion started\r\n\r\n");

    /* 8. DRDY 波形观测：判定轮询方案是否可行（脉冲型必须改 EXTI） */
    App_SelfTest_ObserveDrdy();

    /* 9. 实测读取后 DRDY 是否回高，以及真实转换周期 */
    App_SelfTest_ProbeConversion();

    /* 10. 速率扫描：定位"回读 20 SPS 但实测 5 SPS"的矛盾来源 */
    App_SelfTest_RateSweep();

    /* 11. 单次转换计时：判定速率控制是整体失效还是仅连续模式失效 */
    App_SelfTest_SingleShotTiming();

    return HAL_OK;
}

/*==============================================================================
 * 对外接口
 *============================================================================*/

#if APP_UART_PROBE_ROUNDS > 0
/**
 * @brief 串口链路探针（诊断用）：USART1/USART2 × RS485_EN 高/低 全组合发帧。
 *
 * 背景：新板新增 RS485_EN(PA1)，调试串口可能改走 USART2 或经 RS485
 * 收发器（DE 拉高才允许发送）。banner 在 PA9 发不出但 LED 心跳正常，
 * 用本探针一次判明"适配器到底在哪个口、EN 需要什么电平"。
 * 烧录前先打开串口终端（115200）再复位，看哪一行出现。
 */
static void App_UartProbe(void)
{
    static const char *const msg[4] =
    {
        "PROBE A: USART1(PA9) RS485_EN=LOW\r\n",
        "PROBE B: USART1(PA9) RS485_EN=HIGH\r\n",
        "PROBE C: USART2(PA2) RS485_EN=LOW\r\n",
        "PROBE D: USART2(PA2) RS485_EN=HIGH\r\n",
    };
    uint32_t round;

    for (round = 0; round < APP_UART_PROBE_ROUNDS; round++)
    {
        uint32_t i;

        for (i = 0; i < 4; i++)
        {
            UART_HandleTypeDef *hu = (i < 2U) ? &huart1 : &huart2;
            uint8_t en_high = (i == 1U || i == 3U) ? 1U : 0U;

            HAL_GPIO_WritePin(RS485_EN_GPIO_Port, RS485_EN_Pin,
                              en_high ? GPIO_PIN_SET : GPIO_PIN_RESET);
            (void)HAL_UART_Transmit(hu, (const uint8_t *)msg[i],
                                    (uint16_t)strlen(msg[i]), 50);
            HAL_Delay(60);  /* 组合间留出线缆稳定时间 */
        }
    }
    HAL_GPIO_WritePin(RS485_EN_GPIO_Port, RS485_EN_Pin, GPIO_PIN_RESET);
}
#endif

void App_Init(void)
{
#if APP_UART_PROBE_ROUNDS > 0
    App_UartProbe();    /* 先探明串口链路，再进入正常初始化 */
#endif
    App_LED_Init();     /* 心跳自本处开始自由运行，覆盖自检全程 */
    App_Report_Init(&huart1);
    App_Measure_Init(&s_measure, &s_hads, &huart1);

    s_last_loop_tick = HAL_GetTick();

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

    if (App_ModBus_Init(&s_measure) != HAL_OK)
    {
        App_Report_Printf("ERROR: Modbus/RS485 initialization failed\r\n");
        Error_Handler();
    }
    App_Report_Printf("\r\nModbus RTU: USART2, 115200 8E1, slave=%u, read 03/04\r\n",
                       (unsigned int)APP_MODBUS_SLAVE_ADDRESS);
    App_Report_Printf("ADC acquisition is always active; 0/1 controls VOFA only.\r\n");
    App_Report_Printf("Ready. Send 0 to start output, 1 to stop output.\r\n");
}

void App_Loop(void)
{
    uint32_t now    = HAL_GetTick();
    uint32_t period = now - s_last_loop_tick;
    s_last_loop_tick = now;

    App_LED_Kick();     /* 喂狗：首帧后心跳转为门控模式，卡死则 LED 常亮 */
    App_Shell_Poll();
    /* 把主循环周期上报给采集模块，由诊断通道 CH8 回传。
     * 若该值接近 ADC 转换周期（20 SPS 时为 50ms），说明主循环余量不足。 */
    App_Measure_NoteLoopPeriod(&s_measure, period);
    App_Measure_Poll(&s_measure);
    App_ModBus_Poll();
    HAL_Delay(1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    App_Shell_OnRxComplete(huart);
    App_ModBus_OnRxComplete(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    App_Shell_OnError(huart);
    App_ModBus_OnError(huart);
}
