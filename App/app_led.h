/**
 ******************************************************************************
 * @file    app_led.h
 * @brief   板载 LED（PA8，低电平点亮）调试心跳。
 *
 * 由 SysTick 1ms 中断驱动，不占用任何定时器外设。设计目标是一个
 * "不接串口也能看"的调试窗口：
 *
 *   - 慢闪（亮 APP_LED_HEARTBEAT_ON_MS，灭至周期满）= 系统存活；
 *     上电后自检期间即为慢闪（自由运行模式）。
 *   - 主循环一旦跑起来会调用 App_LED_Kick() 喂狗，之后心跳受喂狗
 *     门控：主循环卡死超过 APP_LED_KICK_TIMEOUT_MS，LED 常亮告警。
 *
 * 因此现场看一眼 LED 即可区分三类状态：
 *   慢闪   = 正常运行（中断时钟 + 主循环都活着）
 *   常亮   = 主循环卡死（SysTick 中断仍活着，否则停在最后一个电平）
 *   完全不变 = SysTick / 时钟本身出问题（含程序跑飞、卡在 Error_Handler）
 ******************************************************************************
 */

#ifndef __APP_LED_H__
#define __APP_LED_H__

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 LED（熄灭）并启动自由运行心跳。在 App_Init 早期调用，
 * 自检期间即可提供存活指示；主循环首帧调用 App_LED_Kick 后转入门控模式。 */
void App_LED_Init(void);

/* 主循环每帧调用一次（喂狗）。此后若超过 APP_LED_KICK_TIMEOUT_MS
 * 未再调用，SysTick 中断会把 LED 置为常亮。 */
void App_LED_Kick(void);

/* 由 stm32f1xx_it.c 的 SysTick_Handler（USER CODE 区）每 1ms 调用一次。
 * 只操作 GPIO 写寄存器，耗时微秒级。 */
void App_LED_SysTickHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_LED_H__ */
