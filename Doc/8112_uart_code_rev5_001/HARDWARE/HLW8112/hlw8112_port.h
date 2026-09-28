/**
 * @file    hlw8112_port.h
 * @brief   HLW8112/HLW8110 跨平台类型定义头文件
 * @details 自动检测编译器平台，提供统一的类型定义。
 *          支持平台：Keil C51, IAR 8051, SDCC, ARM/GCC (STM32/GD32/ESP32/etc.)
 * @note    此文件不依赖任何平台头文件，完全自包含。
 *          新增平台支持时，在此文件中添加对应的 #if 分支即可。
 * @version V3.0 跨平台标准化
 * @date    2026-05-27
 */

#ifndef __HLW8112_PORT_H
#define __HLW8112_PORT_H

/*============================================================
 * 平台检测 & 类型定义
 *
 * 检测优先级: C51 → IAR8051 → SDCC8051 → 通用(ARM/ESP32等)
 *============================================================*/

/*--------------------- Keil C51 / CX51 ---------------------*/
/* C51 特性: int=16bit, long=32bit, 无 <stdint.h>, 小端 */
#if defined(__C51__) || defined(__CX51__)

    typedef unsigned char   u8;
    typedef unsigned int    u16;
    typedef unsigned long   u32;

    #define HLW811X_PLATFORM_C51        1
    #define HLW811X_PLATFORM_NAME       "Keil C51"

/*--------------------- IAR 8051 ----------------------------*/
#elif defined(__ICC8051__)

    typedef unsigned char   u8;
    typedef unsigned int    u16;
    typedef unsigned long   u32;

    #define HLW811X_PLATFORM_C51        1
    #define HLW811X_PLATFORM_NAME       "IAR 8051"

/*--------------------- SDCC 8051 ---------------------------*/
#elif defined(__SDCC_8051)

    typedef unsigned char   u8;
    typedef unsigned int    u16;
    typedef unsigned long   u32;

    #define HLW811X_PLATFORM_C51        1
    #define HLW811X_PLATFORM_NAME       "SDCC 8051"

/*--------------------- 通用平台 (ARM/ESP32/etc.) -----------*/
#else
/*     /* 尝试使用 C99 标准头 <stdint.h> (Keil MDK/IAR ARM/GCC均支持) */
    #if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) || \
        defined(__GNUC__) || defined(__clang__) || \
        defined(__CC_ARM) || defined(__ICCARM__) || \
        !defined(__C51__)
        #include <stdint.h>
        typedef uint8_t   u8;
        typedef uint16_t  u16;
        typedef uint32_t  u32;
    #else
/*         /* 兜底: 手动检测类型宽度 (极少见的旧编译器) */
        #if (UINT_MAX == 0xFFFFU)
            typedef unsigned char   u8;
            typedef unsigned int    u16;
            typedef unsigned long   u32;
        #else
            typedef unsigned char   u8;
            typedef unsigned short  u16;
            typedef unsigned int    u32;
        #endif
    #endif

    #if defined(ESP_PLATFORM) || defined(IDF_VER)
        #define HLW811X_PLATFORM_ESP32      1
        #define HLW811X_PLATFORM_NAME       "ESP32"
    #elif defined(STM32F10X_MD) || defined(STM32F10X_HD) || \
          defined(STM32F10X_XL) || defined(STM32F10X_CL) || \
          defined(STM32F1)
        #define HLW811X_PLATFORM_STM32      1
        #define HLW811X_PLATFORM_NAME       "STM32"
    #elif defined(GD32)
        #define HLW811X_PLATFORM_GD32       1
        #define HLW811X_PLATFORM_NAME       "GD32"
    #elif defined(APM32)
        #define HLW811X_PLATFORM_APM32      1
        #define HLW811X_PLATFORM_NAME       "APM32"
    #else
        #define HLW811X_PLATFORM_ARM        1
        #define HLW811X_PLATFORM_NAME       "Generic ARM"
    #endif
#endif

/*============================================================
 * 平台相关宏定义
 *============================================================*/

/* 布尔值定义 (C90 兼容) */
#ifndef FALSE
    #define FALSE   0
#endif
#ifndef TRUE
    #define TRUE    1
#endif

/* NULL 指针 (防止未包含 stddef.h) */
#ifndef NULL
    #ifdef __cplusplus
        #define NULL    0
    #else
        #define NULL    ((void *)0)
    #endif
#endif

/*============================================================
 * 编译时平台提示
 *============================================================*/
/* 如果想在编译时看到平台信息，可取消注释下面这行 */
/* #pragma message("HLW811X Driver: Platform = " HLW811X_PLATFORM_NAME) */

/* #endif /* __HLW8112_PORT_H */
