#include "app_modbus.h"
#include "app_config.h"
#include "ModBus.h"
#include "tim.h"
#include "usart.h"

static RS485_Handle_t s_rs485;
static ModBus_Handle_t s_modbus;

static uint8_t App_ModBus_ReadRegisters(void *context, uint8_t function,
                                       uint16_t start, uint16_t count,
                                       uint16_t *values)
{
    App_Measure_t *measure = (App_Measure_t *)context;
    App_Measure_Snapshot_t sample;
    uint16_t bank[APP_MODBUS_REGISTER_COUNT] = {0U};
    uint32_t age;
    uint16_t status = 0U;
    uint16_t i;

    (void)function; /* Both 03 and 04 expose the same read-only measurements. */
    if (count == 0U || (uint32_t)start + count > APP_MODBUS_REGISTER_COUNT)
        return MODBUS_EXCEPTION_ADDRESS;

    App_Measure_GetLatest(measure, &sample);
    age = (sample.valid != 0U) ? (HAL_GetTick() - sample.sample_tick) : 0xFFFFFFFFUL;
    if (sample.valid != 0U)
        status |= APP_MODBUS_STATUS_VALID;
    if (sample.valid == 0U || age >= APP_MODBUS_STALE_MS)
        status |= APP_MODBUS_STATUS_STALE;
    if (measure->tare_running != 0U)
        status |= APP_MODBUS_STATUS_TARING;
    if (measure->zero_valid != 0U)
        status |= APP_MODBUS_STATUS_ZERO_VALID;
    if (measure->output_enabled != 0U)
        status |= APP_MODBUS_STATUS_VOFA;

    ModBus_PackInt32(&bank[APP_MODBUS_REG_RAW], sample.raw_code);
    ModBus_PackFloat(&bank[APP_MODBUS_REG_RAW_MV], sample.raw_voltage_mv);
    ModBus_PackInt32(&bank[APP_MODBUS_REG_FILTERED], sample.filtered_code);
    ModBus_PackFloat(&bank[APP_MODBUS_REG_FILTERED_MV], sample.filtered_voltage_mv);
    bank[APP_MODBUS_REG_STATUS] = status;
    bank[APP_MODBUS_REG_GAIN] = ADS1220_GetGainValue(measure->hads);
    ModBus_PackUInt32(&bank[APP_MODBUS_REG_SAMPLES], sample.sample_count);
    ModBus_PackUInt32(&bank[APP_MODBUS_REG_AGE_MS], age);

    for (i = 0U; i < count; i++)
        values[i] = bank[start + i];
    return MODBUS_EXCEPTION_NONE;
}

HAL_StatusTypeDef App_ModBus_Init(App_Measure_t *measure)
{
    if (measure == NULL || measure->hads == NULL)
        return HAL_ERROR;

    if (RS485_Init(&s_rs485, &huart2, RS485_EN_GPIO_Port, RS485_EN_Pin,
                    APP_RS485_TX_LEVEL, APP_RS485_TX_TIMEOUT_MS) != HAL_OK)
        return HAL_ERROR;

    return ModBus_Init(&s_modbus, &s_rs485, &htim2, APP_MODBUS_SLAVE_ADDRESS,
                       App_ModBus_ReadRegisters, measure);
}

void App_ModBus_Poll(void)
{
    ModBus_Poll(&s_modbus);
}

void App_ModBus_OnRxComplete(UART_HandleTypeDef *huart)
{
    RS485_OnRxComplete(&s_rs485, huart);
}

void App_ModBus_OnError(UART_HandleTypeDef *huart)
{
    RS485_OnError(&s_rs485, huart);
}

void App_ModBus_TimerIRQ(void)
{
    ModBus_TimerIRQ(&s_modbus);
}
