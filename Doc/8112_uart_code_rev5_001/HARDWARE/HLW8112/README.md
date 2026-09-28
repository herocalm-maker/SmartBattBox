# HLW8112/HLW8110 跨平台 UART 驱动

## 文件结构（16 个核心 .c/.h）

```
HARDWARE/HLW8112/
├── hlw8112.h                聚合入口（只需 #include 此文件）
├── hlw8112_port.h           跨平台类型自动检测（C51/ARM/ESP32）
├── hlw8112_config.h    ★    功能开关 + 引脚宏 + 波特率（移植者修改）
├── hlw8112_calib.h     ★    用户校准系数（按实际电路修改）
│
├── hlw8112_data.h           数据模型 + extern 全局变量声明
├── hlw8112_regs.h           寄存器映射（来自数据手册，禁止修改）
│
├── hlw8112_hw.h             硬件抽象接口契约（9 个函数声明）
├── hlw8112_hw.c         ★   硬件实现（移植者唯一重写的 .c）
│
├── hlw8112_protocol.h/c     UART 协议层（命令帧 + 校验和 + 缓冲区）
├── hlw8112_calc.h/c         物理量换算（11 个参数化 Calc 函数）
├── hlw8112_reader.h/c       非阻塞状态机（7 态主读取 + 6 态初始化）
├── hlw8112_log.h/c          调试日志输出
│
├── porting_template_c51.c   C51 移植参考模板
├── porting_template_esp32.c ESP32 移植参考模板
├── README.md                本文件
└── OPTIMIZATION_V1.md       时序 / 性能参考
```

**★ = 移植者必须修改的文件**

---

## 快速上手（STM32 示例）

### 1. 添加文件到工程

| 操作 | 文件 |
|:--:|------|
| ➕ 添加 | `hlw8112_protocol.c` |
| ➕ 添加 | `hlw8112_calc.c` |
| ➕ 添加 | `hlw8112_reader.c` |
| ➕ 添加 | `hlw8112_hw.c` |
| ➕ 添加 | `hlw8112_log.c` |
| ✅ Include Path | `HARDWARE\HLW8112\` |

### 2. 配置 `hlw8112_config.h`

```c
#define HLW811X_CHANNEL_A_ENABLE   1   // 开启 A 通道
#define HLW811X_CHANNEL_B_ENABLE   1   // 开启 B 通道
#define HLW811X_LOG_ENABLE         1   // 开启日志输出
#define HLW811X_UART_BAUD       9600   // 波特率
```

> STM32 默认引脚映射在 `hlw8112_hw.c` 中定义（PA5=EN, PA6=CS, PA7=SCLK），其他平台在 `hlw8112_config.h` 中定义 `HLW811X_PIN_EN_SET` 等宏。

### 3. 主循环代码

```c
#include "hlw8112.h"

int main(void)
{
    // ... 系统时钟 / UART1 / 定时器初始化 ...

    // HLW8112 硬件初始化
    HLW811X_HW_Init();
    HLW811X_HW_SetEN(0);      // EN = LOW
    HLW811X_HW_SetSCLK(1);    // SCLK = HIGH
    HLW811X_HW_SetCS(0);      // CS = LOW

    while (1)
    {
        // 阶段1: 非阻塞初始化芯片
        HLW811X_Reader_InitChip();
        if (HLW811X_Reader_IsInitDone())
        {
            HLW811X_Reader_Reset();
            g_hlw811x_chip_state = HLW811X_CHIP_STATE_WORK;
        }

        // 阶段2: 主读取状态机
        HLW811X_Reader_Start();
        HLW811X_Reader_Run();

        if (HLW811X_Reader_IsReady())
        {
            // 直接读取全局变量中的测量结果
            printf("V=%.2f I_A=%.3f P_A=%.2f\n",
                F_AC_V, F_AC_I_A, F_AC_P_A);
        }
    }
}
```

### 4. 定时器中断

```c
// 在 2ms 定时器中断中调用
void TIM3_IRQHandler(void)
{
    HLW811X_Reader_TimerIRQ();       // 递减超时计数器
}
```

---

## 核心数据流

```
HLW8112 芯片寄存器
  ↓ UART @9600bps (中断接收)
g_hlw811x_rx_buf[]  ← ISR 直接写入（hw.c）
  ↓ HLW811X_Proto_GetRxData() 校验（protocol.c）
局部 rx_buf[] 有效数据
  ↓ HLW811X_Calc_*() 参数化换算（calc.c）
F_AC_V, F_AC_I_A, ... 等全局变量  ← 应用层直接读取
```

---

## 移植到新 MCU（详细指导）

### 移植目标：只改 2 个文件

| 文件 | 改动量 | 说明 |
|------|:------:|------|
| `hlw8112_config.h` | ~5 行 | 定义引脚宏 + 确认波特率 |
| `hlw8112_hw.c` | ~150 行 | 重写全部 9 个函数 |

> 其他 .c/.h 文件**严禁修改**（校验和算法、换算公式、寄存器映射）。

### 一步步移植

#### 步骤 1：复制模板

```
cp porting_template_c51.c  hlw8112_hw.c     ← C51 平台
cp porting_template_esp32.c hlw8112_hw.c     ← ESP32 平台
```

或者直接参考 STM32 的 `hlw8112_hw.c` 从头写。

#### 步骤 2：修改 `hlw8112_config.h`

取消注释并定义引脚宏：

```c
// STM32 示例（使用位带操作）
#define HLW811X_PIN_EN_SET(level)   PAout(5) = (level)
#define HLW811X_PIN_CS_SET(level)   PAout(6) = (level)
#define HLW811X_PIN_SCLK_SET(level) PAout(7) = (level)

// C51 示例（使用 sbit）
#define HLW811X_PIN_EN_SET(level)   EN = (level)    // sbit EN = P1^0;
#define HLW811X_PIN_CS_SET(level)   CS = (level)    // sbit CS = P1^1;
#define HLW811X_PIN_SCLK_SET(level) SCLK = (level)  // sbit SCLK = P1^2;

// ESP32 示例（使用 IDF）
#define HLW811X_PIN_EN_SET(level)   gpio_set_level(GPIO_NUM_5, level)
#define HLW811X_PIN_CS_SET(level)   gpio_set_level(GPIO_NUM_18, level)
#define HLW811X_PIN_SCLK_SET(level) gpio_set_level(GPIO_NUM_19, level)
```

确认波特率：
```c
#define HLW811X_UART_BAUD       9600   // 必须与 UART 初始化一致
```

#### 步骤 3：实现 `hlw8112_hw.c` 的 9 个函数

| # | 函数 | 行为契约 | C51 实现要点 | ESP32 实现要点 |
|:-:|------|---------|-------------|---------------|
| 1 | `HLW811X_HW_SetEN(level)` | GPIO 写 EN 引脚 | `sbit EN = P1^0;` | `gpio_set_level()` |
| 2 | `HLW811X_HW_SetCS(level)` | GPIO 写 CS 引脚 | 同上 | 同上 |
| 3 | `HLW811X_HW_SetSCLK(level)` | GPIO 写 SCLK 引脚 | 同上 | 同上 |
| 4 | `HLW811X_HW_DelayMs(ms)` | **唯一允许阻塞** | `for` 循环 | `vTaskDelay()` |
| 5 | `HLW811X_HW_Print(fmt,...)` | 调试输出，可空实现 | `SBUF` 逐字符 | `ESP_LOGI()` |
| 6 | `HLW811X_HW_Init()` | 初始化 GPIO + UART | `SCON/TMOD/TH1` | `uart_param_config()` |
| 7 | `HLW811X_HW_SendData(data,len)` | **非阻塞**中断发送 | 查询式：`SBUF=; while(!TI);` | `uart_write_bytes()` |
| 8 | `HLW811X_HW_IsTxBusy(void)` | 返回发送忙状态 | 查询模式可返回 0 | 阻塞模式可返回 0 |
| 9 | `HLW811X_HW_ClearRxBuf(void)` | 清空硬件缓冲区 | 协议层已清软件缓冲区 | +`uart_flush_input()` |

#### 步骤 4：实现 ISR（接收中断）

ISR 直接操作以下全局变量（定义在 `hlw8112_protocol.c`，`extern` 在 `hlw8112_protocol.h`）：

```c
extern u8  g_hlw811x_rx_buf[10];   // ISR 写入接收字节
extern u8  g_hlw811x_rx_len;        // 协议层设置的期望接收长度
extern u8  g_hlw811x_rx_idx;        // ISR 递增
extern volatile u8  g_hlw811x_rx_done;  // ISR 置 1 表示接收完成
extern volatile u8  g_hlw811x_rx_ing;   // ISR 置 1 表示接收中
```

**ISR 伪代码**：
```c
void UART_ISR(void)
{
    if (接收到字节) {
        if (g_hlw811x_rx_idx < g_hlw811x_rx_len) {
            g_hlw811x_rx_buf[g_hlw811x_rx_idx] = 收到的字节;
            g_hlw811x_rx_idx++;
            g_hlw811x_rx_ing = 1;
            if (g_hlw811x_rx_idx >= g_hlw811x_rx_len) {
                g_hlw811x_rx_done = 1;   // 接收完成
            }
        }
    }
}
```

#### 步骤 5：实现发送超时（可选）

```c
void HLW811X_HW_TxTimeoutIRQ(void)
{
    // 在 2ms 定时中断中调用
    // 如果硬件使用的发送超时计数器递减到头，强制中止并置 g_hlw811x_tx_busy = 0
}
```

如果是查询式 UART（C51 模板），此函数可空实现。

#### 步骤 6：确认 UART 参数

| 参数 | 值 |
|------|-----|
| 波特率 | 9600（默认，可配 19200/38400） |
| 数据位 | 8 |
| 校验位 | 偶校验（Even Parity） |
| 停止位 | 1 |

> **C51 注意**：标准 8051 串口无硬件偶校验，需使用方式 3（9 位）手动处理 TB8。

#### 步骤 7：编译验证

编译通过后，检查以下现象：

| 现象 | 含义 |
|------|------|
| 串口输出 `InitChip: START`...`REG verify OK` | 芯片初始化成功 |
| 串口输出 `Cal factor checksum OK` | 校准系数读取正确 |
| 串口输出 `V=220.50 I_A=1.234 P_A=123.45` | 测量数据正常 |
| 无 `#error` 编译错误 | 防呆检查通过 |

---

## 全部全局变量

### 测量结果（浮点值）

| 变量名 | 含义 | 单位 |
|--------|------|:----:|
| `F_AC_V` | 电压有效值 | V |
| `F_AC_I_A` | A 通道电流 | A |
| `F_AC_P_A` | A 通道有功功率 | W |
| `F_AC_E_A` | A 通道有功电能 | KWH |
| `F_AC_I_B` | B 通道电流 | A |
| `F_AC_P_B` | B 通道有功功率 | W |
| `F_AC_E_B` | B 通道有功电能 | KWH |
| `F_AC_P_SC` | 视在功率 | W |
| `F_AC_PF` | 功率因数 | — |
| `F_AC_LINE_Freq` | 市电频率 | Hz |
| `F_Angle` | 相角 | 度 |

### 寄存器原始值（u32 调试用）

`U32_RMSU_RegData`, `U32_RMSIA_RegData`, `U32_POWERPA_RegData`, `U32_ENERGY_PA_RegData`, `U32_RMSIB_RegData`, `U32_POWERPB_RegData`, `U32_ENERGY_PB_RegData`, `U32_POWERPS_RegData`

### 校准系数（u16）

`U16_RMSIAC_RegData`, `U16_RMSIBC_RegData`, `U16_RMSUC_RegData`, `U16_PowerPAC_RegData`, `U16_PowerPBC_RegData`, `U16_PowerSC_RegData`, `U16_EnergyAC_RegData`, `U16_EnergyBC_RegData`, `U16_CheckSUM_RegData`, `U16_CheckSUM_Data`

### 状态机变量

`g_hlw811x_chip_state`, `g_hlw811x_reader_state`, `g_hlw811x_reader_ready`, `g_hlw811x_reader_errors`, `g_hlw811x_reader_rst_det`, `g_hlw811x_reader_init_done` 等（详见 `hlw8112_data.h`）

---

## 常见问题

**Q: 编译报 `#error "HLW811X: 未检测到已知平台"`**
A: 检查 `hlw8112_port.h` 中是否有对应编译器的平台检测。STM32 需定义 `STM32F10X_MD` 等宏（Keil 工程配置中默认有）。

**Q: 编译报 `#error "HLW811X: 未定义 HLW811X_PIN_EN_SET"`**
A: STM32 平台使用 `hlw8112_hw.c` 中提供的默认定义（`PAout(5)`），无需在 `config.h` 中重复定义。其他平台需在 `config.h` 中定义。

**Q: 测量结果始终为 0**
A: 排查链路：① 示波器测 UART TX 引脚是否有 0xA5 波形 ② 确认校验和是否正确（看 `g_hlw811x_reader_errors` 是否增长）③ 确认校准系数未超范围。

**Q: 校验和错误率很高**
A: ① 确认 UART 偶校验配置正确 ② 尝试降低波特率（9600）③ 检查通信线缆长度和干扰。

**Q: 初始化一直不完成**
A: ① 检查 GPIO 引脚连接 ② 确认 `HLW811X_HW_Init()` 已正确配置 GPIO/UART ③ 检查定时器中断是否在调用 `HLW811X_Reader_TimerIRQ()`。

---

## 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| V4.2 | 2026-06-05 | 全局变量方案（C51 兼容），`->` 全部改为 `.`/直接访问 |
| V4.0 | 2026-06-05 | 结构体重构：Data+Protocol+Calc 三模块拆分 |
| V3.1 | 2026-05-27 | 跨平台标准化 + 移植模板 |
| V2.0 | 2026-05-27 | 非阻塞状态机 + 中断式串口 |
| V1.6 | 2023-06-29 | 原始阻塞式驱动 |
