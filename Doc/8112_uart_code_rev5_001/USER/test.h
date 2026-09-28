/**
 * @file    test.h
 * @brief   测试/演示程序头文件 - HLW8110/8112 电能计量芯片驱动演示
 * @details 定义芯片选择、通信方式配置及IO引脚映射
 * @note    配置说明:
 *          - HLW8110=1, HLW8112=0: 使用UART与HLW8110通信
 *          - HLW8110=0, HLW8112=1: 使用UART与HLW8112通信
 *          - SPI模式: 将上述宏设为相反值
 */

#ifndef __TEST_H
#define __TEST_H    

#include "sys.h"


/*====================================================================================
 * 芯片选择与通信方式配置
 *====================================================================================
 * HLW8110:(HLW8110 = 1, HLW8112 = 0)
 * HLW8112:(HLW8110 = 0, HLW8112 = 1)
 *
 * UART通信模式:
 *   #define HLW8110  1    // 1=UART通信(HLW8110), 0=SPI通信
 *   #define HLW8112  0    // 1=UART通信(HLW8112), 0=SPI通信
 *
 * SPI通信模式:
 *   #define HLW8110  0
 *   #define HLW8112  1
 *====================================================================================*/

// 选择通信方式 (当前配置: UART模式)
#define HLW8110     1       // 启用UART与HLW8110通信
#define HLW8112     0       // 禁用UART与HLW8112通信

// SPI模式配置 (注释掉)
//#define HLW8110    0       // SPI模式
//#define HLW8112    1       // SPI模式

//====================================================================================
// IO端口定义 (SPI模式需要)
//====================================================================================
#if HLW8110
#define IO_HLW8112_EN    PAout(5)   // HLW8112 使能引脚
#define IO_HLW8112_CS    PAout(6)   // HLW8112 片选引脚
#define IO_HLW8112_SCLK  PAout(7)   // HLW8112 时钟引脚
#endif

// SPI模式引脚定义 (未启用)
//#if HLW8112
//#define IO_HLW8112_EN    PAout(5)
//#define IO_HLW8112_CS    PAout(6)
//#define IO_HLW8112_SCLK  PAout(7)
//#define IO_HLW8112_SDI   PAout(2)
//#define IO_HLW8112_SDO   PAin(3)
//#define IO_HLW8112_INT1  PDin(11)
//#define IO_HLW8112_INT2  PDin(12)
//#endif

/* #endif  /* __TEST_H */
