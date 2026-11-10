/**
 ******************************************************************************
 * @file    app_led.c
 * @brief   板载 LED 调试心跳实现（SysTick 中断驱动，见 app_led.h）。
 ******************************************************************************
 */

#include "app_led.h"
#include "app_config.h"

/* 板载 LED 低电平点亮（高电平熄灭） */
#define APP_LED_ON()    HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET)
#define APP_LED_OFF()   HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET)

/*------------------------------------------------------------------------------
 * 状态（ISR 单写者 / 主循环单写者，均为 32 位对齐访问，Cortex-M3 上原子）
 *----------------------------------------------------------------------------*/
static volatile uint32_t s_kick_tick;  /*!< 主循环最近一次喂狗时刻 (HAL tick) */
static volatile uint8_t  s_kick_seen;  /*!< 主循环是否已开始喂狗（门控开关） */

void App_LED_Init(void)
{
    /* gpio.c 把 PA8 初始化为低（点亮），这里立即熄灭。 */
    APP_LED_OFF();

    s_kick_tick = 0U;
    s_kick_seen = 0U;   /* 自由运行心跳：自检期间不受喂狗门控 */
}

void App_LED_Kick(void)
{
    s_kick_tick = HAL_GetTick();
    s_kick_seen = 1U;
}

void App_LED_SysTickHandler(void)
{
    static uint8_t led_on = 0U;   /* 上次写入的电平，避免每毫秒重复写 GPIO */
    uint8_t want_on;
    uint32_t now;

    now = HAL_GetTick();

    if ((s_kick_seen != 0U) &&
        ((now - s_kick_tick) > APP_LED_KICK_TIMEOUT_MS))
    {
        /* 主循环卡死：LED 常亮告警（SysTick 中断本身仍存活）。 */
        want_on = 1U;
    }
    else
    {
        /* 心跳：每周期开头亮 APP_LED_HEARTBEAT_ON_MS，其余时间灭。 */
        want_on = ((now % APP_LED_HEARTBEAT_PERIOD_MS) <
                   APP_LED_HEARTBEAT_ON_MS) ? 1U : 0U;
    }

    if (want_on != led_on)
    {
        if (want_on != 0U)
            APP_LED_ON();
        else
            APP_LED_OFF();
        led_on = want_on;
    }
}
