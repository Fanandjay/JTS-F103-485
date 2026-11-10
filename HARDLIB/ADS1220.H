/**
 ******************************************************************************
 * @file    ADS1220.H
 * @brief   ADS1220 24位ΔΣ ADC 驱动头文件
 * @note    适用于 STM32F1 + HAL 库，通过 SPI2 与 ADS1220 通信
 *          - SPI 模式1 (CPOL=0, CPHA=1)，MSB 在前
 *          - CS  由 PB12 软件控制
 *          - DRDY/MISO 接 PB14
 ******************************************************************************
 */

#ifndef __ADS1220_H__
#define __ADS1220_H__

#include "main.h"
#include "spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*==============================================================================
 * 寄存器地址 (4个8位配置寄存器)
 *============================================================================*/
#define ADS1220_REG_CONFIG0     0x00U
#define ADS1220_REG_CONFIG1     0x01U
#define ADS1220_REG_CONFIG2     0x02U
#define ADS1220_REG_CONFIG3     0x03U

/*==============================================================================
 * SPI 命令字
 *============================================================================*/
#define ADS1220_CMD_RESET       0x06U
#define ADS1220_CMD_START       0x08U
#define ADS1220_CMD_POWERDOWN   0x02U
#define ADS1220_CMD_RDATA       0x10U
#define ADS1220_CMD_RREG        0x20U
#define ADS1220_CMD_WREG        0x40U

/*==============================================================================
 * CONFIG0 — MUX / GAIN / PGA_BYPASS
 *============================================================================*/
/* MUX 位值 (CONFIG0[7:4]) —— 按数值递增排列，与 ADS1220_Channel_t 枚举一一对应 */
#define ADS1220_MUX_AIN0_AIN1       0x00U
#define ADS1220_MUX_AIN0_AIN2       0x10U
#define ADS1220_MUX_AIN0_AIN3       0x20U
#define ADS1220_MUX_AIN1_AIN2       0x30U
#define ADS1220_MUX_AIN1_AIN3       0x40U
#define ADS1220_MUX_AIN2_AIN3       0x50U
#define ADS1220_MUX_AIN1_AIN0       0x60U   /* AIN1 为正、AIN0 为负（反相） */
#define ADS1220_MUX_AIN3_AIN2       0x70U   /* AIN3 为正、AIN2 为负（反相） */
#define ADS1220_MUX_MASK            0xF0U

/* GAIN 位值 */
#define ADS1220_GAIN_VAL_1         0x00U
#define ADS1220_GAIN_VAL_2         0x02U
#define ADS1220_GAIN_VAL_4         0x04U
#define ADS1220_GAIN_VAL_8         0x06U
#define ADS1220_GAIN_VAL_16        0x08U
#define ADS1220_GAIN_VAL_32        0x0AU
#define ADS1220_GAIN_VAL_64        0x0CU
#define ADS1220_GAIN_VAL_128       0x0EU
#define ADS1220_GAIN_MASK          0x0EU

/*==============================================================================
 * CONFIG1 — DR / MODE / CM / TS / BCS
 *============================================================================*/
/* 数据速率 DR 位值 */
#define ADS1220_DR_VAL_20SPS          0x00U
#define ADS1220_DR_VAL_45SPS          0x20U
#define ADS1220_DR_VAL_90SPS          0x40U
#define ADS1220_DR_VAL_175SPS         0x60U
#define ADS1220_DR_VAL_330SPS         0x80U
#define ADS1220_DR_VAL_600SPS         0xA0U
#define ADS1220_DR_VAL_1000SPS        0xC0U
#define ADS1220_DR_VAL_2000SPS        0xE0U
#define ADS1220_DR_MASK               0xE0U

/* 模式位值 */
#define ADS1220_MODE_VAL_NORMAL        0x00U
#define ADS1220_MODE_VAL_DUTY_CYCLE  0x08U
#define ADS1220_MODE_VAL_TURBO        0x10U
#define ADS1220_MODE_MASK             0x18U

/* CM 位值 */
#define ADS1220_CM_VAL_SINGLE         0x00U
#define ADS1220_CM_VAL_CONTINUOUS     0x04U
#define ADS1220_CM_MASK               0x04U

/* TS 位值 */
#define ADS1220_TS_DISABLED           0x00U
#define ADS1220_TS_ENABLED            0x02U
#define ADS1220_TS_MASK               0x02U

/* BCS 位值 */
#define ADS1220_BCS_DISABLED          0x00U
#define ADS1220_BCS_ENABLED           0x01U
#define ADS1220_BCS_MASK              0x01U

/*==============================================================================
 * CONFIG2 — VREF / 50-60Hz / PSW / IDAC
 *============================================================================*/
/* VREF 位值 */
#define ADS1220_VREF_VAL_INTERNAL     0x00U
#define ADS1220_VREF_VAL_EXT_REFP0   0x40U
#define ADS1220_VREF_VAL_AVDD         0x80U
#define ADS1220_VREF_VAL_EXT_ALT      0xC0U
#define ADS1220_VREF_MASK             0xC0U

/* 50/60Hz 抑制 */
#define ADS1220_50_60_BOTH            0x00U
#define ADS1220_50_60_50HZ            0x10U
#define ADS1220_50_60_60HZ            0x20U
#define ADS1220_50_60_MASK            0x30U

/* PSW */
#define ADS1220_PSW_OPEN              0x00U
#define ADS1220_PSW_CLOSED            0x08U

/* IDAC 电流值 */
#define ADS1220_IDAC_VAL_OFF          0x00U
#define ADS1220_IDAC_VAL_10UA         0x01U
#define ADS1220_IDAC_VAL_50UA         0x02U
#define ADS1220_IDAC_VAL_100UA        0x03U
#define ADS1220_IDAC_VAL_250UA        0x04U
#define ADS1220_IDAC_VAL_500UA        0x05U
#define ADS1220_IDAC_VAL_1000UA       0x06U
#define ADS1220_IDAC_VAL_1500UA       0x07U
#define ADS1220_IDAC_MASK             0x07U

/*==============================================================================
 * CONFIG3 — IDAC1/2路由 / DRDYM
 *============================================================================*/
#define ADS1220_IDAC1_OFF             0x00U
#define ADS1220_IDAC1_AIN0            0x20U
#define ADS1220_IDAC1_AIN1            0x40U
#define ADS1220_IDAC1_AIN2            0x60U
#define ADS1220_IDAC1_AIN3            0x80U
#define ADS1220_IDAC1_REFP0           0xA0U
#define ADS1220_IDAC1_REFN0           0xC0U
#define ADS1220_IDAC1_MASK            0xE0U

#define ADS1220_IDAC2_OFF             0x00U
#define ADS1220_IDAC2_AIN0            0x04U
#define ADS1220_IDAC2_AIN1            0x08U
#define ADS1220_IDAC2_AIN2            0x0CU
#define ADS1220_IDAC2_AIN3            0x10U
#define ADS1220_IDAC2_REFP0           0x14U
#define ADS1220_IDAC2_REFN0           0x18U
#define ADS1220_IDAC2_MASK            0x1CU

#define ADS1220_DRDY_OLD_MODE         0x00U
#define ADS1220_DRDY_NEW_MODE         0x02U
#define ADS1220_DRDY_MODE_MASK        0x02U

/*==============================================================================
 * 寄存器默认值
 *============================================================================*/
#define ADS1220_CONFIG0_DEFAULT       0x00U
#define ADS1220_CONFIG1_DEFAULT       0x00U
#define ADS1220_CONFIG2_DEFAULT       0x00U
#define ADS1220_CONFIG3_DEFAULT       0x00U

/*==============================================================================
 * 驱动配置常量
 *============================================================================*/
#define ADS1220_SPI_TIMEOUT           100U
#define ADS1220_DRDY_TIMEOUT         500U
#define ADS1220_DATA_SIZE             3U

/*==============================================================================
 * 数据类型定义
 *============================================================================*/

/* 通道枚举 - 索引值，不是寄存器位值 */
typedef enum {
    ADS1220_CH0_AIN0_AIN1 = 0,    /* 差分: AIN0-AIN1 */
    ADS1220_CH1_AIN0_AIN2 = 1,    /* 差分: AIN0-AIN2 */
    ADS1220_CH2_AIN0_AIN3 = 2,    /* 差分: AIN0-AIN3 */
    ADS1220_CH3_AIN1_AIN2 = 3,    /* 差分: AIN1-AIN2 */
    ADS1220_CH4_AIN1_AIN3 = 4,    /* 差分: AIN1-AIN3 */
    ADS1220_CH5_AIN2_AIN3 = 5,    /* 差分: AIN2-AIN3 */
    ADS1220_CH6_AIN1_AIN0 = 6,    /* 差分: AIN1-AIN0 (反接) */
    ADS1220_CH7_AIN3_AIN2 = 7,    /* 差分: AIN3-AIN2 (反接) */
    ADS1220_CHANNEL_COUNT,        /* 通道总数，非合法通道；用于查表长度与边界检查 */
} ADS1220_Channel_t;

/* 增益枚举 */
typedef enum {
    ADS1220_G1 = 0,
    ADS1220_G2 = 1,
    ADS1220_G4 = 2,
    ADS1220_G8 = 3,
    ADS1220_G16 = 4,
    ADS1220_G32 = 5,
    ADS1220_G64 = 6,
    ADS1220_G128 = 7,
    ADS1220_GAIN_COUNT,           /* 增益档位总数，非合法档位 */
} ADS1220_Gain_t;

/* 数据速率枚举 */
typedef enum {
    ADS1220_DR20 = 0,
    ADS1220_DR45 = 1,
    ADS1220_DR90 = 2,
    ADS1220_DR175 = 3,
    ADS1220_DR330 = 4,
    ADS1220_DR600 = 5,
    ADS1220_DR1000 = 6,
    ADS1220_DR2000 = 7,
    ADS1220_DR_COUNT,             /* 速率档位总数，非合法档位 */
} ADS1220_DataRate_t;

/* 模式枚举 */
typedef enum {
    ADS1220_MOD_NORM = 0,
    ADS1220_MOD_TURBO = 1,
    ADS1220_MOD_DUTY = 2,
    ADS1220_MODE_COUNT,           /* 模式总数，非合法模式 */
} ADS1220_Mode_t;

/* 转换模式枚举 */
typedef enum {
    ADS1220_CONV_SINGLE = 0,
    ADS1220_CONV_CONT = 1,
    ADS1220_CONV_COUNT,           /* 转换模式总数，非合法模式 */
} ADS1220_ConvMode_t;

/* VREF 枚举 */
typedef enum {
    ADS1220_VREF_INT = 0,
    ADS1220_VREF_EXT = 1,
    ADS1220_VREF_AVDD = 2,
    ADS1220_VREF_EXT2 = 3,
    ADS1220_VREF_COUNT,           /* 基准源总数，非合法基准 */
} ADS1220_VRef_t;

/* IDAC 电流枚举 */
typedef enum {
    ADS1220_IDAC0 = 0,
    ADS1220_IDAC10UA = 1,
    ADS1220_IDAC50UA = 2,
    ADS1220_IDAC100UA = 3,
    ADS1220_IDAC250UA = 4,
    ADS1220_IDAC500UA = 5,
    ADS1220_IDAC1000UA = 6,
    ADS1220_IDAC1500UA = 7,
    ADS1220_IDAC_COUNT,           /* IDAC 档位总数，非合法档位 */
} ADS1220_IDAC_Current_t;

/* 器件句柄 */
typedef struct {
    SPI_HandleTypeDef   *hspi;
    GPIO_TypeDef        *cs_port;
    uint16_t             cs_pin;
    GPIO_TypeDef        *drdy_port;
    uint16_t             drdy_pin;

    uint8_t              config[4];
    ADS1220_Channel_t    channel;
    ADS1220_Gain_t       gain;
    ADS1220_DataRate_t   data_rate;
    ADS1220_Mode_t       mode;
    ADS1220_ConvMode_t   conv_mode;
    ADS1220_VRef_t       vref;

    uint8_t              is_initialized;
} ADS1220_Handle_t;

/*==============================================================================
 * 函数声明
 *============================================================================*/
HAL_StatusTypeDef ADS1220_Init(ADS1220_Handle_t *hads,
                               SPI_HandleTypeDef *hspi,
                               GPIO_TypeDef *cs_port, uint16_t cs_pin,
                               GPIO_TypeDef *drdy_port, uint16_t drdy_pin);

HAL_StatusTypeDef ADS1220_Reset(ADS1220_Handle_t *hads);
HAL_StatusTypeDef ADS1220_ReadReg(ADS1220_Handle_t *hads, uint8_t reg, uint8_t *pData);
HAL_StatusTypeDef ADS1220_WriteReg(ADS1220_Handle_t *hads, uint8_t reg, uint8_t data);
HAL_StatusTypeDef ADS1220_ReadAllRegs(ADS1220_Handle_t *hads);

/*==============================================================================
 * 配置期望值与回读校验
 *============================================================================*/

/**
 * @brief 回读 CONFIG0..3，并与驱动内部记录的期望值比对。
 *
 * 期望值取自 hads->config[]（即所有 Set* 写入后的结果），因此校验对象
 * 与实际写入内容同源，不需要在调用方重复硬编码寄存器期望值。
 * 校验完成后 hads->config[] 会被刷新为芯片的实际值。
 *
 * @param hads     器件句柄
 * @param expected 输出：写入后期望的 4 个寄存器值，可为 NULL
 * @param actual   输出：从芯片读回的实际 4 个寄存器值，可为 NULL
 * @return HAL_OK 表示 4 个寄存器完全一致，否则 HAL_ERROR
 */
HAL_StatusTypeDef ADS1220_VerifyConfig(ADS1220_Handle_t *hads,
                                       uint8_t expected[4],
                                       uint8_t actual[4]);

/**
 * @brief 连通性自检：确认 SPI 写通路真正生效。
 *
 * 原理：把 CONFIG3 的 DRDYM 位翻转成一个与当前值不同的合法值，回读确认
 * 写入生效，随后恢复原值。这样即使目标配置恰好等于芯片当前值，也能发现
 * 写通路故障（仅靠"目标值 == 读回值"的比对无法覆盖这种情形）。
 *
 * 扰动只涉及 DRDYM（DRDY 新旧模式），不改变模拟通路，也不启用 IDAC 电流，
 * 应在启动转换之前调用。
 *
 * @return HAL_OK 表示写入生效且原值已恢复，否则 HAL_ERROR
 */
HAL_StatusTypeDef ADS1220_ConnectionTest(ADS1220_Handle_t *hads);

/**
 * @brief DRDY 波形观测结果（诊断用）。
 *
 * 用于在没有示波器时判定 DOUT/DRDY 属于哪一类行为：
 *
 *   - **锁存型**（老 DRDY 模式，CONFIG3.DRDYM = 0）
 *     转换完成后拉低，并**保持低直到转换结果被读走**。
 *     因此在"只观测不读取"的窗口内只会出现 1 个下降沿，之后长期为低
 *     —— 轮询方案可以正常工作。
 *
 *   - **脉冲型**（新 DRDY 模式，CONFIG3.DRDYM = 1）
 *     每次转换完成后短暂拉低随即自行回高。
 *     下降沿数量约等于转换次数，低电平占空比很小
 *     —— **轮询方案会大量漏检，必须改用 EXTI 中断**。
 */
typedef struct
{
    uint32_t duration_us;     /*!< 实际观测窗口时长 (us) */
    uint32_t samples;         /*!< 采样点数，用于评估时间分辨率 */
    uint32_t falling_edges;   /*!< 观测到的下降沿次数 */
    uint32_t high_samples;    /*!< DRDY 为高（未就绪）的采样点数 */
    uint32_t low_samples;     /*!< DRDY 为低（就绪）的采样点数 */
    uint32_t low_us_max;      /*!< 已完整测到的最长一段连续低电平 (us) */
    uint32_t low_us_pending;  /*!< 窗口结束时若仍为低，该段已持续时长 (us) */
    uint8_t  level_at_end;    /*!< 1 = 窗口结束时 DRDY 仍为低 */
} ADS1220_DrdyStats_t;

/**
 * @brief 在指定时长内高速采样 DOUT/DRDY，统计其波形特征。
 *
 * 只读取引脚电平，**不做任何 SPI 传输**，因此不会改变 ADS1220 的状态。
 * 调用前应已启动连续转换，否则不会产生下降沿。
 *
 * 计时使用 Cortex-M3/M4 自带的 DWT 周期计数器（微秒级，无需占用定时器）。
 *
 * @param hads        器件句柄
 * @param duration_ms 观测时长 (ms)
 * @param stats       输出：统计结果
 * @return HAL_OK 成功；参数非法或 DWT 不可用返回 HAL_ERROR
 */
HAL_StatusTypeDef ADS1220_ObserveDrdy(ADS1220_Handle_t *hads,
                                      uint32_t duration_ms,
                                      ADS1220_DrdyStats_t *stats);

/**
 * @brief "读取后 DRDY 行为 + 真实转换周期"探测结果。
 */
typedef struct
{
    uint8_t  rounds_done;       /*!< 实际完成的轮数 */
    uint8_t  high_ok_rounds;    /*!< 其中"读完后 DRDY 回高"的轮数 */
    uint32_t high_wait_us_max;  /*!< 读完后 DRDY 回高的最长耗时 (us) */
    uint32_t period_us_min;     /*!< 实测转换周期最小值 (us)，0 表示未测到 */
    uint32_t period_us_max;     /*!< 实测转换周期最大值 (us) */
    uint32_t period_us_avg;     /*!< 实测转换周期平均值 (us) */
} ADS1220_ConvProbe_t;

/**
 * @brief 实测"读取后 DRDY 是否回高"以及真实的转换周期。
 *
 * 单轮流程：
 *   1. 等 DRDY 变低（读取就绪）
 *   2. 读走 3 字节转换结果（锁存型下这会让 DRDY 回高）
 *   3. 计时：读完 -> DRDY 回高      —— 用于验证 DRDY 握手是否正常
 *   4. 计时：DRDY 回高 -> 再次变低  —— 这就是一个完整转换周期
 *
 * 第 4 步的值**不受上电整定时间影响**，因此能直接回答
 * "ADC 实际是不是配的那个速率"。多轮取平均可抑制抖动。
 *
 * @param hads       器件句柄
 * @param rounds     轮数（建议 3）
 * @param timeout_ms 每步等待超时 (ms)，应大于预期转换周期
 * @param probe      输出：探测结果
 * @return HAL_OK 成功；HAL_TIMEOUT 等待超时；HAL_ERROR 参数非法或 DWT 不可用
 */
HAL_StatusTypeDef ADS1220_ProbeConversion(ADS1220_Handle_t *hads,
                                          uint32_t rounds,
                                          uint32_t timeout_ms,
                                          ADS1220_ConvProbe_t *probe);

/* 句柄当前增益的实际数值 (1/2/4/.../128)，用于调试打印 */
uint8_t ADS1220_GetGainValue(ADS1220_Handle_t *hads);

/* 数据速率枚举 -> 实际采样率 (SPS)，越界返回 0，用于调试打印 */
uint16_t ADS1220_DataRateSps(ADS1220_DataRate_t rate);

/* 通道枚举 -> 名称字符串，越界返回 "unknown"，仅用于调试打印 */
const char *ADS1220_ChannelName(ADS1220_Channel_t channel);

HAL_StatusTypeDef ADS1220_SetChannel(ADS1220_Handle_t *hads, ADS1220_Channel_t channel);
HAL_StatusTypeDef ADS1220_SetGain(ADS1220_Handle_t *hads, ADS1220_Gain_t gain);
HAL_StatusTypeDef ADS1220_SetDataRate(ADS1220_Handle_t *hads, ADS1220_DataRate_t rate);
HAL_StatusTypeDef ADS1220_SetMode(ADS1220_Handle_t *hads, ADS1220_Mode_t mode);
HAL_StatusTypeDef ADS1220_SetConvMode(ADS1220_Handle_t *hads, ADS1220_ConvMode_t mode);
HAL_StatusTypeDef ADS1220_SetVRef(ADS1220_Handle_t *hads, ADS1220_VRef_t vref);
HAL_StatusTypeDef ADS1220_SetIDACCurrent(ADS1220_Handle_t *hads, ADS1220_IDAC_Current_t current);
HAL_StatusTypeDef ADS1220_SetIDAC1Channel(ADS1220_Handle_t *hads, uint8_t ch);
HAL_StatusTypeDef ADS1220_SetIDAC2Channel(ADS1220_Handle_t *hads, uint8_t ch);
HAL_StatusTypeDef ADS1220_EnableTempSensor(ADS1220_Handle_t *hads, uint8_t enable);

HAL_StatusTypeDef ADS1220_Start(ADS1220_Handle_t *hads);
HAL_StatusTypeDef ADS1220_PowerDown(ADS1220_Handle_t *hads);

uint8_t ADS1220_IsDataReady(ADS1220_Handle_t *hads);
HAL_StatusTypeDef ADS1220_WaitForDataReady(ADS1220_Handle_t *hads, uint32_t timeout);
HAL_StatusTypeDef ADS1220_WaitForConversion(ADS1220_Handle_t *hads, uint32_t timeout);
HAL_StatusTypeDef ADS1220_ReadData(ADS1220_Handle_t *hads, int32_t *pRaw);
HAL_StatusTypeDef ADS1220_ReadDataContinuous(ADS1220_Handle_t *hads, int32_t *pRaw);

float ADS1220_RawToVoltage(int32_t raw, float vref_mv, uint8_t gain);
HAL_StatusTypeDef ADS1220_ReadTemperature(ADS1220_Handle_t *hads, float *temperature);

HAL_StatusTypeDef ADS1220_Config(ADS1220_Handle_t *hads,
                                  ADS1220_Channel_t ch,
                                  ADS1220_Gain_t gain,
                                  ADS1220_DataRate_t rate,
                                  ADS1220_Mode_t mode,
                                  ADS1220_ConvMode_t conv,
                                  ADS1220_VRef_t vref);

#ifdef __cplusplus
}
#endif

#endif
