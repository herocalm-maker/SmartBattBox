/**
 * @file    hlw8112_config.h
 * @brief   HLW8112/HLW8110 用户功能配置文件（跨平台版本）
 * @details 移植者必须修改此文件：
 *          1. 定义功能开关（按需开启/关闭）
 *          2. 定义引脚映射宏 HLW811X_PIN_EN_SET/CS_SET/SCLK_SET
 *          3. 定义 UART 外设实例 HLW811X_UART_INSTANCE
 *          4. 配置波特率、寄存器值等
 *
 * @note    文件头部包含静态断言检查（寄存器宽度、延时基数、波特率）
 * @version V3.1 增加移植者防呆配置 + 静态断言
 * @date    2026-06-05 重构实现
 */

#ifndef __HLW8112_CONFIG_H
#define __HLW8112_CONFIG_H

/*============================================================
 * ★★★ 移植者必须定义 — 硬件引脚映射宏 ★★★
 *
 * 将以下宏定义为目标平台的 GPIO 置位操作。
 * 示例 (STM32): #define HLW811X_PIN_EN_SET(level)  PAout(5) = (level)
 * 示例 (C51):   #define HLW811X_PIN_EN_SET(level)  EN = (level)
 *============================================================*/
/* // #define HLW811X_PIN_EN_SET(level)    /* TODO: 定义EN引脚置位 */
/* // #define HLW811X_PIN_CS_SET(level)    /* TODO: 定义CS引脚置位 */
/* // #define HLW811X_PIN_SCLK_SET(level)  /* TODO: 定义SCLK引脚置位 */

/* ★★★ 移植者必须定义 — UART 外设实例（ARM 平台用，C51 可忽略） ★★★ */
// #define HLW811X_UART_INSTANCE        USART2

/*============================================================
 * 功能模块开关配置 - 设置为1开启，0关闭
 *============================================================*/

/* 1. A通道测量功能（电压、电流、功率、电量）
 *    ROM: ~600B (含 3 个 float 计算函数)
 *    RAM: ~24B  (3 × float + 1 × u32 + 1 × u16) */
#define HLW811X_CHANNEL_A_ENABLE        1

/* 2. B通道测量功能（电压、电流、功率、电量）
 *    ROM: ~600B (同 A 通道)
 *    RAM: ~24B  (同 A 通道) */
#define HLW811X_CHANNEL_B_ENABLE        1

/* 3. 频率和相角测量功能
 *    ROM: ~400B (含 HLW811X_Calc_Freq + HLW811X_Calc_Angle)
 *    RAM: ~8B   (2 × float) */
#define HLW811X_FREQ_ANGLE_ENABLE       1

/* 4. OFFSET校准功能（仅在出厂校准时使用）
 *    ROM: ~500B (Init_HLW8110_OFFSET 函数)
 *    RAM: 0B (仅使用局部变量)
 *    注：此功能已从非阻塞状态机中移除，如需使用请自行实现 */
#define HLW811X_OFFSET_CALIB_ENABLE     0

/* 5. 温度测量功能
 *    ROM: ~800B (Init_Temp_Channel_B + Calculate_Temp_Channel_B)
 *    RAM: ~12B  (2 × float + 1 × u32)
 *    注：此功能已从非阻塞状态机中移除，如需使用请自行实现非阻塞版本 */
#define HLW811X_TEMPERATURE_ENABLE      0

/* 6. 功率因数测量功能（单独开关）
 *    ROM: ~200B (HLW811X_Calc_PF 函数)
 *    RAM: ~4B   (1 × float) */
#define HLW811X_POWER_FACTOR_ENABLE     1

/* 7. 视在功率测量功能（单独开关）
 *    ROM: ~250B (HLW811X_Calc_PSC 函数)
 *    RAM: ~8B   (1 × float + 1 × u32) */
#define HLW811X_APPARENT_POWER_ENABLE   1

/*============================================================
 * 日志输出开关配置
 * 设置为1: 开启日志输出（默认）
 * 设置为0: 关闭日志输出，减少代码体积
 * ROM 影响: 开启 → +~2KB (所有 HLW811X_LOG_* 函数 + vsprintf)
 * RAM 影响: 开启 → +~200B (日志缓冲区)
 *============================================================*/
#define HLW811X_LOG_ENABLE      1

/*============================================================
 * 通信延时时间定义 (单位：ms)
 * 根据 UART 波特率调整，9600bps 建议 10ms
 * ROM 影响: 不变（仅宏展开值）
 * RAM 影响: 不变
 *============================================================*/
#define D_DELAYTIME     10

/*============================================================
 * 浮点运算开关 (保留给未来扩展)
 * 设置为1: 使用 float 计算 (默认，所有平台支持)
 * 设置为0: 使用定点数计算 (适用于极端资源受限场景，需自行实现)
 * ROM 影响: float → +~3KB (Keil C51 float 库)
 * RAM 影响: float → +4B/变量（float vs u32 定点）
 *============================================================*/
#define HLW811X_USE_FLOAT    1

/*============================================================
 * 数据读取器模块开关
 * 设置为1: 启用非阻塞状态机 (hlw8112_reader.c)
 * 设置为0: 仅使用阻塞模式 API
 * ROM 影响: +~2KB (状态机+初始化状态机+查表)
 * RAM 影响: +~30B (状态变量+缓冲区)
 *============================================================*/
#define HLW811X_READER_ENABLE    1

/*============================================================
 * UART 波特率选择 (需与 uart2_init 保持一致)
 * 可选: 9600 / 19200 / 38400
 *
 * 超时时间自动计算:
 *   9600 → 10ms (timeout=5, TIM3=2ms)
 *   19200 → 6ms  (timeout=3, TIM3=2ms)
 *   38400 → 4ms  (timeout=2, TIM3=2ms)
 *
 * ROM 影响: 不变（编译时常量）
 * RAM 影响: 不变
 *============================================================*/
#define HLW811X_UART_BAUD       9600

/*============================================================
 * 芯片寄存器配置值 (SYSCON / EMUCON / EMUCON2)
 * 用户可根据应用场景修改，如切换交流/直流测量模式
 *============================================================*/
/* #define D_REG_SYSCON_DATA       0x0f04  /* 系统控制: A/B通道开启, PGA配置 */
/* #define D_REG_EMUCON_DATA       0x1003  /* 计量控制: B通道电流, 脉冲使能 */
/* #define D_REG_EMUCON2_DATA      0x0fff  /* 计量控制2: 数据更新率3.4Hz */

/*============================================================
 * ==================== 静态断言检查 ====================
 *
 * 注意：引脚宏 HLW811X_PIN_EN_SET/CS_SET/SCLK_SET 的检查
 *       在 hlw8112_hw.c 中进行（因 STM32 需要先包含 sys.h）。
 *============================================================*/

/* 静态断言：确保寄存器值宽度正确 */
#if ((D_REG_SYSCON_DATA) > 0xFFFF)
    #error "D_REG_SYSCON_DATA 超出16位范围！SYSCON 是16位寄存器。"
#endif
#if ((D_REG_EMUCON_DATA) > 0xFFFF)
    #error "D_REG_EMUCON_DATA 超出16位范围！"
#endif
#if ((D_REG_EMUCON2_DATA) > 0xFFFF)
    #error "D_REG_EMUCON2_DATA 超出16位范围！"
#endif

/* 静态断言：确保延时基数非零 */
#if (D_DELAYTIME == 0)
    #error "D_DELAYTIME 不能为0！建议9600bps使用10ms。"
#endif

/* 静态断言：波特率合理性检查 */
#if (HLW811X_UART_BAUD != 9600) && (HLW811X_UART_BAUD != 19200) && (HLW811X_UART_BAUD != 38400)
    #warning "HLW811X_UART_BAUD 非标准值，请确认超时时间是否合适。"
#endif

/* #endif /* __HLW8112_CONFIG_H */
