# HLW8112 模块化设计规格书

> 版本: 1.0  
> 日期: 2026-04-10

## 设计目标
- 兼容 C51 (8位) 和 ARM (32位)
- 无回调函数，纯宏开关控制
- 最小移植工作量

---

## 文件结构

```
hlw8112.h          // 总头文件 + 配置开关
hlw8112_manage.c   // 初始化 + 综合读取（必选）
hlw8112_comm.c     // UART通信底层
hlw8112_read.c     // 单独读取函数
hlw8112_calc.c     // 数据计算
hlw8112_nonblock.c // 非阻塞（可选）
hlw8112_hw.c       // 硬件层（用户实现）
hlw8112_config.h  // 用户配置
hlw8112_types.h    // 数据类型定义
```

## 模块依赖关系

```
hlw8112_manage.c (必选)
    |
    +-- hlw8112_comm.c
    |       |
    |       +-- hlw8112_hw.c (用户实现)
    |
    +-- hlw8112_read.c
    |       |
    |       +-- hlw8112_calc.c
    |
    +-- hlw8112_nonblock.c (可选)
```

## 配置开关设计

### hlw8112.h 主开关
```c
// 芯片选择
#define HLW8112_CHIP_8110    1
#define HLW8112_CHIP_8112    2
#define HLW8112_CHIP_SELECT HLW8112_CHIP_8110

// 功能模块开关
#define HLW8112_ENABLE_CHANNEL_B    1
#define HLW8112_ENABLE_NONBLOCK     1
#define HLW8112_ENABLE_LOG          0
#define HLW8112_ENABLE_TEMP         0

// 函数裁剪开关
#define HLW8112_FUNC_READ_U        1
#define HLW8112_FUNC_READ_IA       1
#define HLW8112_FUNC_READ_IB       1
#define HLW8112_FUNC_READ_PA       1
#define HLW8112_FUNC_READ_PB       1
#define HLW8112_FUNC_READ_EA       1
#define HLW8112_FUNC_READ_EB       1
#define HLW8112_FUNC_READ_PF       1
#define HLW8112_FUNC_READ_PSC      1
```

## 移植接口

### 必须实现的函数（hlw8112_hw.c）
```c
void HLW_HW_Init(void);              // GPIO初始化
void HLW_HW_SendData(uint8_t *d, uint16_t len);  // UART发送
void HLW_HW_ClearRxBuf(void);        // 清接收缓冲
void HLW_DELAY_MS(uint16_t ms);      // 延时函数
// 可选: void HLW_HW_IRQHandler(void);  // 中断处理
```

### 最小API
```c
void Init_HLW8110(void);              // 初始化
void Calculate_HLW8110_MeterData(void);  // 读取所有数据

// 全局变量（读取结果）
extern float F_AC_V;     // 电压 (V)
extern float F_AC_I_A;   // A通道电流 (A)
extern float F_AC_P_A;   // A通道功率 (W)
extern float F_AC_E_A;   // A通道电能 (kWh)
```

## C51兼容性
- 无函数指针
- 无 struct/bit-field 跨平台
- 使用 #define 宏替代复杂类型
- 避免 float (可配置整数模式)

## 实施计划
1. 创建 hlw8112_types.h - 类型定义
2. 创建 hlw8112.h - 总头文件
3. 创建 hlw8112_comm.c - 通信底层
4. 创建 hlw8112_read.c - 读取函数
5. 创建 hlw8112_calc.c - 数据计算
6. 创建 hlw8112_manage.c - 初始化管理
7. 创建 hlw8112_config.h - 用户配置