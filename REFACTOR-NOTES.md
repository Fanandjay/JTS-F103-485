# 重构说明与上板验证清单

本次重构（方案 B：阶段 1+2+3+4）的交付说明。**编译与逻辑正确性已验证；功能验证需在硬件上完成**（见文末清单）。

---

## 1. 提交记录

| 提交 | 内容 |
|---|---|
| `d24f01b` | 重构前原始状态基线（首次纳入 git） |
| `5517651` | 脱离 Keil GUI 的命令行构建脚本 `MDK-ARM/tools/build.ps1` |
| `e11e15e` | 阶段 1：建立单一配置源，修正自检与实配不一致 |
| `1ec6b76` | 阶段 2：修正 MUX 常量错误，加固驱动边界与连通性自检 |
| `f7c908b` | 阶段 3：拆出 App 业务层，`main.c` 由 519 行降至 89 行 |
| `fd8ce33` | 阶段 4：接通比例式物理量换算，VOFA 增发电压通道 |

出问题时可用 `git revert` / `git checkout <提交> -- <文件>` 精确回退。

---

## 2. 新的代码结构

```
App/                        新增：业务层（位于 Core/ 之外，CubeMX 重新生成不覆盖）
  app_config.h              唯一配置源 —— 改参数只看这一个文件
  app.c / app.h             App_Init() / App_Loop() 调度 + ADS1220 自检 + 命令处理
  app_measure.c / .h        采集状态机（DRDY 轮询 / 去皮 / 滤波 / 换算 / 输出）
  app_shell.c / .h          串口命令：逐字节接收 + 行缓冲 + 命令表分发
  app_report.c / .h         调试输出（格式化打印 + Banner）
HARDLIB/                    驱动层（芯片相关，不含业务）
  ADS1220.C / .H            加固后的 ADS1220 驱动
  Filter.c / .h  VOFA.c / .h
Core/Src/main.c             只剩 HAL 初始化 + 时钟 + Error_Handler（89 行）
```

## 3. 要改参数在哪里改

全部集中在 **`App/app_config.h`**：

```c
#define APP_ADC_CHANNEL   ADS1220_CH5_AIN2_AIN3   /* 通道 */
#define APP_ADC_GAIN      ADS1220_G64             /* 增益 */
#define APP_ADC_DR        ADS1220_DR20            /* 速率 */
#define APP_VREF_MV       5000.0f                 /* 基准电压(mV) —— 见第 6 节 */
#define APP_VEXC_MV       5000.0f                 /* 激励电压(mV) */
#define APP_LPF_ALPHA     0.25f                   /* 一阶低通系数 */
#define APP_TARE_SAMPLES  100U                    /* 去皮样本数 */
#define APP_DRDY_CONFIRM        2U                /* DRDY 确认次数 */
#define APP_DRDY_TIMEOUT_MS     60U               /* DRDY 锁定超时 */
```

改这里会**同时**改变：写入芯片的寄存器、自检打印的文字、回读校验的期望值。三者不再可能不一致。

---

## 4. 预期的正常自检输出（上板对照用）

```
========================================
  ADS1220 Wheatstone Bridge ADC Reader
  STM32F103C8
========================================
  SPI2: PB13=SCK, PB14=MISO, PB15=MOSI
  CS  : PB12
  Command: 0=start, 1=stop, 2=tare
========================================

========================================
  ADS1220 Initialization
========================================

[1] Initializing ADS1220...
     [OK] ADS1220_Init success

[2] Sending RESET command...
     [OK] RESET success

[3] Reset defaults...
     CONFIG0..3 = 00 00 00 00          <- 复位默认值，非 0xFF
     [OK]

[4] Connection test...
     [OK] Write/read probe passed

[5] Configuring from App/app_config.h...
     Channel    : AIN2-AIN3
     Gain       : 64
     Data rate  : 20 SPS
     Mode       : normal
     Conversion : continuous
     VREF       : external REFP0/REFN0

[6] Verifying registers...
     CONFIG0 = 0x5C                    <- MUX=AIN2-AIN3(0x50) | GAIN=64(0x0C)
     CONFIG1 = 0x04                    <- DR=20SPS | CM=连续
     CONFIG2 = 0x40                    <- VREF=REFP0/REFN0
     CONFIG3 = 0x00
     [OK] Registers verified

[7] Starting continuous conversion...
     [OK] Continuous conversion started

Ready. Send 0 to start output, 1 to stop output.
```

### 故障时的新行为

| 现象 | 含义 |
|---|---|
| 第 [3] 步打印 `FF FF FF FF` 并报 `ADS1220 not responding` | DOUT/MISO 恒高：芯片未焊接 / 未供电 / 片选或时钟异常 |
| 第 [4] 步报 `SPI write path broken` | 写不进芯片，或写进去读不回来，SPI 通路故障 |
| 第 [6] 步逐寄存器打印 `expect 0xNN, actual 0xNN` | 配置没写进去或读回链路有问题，对照两列即可定位 |
| 以上任一失败 | 打印原因后**停机**（不再静默带病运行） |

---

## 5. VOFA+ 上位机设置

- 协议 **JustFloat**，串口 **115200**，共 **4 个通道**（由 3 个增加到 4 个，**需要在上位机里加一个通道**）
- 每帧 20 字节 = 4×float + 帧尾 `00 00 80 7F`

| 通道 | 含义 |
|---|---|
| CH1 | 原始 ADC 码（未去皮） |
| CH2 | 去皮后的 ADC 码 |
| CH3 | 去皮并滤波后的 ADC 码 |
| CH4 | CH3 换算出的差分电压 (mV) |

---

## 6. 需要你确认的三件事

1. **`APP_VREF_MV` 目前填的是 5000.0f**
   依据是"电桥激励端接 REFP0/REFN0"（比例式），此时基准 = 激励。
   原工程的 `ADS1220_EXT_VREF_MV = 4841.0f` 与此矛盾，且从未被引用，已删除。
   **请用万用表实测 REFP0-REFN0**，若与 5000mV 不符，改 `app_config.h` 一个数即可 —— 它直接决定 CH4 的 mV 是否准确。

2. **`RS485_EN`（PA1）的用途与有效极性**
   当前被初始化为低电平，全程再未操作。若 USART1 经过 RS485 收发器且 DE 由 PA1 控制，发送时必须为高，否则串口一个字节都发不出去。本次**未改动**这一处。

3. **VOFA+ 通道数由 3 改为 4**，上位机需同步。

---

## 7. 上板验证清单

> 以下每项都需要你在硬件上执行，我无法访问设备。

- [ ] 1. 上电，串口输出与第 4 节完全一致；第 [3] 步为 `00 00 00 00`
- [ ] 2. 第 [6] 步四个寄存器值为 `0x5C / 0x04 / 0x40 / 0x00`
- [ ] 3. 发 `0` → 回显 `RX OK: 0` 与 `Conversion output started`；VOFA+ 开始出波形
- [ ] 4. 未去皮时 CH1 ≈ CH2，CH3 比 CH1 明显平滑
- [ ] 5. 发 `2` 并保持空载 → 采集 100 个样本后提示 `Tare completed`，打印 `Zero offset = <数>`
- [ ] 6. 再发 `0` → CH2/CH3 应在 0 附近，CH4 在 0mV 附近
- [ ] 7. 加载已知载荷 → CH4 稳定；与万用表实测桥路输出对照（验证 `APP_VREF_MV`）
- [ ] 8. 发 `1` → 回显 `Conversion output stopped`，波形停止
- [ ] 9. **断线检测**：断开 ADS1220 的 MISO 或电源后重新上电 → 应报 `not responding` 并停机（旧固件此时会静默继续）
- [ ] 10. 输入超范围时观察是否出现 `ADC invalid sample count=...` 提示
- [ ] 11. 反向极性验证（可选）：把 `APP_ADC_CHANNEL` 改成 `ADS1220_CH7_AIN3_AIN2` 重编，CH4 符号应翻转 —— **这正是旧固件会踩雷的路径**（旧代码 CH7 常量错误，会误选到悬空的 AIN1-AIN0）
- [ ] 12. 在 Keil 中打开工程，确认 `App` 分组下 9 个文件显示正常、可正常编译

---

## 8. 尺寸对比

| | RO (Flash) | RW+ZI (RAM) |
|---|---|---|
| 重构前 | 19584 | 2096 |
| 重构后 | **21284** (+1700) | **2184** (+88) |
| 容量 | 21316 / 65536 (32%) | 2184 / 20480 (11%) |

增量构成：更详细的调试文本与通道名表（+616）、驱动边界检查与连通性自检（+328）、分层拆分的模块开销与命令表（+452）、物理量换算首次引入浮点库例程（+304）。

---

## 9. 如何自行构建

```powershell
# 全量构建 + 链接，输出 .axf 与 .hex
powershell -File MDK-ARM\tools\build.ps1 -Clean

# 只做语法检查
powershell -File MDK-ARM\tools\build.ps1 -CompileOnly
```

判定标准：结尾出现 `BUILD OK  0 error, 0 warning`，退出码 0。
详见 `MDK-ARM/tools/README.md`。

---

## 10. 本次未处理的事项（按约定保留或留待后续）

| 项 | 说明 |
|---|---|
| 死代码 | 按约定原样保留：`KalmanFilter*`、USART2（`MX_USART2_UART_Init` 从未调用）、`ADS1220_ReadData` / `WaitForConversion` / `ReadTemperature` / `EnableTempSensor` / IDAC 相关 / `PowerDown`。注：`ADS1220_Config` 原本也是死代码，本次已启用 |
| DRDY 轮询与采样率耦合 | 主循环 `HAL_Delay(1)` + 连续 2 次确认 ⇒ 最短检测延迟约 2ms。当前 20SPS（50ms/次）余量充足，但**若要改成 1000/2000SPS 必须先改这里**（改用 EXTI 中断或更紧的轮询） |
| CS 上电默认电平 | `gpio.c` 里 CS 被初始化为低（片选有效），它是 CubeMX 生成代码。要改需在 `.ioc` 里设置默认输出电平，或写进 USER CODE 区 |
| 行尾不统一 | `Core/` 为 CRLF，`App/` 与 `HARDLIB/` 为 LF。本次编辑已保持各自原状，未做全库统一 |
| PGA 共模范围 | 增益 64、电桥共模约 2.5V。请对照 ADS1220 数据手册的 PGA 共模限制确认 AVDD 与激励电压的关系（比例式接法下 AIN2/AIN3 绝对电压受 AVDD 上限约束） |
