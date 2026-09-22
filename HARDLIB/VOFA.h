/**
  ******************************************************************************
  * @file    VOFA.h
  * @brief   VOFA+ JustFloat protocol driver.
  ******************************************************************************
  */

#ifndef __VOFA_H__
#define __VOFA_H__

#include "stm32f1xx_hal.h"

/* Maximum number of float channels in one JustFloat frame. */
#define VOFA_JUSTFLOAT_MAX_CHANNELS 16U

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Send one or more float channels using the VOFA+ JustFloat format.
 *
 * The payload is a sequence of IEEE-754 single-precision floats followed by
 * the JustFloat frame tail 00 00 80 7F. STM32F1 uses little-endian byte order,
 * which matches the byte order expected by VOFA+ JustFloat.
 *
 * @param huart         UART handle used for transmission.
 * @param data          Array containing the channel values.
 * @param channel_count Number of float channels in data. The value must not
 *                      exceed VOFA_JUSTFLOAT_MAX_CHANNELS.
 * @return HAL_OK on success, otherwise the HAL UART status.
 */
HAL_StatusTypeDef VOFA_JustFloat_Send(UART_HandleTypeDef *huart,
                                      const float *data,
                                      uint8_t channel_count);

/**
 * @brief Send one float channel using VOFA+ JustFloat.
 * @param huart UART handle used for transmission.
 * @param value Channel value to send.
 * @return HAL_OK on success, otherwise the HAL UART status.
 */
HAL_StatusTypeDef VOFA_JustFloat_Send1(UART_HandleTypeDef *huart,
                                       float value);

/**
 * @brief Send two float channels using VOFA+ JustFloat.
 * @param huart UART handle used for transmission.
 * @param value0 First channel value.
 * @param value1 Second channel value.
 * @return HAL_OK on success, otherwise the HAL UART status.
 */
HAL_StatusTypeDef VOFA_JustFloat_Send2(UART_HandleTypeDef *huart,
                                       float value0,
                                       float value1);

#ifdef __cplusplus
}
#endif

#endif /* __VOFA_H__ */
