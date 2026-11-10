/**
 ******************************************************************************
 * @file    app_shell.h
 * @brief   串口命令解析：逐字节中断接收 + 行缓冲 + 命令表分发。
 *
 * @note    本模块只管"收命令并分发给处理函数"，不认识任何具体命令语义；
 *          命令语义由 app.c 注册。新增命令只需注册一行，不必改动本文件。
 ******************************************************************************
 */

#ifndef __APP_SHELL_H__
#define __APP_SHELL_H__

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 可注册的命令条数上限 */
#define APP_SHELL_MAX_HANDLERS  8U

typedef void (*App_Shell_Handler_t)(void);

/**
 * @brief 绑定接收串口并启动逐字节中断接收。
 * @param huart 用于接收命令的串口
 * @return HAL_OK 表示中断接收已成功启动
 */
HAL_StatusTypeDef App_Shell_Init(UART_HandleTypeDef *huart);

/**
 * @brief 注册单字符命令的处理函数。
 *
 * 命令既可用 CR/LF 结尾发送，也可以单字符直接发送（后者立即生效）。
 *
 * @param command 命令字符
 * @param handler 处理函数，不可为 NULL
 * @return HAL_OK 成功；命令表已满返回 HAL_ERROR
 */
HAL_StatusTypeDef App_Shell_Register(uint8_t command,
                                     App_Shell_Handler_t handler);

/**
 * @brief 主循环调用：若有完整命令则取出并分发。
 * @note  无命令时立即返回，不会阻塞。
 */
void App_Shell_Poll(void);

/* 由 app.c 中统一的 HAL UART 回调分发。 */
void App_Shell_OnRxComplete(UART_HandleTypeDef *huart);
void App_Shell_OnError(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* __APP_SHELL_H__ */
