/**
 ******************************************************************************
 * @file    ADS1220.C
 * @brief   ADS1220 24位ΔΣ ADC 驱动实现
 * @note    SPI 模式1 (CPOL=0, CPHA=1) — 时钟空闲低，数据在SCLK下降沿移出
 ******************************************************************************
 */

#include "ADS1220.H"

/*==============================================================================
 * === 底层硬件操作 ===
 *============================================================================*/

static void ADS1220_CS_Low(ADS1220_Handle_t *hads)
{
    HAL_GPIO_WritePin(hads->cs_port, hads->cs_pin, GPIO_PIN_RESET);
}

static void ADS1220_CS_High(ADS1220_Handle_t *hads)
{
    HAL_GPIO_WritePin(hads->cs_port, hads->cs_pin, GPIO_PIN_SET);
}

static HAL_StatusTypeDef ADS1220_SPI_Transfer(ADS1220_Handle_t *hads,
                                              uint8_t tx_byte, uint8_t *rx_byte)
{
    return HAL_SPI_TransmitReceive(hads->hspi, &tx_byte, rx_byte, 1,
                                   ADS1220_SPI_TIMEOUT);
}

static HAL_StatusTypeDef ADS1220_SPI_Write(ADS1220_Handle_t *hads, uint8_t tx_byte)
{
    uint8_t rx_dummy;
    HAL_StatusTypeDef ret;
    ret = HAL_SPI_TransmitReceive(hads->hspi, &tx_byte, &rx_dummy, 1,
                                  ADS1220_SPI_TIMEOUT);
    /* CPHA=1 errata workaround */
    for (volatile uint8_t i = 0; i < 72; i++) { __NOP(); }
    return ret;
}

/*==============================================================================
 * === 初始化 ===
 *============================================================================*/

HAL_StatusTypeDef ADS1220_Init(ADS1220_Handle_t *hads,
                               SPI_HandleTypeDef *hspi,
                               GPIO_TypeDef *cs_port, uint16_t cs_pin,
                               GPIO_TypeDef *drdy_port, uint16_t drdy_pin)
{
    if (hads == NULL || hspi == NULL)
        return HAL_ERROR;

    hads->hspi      = hspi;
    hads->cs_port   = cs_port;
    hads->cs_pin    = cs_pin;
    hads->drdy_port = drdy_port;
    hads->drdy_pin  = drdy_pin;

    ADS1220_CS_High(hads);

    if (ADS1220_ReadAllRegs(hads) != HAL_OK)
    {
        hads->is_initialized = 0;
        return HAL_ERROR;
    }

    hads->channel   = ADS1220_CH0_AIN0_AIN1;
    hads->gain      = ADS1220_G1;
    hads->data_rate = ADS1220_DR20;
    hads->mode      = ADS1220_MOD_NORM;
    hads->conv_mode = ADS1220_CONV_SINGLE;
    hads->vref      = ADS1220_VREF_INT;

    hads->is_initialized = 1;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_Reset(ADS1220_Handle_t *hads)
{
    HAL_StatusTypeDef status;

    ADS1220_CS_Low(hads);
    status = ADS1220_SPI_Write(hads, ADS1220_CMD_RESET);
    ADS1220_CS_High(hads);

    if (status != HAL_OK)
        return status;

    HAL_Delay(1);
    return ADS1220_ReadAllRegs(hads);
}

/*==============================================================================
 * === 寄存器读写 ===
 *============================================================================*/

HAL_StatusTypeDef ADS1220_ReadReg(ADS1220_Handle_t *hads, uint8_t reg, uint8_t *pData)
{
    HAL_StatusTypeDef status;
    uint8_t cmd;

    if (reg > ADS1220_REG_CONFIG3)
        return HAL_ERROR;

    cmd = ADS1220_CMD_RREG | ((reg & 0x03U) << 2);

    ADS1220_CS_Low(hads);
    status = ADS1220_SPI_Write(hads, cmd);
    if (status != HAL_OK) { ADS1220_CS_High(hads); return status; }
    status = ADS1220_SPI_Transfer(hads, 0xFFU, pData);
    ADS1220_CS_High(hads);
    return status;
}

HAL_StatusTypeDef ADS1220_WriteReg(ADS1220_Handle_t *hads, uint8_t reg, uint8_t data)
{
    HAL_StatusTypeDef status;
    uint8_t cmd;

    if (reg > ADS1220_REG_CONFIG3)
        return HAL_ERROR;

    cmd = ADS1220_CMD_WREG | ((reg & 0x03U) << 2);

    ADS1220_CS_Low(hads);
    status = ADS1220_SPI_Write(hads, cmd);
    if (status != HAL_OK) { ADS1220_CS_High(hads); return status; }
    status = ADS1220_SPI_Write(hads, data);
    ADS1220_CS_High(hads);

    if (status == HAL_OK)
        hads->config[reg] = data;
    return status;
}

HAL_StatusTypeDef ADS1220_ReadAllRegs(ADS1220_Handle_t *hads)
{
    HAL_StatusTypeDef status;
    uint8_t cmd = ADS1220_CMD_RREG | 0x03U;  /* 读4个寄存器 */
    uint8_t dummy = 0xFFU;
    uint8_t rx_buf[4] = {0};

    ADS1220_CS_Low(hads);

    status = ADS1220_SPI_Write(hads, cmd);
    if (status != HAL_OK) { ADS1220_CS_High(hads); return status; }

    for (uint8_t i = 0; i < 4; i++)
    {
        status = HAL_SPI_TransmitReceive(hads->hspi, &dummy, &rx_buf[i], 1,
                                         ADS1220_SPI_TIMEOUT);
        if (status != HAL_OK) { ADS1220_CS_High(hads); return status; }
    }

    ADS1220_CS_High(hads);

    hads->config[0] = rx_buf[0];
    hads->config[1] = rx_buf[1];
    hads->config[2] = rx_buf[2];
    hads->config[3] = rx_buf[3];

    return HAL_OK;
}

/*==============================================================================
 * === 配置函数 ===
 *============================================================================*/

/* MUX 位值表 —— 指定初始化器，下标与 ADS1220_Channel_t 显式绑定，
 * 枚举与查表不再依赖书写顺序，改动其一不会被静默错位。 */
static const uint8_t mux_values[ADS1220_CHANNEL_COUNT] = {
    [ADS1220_CH0_AIN0_AIN1] = ADS1220_MUX_AIN0_AIN1,
    [ADS1220_CH1_AIN0_AIN2] = ADS1220_MUX_AIN0_AIN2,
    [ADS1220_CH2_AIN0_AIN3] = ADS1220_MUX_AIN0_AIN3,
    [ADS1220_CH3_AIN1_AIN2] = ADS1220_MUX_AIN1_AIN2,
    [ADS1220_CH4_AIN1_AIN3] = ADS1220_MUX_AIN1_AIN3,
    [ADS1220_CH5_AIN2_AIN3] = ADS1220_MUX_AIN2_AIN3,
    [ADS1220_CH6_AIN1_AIN0] = ADS1220_MUX_AIN1_AIN0,
    [ADS1220_CH7_AIN3_AIN2] = ADS1220_MUX_AIN3_AIN2,
};

/* GAIN 位值表 */
static const uint8_t gain_values[ADS1220_GAIN_COUNT] = {
    [ADS1220_G1]   = ADS1220_GAIN_VAL_1,
    [ADS1220_G2]   = ADS1220_GAIN_VAL_2,
    [ADS1220_G4]   = ADS1220_GAIN_VAL_4,
    [ADS1220_G8]   = ADS1220_GAIN_VAL_8,
    [ADS1220_G16]  = ADS1220_GAIN_VAL_16,
    [ADS1220_G32]  = ADS1220_GAIN_VAL_32,
    [ADS1220_G64]  = ADS1220_GAIN_VAL_64,
    [ADS1220_G128] = ADS1220_GAIN_VAL_128,
};

/* DR 位值表 */
static const uint8_t dr_values[ADS1220_DR_COUNT] = {
    [ADS1220_DR20]   = ADS1220_DR_VAL_20SPS,
    [ADS1220_DR45]   = ADS1220_DR_VAL_45SPS,
    [ADS1220_DR90]   = ADS1220_DR_VAL_90SPS,
    [ADS1220_DR175]  = ADS1220_DR_VAL_175SPS,
    [ADS1220_DR330]  = ADS1220_DR_VAL_330SPS,
    [ADS1220_DR600]  = ADS1220_DR_VAL_600SPS,
    [ADS1220_DR1000] = ADS1220_DR_VAL_1000SPS,
    [ADS1220_DR2000] = ADS1220_DR_VAL_2000SPS,
};

/* 实际增益值表 - 用于计算与打印 */
static const uint8_t gain_actual[ADS1220_GAIN_COUNT] = {
    [ADS1220_G1]   = 1U,
    [ADS1220_G2]   = 2U,
    [ADS1220_G4]   = 4U,
    [ADS1220_G8]   = 8U,
    [ADS1220_G16]  = 16U,
    [ADS1220_G32]  = 32U,
    [ADS1220_G64]  = 64U,
    [ADS1220_G128] = 128U,
};

/* 实际采样率表 (SPS) - 与 dr_values[] 一一对应，用于调试打印 */
static const uint16_t dr_sps[ADS1220_DR_COUNT] = {
    [ADS1220_DR20]   = 20U,
    [ADS1220_DR45]   = 45U,
    [ADS1220_DR90]   = 90U,
    [ADS1220_DR175]  = 175U,
    [ADS1220_DR330]  = 330U,
    [ADS1220_DR600]  = 600U,
    [ADS1220_DR1000] = 1000U,
    [ADS1220_DR2000] = 2000U,
};

/* 通道名称表 - 与 mux_values[] 一一对应，仅用于调试打印 */
static const char *const mux_names[ADS1220_CHANNEL_COUNT] = {
    [ADS1220_CH0_AIN0_AIN1] = "AIN0-AIN1",
    [ADS1220_CH1_AIN0_AIN2] = "AIN0-AIN2",
    [ADS1220_CH2_AIN0_AIN3] = "AIN0-AIN3",
    [ADS1220_CH3_AIN1_AIN2] = "AIN1-AIN2",
    [ADS1220_CH4_AIN1_AIN3] = "AIN1-AIN3",
    [ADS1220_CH5_AIN2_AIN3] = "AIN2-AIN3",
    [ADS1220_CH6_AIN1_AIN0] = "AIN1-AIN0 (rev)",
    [ADS1220_CH7_AIN3_AIN2] = "AIN3-AIN2 (rev)",
};

/*==============================================================================
 * === 期望值与回读校验 ===
 *============================================================================*/

uint16_t ADS1220_DataRateSps(ADS1220_DataRate_t rate)
{
    if ((uint8_t)rate >= (uint8_t)ADS1220_DR_COUNT)
        return 0U;

    return dr_sps[rate];
}

const char *ADS1220_ChannelName(ADS1220_Channel_t channel)
{
    if ((uint8_t)channel >= (uint8_t)ADS1220_CHANNEL_COUNT)
        return "unknown";

    return mux_names[channel];
}

HAL_StatusTypeDef ADS1220_VerifyConfig(ADS1220_Handle_t *hads,
                                       uint8_t expected[4],
                                       uint8_t actual[4])
{
    uint8_t want[4];
    uint8_t got[4];
    uint8_t i;
    uint8_t match = 1U;

    if (hads == NULL)
        return HAL_ERROR;

    /* 先取出驱动认为已经写入的值。 */
    for (i = 0; i < 4U; i++)
        want[i] = hads->config[i];

    /* ADS1220_ReadAllRegs 会把芯片实际值刷新进 hads->config[]。 */
    if (ADS1220_ReadAllRegs(hads) != HAL_OK)
        return HAL_ERROR;

    for (i = 0; i < 4U; i++)
    {
        got[i] = hads->config[i];
        if (got[i] != want[i])
            match = 0U;
    }

    if (expected != NULL)
    {
        for (i = 0; i < 4U; i++)
            expected[i] = want[i];
    }

    if (actual != NULL)
    {
        for (i = 0; i < 4U; i++)
            actual[i] = got[i];
    }

    return (match != 0U) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef ADS1220_ConnectionTest(ADS1220_Handle_t *hads)
{
    uint8_t original;
    uint8_t probe;
    uint8_t readback;
    HAL_StatusTypeDef status;

    if (hads == NULL)
        return HAL_ERROR;

    original = hads->config[ADS1220_REG_CONFIG3];

    /* 只翻转 DRDYM 位（DRDY 新旧模式）：不改变模拟通路，也不启用 IDAC。 */
    probe = (uint8_t)(original ^ ADS1220_DRDY_NEW_MODE);

    status = ADS1220_WriteReg(hads, ADS1220_REG_CONFIG3, probe);
    if (status == HAL_OK)
        status = ADS1220_ReadReg(hads, ADS1220_REG_CONFIG3, &readback);

    /* 无论成功与否都恢复原值，避免把自检用的扰动值留在芯片里。 */
    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG3, original) != HAL_OK)
        status = HAL_ERROR;

    if (status != HAL_OK)
        return HAL_ERROR;

    return (readback == probe) ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef ADS1220_SetChannel(ADS1220_Handle_t *hads, ADS1220_Channel_t channel)
{
    uint8_t cfg0;

    if (hads == NULL || (uint8_t)channel >= (uint8_t)ADS1220_CHANNEL_COUNT)
        return HAL_ERROR;

    cfg0 = hads->config[ADS1220_REG_CONFIG0];
    cfg0 &= ~ADS1220_MUX_MASK;
    cfg0 |= mux_values[channel];

    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG0, cfg0) != HAL_OK)
        return HAL_ERROR;

    hads->channel = channel;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_SetGain(ADS1220_Handle_t *hads, ADS1220_Gain_t gain)
{
    uint8_t cfg0;

    if (hads == NULL || (uint8_t)gain >= (uint8_t)ADS1220_GAIN_COUNT)
        return HAL_ERROR;

    cfg0 = hads->config[ADS1220_REG_CONFIG0];
    cfg0 &= ~ADS1220_GAIN_MASK;
    cfg0 |= gain_values[gain];

    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG0, cfg0) != HAL_OK)
        return HAL_ERROR;

    hads->gain = gain;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_SetDataRate(ADS1220_Handle_t *hads, ADS1220_DataRate_t rate)
{
    uint8_t cfg1;

    if (hads == NULL || (uint8_t)rate >= (uint8_t)ADS1220_DR_COUNT)
        return HAL_ERROR;

    cfg1 = hads->config[ADS1220_REG_CONFIG1];
    cfg1 &= ~ADS1220_DR_MASK;
    cfg1 |= dr_values[rate];

    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG1, cfg1) != HAL_OK)
        return HAL_ERROR;

    hads->data_rate = rate;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_SetMode(ADS1220_Handle_t *hads, ADS1220_Mode_t mode)
{
    uint8_t cfg1;

    if (hads == NULL || (uint8_t)mode >= (uint8_t)ADS1220_MODE_COUNT)
        return HAL_ERROR;

    cfg1 = hads->config[ADS1220_REG_CONFIG1];
    cfg1 &= ~ADS1220_MODE_MASK;

    if (mode == ADS1220_MOD_TURBO)
        cfg1 |= ADS1220_MODE_VAL_TURBO;
    else if (mode == ADS1220_MOD_DUTY)
        cfg1 |= ADS1220_MODE_VAL_DUTY_CYCLE;

    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG1, cfg1) != HAL_OK)
        return HAL_ERROR;

    hads->mode = mode;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_SetConvMode(ADS1220_Handle_t *hads, ADS1220_ConvMode_t mode)
{
    uint8_t cfg1;

    if (hads == NULL || (uint8_t)mode >= (uint8_t)ADS1220_CONV_COUNT)
        return HAL_ERROR;

    cfg1 = hads->config[ADS1220_REG_CONFIG1];
    cfg1 &= ~ADS1220_CM_MASK;
    if (mode == ADS1220_CONV_CONT)
        cfg1 |= ADS1220_CM_VAL_CONTINUOUS;

    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG1, cfg1) != HAL_OK)
        return HAL_ERROR;

    hads->conv_mode = mode;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_SetVRef(ADS1220_Handle_t *hads, ADS1220_VRef_t vref)
{
    uint8_t cfg2;

    if (hads == NULL || (uint8_t)vref >= (uint8_t)ADS1220_VREF_COUNT)
        return HAL_ERROR;

    cfg2 = hads->config[ADS1220_REG_CONFIG2];
    cfg2 &= ~ADS1220_VREF_MASK;
    cfg2 |= ((uint8_t)vref << 6);

    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG2, cfg2) != HAL_OK)
        return HAL_ERROR;

    hads->vref = vref;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_SetIDACCurrent(ADS1220_Handle_t *hads, ADS1220_IDAC_Current_t current)
{
    uint8_t cfg2;

    if (hads == NULL || (uint8_t)current >= (uint8_t)ADS1220_IDAC_COUNT)
        return HAL_ERROR;

    cfg2 = hads->config[ADS1220_REG_CONFIG2];
    cfg2 &= ~ADS1220_IDAC_MASK;
    cfg2 |= (uint8_t)current;
    return ADS1220_WriteReg(hads, ADS1220_REG_CONFIG2, cfg2);
}

/**
 * @brief 设置 IDAC1 的输出通道。
 *
 * @param idac1_bits 取值必须是 ADS1220_IDAC1_xxx 常量之一（已按位对位的
 *                   寄存器值，例如 ADS1220_IDAC1_AIN0 = 0x20）。
 *                   相比"传 0~7 索引再由驱动移位"，直接传寄存器值可以让
 *                   头文件里已有的 ADS1220_IDAC1_xxx 常量直接可用，
 *                   避免出现"常量是移位值、函数却要索引"的混用陷阱。
 */
HAL_StatusTypeDef ADS1220_SetIDAC1Channel(ADS1220_Handle_t *hads, uint8_t idac1_bits)
{
    uint8_t cfg3;

    if (hads == NULL || (idac1_bits & ~ADS1220_IDAC1_MASK) != 0U)
        return HAL_ERROR;

    cfg3 = hads->config[ADS1220_REG_CONFIG3];
    cfg3 &= ~ADS1220_IDAC1_MASK;
    cfg3 |= idac1_bits;
    return ADS1220_WriteReg(hads, ADS1220_REG_CONFIG3, cfg3);
}

/** @brief 设置 IDAC2 的输出通道，参数含义同 ADS1220_SetIDAC1Channel()。 */
HAL_StatusTypeDef ADS1220_SetIDAC2Channel(ADS1220_Handle_t *hads, uint8_t idac2_bits)
{
    uint8_t cfg3;

    if (hads == NULL || (idac2_bits & ~ADS1220_IDAC2_MASK) != 0U)
        return HAL_ERROR;

    cfg3 = hads->config[ADS1220_REG_CONFIG3];
    cfg3 &= ~ADS1220_IDAC2_MASK;
    cfg3 |= idac2_bits;
    return ADS1220_WriteReg(hads, ADS1220_REG_CONFIG3, cfg3);
}

HAL_StatusTypeDef ADS1220_EnableTempSensor(ADS1220_Handle_t *hads, uint8_t enable)
{
    uint8_t cfg1;

    if (hads == NULL)
        return HAL_ERROR;

    cfg1 = hads->config[ADS1220_REG_CONFIG1];
    if (enable)
        cfg1 |= ADS1220_TS_ENABLED;
    else
        cfg1 &= ~ADS1220_TS_MASK;
    return ADS1220_WriteReg(hads, ADS1220_REG_CONFIG1, cfg1);
}

/*==============================================================================
 * === 命令控制 ===
 *============================================================================*/

HAL_StatusTypeDef ADS1220_Start(ADS1220_Handle_t *hads)
{
    HAL_StatusTypeDef status;

    ADS1220_CS_Low(hads);
    status  = ADS1220_SPI_Write(hads, ADS1220_CMD_START);
    status |= ADS1220_SPI_Write(hads, 0x00U);
    ADS1220_CS_High(hads);

    return status;
}

HAL_StatusTypeDef ADS1220_PowerDown(ADS1220_Handle_t *hads)
{
    ADS1220_CS_Low(hads);
    HAL_StatusTypeDef status = ADS1220_SPI_Write(hads, ADS1220_CMD_POWERDOWN);
    ADS1220_CS_High(hads);
    return status;
}

/*==============================================================================
 * === 数据读取 ===
 *============================================================================*/

static uint32_t ADS1220_GetConversionTimeMs(ADS1220_Handle_t *hads)
{
    static const uint32_t conv_ms[] = {55, 30, 18, 12, 8, 5, 4, 3};
    uint8_t dr = (hads->config[ADS1220_REG_CONFIG1] >> 5) & 0x07U;
    if (dr > 7U) dr = 0;
    return conv_ms[dr];
}

static int32_t ADS1220_ParseRaw24(const uint8_t *data)
{
    int32_t raw = ((int32_t)data[0] << 16) |
                  ((int32_t)data[1] << 8)  |
                  ((int32_t)data[2]);
    if (raw & 0x800000UL)
        raw |= 0xFF000000UL;
    return raw;
}

uint8_t ADS1220_IsDataReady(ADS1220_Handle_t *hads)
{
    if (HAL_GPIO_ReadPin(hads->drdy_port, hads->drdy_pin) == GPIO_PIN_RESET)
        return 1;
    return 0;
}

/*==============================================================================
 * === DRDY 波形观测（诊断用） ===
 *============================================================================*/

/* 使能并验证 DWT 周期计数器。它由 Cortex-M3 内核自带，不占用定时器资源。
 * 返回 1 表示可用（计数器确实在递增），0 表示不可用。 */
static uint8_t ADS1220_DwtEnable(void)
{
    uint32_t before;
    volatile uint32_t i;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* 必须验证：某些配置下 CYCCNT 不会递增，
     * 若不检查会让后面所有时间量都变成 0，反而误导判断。 */
    before = DWT->CYCCNT;
    for (i = 0U; i < 200U; i++)
    {
        __NOP();
    }

    return (DWT->CYCCNT != before) ? 1U : 0U;
}

HAL_StatusTypeDef ADS1220_ObserveDrdy(ADS1220_Handle_t *hads,
                                      uint32_t duration_ms,
                                      ADS1220_DrdyStats_t *stats)
{
    uint32_t cycles_per_us;
    uint32_t limit_cycles;
    uint32_t t0;
    uint32_t t_end;
    uint32_t now;
    uint32_t low_len;
    uint32_t low_start = 0U;
    uint32_t low_max = 0U;
    uint32_t low_pending = 0U;
    uint32_t samples = 0U;
    uint32_t high_cnt = 0U;
    uint32_t low_cnt = 0U;
    uint32_t edges = 0U;
    uint8_t  prev;
    uint8_t  level;

    if ((hads == NULL) || (stats == NULL) || (duration_ms == 0U))
        return HAL_ERROR;

    /* 先全部清零，保证任何提前返回都不会留下脏数据 */
    stats->duration_us    = 0U;
    stats->samples        = 0U;
    stats->falling_edges  = 0U;
    stats->high_samples   = 0U;
    stats->low_samples    = 0U;
    stats->low_us_max     = 0U;
    stats->low_us_pending = 0U;
    stats->level_at_end   = 0U;

    if (ADS1220_DwtEnable() == 0U)
        return HAL_ERROR;

    cycles_per_us = SystemCoreClock / 1000000U;
    if (cycles_per_us == 0U)
        return HAL_ERROR;

    limit_cycles = cycles_per_us * 1000U * duration_ms;

    /* 1 = DRDY 为低（转换结果就绪） */
    prev = ADS1220_IsDataReady(hads);
    if (prev != 0U)
        low_start = DWT->CYCCNT;

    t0 = DWT->CYCCNT;

    for (;;)
    {
        level = ADS1220_IsDataReady(hads);
        now   = DWT->CYCCNT;

        samples++;

        if (level != 0U)
        {
            low_cnt++;
            if (prev == 0U)
                low_start = now;            /* 下降沿：进入低电平 */
        }
        else
        {
            high_cnt++;
            if (prev != 0U)
            {
                /* 上升沿：结算一段完整的低电平 */
                low_len = now - low_start;
                if (low_len > low_max)
                    low_max = low_len;
            }
        }

        if ((prev == 0U) && (level != 0U))
            edges++;

        prev = level;

        if ((now - t0) >= limit_cycles)
            break;
    }

    t_end = DWT->CYCCNT;

    /* 窗口结束时若仍处于低电平，这段还没"结算"，单独记录 */
    if (prev != 0U)
    {
        stats->level_at_end = 1U;
        low_pending = t_end - low_start;
    }

    stats->duration_us    = (t_end - t0) / cycles_per_us;
    stats->samples        = samples;
    stats->falling_edges  = edges;
    stats->high_samples   = high_cnt;
    stats->low_samples    = low_cnt;
    stats->low_us_max     = low_max / cycles_per_us;
    stats->low_us_pending = low_pending / cycles_per_us;

    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_WaitForDataReady(ADS1220_Handle_t *hads, uint32_t timeout)
{
    uint32_t tickstart = HAL_GetTick();
    while (ADS1220_IsDataReady(hads) == 0)
    {
        if ((HAL_GetTick() - tickstart) >= timeout)
            return HAL_TIMEOUT;
    }
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_WaitForConversion(ADS1220_Handle_t *hads, uint32_t timeout)
{
    if (ADS1220_WaitForDataReady(hads, timeout) == HAL_OK)
        return HAL_OK;
    HAL_Delay(ADS1220_GetConversionTimeMs(hads));
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_ReadData(ADS1220_Handle_t *hads, int32_t *pRaw)
{
    uint8_t tx_buf[4] = {ADS1220_CMD_RDATA, 0xFFU, 0xFFU, 0xFFU};
    uint8_t rx_buf[4];

    ADS1220_CS_Low(hads);
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(hads->hspi, tx_buf, rx_buf, 4,
                                                        ADS1220_SPI_TIMEOUT);
    ADS1220_CS_High(hads);

    if (status != HAL_OK)
        return status;

    *pRaw = ADS1220_ParseRaw24(&rx_buf[1]);
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_ReadDataContinuous(ADS1220_Handle_t *hads, int32_t *pRaw)
{
    uint8_t tx_buf[3] = {0xFFU, 0xFFU, 0xFFU};
    uint8_t rx_buf[3];

    ADS1220_CS_Low(hads);
    HAL_StatusTypeDef status = HAL_SPI_TransmitReceive(hads->hspi, tx_buf, rx_buf, 3,
                                                        ADS1220_SPI_TIMEOUT);
    ADS1220_CS_High(hads);

    if (status != HAL_OK)
        return status;

    *pRaw = ADS1220_ParseRaw24(rx_buf);
    return HAL_OK;
}

float ADS1220_RawToVoltage(int32_t raw, float vref_mv, uint8_t gain)
{
    float lsb = vref_mv / ((float)gain * 8388608.0f);
    return (float)raw * lsb;
}

HAL_StatusTypeDef ADS1220_ReadTemperature(ADS1220_Handle_t *hads, float *temperature)
{
    int32_t raw;
    HAL_StatusTypeDef status;

    status = ADS1220_WaitForDataReady(hads, ADS1220_DRDY_TIMEOUT);
    if (status != HAL_OK) return status;

    status = ADS1220_ReadData(hads, &raw);
    if (status != HAL_OK) return status;

    int32_t code14 = raw >> 10;
    *temperature = ((float)code14 - 16384.0f) / 125.0f;
    return HAL_OK;
}

/*==============================================================================
 * === 便捷配置函数 ===
 *============================================================================*/

HAL_StatusTypeDef ADS1220_Config(ADS1220_Handle_t *hads,
                                  ADS1220_Channel_t ch,
                                  ADS1220_Gain_t gain,
                                  ADS1220_DataRate_t rate,
                                  ADS1220_Mode_t mode,
                                  ADS1220_ConvMode_t conv,
                                  ADS1220_VRef_t vref)
{
    if (ADS1220_SetChannel(hads, ch) != HAL_OK)       return HAL_ERROR;
    if (ADS1220_SetGain(hads, gain) != HAL_OK)         return HAL_ERROR;
    if (ADS1220_SetDataRate(hads, rate) != HAL_OK)     return HAL_ERROR;
    if (ADS1220_SetMode(hads, mode) != HAL_OK)         return HAL_ERROR;
    if (ADS1220_SetConvMode(hads, conv) != HAL_OK)     return HAL_ERROR;
    if (ADS1220_SetVRef(hads, vref) != HAL_OK)         return HAL_ERROR;
    return HAL_OK;
}

/* 获取当前 PGA 增益值（用于计算） */
uint8_t ADS1220_GetGainValue(ADS1220_Handle_t *hads)
{
    return gain_actual[hads->gain];
}
