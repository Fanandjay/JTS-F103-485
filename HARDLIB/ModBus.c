#include "ModBus.h"
#include <string.h>

static void ModBus_StopTimer(ModBus_Handle_t *handle)
{
    __HAL_TIM_DISABLE_IT(handle->htim, TIM_IT_UPDATE);
    __HAL_TIM_DISABLE(handle->htim);
    __HAL_TIM_CLEAR_FLAG(handle->htim, TIM_FLAG_UPDATE);
}

static void ModBus_RestartTimer(ModBus_Handle_t *handle)
{
    ModBus_StopTimer(handle);
    __HAL_TIM_SET_COUNTER(handle->htim, 0U);
    __HAL_TIM_ENABLE_IT(handle->htim, TIM_IT_UPDATE);
    __HAL_TIM_ENABLE(handle->htim);
}

/* Called with interrupts masked. Keep the pending buffer owned by the main
 * loop until its response has completed; new frames cannot overwrite it. */
static void ModBus_EndFrame(ModBus_Handle_t *handle)
{
    ModBus_StopTimer(handle);
    if (handle->receiving != 0U)
    {
        if (handle->discard == 0U && handle->rx_length >= 4U &&
            handle->frame_ready == 0U)
        {
            memcpy(handle->pending_buffer, handle->rx_buffer, handle->rx_length);
            handle->pending_length = handle->rx_length;
            __DMB();
            handle->frame_ready = 1U;
            handle->received_frames++;
        }
        else if (handle->rx_length != 0U || handle->discard != 0U)
        {
            handle->dropped_frames++;
        }
    }
    handle->rx_length = 0U;
    handle->receiving = 0U;
    handle->discard = 0U;
}

static void ModBus_OnByte(void *context, uint8_t byte)
{
    ModBus_Handle_t *handle = (ModBus_Handle_t *)context;
    uint32_t elapsed;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    elapsed = __HAL_TIM_GET_COUNTER(handle->htim);

    /* A timer update may be pending at the same time as RXNE. Finish the
     * old frame before storing this byte, whichever IRQ ran first. */
    if (__HAL_TIM_GET_FLAG(handle->htim, TIM_FLAG_UPDATE) != RESET)
        ModBus_EndFrame(handle);

    if (handle->receiving != 0U && elapsed > handle->byte_timeout_us)
        handle->discard = 1U;

    handle->receiving = 1U;
    if (handle->rx_length < MODBUS_RTU_MAX_ADU)
        handle->rx_buffer[handle->rx_length++] = byte;
    else
        handle->discard = 1U;

    ModBus_RestartTimer(handle);
    __set_PRIMASK(primask);
}

static void ModBus_OnError(void *context)
{
    ModBus_Handle_t *handle = (ModBus_Handle_t *)context;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    handle->discard = 1U;
    handle->receiving = 1U;
    ModBus_RestartTimer(handle);
    __set_PRIMASK(primask);
}

HAL_StatusTypeDef ModBus_Init(ModBus_Handle_t *handle, RS485_Handle_t *rs485,
                              TIM_HandleTypeDef *htim, uint8_t slave_address,
                              ModBus_ReadRegisters_t read_registers,
                              void *context)
{
    uint32_t bits;
    uint32_t baud;
    uint32_t char_us;
    uint32_t t15_us;

    if (handle == NULL || rs485 == NULL || rs485->huart == NULL ||
        htim == NULL || slave_address == 0U || slave_address > 247U ||
        read_registers == NULL)
        return HAL_ERROR;

    baud = rs485->huart->Init.BaudRate;
    if (baud == 0U ||
        (rs485->huart->Init.StopBits != UART_STOPBITS_1 &&
         rs485->huart->Init.StopBits != UART_STOPBITS_2))
        return HAL_ERROR;

    memset(handle, 0, sizeof(*handle));
    handle->rs485 = rs485;
    handle->htim = htim;
    handle->slave_address = slave_address;
    handle->read_registers = read_registers;
    handle->context = context;

    bits = 1U + ((rs485->huart->Init.WordLength == UART_WORDLENGTH_9B) ? 9U : 8U);
    bits += (rs485->huart->Init.StopBits == UART_STOPBITS_2) ? 2U : 1U;
    char_us = (bits * 1000000U + baud - 1U) / baud;
    if (baud > 19200U)
    {
        t15_us = 750U;
        handle->frame_timeout_us = 1750U;
    }
    else
    {
        t15_us = (15U * bits * 100000U + baud - 1U) / baud;
        handle->frame_timeout_us = (35U * bits * 100000U + baud - 1U) / baud;
    }
    /* RXNE timestamps are at character END. Subtract one character from
     * successive RXNE intervals when checking the silent t1.5 gap. */
    handle->byte_timeout_us = t15_us + char_us;
    if (handle->frame_timeout_us > 65536U)
        return HAL_ERROR;

    ModBus_StopTimer(handle);
    __HAL_TIM_SET_AUTORELOAD(htim, handle->frame_timeout_us - 1U);
    handle->initialized = 1U;

    /* Require a clean idle interval after startup or a partial request. */
    handle->discard = 1U;
    handle->receiving = 1U;
    ModBus_RestartTimer(handle);
    if (RS485_StartReceive(rs485, ModBus_OnByte, ModBus_OnError, handle) != HAL_OK)
    {
        ModBus_StopTimer(handle);
        handle->initialized = 0U;
        return HAL_ERROR;
    }
    return HAL_OK;
}

void ModBus_TimerIRQ(ModBus_Handle_t *handle)
{
    uint32_t primask;

    if (handle == NULL || handle->initialized == 0U)
        return;

    primask = __get_PRIMASK();
    __disable_irq();
    if (__HAL_TIM_GET_FLAG(handle->htim, TIM_FLAG_UPDATE) != RESET &&
        __HAL_TIM_GET_IT_SOURCE(handle->htim, TIM_IT_UPDATE) != RESET)
        ModBus_EndFrame(handle);
    __set_PRIMASK(primask);
}

uint16_t ModBus_CRC16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t i;
    uint8_t bit;

    if (data == NULL)
        return crc;

    for (i = 0U; i < length; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
            crc = ((crc & 1U) != 0U) ? (uint16_t)((crc >> 1) ^ 0xA001U) :
                                       (uint16_t)(crc >> 1);
    }
    return crc;
}

static void ModBus_Reply(ModBus_Handle_t *handle, uint16_t length)
{
    uint16_t crc = ModBus_CRC16(handle->tx_buffer, length);
    handle->tx_buffer[length++] = (uint8_t)crc;
    handle->tx_buffer[length++] = (uint8_t)(crc >> 8);
    if (RS485_Send(handle->rs485, handle->tx_buffer, length) != HAL_OK)
        handle->transmit_errors++;
}

static void ModBus_ProcessFrame(ModBus_Handle_t *handle)
{
    const uint8_t *request = handle->pending_buffer;
    uint16_t length = handle->pending_length;
    uint16_t crc;
    uint16_t start;
    uint16_t count;
    uint16_t i;
    uint8_t exception = MODBUS_EXCEPTION_NONE;
    uint8_t function = request[1];

    if (request[0] != handle->slave_address)
        return; /* Includes broadcast address 0: no read response or writes. */

    crc = (uint16_t)(request[length - 2U] | ((uint16_t)request[length - 1U] << 8));
    if (ModBus_CRC16(request, length - 2U) != crc)
    {
        handle->crc_errors++;
        return;
    }

    if (function != MODBUS_FC_READ_HOLDING && function != MODBUS_FC_READ_INPUT)
        exception = MODBUS_EXCEPTION_FUNCTION;
    else if (length != 8U)
        exception = MODBUS_EXCEPTION_VALUE;
    else
    {
        start = (uint16_t)(((uint16_t)request[2] << 8) | request[3]);
        count = (uint16_t)(((uint16_t)request[4] << 8) | request[5]);
        if (count == 0U || count > MODBUS_RTU_MAX_READ_REGISTERS)
            exception = MODBUS_EXCEPTION_VALUE;
        else if ((uint32_t)start + count > 65536UL)
            exception = MODBUS_EXCEPTION_ADDRESS;
        else
            exception = handle->read_registers(handle->context, function, start,
                                                count, handle->registers);
    }

    handle->tx_buffer[0] = handle->slave_address;
    if (exception != MODBUS_EXCEPTION_NONE)
    {
        handle->tx_buffer[1] = function | 0x80U;
        handle->tx_buffer[2] = exception;
        ModBus_Reply(handle, 3U);
        return;
    }

    handle->tx_buffer[1] = function;
    handle->tx_buffer[2] = (uint8_t)(count * 2U);
    for (i = 0U; i < count; i++)
    {
        handle->tx_buffer[3U + 2U * i] = (uint8_t)(handle->registers[i] >> 8);
        handle->tx_buffer[4U + 2U * i] = (uint8_t)handle->registers[i];
    }
    ModBus_Reply(handle, (uint16_t)(3U + count * 2U));
}

void ModBus_Poll(ModBus_Handle_t *handle)
{
    uint32_t primask;

    if (handle == NULL || handle->initialized == 0U)
        return;

    RS485_Poll(handle->rs485);
    if (handle->frame_ready == 0U)
        return;

    __DMB();
    ModBus_ProcessFrame(handle);
    primask = __get_PRIMASK();
    __disable_irq();
    handle->frame_ready = 0U;
    __set_PRIMASK(primask);
}

void ModBus_PackUInt32(uint16_t values[2], uint32_t value)
{
    values[0] = (uint16_t)(value >> 16);
    values[1] = (uint16_t)value;
}

void ModBus_PackInt32(uint16_t values[2], int32_t value)
{
    ModBus_PackUInt32(values, (uint32_t)value);
}

void ModBus_PackFloat(uint16_t values[2], float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    ModBus_PackUInt32(values, bits);
}
