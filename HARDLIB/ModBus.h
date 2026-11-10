#ifndef __MODBUS_H__
#define __MODBUS_H__

#include "RS485.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_RTU_MAX_ADU             256U
#define MODBUS_RTU_MAX_READ_REGISTERS  125U
#define MODBUS_FC_READ_HOLDING         0x03U
#define MODBUS_FC_READ_INPUT           0x04U
#define MODBUS_EXCEPTION_NONE          0x00U
#define MODBUS_EXCEPTION_FUNCTION      0x01U
#define MODBUS_EXCEPTION_ADDRESS       0x02U
#define MODBUS_EXCEPTION_VALUE         0x03U
#define MODBUS_EXCEPTION_DEVICE        0x04U

/* Main-loop callback: fill values with host-endian 16-bit registers.
 * Return zero on success or a MODBUS_EXCEPTION_* code on failure. */
typedef uint8_t (*ModBus_ReadRegisters_t)(void *context, uint8_t function,
                                         uint16_t start, uint16_t count,
                                         uint16_t *values);

typedef struct
{
    RS485_Handle_t *rs485;
    TIM_HandleTypeDef *htim;
    ModBus_ReadRegisters_t read_registers;
    void *context;
    uint8_t slave_address;
    uint8_t initialized;
    uint32_t frame_timeout_us;
    uint32_t byte_timeout_us;

    uint8_t rx_buffer[MODBUS_RTU_MAX_ADU];
    uint8_t pending_buffer[MODBUS_RTU_MAX_ADU];
    uint8_t tx_buffer[MODBUS_RTU_MAX_ADU];
    uint16_t registers[MODBUS_RTU_MAX_READ_REGISTERS];
    volatile uint16_t rx_length;
    volatile uint16_t pending_length;
    volatile uint8_t receiving;
    volatile uint8_t discard;
    volatile uint8_t frame_ready;

    volatile uint32_t received_frames;
    volatile uint32_t dropped_frames;
    uint32_t crc_errors;
    uint32_t transmit_errors;
} ModBus_Handle_t;

/* htim is dedicated to RTU, configured for a 1 MHz counter, no preload.
 * Call after UART/GPIO/TIM initialization, from the main loop.
 * Addresses 1..247; functions 03/04 are read-only. Broadcasts stay silent. */
HAL_StatusTypeDef ModBus_Init(ModBus_Handle_t *handle, RS485_Handle_t *rs485,
                              TIM_HandleTypeDef *htim, uint8_t slave_address,
                              ModBus_ReadRegisters_t read_registers,
                              void *context);
void ModBus_Poll(ModBus_Handle_t *handle);

/* Call BEFORE HAL_TIM_IRQHandler from this timer's IRQ USER CODE section.
 * This consumes UIF atomically with RX state, including UART/TIM races. */
void ModBus_TimerIRQ(ModBus_Handle_t *handle);

uint16_t ModBus_CRC16(const uint8_t *data, uint16_t length);
void ModBus_PackInt32(uint16_t values[2], int32_t value);
void ModBus_PackUInt32(uint16_t values[2], uint32_t value);
void ModBus_PackFloat(uint16_t values[2], float value);

#ifdef __cplusplus
}
#endif
#endif
