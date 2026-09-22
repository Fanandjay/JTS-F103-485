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

/* MUX 位值表 - 按枚举索引 */
static const uint8_t mux_values[] = {
    ADS1220_MUX_AIN0_AIN1,   /* CH0 */
    ADS1220_MUX_AIN0_AIN2,   /* CH1 */
    ADS1220_MUX_AIN0_AIN3,   /* CH2 */
    ADS1220_MUX_AIN1_AIN2,   /* CH3 */
    ADS1220_MUX_AIN1_AIN3,   /* CH4 */
    ADS1220_MUX_AIN2_AIN3,   /* CH5 */
    ADS1220_MUX_AIN1_AIN0,   /* CH6 */
    ADS1220_MUX_AIN3_AIN2,   /* CH7 */
};

/* GAIN 位值表 */
static const uint8_t gain_values[] = {
    ADS1220_GAIN_VAL_1,    /* G1 */
    ADS1220_GAIN_VAL_2,    /* G2 */
    ADS1220_GAIN_VAL_4,    /* G4 */
    ADS1220_GAIN_VAL_8,    /* G8 */
    ADS1220_GAIN_VAL_16,   /* G16 */
    ADS1220_GAIN_VAL_32,   /* G32 */
    ADS1220_GAIN_VAL_64,   /* G64 */
    ADS1220_GAIN_VAL_128,  /* G128 */
};

/* DR 位值表 */
static const uint8_t dr_values[] = {
    ADS1220_DR_VAL_20SPS,    /* DR20 */
    ADS1220_DR_VAL_45SPS,    /* DR45 */
    ADS1220_DR_VAL_90SPS,    /* DR90 */
    ADS1220_DR_VAL_175SPS,   /* DR175 */
    ADS1220_DR_VAL_330SPS,   /* DR330 */
    ADS1220_DR_VAL_600SPS,   /* DR600 */
    ADS1220_DR_VAL_1000SPS,  /* DR1000 */
    ADS1220_DR_VAL_2000SPS,  /* DR2000 */
};

/* 实际增益值表 - 用于计算 */
static const uint8_t gain_actual[] = {1, 2, 4, 8, 16, 32, 64, 128};

HAL_StatusTypeDef ADS1220_SetChannel(ADS1220_Handle_t *hads, ADS1220_Channel_t channel)
{
    uint8_t cfg0 = hads->config[ADS1220_REG_CONFIG0];
    cfg0 &= ~ADS1220_MUX_MASK;
    cfg0 |= mux_values[channel];

    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG0, cfg0) != HAL_OK)
        return HAL_ERROR;

    hads->channel = channel;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_SetGain(ADS1220_Handle_t *hads, ADS1220_Gain_t gain)
{
    uint8_t cfg0 = hads->config[ADS1220_REG_CONFIG0];
    cfg0 &= ~ADS1220_GAIN_MASK;
    cfg0 |= gain_values[gain];

    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG0, cfg0) != HAL_OK)
        return HAL_ERROR;

    hads->gain = gain;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_SetDataRate(ADS1220_Handle_t *hads, ADS1220_DataRate_t rate)
{
    uint8_t cfg1 = hads->config[ADS1220_REG_CONFIG1];
    cfg1 &= ~ADS1220_DR_MASK;
    cfg1 |= dr_values[rate];

    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG1, cfg1) != HAL_OK)
        return HAL_ERROR;

    hads->data_rate = rate;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_SetMode(ADS1220_Handle_t *hads, ADS1220_Mode_t mode)
{
    uint8_t cfg1 = hads->config[ADS1220_REG_CONFIG1];
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
    uint8_t cfg1 = hads->config[ADS1220_REG_CONFIG1];
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
    uint8_t cfg2 = hads->config[ADS1220_REG_CONFIG2];
    cfg2 &= ~ADS1220_VREF_MASK;
    cfg2 |= ((uint8_t)vref << 6);

    if (ADS1220_WriteReg(hads, ADS1220_REG_CONFIG2, cfg2) != HAL_OK)
        return HAL_ERROR;

    hads->vref = vref;
    return HAL_OK;
}

HAL_StatusTypeDef ADS1220_SetIDACCurrent(ADS1220_Handle_t *hads, ADS1220_IDAC_Current_t current)
{
    uint8_t cfg2 = hads->config[ADS1220_REG_CONFIG2];
    cfg2 &= ~ADS1220_IDAC_MASK;
    cfg2 |= (uint8_t)current;
    return ADS1220_WriteReg(hads, ADS1220_REG_CONFIG2, cfg2);
}

HAL_StatusTypeDef ADS1220_SetIDAC1Channel(ADS1220_Handle_t *hads, uint8_t ch)
{
    uint8_t cfg3 = hads->config[ADS1220_REG_CONFIG3];
    cfg3 &= ~ADS1220_IDAC1_MASK;
    cfg3 |= ((ch & 0x07U) << 5);
    return ADS1220_WriteReg(hads, ADS1220_REG_CONFIG3, cfg3);
}

HAL_StatusTypeDef ADS1220_SetIDAC2Channel(ADS1220_Handle_t *hads, uint8_t ch)
{
    uint8_t cfg3 = hads->config[ADS1220_REG_CONFIG3];
    cfg3 &= ~ADS1220_IDAC2_MASK;
    cfg3 |= ((ch & 0x07U) << 2);
    return ADS1220_WriteReg(hads, ADS1220_REG_CONFIG3, cfg3);
}

HAL_StatusTypeDef ADS1220_EnableTempSensor(ADS1220_Handle_t *hads, uint8_t enable)
{
    uint8_t cfg1 = hads->config[ADS1220_REG_CONFIG1];
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
