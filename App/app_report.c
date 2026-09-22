/**
 ******************************************************************************
 * @file    app_report.c
 * @brief   调试输出层实现。
 ******************************************************************************
 */

#include "app_report.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* 栈缓冲区大小。本工程栈仅 1KB，且 vsnprintf 自身还要占用一定栈空间，
 * 因此不宜取得更大；当前最长输出不足 80 字节，128 已足够并留有裕量。 */
#define APP_REPORT_BUFFER_SIZE 128U

static UART_HandleTypeDef *s_huart;

void App_Report_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
}

void App_Report_Printf(const char *fmt, ...)
{
    char buf[APP_REPORT_BUFFER_SIZE];
    va_list args;

    if (s_huart == NULL)
        return;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    HAL_UART_Transmit(s_huart, (uint8_t *)buf, (uint16_t)strlen(buf),
                      HAL_MAX_DELAY);
}

void App_Report_Banner(void)
{
    App_Report_Printf("\r\n");
    App_Report_Printf("========================================\r\n");
    App_Report_Printf("  ADS1220 Wheatstone Bridge ADC Reader\r\n");
    App_Report_Printf("  STM32F103C8\r\n");
    App_Report_Printf("========================================\r\n");
    App_Report_Printf("  SPI2: PB13=SCK, PB14=MISO, PB15=MOSI\r\n");
    App_Report_Printf("  CS  : PB12\r\n");
    App_Report_Printf("  Command: 0=start, 1=stop, 2=tare\r\n");
    App_Report_Printf("========================================\r\n\r\n");
}
