/**
 ******************************************************************************
 * @file    app_measure.h
 * @brief   采集状态机：PB11 下降沿中断 → 取数 → 去皮 → 滤波 → 输出。
 *
 * @note    原先这些状态是散落在 main.c 里的 10 个 file-scope 变量，
 *          无法看出彼此关系，也无法整体复位。这里统一收进 App_Measure_t，
 *          模块只暴露行为接口，调用方不接触内部状态。
 ******************************************************************************
 */

#ifndef __APP_MEASURE_H__
#define __APP_MEASURE_H__

#include "main.h"
#include "ADS1220.H"
#include "Filter.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int32_t raw_code;             /*!< 原始 ADC 码，不去皮、不滤波 */
    float raw_voltage_mv;         /*!< 与 raw_code 对应的输入差分电压 */
    int32_t filtered_code;        /*!< 去皮并滤波后的码值，与 VOFA CH3 相同 */
    float filtered_voltage_mv;    /*!< 与 VOFA CH4 相同，单位 mV */
    uint32_t sample_count;
    uint32_t sample_tick;
    uint8_t valid;
} App_Measure_Snapshot_t;

typedef struct
{
    ADS1220_Handle_t   *hads;          /*!< 已初始化的 ADC 句柄 */
    UART_HandleTypeDef *huart;         /*!< VOFA+ 数据输出串口 */

    uint8_t  output_enabled;           /*!< 仅控制 VOFA 输出；ADC/Modbus 持续更新 */
    uint8_t  filter_started;           /*!< 非 0 表示滤波器已按首个有效样本初始化 */

    /* DRDY 中断同步状态（边沿计数由 EXTI 回调维护） */
    uint32_t drdy_edge_seen;           /*!< 上次已消费的 DRDY 边沿计数 */
    uint32_t drdy_last_data_tick;      /*!< 最近一次取数/同步的 tick，用于兜底计时 */

    /* 去皮（零点标定） */
    uint8_t  tare_running;             /*!< 非 0 表示正在采集零点 */
    uint8_t  zero_valid;               /*!< 非 0 表示零点有效 */
    int64_t  zero_sum;                 /*!< 零点样本累加 */
    uint32_t tare_sample_count;        /*!< 已采集的零点样本数 */
    int32_t  zero_offset;              /*!< 零点偏移（原始码） */

    /* 滤波 */
    MovingAverage5_t    moving_average;
    FirstOrderLowPass_t lowpass;

    App_Measure_Snapshot_t latest; /*!< 持续更新，独立于 VOFA 输出开关 */

    /* 累计统计 */
    uint32_t invalid_sample_count;     /*!< 读回全 1 等无效样本累计数 */
    uint32_t drdy_timeout_count;       /*!< 中断缺席时兜底轮询命中次数（正常恒为 0） */

    /* 每秒统计窗口（原始累加量） */
    uint32_t window_tick;              /*!< 统计窗口起点 */
    uint32_t frames_in_window;         /*!< 本窗口内成功发出的帧数 */
    uint32_t invalid_in_window;        /*!< 本窗口内无效样本数 */
    uint32_t loop_period_max;          /*!< 本窗口内主循环最长周期 (ms) */

    /* 每秒统计窗口（上报给上位机的值） */
    uint32_t frames_per_second;        /*!< 上一窗口的输出帧率 */
    uint32_t invalid_per_second;       /*!< 上一窗口的无效样本数 */
    uint32_t loop_period_max_reported; /*!< 上一窗口的主循环最长周期 (ms) */
} App_Measure_t;

/* 初始化采集上下文（清零并绑定句柄）。 */
void App_Measure_Init(App_Measure_t *measure,
                      ADS1220_Handle_t *hads,
                      UART_HandleTypeDef *huart);

/* 使能/停止样本输出。使能时会复位滤波器，避免带入旧状态。 */
void App_Measure_SetOutput(App_Measure_t *measure, uint8_t enable);

/* 开始去皮：暂停输出并清零零点累加器，等待 APP_TARE_SAMPLES 个有效样本。 */
void App_Measure_StartTare(App_Measure_t *measure);

/* 主循环调用：消费 DRDY 边沿计数并处理样本，独立于 VOFA 输出开关。
 * 长时间未见新边沿时自动轮询一次 DRDY 电平兜底（命中计入 CH7）。 */
void App_Measure_Poll(App_Measure_t *measure);

/* 仅主循环调用。一次复制同一样本的码值、电压和采样时刻。 */
void App_Measure_GetLatest(const App_Measure_t *measure,
                          App_Measure_Snapshot_t *snapshot);

/**
 * @brief 主循环把自己的周期上报给采集模块，用于诊断最长阻塞。
 *
 * 该值只用于诊断通道 CH8。若它接近 ADC 转换周期，说明主循环余量不足，
 * 采样会开始被主循环拖慢。
 *
 * @param measure   采集上下文
 * @param period_ms 距上一次 App_Loop 调用的毫秒数
 */
void App_Measure_NoteLoopPeriod(App_Measure_t *measure, uint32_t period_ms);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MEASURE_H__ */
