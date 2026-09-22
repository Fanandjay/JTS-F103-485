/**
 ******************************************************************************
 * @file    app_measure.h
 * @brief   采集状态机：DRDY 轮询 → 取数 → 去皮 → 滤波 → 输出。
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
    ADS1220_Handle_t   *hads;          /*!< 已初始化的 ADC 句柄 */
    UART_HandleTypeDef *huart;         /*!< VOFA+ 数据输出串口 */

    uint8_t  output_enabled;           /*!< 非 0 表示正在输出样本 */
    uint8_t  filter_started;           /*!< 非 0 表示滤波器已按首个有效样本初始化 */

    /* DRDY 轮询状态 */
    uint8_t  drdy_low_count;           /*!< 连续观察到低电平的次数 */
    uint8_t  drdy_read_locked;         /*!< 已读取一次，等待 DRDY 回高 */
    uint32_t drdy_lock_tick;           /*!< 进入锁定时的 tick，用于超时解锁 */

    /* 去皮（零点标定） */
    uint8_t  tare_running;             /*!< 非 0 表示正在采集零点 */
    uint8_t  zero_valid;               /*!< 非 0 表示零点有效 */
    int64_t  zero_sum;                 /*!< 零点样本累加 */
    uint32_t tare_sample_count;        /*!< 已采集的零点样本数 */
    int32_t  zero_offset;              /*!< 零点偏移（原始码） */

    /* 滤波 */
    MovingAverage5_t    moving_average;
    FirstOrderLowPass_t lowpass;

    /* 统计 */
    uint32_t invalid_sample_count;     /*!< 读回全 1 等无效样本计数 */
} App_Measure_t;

/* 初始化采集上下文（清零并绑定句柄）。 */
void App_Measure_Init(App_Measure_t *measure,
                      ADS1220_Handle_t *hads,
                      UART_HandleTypeDef *huart);

/* 使能/停止样本输出。使能时会复位滤波器，避免带入旧状态。 */
void App_Measure_SetOutput(App_Measure_t *measure, uint8_t enable);

/* 开始去皮：暂停输出并清零零点累加器，等待 APP_TARE_SAMPLES 个有效样本。 */
void App_Measure_StartTare(App_Measure_t *measure);

/* 主循环调用：推进 DRDY 轮询与采样处理，不阻塞。 */
void App_Measure_Poll(App_Measure_t *measure);

#ifdef __cplusplus
}
#endif

#endif /* __APP_MEASURE_H__ */
