/**
  ******************************************************************************
  * @file    VOFA.c
  * @brief   VOFA+ JustFloat protocol driver.
  ******************************************************************************
  */

#include "VOFA.h"
#include <string.h>

#define VOFA_JUSTFLOAT_FRAME_TAIL_SIZE 4U
#define VOFA_JUSTFLOAT_FRAME_SIZE \
    (VOFA_JUSTFLOAT_MAX_CHANNELS * (uint16_t)sizeof(float) + \
     VOFA_JUSTFLOAT_FRAME_TAIL_SIZE)

/* JustFloat frame tail: 0x7F800000 transmitted as little-endian bytes. */
static const uint8_t vofa_justfloat_frame_tail[VOFA_JUSTFLOAT_FRAME_TAIL_SIZE] =
{
    0x00U, 0x00U, 0x80U, 0x7FU
};

/* One complete frame buffer allows all channels and the tail to be sent
 * atomically through one HAL_UART_Transmit call. */
static uint8_t vofa_justfloat_frame[VOFA_JUSTFLOAT_FRAME_SIZE];

HAL_StatusTypeDef VOFA_JustFloat_Send(UART_HandleTypeDef *huart,
                                      const float *data,
                                      uint8_t channel_count)
{
    uint16_t payload_size;
    uint16_t frame_size;

    if ((huart == 0) || (data == 0) || (channel_count == 0U) ||
        (channel_count > VOFA_JUSTFLOAT_MAX_CHANNELS))
    {
        return HAL_ERROR;
    }

    payload_size = (uint16_t)channel_count * (uint16_t)sizeof(float);
    frame_size = payload_size + VOFA_JUSTFLOAT_FRAME_TAIL_SIZE;

    memcpy(vofa_justfloat_frame, data, payload_size);
    memcpy(&vofa_justfloat_frame[payload_size],
           vofa_justfloat_frame_tail,
           VOFA_JUSTFLOAT_FRAME_TAIL_SIZE);

    return HAL_UART_Transmit(huart,
                             vofa_justfloat_frame,
                             frame_size,
                             HAL_MAX_DELAY);
}

HAL_StatusTypeDef VOFA_JustFloat_Send1(UART_HandleTypeDef *huart,
                                       float value)
{
    return VOFA_JustFloat_Send(huart, &value, 1U);
}

HAL_StatusTypeDef VOFA_JustFloat_Send2(UART_HandleTypeDef *huart,
                                       float value0,
                                       float value1)
{
    float values[2];

    values[0] = value0;
    values[1] = value1;

    return VOFA_JustFloat_Send(huart, values, 2U);
}
