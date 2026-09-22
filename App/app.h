/**
 ******************************************************************************
 * @file    app.h
 * @brief   应用层入口：初始化与主循环调度。
 *
 * @note    main.c 只负责 HAL/时钟/CubeMX 外设初始化，随后把控制权交给
 *          App_Init() 与 App_Loop()。业务逻辑不再与生成代码混在一起，
 *          将来从 .ioc 重新生成也不会被覆盖。
 ******************************************************************************
 */

#ifndef __APP_H__
#define __APP_H__

#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 应用初始化：绑定串口输出、启动命令接收、注册命令、ADC 自检。
 * @note  自检失败会打印原因并停机（Error_Handler），不会带病进入主循环。
 */
void App_Init(void);

/**
 * @brief 应用主循环体。每次调用处理一条命令并推进一步采集，内部含 1ms 延时。
 */
void App_Loop(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H__ */
