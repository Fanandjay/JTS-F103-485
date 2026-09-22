# 构建与验证工具

本目录存放脱离 Keil GUI 的构建脚本，用于重构期间快速、可重复地验证编译与链接。

## build.ps1

用命令行复现 Keil MDK / EIDE 的编译与链接流程（ARMCC 5.06u7 + armlink）。

### 用法

```powershell
# 全量构建（推荐用于每次重构后的验收）
powershell -File MDK-ARM\tools\build.ps1 -Clean

# 增量思路：不加 -Clean 时仍会全部重编（脚本不做依赖追踪），
# 但省去删除输出目录的开销
powershell -File MDK-ARM\tools\build.ps1

# 只编译不链接，快速语法检查
powershell -File MDK-ARM\tools\build.ps1 -CompileOnly

# 工具链不在默认位置时
powershell -File MDK-ARM\tools\build.ps1 -ToolchainRoot 'D:\your\ARMCC'
```

判定标准：结尾输出 `BUILD OK  0 error, 0 warning`，退出码 0。

### 基线尺寸（重构前，commit d24f01b）

```
Total RO  Size (Code + RO Data)              19584 (19.13kB)
Total RW  Size (RW Data + ZI Data)            2096 ( 2.05kB)
Total ROM Size (Code + RO Data + RW Data)    19640 (19.18kB)
```

> **与历史 Keil/EIDE 数字（19580 / 2096 / 19636）的关系**
>
> 用本脚本的链接参数重新链接**原始 Keil 的目标文件**，得到的同样是 19584 ——
> 这证明本脚本的**编译设置与工程等价**。剩下的 4 字节 RO 差异来自链接顺序引起的
> 段填充（`.ANY` 布局），与代码生成和源码无关。
>
> 因此后续每个阶段都应以 **19584 这一组**为基准比对。

### 为什么脚本是纯 ASCII、且只给工具链传相对路径

这两条都是为了绕开同一个坑：**工程路径含中文（`…\项目历史\…`）**。

1. **相对路径**：ARMCC 无法正确处理命令行中的非 ASCII 绝对路径——它会按 ANSI
   代码页重新编码，然后连自己刚创建的输出文件都打不开（报 `error #564`）。
   所以交给 `armcc` / `armasm` / `armlink` 的路径一律相对于 `MDK-ARM\`；
   PowerShell 自身读写文件仍用绝对路径（.NET 正确处理 Unicode）。

2. **纯 ASCII 脚本**：Windows PowerShell 5.1 会用 ANSI 代码页解码**无 BOM** 的
   `.ps1` 文件。一旦脚本里写了中文，注释和字符串会被解码成乱码，多字节序列甚至
   会吞掉引号导致语法错误。工具自带的 `pwsh` 实际就是 PowerShell 5.1，
   因此这里放弃中文，改为把中文说明放在本文件中。

   如果将来确实要在脚本里写中文，必须用 **UTF-8 with BOM** 保存——但任何一次
   用普通文本工具重写该文件都会丢掉 BOM，所以不建议。

### 脚本做了什么

- 源文件清单：`Core/Src`、`HARDLIB`、`App`（存在时）按目录收集，
  `Drivers/STM32F1xx_HAL_Driver/Src` 全量——与 Keil/EIDE 工程一致。
  新增 `App\` 目录后无需修改脚本即可自动纳入。
- 编译参数与工程一致：`Cortex-M3`、`--c99`、`-O3`、`--split_sections`、
  `--apcs=interwork`、`-g`、`-DUSE_HAL_DRIVER -DSTM32F103xB`。
- 散列文件（scatter）自动生成，Flash 64K / RAM 20K，与 EIDE 生成的布局相同。
- 结束后打印 Program Size，并生成 `.axf` 与 `.hex`。

### 注意事项

- Keil 与 EIDE 两个工程文件（`20260703-ADS1220.uvprojx`、`.eide/eide.yml`）
  **不会**被本脚本更新。新增源文件时仍需手工同步这两个文件。
- 脚本不做依赖追踪，每次都全量重编（25 个文件约 7 秒，可接受）。
- 输出目录 `MDK-ARM\build\` 已被 `.gitignore` 忽略。

## 相关提交

| 提交 | 内容 |
|---|---|
| `d24f01b` | 重构前原始状态基线 |
| 阶段 0 | 建立 git 基线 + 本构建脚本 |
