#ifndef __RS485_H__
#define __RS485_H__

#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*RS485_ReceiveCallback_t)(void *context, uint8_t byte);
typedef void (*RS485_ErrorCallback_t)(void *context);

typedef struct
{
    UART_HandleTypeDef *huart;
    GPIO_TypeDef *direction_port;
    uint16_t direction_pin;
    GPIO_PinState transmit_level;
    uint32_t transmit_timeout_ms;
    uint8_t rx_byte;
    volatile uint8_t transmitting;
    RS485_ReceiveCallback_t on_receive;
    RS485_ErrorCallback_t on_error;
    void *context;
} RS485_Handle_t;

/* The USART must use separate TX/RX pins, with eight payload bits.
 * DE and /RE may share direction_pin: transmit_level selects transmit. */
HAL_StatusTypeDef RS485_Init(RS485_Handle_t *handle,
                             UART_HandleTypeDef *huart,
                             GPIO_TypeDef *direction_port,
                             uint16_t direction_pin,
                             GPIO_PinState transmit_level,
                             uint32_t transmit_timeout_ms);
HAL_StatusTypeDef RS485_StartReceive(RS485_Handle_t *handle,
                                     RS485_ReceiveCallback_t on_receive,
                                     RS485_ErrorCallback_t on_error,
                                     void *context);

/* Main-loop only. HAL_UART_Transmit waits for TC, including the last stop bit.
 * Receive direction is restored on success and on every transmit error. */
HAL_StatusTypeDef RS485_Send(RS485_Handle_t *handle,
                             const uint8_t *data, uint16_t length);
void RS485_Poll(RS485_Handle_t *handle);

/* Forward the application's HAL UART callbacks to these functions. */
void RS485_OnRxComplete(RS485_Handle_t *handle, UART_HandleTypeDef *huart);
void RS485_OnError(RS485_Handle_t *handle, UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif
#endif
