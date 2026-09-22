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

/* 无效样本提示的打印间隔 */
#define APP_MEASURE_INVALID_REPORT_INTERVAL  20U

static void App_Measure_ReportTareDone(App_Measure_t *measure);
static void App_Measure_CollectTareSample(App_Measure_t *measure, int32_t raw);
static void App_Measure_EmitSample(App_Measure_t *measure, int32_t raw);
static void App_Measure_HandleSample(App_Measure_t *measure);

void App_Measure_Init(App_Measure_t *measure,
                      ADS1220_Handle_t *hads,
                      UART_HandleTypeDef *huart)
{
    if (measure == NULL)
        return;

    memset(measure, 0, sizeof(*measure));
    measure->hads = hads;
    measure->huart = huart;
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
    uint8_t data_ready;

    if (measure == NULL || measure->hads == NULL)
        return;

    data_ready = ADS1220_IsDataReady(measure->hads);

    /* 既不输出也不去皮时，完全不做采集。 */
    if (measure->output_enabled == 0U && measure->tare_running == 0U)
    {
        measure->drdy_low_count = 0U;
        measure->drdy_read_locked = 0U;
        return;
    }

    if (measure->drdy_read_locked != 0U)
    {
        /* 正常路径：DOUT/DRDY 回高即解锁；超时解锁用于防止状态机卡死。 */
        if (data_ready == 0U ||
            (HAL_GetTick() - measure->drdy_lock_tick) >= APP_DRDY_TIMEOUT_MS)
        {
            measure->drdy_read_locked = 0U;
            measure->drdy_low_count = 0U;
        }
        return;
    }

    if (data_ready == 0U)
    {
        measure->drdy_low_count = 0U;
        return;
    }

    /* 要求连续观察到 APP_DRDY_CONFIRM 次低电平，滤掉毛刺。 */
    if (measure->drdy_low_count < APP_DRDY_CONFIRM)
        measure->drdy_low_count++;

    if (measure->drdy_low_count < APP_DRDY_CONFIRM)
        return;

    measure->drdy_read_locked = 1U;
    measure->drdy_lock_tick = HAL_GetTick();
    measure->drdy_low_count = 0U;

    App_Measure_HandleSample(measure);
}

static void App_Measure_HandleSample(App_Measure_t *measure)
{
    int32_t raw;

    if (ADS1220_ReadDataContinuous(measure->hads, &raw) != HAL_OK)
        return;

    if ((uint32_t)raw == APP_MEASURE_INVALID_CODE)
    {
        measure->invalid_sample_count++;
        if ((measure->invalid_sample_count % APP_MEASURE_INVALID_REPORT_INTERVAL) == 0U)
        {
            App_Report_Printf("ADC invalid sample count=%lu\r\n",
                              (unsigned long)measure->invalid_sample_count);
        }
        return;
    }

    if (measure->tare_running != 0U)
    {
        App_Measure_CollectTareSample(measure, raw);
        return;
    }

    if (measure->output_enabled != 0U)
    {
        App_Measure_EmitSample(measure, raw);
    }
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

static void App_Measure_EmitSample(App_Measure_t *measure, int32_t raw)
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

    vofa_values[APP_VOFA_CH_RAW]      = (float)raw;
    vofa_values[APP_VOFA_CH_TARED]    = (float)corrected_raw;
    vofa_values[APP_VOFA_CH_FILTERED] = (float)filtered_raw;

    /* 比例式接法下基准即电桥激励，可直接换算为差分电压(mV)：
     *   Vdiff = raw * Vref / (gain * 2^23)
     * 用与 CH3 相同的滤波后码值换算，方便与 CH3 直接对照。 */
    vofa_values[APP_VOFA_CH_MV] =
        ADS1220_RawToVoltage(filtered_raw,
                             APP_VREF_MV,
                             ADS1220_GetGainValue(measure->hads));

    (void)VOFA_JustFloat_Send(measure->huart, vofa_values, APP_VOFA_CHANNELS);
}
