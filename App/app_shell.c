/**
 ******************************************************************************
 * @file    app_shell.c
 * @brief   串口命令解析实现。
 *
 * 数据流：
 *   USART1 中断 → HAL_UART_RxCpltCallback → 组帧（行缓冲/单字符命令）
 *   → App_Shell_Poll（主循环）→ 命令表分发 → 注册的处理函数
 *
 * HAL 串口回调由 app.c 统一分发，避免 USART1/USART2 重复定义回调。
 ******************************************************************************
 */

#include "app_shell.h"
#include "app_config.h"
#include "app_report.h"
#include <string.h>

typedef struct
{
    uint8_t             command;
    App_Shell_Handler_t handler;
} App_Shell_Entry_t;

static UART_HandleTypeDef *s_huart;

/* 接收状态。ISR 与主循环共享，故加 volatile。 */
static uint8_t          s_rx_byte;
static uint8_t          s_buffer[APP_CMD_BUFFER_SIZE];
static volatile uint8_t s_length;
static volatile uint8_t s_pending_length;
static volatile uint8_t s_ready;
static volatile uint8_t s_overflow;

static App_Shell_Entry_t s_handlers[APP_SHELL_MAX_HANDLERS];
static uint8_t           s_handler_count;

/* 单字符命令能否免回车立即生效，取决于它是否已被注册。 */
static uint8_t App_Shell_IsRegistered(uint8_t command)
{
    uint8_t i;

    for (i = 0U; i < s_handler_count; i++)
    {
        if (s_handlers[i].command == command)
            return 1U;
    }

    return 0U;
}

HAL_StatusTypeDef App_Shell_Init(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
        return HAL_ERROR;

    s_huart = huart;
    s_length = 0U;
    s_pending_length = 0U;
    s_ready = 0U;
    s_overflow = 0U;

    s_handler_count = 0U;
    memset(s_handlers, 0, sizeof(s_handlers));

    return HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1U);
}

HAL_StatusTypeDef App_Shell_Register(uint8_t command,
                                     App_Shell_Handler_t handler)
{
    uint8_t i;

    if (handler == NULL)
        return HAL_ERROR;

    for (i = 0U; i < s_handler_count; i++)
    {
        if (s_handlers[i].command == command)
        {
            s_handlers[i].handler = handler;
            return HAL_OK;
        }
    }

    if (s_handler_count >= APP_SHELL_MAX_HANDLERS)
        return HAL_ERROR;

    s_handlers[s_handler_count].command = command;
    s_handlers[s_handler_count].handler = handler;
    s_handler_count++;

    return HAL_OK;
}

void App_Shell_Poll(void)
{
    uint8_t command[APP_CMD_BUFFER_SIZE];
    uint8_t length;
    uint8_t i;

    if (!s_ready)
        return;

    /* 原子地把待处理命令搬出来，随后立即释放缓冲供 ISR 继续使用。 */
    __disable_irq();
    length = s_pending_length;
    memcpy(command, s_buffer, (size_t)length + 1U);
    s_ready = 0U;
    s_length = 0U;
    __enable_irq();

    if (length == 1U)
    {
        for (i = 0U; i < s_handler_count; i++)
        {
            if (s_handlers[i].command == command[0])
            {
                s_handlers[i].handler();
                return;
            }
        }
    }

    App_Report_Printf("Unknown command\r\n");
}

void App_Shell_OnRxComplete(UART_HandleTypeDef *huart)
{
    if (huart != s_huart)
        return;

    if (s_rx_byte == '\r' || s_rx_byte == '\n')
    {
        if (s_length > 0U && !s_overflow && !s_ready)
        {
            s_buffer[s_length] = '\0';
            s_pending_length = s_length;
            s_ready = 1U;
        }

        s_length = 0U;
        s_overflow = 0U;
    }
    else if (!s_ready && !s_overflow)
    {
        if (s_length == 0U && App_Shell_IsRegistered(s_rx_byte))
        {
            /* 已注册的单字符命令：无需回车，立即生效。 */
            s_buffer[0] = s_rx_byte;
            s_buffer[1] = '\0';
            s_pending_length = 1U;
            s_ready = 1U;
        }
        else if (s_length < (APP_CMD_BUFFER_SIZE - 1U))
        {
            s_buffer[s_length++] = s_rx_byte;
        }
        else
        {
            /* 行过长：丢弃并等待下一个行结束符重新同步。 */
            s_length = 0U;
            s_overflow = 1U;
        }
    }

    /* 重新武装接收，等待下一个字节。 */
    HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1U);
}

void App_Shell_OnError(UART_HandleTypeDef *huart)
{
    if (huart != s_huart)
        return;

    /* 溢出/噪声/帧错误后恢复接收。 */
    HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1U);
}
