#ifndef __APP_MODBUS_H__
#define __APP_MODBUS_H__

#include "app_measure.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MODBUS_REGISTER_COUNT  14U

/* 32-bit values: high register first; register bytes: high byte first.
 * Voltage is IEEE-754 float32 in mV. Addresses are zero-based on wire. */
#define APP_MODBUS_REG_RAW          0x0000U
#define APP_MODBUS_REG_RAW_MV       0x0002U
#define APP_MODBUS_REG_FILTERED     0x0004U
#define APP_MODBUS_REG_FILTERED_MV  0x0006U
#define APP_MODBUS_REG_STATUS       0x0008U
#define APP_MODBUS_REG_GAIN         0x0009U
#define APP_MODBUS_REG_SAMPLES      0x000AU
#define APP_MODBUS_REG_AGE_MS       0x000CU

#define APP_MODBUS_STATUS_VALID       0x0001U
#define APP_MODBUS_STATUS_TARING      0x0002U
#define APP_MODBUS_STATUS_ZERO_VALID  0x0004U
#define APP_MODBUS_STATUS_VOFA        0x0008U
#define APP_MODBUS_STATUS_STALE       0x0010U

HAL_StatusTypeDef App_ModBus_Init(App_Measure_t *measure);
void App_ModBus_Poll(void);
void App_ModBus_OnRxComplete(UART_HandleTypeDef *huart);
void App_ModBus_OnError(UART_HandleTypeDef *huart);
void App_ModBus_TimerIRQ(void);

#ifdef __cplusplus
}
#endif
#endif
