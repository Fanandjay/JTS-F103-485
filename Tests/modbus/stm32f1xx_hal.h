/* Host-test HAL substitute. Not part of the firmware include path. */
#ifndef TEST_STM32F1XX_HAL_H
#define TEST_STM32F1XX_HAL_H
#include <stdint.h>
#include <stddef.h>

typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { uint32_t output; } GPIO_TypeDef;
typedef struct { uint32_t unused; } SPI_HandleTypeDef;
typedef struct
{
    uint32_t BaudRate, WordLength, StopBits, Parity;
} UART_InitTypeDef;
typedef struct
{
    UART_InitTypeDef Init;
    uint32_t gState, RxState;
    uint8_t *rx_target;
} UART_HandleTypeDef;
typedef struct
{
    uint32_t Prescaler, Period;
} TIM_Base_InitTypeDef;
typedef struct
{
    TIM_Base_InitTypeDef Init;
    uint32_t counter, autoreload, flags, interrupts, enabled;
} TIM_HandleTypeDef;

extern GPIO_TypeDef test_gpio_a, test_gpio_b;
#define GPIOA (&test_gpio_a)
#define GPIOB (&test_gpio_b)
#define GPIO_PIN_1 (1U << 1)
#define GPIO_PIN_8 (1U << 8)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_12 (1U << 12)
#define UART_WORDLENGTH_8B 8U
#define UART_WORDLENGTH_9B 9U
#define UART_PARITY_NONE 0U
#define UART_PARITY_EVEN 1U
#define UART_STOPBITS_1 1U
#define UART_STOPBITS_2 2U
#define HAL_UART_STATE_READY 0U
#define HAL_UART_STATE_BUSY_RX 1U
#define RESET 0U
#define TIM_FLAG_UPDATE 1U
#define TIM_IT_UPDATE 1U

#define __HAL_TIM_DISABLE_IT(h, f) ((h)->interrupts &= ~(f))
#define __HAL_TIM_ENABLE_IT(h, f) ((h)->interrupts |= (f))
#define __HAL_TIM_DISABLE(h) ((h)->enabled = 0U)
#define __HAL_TIM_ENABLE(h) ((h)->enabled = 1U)
#define __HAL_TIM_CLEAR_FLAG(h, f) ((h)->flags &= ~(f))
#define __HAL_TIM_GET_FLAG(h, f) ((h)->flags & (f))
#define __HAL_TIM_GET_IT_SOURCE(h, f) ((h)->interrupts & (f))
#define __HAL_TIM_GET_COUNTER(h) ((h)->counter)
#define __HAL_TIM_SET_COUNTER(h, v) ((h)->counter = (v))
#define __HAL_TIM_SET_AUTORELOAD(h, v) ((h)->autoreload = (v))

uint32_t __get_PRIMASK(void);
uint32_t __get_IPSR(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t value);
void __DMB(void);
uint32_t HAL_GetTick(void);
void HAL_GPIO_WritePin(GPIO_TypeDef *, uint16_t, GPIO_PinState);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *, const uint8_t *,
                                   uint16_t, uint32_t);
void HAL_GPIO_EXTI_Callback(uint16_t pin);
#endif
