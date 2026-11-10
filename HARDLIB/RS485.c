#include "RS485.h"
#include <string.h>

static void RS485_SetDirection(RS485_Handle_t *handle, uint8_t transmit)
{
    GPIO_PinState level = handle->transmit_level;

    if (transmit == 0U)
        level = (level == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET;

    HAL_GPIO_WritePin(handle->direction_port, handle->direction_pin, level);
}

HAL_StatusTypeDef RS485_Init(RS485_Handle_t *handle,
                             UART_HandleTypeDef *huart,
                             GPIO_TypeDef *direction_port,
                             uint16_t direction_pin,
                             GPIO_PinState transmit_level,
                             uint32_t transmit_timeout_ms)
{
    if (handle == NULL || huart == NULL || direction_port == NULL ||
        direction_pin == 0U || transmit_timeout_ms == 0U ||
        (transmit_level != GPIO_PIN_SET && transmit_level != GPIO_PIN_RESET))
        return HAL_ERROR;

    /* F1 WordLength includes parity. Reject seven/nine-bit payload modes. */
    if (!((huart->Init.WordLength == UART_WORDLENGTH_8B &&
           huart->Init.Parity == UART_PARITY_NONE) ||
          (huart->Init.WordLength == UART_WORDLENGTH_9B &&
           huart->Init.Parity != UART_PARITY_NONE)))
        return HAL_ERROR;

    memset(handle, 0, sizeof(*handle));
    handle->huart = huart;
    handle->direction_port = direction_port;
    handle->direction_pin = direction_pin;
    handle->transmit_level = transmit_level;
    handle->transmit_timeout_ms = transmit_timeout_ms;
    RS485_SetDirection(handle, 0U);
    return HAL_OK;
}

HAL_StatusTypeDef RS485_StartReceive(RS485_Handle_t *handle,
                                     RS485_ReceiveCallback_t on_receive,
                                     RS485_ErrorCallback_t on_error,
                                     void *context)
{
    if (handle == NULL || handle->huart == NULL || on_receive == NULL)
        return HAL_ERROR;

    handle->on_receive = on_receive;
    handle->on_error = on_error;
    handle->context = context;
    return HAL_UART_Receive_IT(handle->huart, &handle->rx_byte, 1U);
}

HAL_StatusTypeDef RS485_Send(RS485_Handle_t *handle,
                             const uint8_t *data, uint16_t length)
{
    HAL_StatusTypeDef status;
    uint32_t primask;

    if (handle == NULL || handle->huart == NULL || data == NULL || length == 0U)
        return HAL_ERROR;

    /* Blocking HAL timeouts need SysTick; never send from an ISR. */
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U)
        return HAL_ERROR;

    primask = __get_PRIMASK();
    __disable_irq();
    if (handle->transmitting != 0U ||
        handle->huart->gState != HAL_UART_STATE_READY)
    {
        __set_PRIMASK(primask);
        return HAL_BUSY;
    }
    handle->transmitting = 1U;
    RS485_SetDirection(handle, 1U);
    __set_PRIMASK(primask);

    status = HAL_UART_Transmit(handle->huart, data, length,
                               handle->transmit_timeout_ms);

    primask = __get_PRIMASK();
    __disable_irq();
    RS485_SetDirection(handle, 0U);
    handle->transmitting = 0U;
    __set_PRIMASK(primask);
    return status;
}

void RS485_Poll(RS485_Handle_t *handle)
{
    if (handle != NULL && handle->huart != NULL &&
        handle->on_receive != NULL && handle->transmitting == 0U &&
        handle->huart->RxState == HAL_UART_STATE_READY)
        (void)HAL_UART_Receive_IT(handle->huart, &handle->rx_byte, 1U);
}

void RS485_OnRxComplete(RS485_Handle_t *handle, UART_HandleTypeDef *huart)
{
    if (handle == NULL || handle->huart == NULL || huart != handle->huart)
        return;

    if (handle->transmitting == 0U && handle->on_receive != NULL)
        handle->on_receive(handle->context, handle->rx_byte);

    if (HAL_UART_Receive_IT(huart, &handle->rx_byte, 1U) != HAL_OK &&
        handle->on_error != NULL)
        handle->on_error(handle->context);
}

void RS485_OnError(RS485_Handle_t *handle, UART_HandleTypeDef *huart)
{
    if (handle == NULL || handle->huart == NULL || huart != handle->huart)
        return;

    if (handle->transmitting == 0U && handle->on_error != NULL)
        handle->on_error(handle->context);

    /* HAL ends reception on ORE. PE/FE/NE may leave reception running. */
    if (huart->RxState == HAL_UART_STATE_READY)
        (void)HAL_UART_Receive_IT(huart, &handle->rx_byte, 1U);
}
