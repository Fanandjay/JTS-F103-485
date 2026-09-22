/**
 ******************************************************************************
 * @file    app_report.h
 * @brief   调试输出层：串口格式化打印与启动 Banner。
 *
 * @note    仅负责"输出"，不含任何采集逻辑。其他模块通过它打印，
 *          避免各自持有串口句柄。
 ******************************************************************************
 */

#ifndef __APP_REPORT_H__
#define __APP_REPORT_H__

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 绑定调试输出使用的串口。未调用时 App_Report_Printf 不产生任何输出。 */
void App_Report_Init(UART_HandleTypeDef *huart);

/* 阻塞式格式化输出。缓冲区固定，超长内容会被安全截断。 */
void App_Report_Printf(const char *fmt, ...);

/* 打印启动 Banner。ADC 配置不在这里打印 —— 它由自检段在配置完成后打印，
 * 以保证"打印的描述"与"真正写进芯片的寄存器"一致。 */
void App_Report_Banner(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_REPORT_H__ */
