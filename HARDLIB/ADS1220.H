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
/* MUX 位值 */
#define ADS1220_MUX_AIN0_AIN1       0x00U
#define ADS1220_MUX_AIN0_AIN2       0x10U
#define ADS1220_MUX_AIN0_AIN3       0x20U
#define ADS1220_MUX_AIN1_AIN0       0x01U
#define ADS1220_MUX_AIN1_AIN2       0x30U
#define ADS1220_MUX_AIN1_AIN3       0x40U
#define ADS1220_MUX_AIN2_AIN3       0x50U
#define ADS1220_MUX_AIN3_AIN2       0x60U
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
} ADS1220_DataRate_t;

/* 模式枚举 */
typedef enum {
    ADS1220_MOD_NORM = 0,
    ADS1220_MOD_TURBO = 1,
    ADS1220_MOD_DUTY = 2,
} ADS1220_Mode_t;

/* 转换模式枚举 */
typedef enum {
    ADS1220_CONV_SINGLE = 0,
    ADS1220_CONV_CONT = 1,
} ADS1220_ConvMode_t;

/* VREF 枚举 */
typedef enum {
    ADS1220_VREF_INT = 0,
    ADS1220_VREF_EXT = 1,
    ADS1220_VREF_AVDD = 2,
    ADS1220_VREF_EXT2 = 3,
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
