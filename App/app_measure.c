/**
 ******************************************************************************
 * @file    app_measure.c
 * @brief   采集状态机实现。
 ******************************************************************************
 */

#include "app_measure.h"
#include "app_config.h"
#include "app_report.h"
#include "VOFA.h"
#include <string.h>

/* ADS1220 在连续转换模式下，数据全部为 1 表示 DOUT 恒高（通常意味着
 * 芯片未响应或输入超范围），视为无效样本丢弃。 */
#define APP_MEASURE_INVALID_CODE  0xFFFFFFFFUL

/* 诊断统计窗口长度 (ms) */
#define APP_MEASURE_DIAG_WINDOW_MS  1000U

/*------------------------------------------------------------------------------
 * DRDY 边沿计数（ISR 与主循环之间的同步通道）
 *
 * PB11 下降沿（新转换结果就绪）只把计数 +1，取数留给主循环做 ——
 * ISR 里不碰 SPI。单写者（ISR）单读者（主循环）的 32 位计数
 * 在 Cortex-M3 上天然原子，无需关中断。
 *----------------------------------------------------------------------------*/
static volatile uint32_t s_drdy_edges = 0U;

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == ADS1220_DRDY_Pin)
        s_drdy_edges++;
}

static void App_Measure_ReportTareDone(App_Measure_t *measure);
static void App_Measure_CollectTareSample(App_Measure_t *measure, int32_t raw);
static void App_Measure_ProcessSample(App_Measure_t *measure, int32_t raw);
static void App_Measure_HandleSample(App_Measure_t *measure);
static void App_Measure_DiagTick(App_Measure_t *measure);

void App_Measure_Init(App_Measure_t *measure,
                      ADS1220_Handle_t *hads,
                      UART_HandleTypeDef *huart)
{
    if (measure == NULL)
        return;

    memset(measure, 0, sizeof(*measure));
    measure->hads = hads;
    measure->huart = huart;
    measure->window_tick = HAL_GetTick();
}

void App_Measure_SetOutput(App_Measure_t *measure, uint8_t enable)
{
    if (measure == NULL)
        return;

    measure->output_enabled = (enable != 0U) ? 1U : 0U;

    if (measure->output_enabled != 0U)
    {
        /* 重新开始输出时复位滤波，避免把停采期间的旧状态带进新数据。 */
        measure->filter_started = 0U;
    }
}

void App_Measure_StartTare(App_Measure_t *measure)
{
    if (measure == NULL)
        return;

    /* 保持 ADS1220 继续转换，只是暂停样本输出，并重新开始累加零点。 */
    measure->output_enabled = 0U;
    measure->tare_running = 1U;
    measure->zero_sum = 0;
    measure->tare_sample_count = 0U;
}

void App_Measure_Poll(App_Measure_t *measure)
{
    uint32_t edges_now;

    if (measure == NULL || measure->hads == NULL)
        return;

    /* 诊断窗口刷新必须早于所有 return，保证停止输出/去皮期间统计不中断。 */
    App_Measure_DiagTick(measure);

    /* ADC 持续采集，Modbus 无需先发 '0'。output_enabled 只控制 VOFA。 */

    /* 新边沿 = 新转换结果就绪（锁存型 DRDY 下读取会让它回高，
     * 下一次下降沿必然对应新数据，不会重复读同一个样本）。 */
    edges_now = s_drdy_edges;
    if (measure->drdy_edge_seen != edges_now)
    {
        measure->drdy_edge_seen      = edges_now;
        measure->drdy_last_data_tick = HAL_GetTick();
        App_Measure_HandleSample(measure);
        return;
    }

    /* 中断缺席兜底：超过窗口仍未见新边沿，轮询一次 DRDY 电平。
     * 命中说明边沿丢失或 EXTI 链路未通，计数进 CH7 供诊断；
     * 电平为高（转换进行中）则只刷新计时，不打扰正常节奏。 */
    if ((HAL_GetTick() - measure->drdy_last_data_tick) >= APP_DRDY_IRQ_TIMEOUT_MS)
    {
        measure->drdy_last_data_tick = HAL_GetTick();

        if (ADS1220_IsDataReady(measure->hads))
        {
            measure->drdy_timeout_count++;
            App_Measure_HandleSample(measure);
        }
    }
}

static void App_Measure_HandleSample(App_Measure_t *measure)
{
    int32_t raw;

    if (ADS1220_ReadDataContinuous(measure->hads, &raw) != HAL_OK)
        return;

    if ((uint32_t)raw == APP_MEASURE_INVALID_CODE)
    {
        /* 只计数、不打字。
         * 数据流是 JustFloat 二进制帧，混入任何 ASCII 文本都会破坏上位机的
         * 帧同步，造成"看起来丢帧"的假象。该计数改由诊断通道 CH6 回传。 */
        measure->invalid_sample_count++;
        measure->invalid_in_window++;
        return;
    }

    if (measure->tare_running != 0U)
    {
        App_Measure_CollectTareSample(measure, raw);
    }

    App_Measure_ProcessSample(measure, raw);
}

static void App_Measure_CollectTareSample(App_Measure_t *measure, int32_t raw)
{
    /* 只累加有效样本；期间 ADS1220 一直保持在连续转换模式。 */
    measure->zero_sum += (int64_t)raw;
    measure->tare_sample_count++;

    if (measure->tare_sample_count < APP_TARE_SAMPLES)
        return;

    measure->zero_offset = (int32_t)(measure->zero_sum /
                                     (int64_t)measure->tare_sample_count);
    measure->zero_valid = 1U;
    measure->tare_running = 0U;
    measure->output_enabled = 0U;

    /* 不把去皮前的样本带进之后的输出。 */
    measure->filter_started = 0U;

    App_Measure_ReportTareDone(measure);
}

static void App_Measure_ReportTareDone(App_Measure_t *measure)
{
    App_Report_Printf("Tare completed\r\n");
    App_Report_Printf("Zero offset = %ld\r\n", (long)measure->zero_offset);
    App_Report_Printf("Send 0 to start zero-corrected output\r\n");
}

static void App_Measure_ProcessSample(App_Measure_t *measure, int32_t raw)
{
    float   vofa_values[APP_VOFA_CHANNELS];
    int32_t corrected_raw;
    float   filtered_value;
    int32_t filtered_raw;

    /* 去皮在滤波之前完成；未标定过零点时保持原始码。 */
    corrected_raw = raw;
    if (measure->zero_valid != 0U)
        corrected_raw = raw - measure->zero_offset;

    if (measure->filter_started == 0U)
    {
        /* 用第一个有效码初始化两级滤波，避免从 0 开始的启动瞬态。 */
        MovingAverage5_Init(&measure->moving_average);
        FirstOrderLowPass_Init(&measure->lowpass, APP_LPF_ALPHA);
        measure->filter_started = 1U;
    }

    /* 滤波链路：原始码 → 五点滑动平均 → 一阶低通 */
    filtered_value = MovingAverage5_Update(&measure->moving_average,
                                           (float)corrected_raw);
    filtered_value = FirstOrderLowPass_Update(&measure->lowpass,
                                              filtered_value);

    /* 四舍五入回 ADC 码，便于与原始码直接比较。 */
    if (filtered_value >= 0.0f)
        filtered_raw = (int32_t)(filtered_value + 0.5f);
    else
        filtered_raw = (int32_t)(filtered_value - 0.5f);

    measure->latest.raw_code = raw;
    measure->latest.raw_voltage_mv =
        ADS1220_RawToVoltage(raw, APP_VREF_MV,
                             ADS1220_GetGainValue(measure->hads));
    measure->latest.filtered_code = filtered_raw;
    measure->latest.filtered_voltage_mv =
        ADS1220_RawToVoltage(filtered_raw, APP_VREF_MV,
                             ADS1220_GetGainValue(measure->hads));
    measure->latest.sample_tick = HAL_GetTick();
    measure->latest.sample_count++;
    measure->latest.valid = 1U;

    if (measure->output_enabled == 0U)
        return;

    vofa_values[APP_VOFA_CH_RAW]      = (float)raw;
    vofa_values[APP_VOFA_CH_TARED]    = (float)corrected_raw;
    vofa_values[APP_VOFA_CH_FILTERED] = (float)filtered_raw;

    /* 比例式接法下基准即电桥激励，可直接换算为差分电压(mV)：
     *   Vdiff = raw * Vref / (gain * 2^23)
     * 用与 CH3 相同的滤波后码值换算，方便与 CH3 直接对照。 */
    vofa_values[APP_VOFA_CH_MV] = measure->latest.filtered_voltage_mv;

    /* 诊断通道：每秒刷新一次，帧间保持不变，上位机看到阶梯状。
     * 这些值让固件自证"实际输出速率"，不必再靠上位机抓包反推。 */
    vofa_values[APP_VOFA_CH_RATE]    = (float)measure->frames_per_second;
    vofa_values[APP_VOFA_CH_INVALID] = (float)measure->invalid_per_second;
    vofa_values[APP_VOFA_CH_TIMEOUT] = (float)measure->drdy_timeout_count;
    vofa_values[APP_VOFA_CH_LOOPMAX] = (float)measure->loop_period_max_reported;

    /* 只统计真正发出去的帧，UART 失败不计入，避免把速率算高。 */
    if (VOFA_JustFloat_Send(measure->huart, vofa_values,
                            APP_VOFA_CHANNELS) == HAL_OK)
    {
        measure->frames_in_window++;
    }
}

void App_Measure_GetLatest(const App_Measure_t *measure,
                          App_Measure_Snapshot_t *snapshot)
{
    if (measure != NULL && snapshot != NULL)
        *snapshot = measure->latest;
}

void App_Measure_NoteLoopPeriod(App_Measure_t *measure, uint32_t period_ms)
{
    if (measure == NULL)
        return;

    if (period_ms > measure->loop_period_max)
        measure->loop_period_max = period_ms;
}

/**
 * @brief 每秒刷新一次诊断统计窗口。
 *
 * 把本窗口累加的三个量提交为"上报值"，再清零开始下一窗口。
 * 上报值只在窗口边界变化，因此每帧都携带的是"上一秒的统计结果"，
 * 上位机直接读 CH5~CH8 即可。
 */
static void App_Measure_DiagTick(App_Measure_t *measure)
{
    uint32_t now = HAL_GetTick();

    if ((now - measure->window_tick) < APP_MEASURE_DIAG_WINDOW_MS)
        return;

    measure->frames_per_second        = measure->frames_in_window;
    measure->invalid_per_second       = measure->invalid_in_window;
    measure->loop_period_max_reported = measure->loop_period_max;

    measure->frames_in_window  = 0U;
    measure->invalid_in_window = 0U;
    measure->loop_period_max   = 0U;

    measure->window_tick += APP_MEASURE_DIAG_WINDOW_MS;

    /* 若窗口被拖得落后超过一个周期（主循环长时间被阻塞），直接对齐，
     * 避免窗口起点持续累积漂移导致上报速率被高估。 */
    if ((now - measure->window_tick) >= APP_MEASURE_DIAG_WINDOW_MS)
        measure->window_tick = now;
}
