---
name: HLW8112-driver-deep-refactor
overview: 对 HLW8112 STM32 驱动进行深度架构重构：消除全局变量泛滥和隐式数据流，将所有 Calc 函数改为显式参数传参，删除所有阻塞式 API，简化初始化状态机，建立严格的极速移植契约。此阶段仅输出《重构执行计划书》，不生成任何代码。
todos:
  - id: analyze-global-vars
    content: 使用 [subagent:code-explorer] 全面扫描全局变量：梳理 hlw8112.h、hlw8112_internal.h、hlw8112_reader.h 中所有 extern 声明及其定义位置
    status: completed
  - id: trace-implicit-dataflow
    content: 使用 [subagent:code-explorer] 追踪隐式数据流：逐一分析 11 个 HLW811X_Calc_XXX 函数对全局 u8_RxBuf 的直接读取链路
    status: completed
    dependencies:
      - analyze-global-vars
  - id: locate-blocking-apis
    content: 使用 [subagent:code-explorer] 定位阻塞 API：搜索 hlw8112.c 中全部 37 处 delay_ms 调用及所属函数名
    status: completed
  - id: map-init-statemachine
    content: 使用 [subagent:code-explorer] 映射初始化状态机：分析 hlw8112_reader.c:340-652 中 10 子状态和 7 个 Phase 函数的嵌套结构
    status: completed
  - id: generate-plan
    content: 汇总全部分析结果，生成包含六大板块的完整《重构执行计划书》Markdown 文档
    status: completed
    dependencies:
      - analyze-global-vars
      - trace-implicit-dataflow
      - locate-blocking-apis
      - map-init-statemachine
---

## 用户需求

用户要求输出一份详尽的 Markdown 格式《重构执行计划书》，对 HLW8112 STM32 驱动代码进行深度架构重构。核心目标：

1. **极速看懂**：消除全局变量泛滥和隐式数据流，数据流向必须一目了然
2. **极速移植**：向其他MCU移植时，修改的文件极少且具有防呆提示

## 产品概述

重构后将产出一个清晰分层的电能计量芯片驱动框架，让5年经验的工程师30分钟内看懂系统、30分钟内完成跨MCU底层移植。

## 核心交付物

仅输出计划书，不输出代码。计划书包含六大板块：旧代码诊断、全新架构设计、精细化优化策略、极速移植契约、重构实施清单、验收标准。

## 技术方案概述

本阶段为**纯规划分析**，不涉及代码实现。技术分析基于对现有代码库（17个文件、~3500行代码）的全面研读，覆盖以下技术要点：

### 分析覆盖范围

- **17个源代码文件** 全部精读（10个 .c + 7个 .h + 2个 .md）
- **63+个 extern 变量声明** 逐一追踪定义位置和引用链路
- **11个 Calc 函数** 的隐式数据流完整溯源
- **37处 delay_ms 阻塞调用** 的位置和影响评估
- **10子状态初始化状态机** 的嵌套层次和 step 变量分布分析
- **2个移植模板文件**（C51/ESP32）的接口一致性审查

### 关键发现

1. `hlw8112_internal.h` 泄漏21+个内部实现细节
2. 11个 `HLW811X_Calc_XXX()` 函数全部隐式依赖全局 `u8_RxBuf[]`，接口无法反映数据来源
3. `hlw8112.c` 中保留6个阻塞函数（含 `delay_ms`），与 `hlw8112_reader.c` 的非阻塞状态机设计理念冲突
4. 初始化状态机 `HLW811X_Reader_InitChip()` 带10个子状态和7个分散的 Phase 函数，step 变量跨多处定义

### 计划书结构

计划书将按照用户要求的六大板块组织，每个痛点标注精确的文件名:行号或函数名，实施清单以表格形式列出所有文件级改动及禁止改动区域。

## Agent Extensions

### SubAgent

- **code-explorer**
- 用途：在制定计划前全面探索代码库，读取所有关键文件并定位问题
- 预期结果：完成所有 .h/.c 文件的精读，梳理 extern 变量、delay_ms 调用、Calc 函数隐式数据流的完整清单