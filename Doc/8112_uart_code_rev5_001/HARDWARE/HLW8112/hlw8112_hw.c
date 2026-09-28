/**
 * @file    hlw8112_hw.c
 * @brief   HLW8112/HLW8110 硬件抽象层实现 — STM32 平台
 * @details 提供 STM32F103 平台的 GPIO 和 UART 实现，包含中断服务函数。
 *          移植到其他 MCU 时，复制此文件并按目标平台重写所有函数。
 *
 * 移植注意:
 *   - ⚠️ GPIO 使用 hlw8112_config.h 中的 HLW811X_PIN_EN_SET/CS_SET/SCLK_SET 宏
 *   - ⚠️ UART 使用 HLW811X_UART_INSTANCE 宏（默认 USART2）
 *   - ⚠️ ISR 直接读写全局变量 g_hlw811x_rx_buf[] / g_hlw811x_rx_idx / g_hlw811x_rx_done
 *   - ⚠️ 发送完成时置 g_hlw811x_tx_busy = 0
 *   - ⚠️ 发送超时时置 g_hlw811x_tx_busy = 0
 *
 * @version V3.2 ISR 使用全局变量直接访问
 * @date    2026-06-05
 */

/*============================================================
 * 头文件包含
 *============================================================*/
#include "hlw8112_hw.h"
/* #include "hlw8112_protocol.h"   /* 全局缓冲区 g_hlw811x_rx_buf, g_hlw811x_rx_len, g_hlw811x_rx_idx, g_hlw811x_rx_done */

/*============================================================
 * STM32 平台头文件 (仅在此文件中包含)
 *============================================================*/
/* #include "sys.h"            /* 位带操作/类型定义 — 提供 PAout() 宏 */
/* #include "delay.h"          /* 延时函数 */
/* #include <stm32f10x.h>      /* STM32 外设寄存器定义 */

/*============================================================
 * ★★★ 防呆检查 #1: 平台检测 ★★★
 *   需要在包含 hlw8112_port.h（通过 hlw8112_hw.h 间接包含）之后执行
 *============================================================*/
#if !defined(HLW811X_PLATFORM_C51)   && \
    !defined(HLW811X_PLATFORM_STM32) && \
    !defined(HLW811X_PLATFORM_GD32)  && \
    !defined(HLW811X_PLATFORM_APM32) && \
    !defined(HLW811X_PLATFORM_ESP32) && \
    !defined(HLW811X_PLATFORM_ARM)
    #error "HLW811X: 未检测到已知平台！请确认 hlw8112_port.h 中的平台检测逻辑，或添加新平台支持。"
#endif

/*============================================================
 * STM32 默认引脚映射 (未在 config.h 中定义时使用)
 * 其他平台移植者应在 hlw8112_config.h 中定义对应的宏
 *============================================================*/
#ifndef HLW811X_PIN_EN_SET
    #define HLW811X_PIN_EN_SET(level)   PAout(5) = (level)
#endif
#ifndef HLW811X_PIN_CS_SET
    #define HLW811X_PIN_CS_SET(level)   PAout(6) = (level)
#endif
#ifndef HLW811X_PIN_SCLK_SET
    #define HLW811X_PIN_SCLK_SET(level) PAout(7) = (level)
#endif

/**
 * @brief  防呆检查：确保引脚宏已定义
 * @note   如果既不在 config.h 定义，也不在上方默认定义中覆盖，则报错
 */
#ifndef HLW811X_PIN_EN_SET
    #error "HLW811X: 未定义 HLW811X_PIN_EN_SET！请在 hlw8112_config.h 中定义。"
#endif
#ifndef HLW811X_PIN_CS_SET
    #error "HLW811X: 未定义 HLW811X_PIN_CS_SET！请在 hlw8112_config.h 中定义。"
#endif
#ifndef HLW811X_PIN_SCLK_SET
    #error "HLW811X: 未定义 HLW811X_PIN_SCLK_SET！请在 hlw8112_config.h 中定义。"
#endif

/*============================================================
 * STM32 UART 实例宏 (可被 config.h 中的 HLW811X_UART_INSTANCE 覆盖)
 *============================================================*/
#ifndef HLW811X_UART_INSTANCE
    #define HLW811X_UART_INSTANCE       USART2
#endif

/*============================================================
 * GPIO 控制函数实现 — 使用 config.h 中的宏
 *============================================================*/

void HLW811X_HW_SetEN(u8 level)
{
    HLW811X_PIN_EN_SET((level != 0) ? 1 : 0);
}

void HLW811X_HW_SetCS(u8 level)
{
    HLW811X_PIN_CS_SET((level != 0) ? 1 : 0);
}

void HLW811X_HW_SetSCLK(u8 level)
{
    HLW811X_PIN_SCLK_SET((level != 0) ? 1 : 0);
}

/**
 * @brief  毫秒延时函数
 */
void HLW811X_HW_DelayMs(u16 ms)
{
    delay_ms(ms);
}

/**
 * @brief  调试信息输出函数
 * @note   STM32 平台使用 printf 重定向到串口1
 */
#if HLW811X_LOG_ENABLE
#include <stdarg.h>
#include <stdio.h>
void HLW811X_HW_Print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}
/* #endif /* HLW811X_LOG_ENABLE */

/*============================================================
 * 硬件初始化
 * GPIO 引脚使用 hlw8112_config.h 中的宏
 *============================================================*/

void HLW811X_HW_Init(void)
{
/*     /* STM32: 配置 PA7、PA6、PA5 为推挽输出 (50MHz) */
/*     RCC->APB2ENR |= 1 << 2;             /* 使能 PORTA 时钟 */
    GPIOA->CRL &= 0X000FFFFF;
/*     GPIOA->CRL |= 0X33300000;           /* PA7(SCLK)、PA6(CS)、PA5(EN) 推挽输出 50MHz */
}

/*============================================================
 * UART 发送变量
 *============================================================*/
/* static volatile u8 g_hw_tx_busy = 0;        /* 硬件发送忙标志 */
/* static volatile u8 g_hw_tx_timeout = 0;     /* 硬件发送超时 (1 tick = 2ms) */

/**
 * @brief  UART 发送数据（中断方式，非阻塞）
 * @note   数据通过协议层缓冲区发送，使用 config.h 中的 UART 实例宏
 */
void HLW811X_HW_SendData(const u8 *data, u16 len)
{
    if (len == 0) return;
/*     if (g_hw_tx_busy) return;               /* 上一帧未发完则丢弃 */

/*     /* 协议层已将数据复制到内部 tx_buf，data 指针有效直到发送完成 */
/*     (void)data;                              /* data 指针来自协议层内部 tx_buf */

    g_hw_tx_busy = 1;
/*     g_hw_tx_timeout = 5;                    /* 10ms 超时 (5 × 2ms) */

/*     /* 使能 TXE 中断，中断中逐字节发送 */
/*     /* 读 SR 清除可能残留的标志 */
    (void)HLW811X_UART_INSTANCE->SR;
    HLW811X_UART_INSTANCE->CR1 |= USART_CR1_TXEIE;
}

/**
 * @brief  查询 UART 发送是否忙
 */
u8 HLW811X_HW_IsTxBusy(void)
{
    return g_hw_tx_busy;
}

/**
 * @brief  清空接收缓冲区
 * @note   委托协议层清空内部缓冲区
 */
void HLW811X_HW_ClearRxBuf(void)
{
/*     /* 协议层已提供 HLW811X_Proto_ClearRxBuf() */
/*     /* 此函数保留供兼容调用，实际委托给了协议层 */
}

/*============================================================
 * USART2 中断服务函数
 *   发送: 从 g_hlw811x_tx_buf[] 逐字节读取
 *   接收: 写入 g_hlw811x_rx_buf[]，操作 g_hlw811x_rx_idx / g_hlw811x_rx_done
 *============================================================*/

void USART2_IRQHandler(void)
{
    unsigned long sr = HLW811X_UART_INSTANCE->SR;

/*     /* 检查并清除错误标志 (PE|FE|NE|ORE) — 读取 DR 自动清除 */
    if (sr & (USART_SR_PE | USART_SR_FE | USART_SR_NE | USART_SR_ORE))
    {
        (void)HLW811X_UART_INSTANCE->DR;
    }

    /*-------------------- TXE 发送中断处理 --------------------*/
    if ((sr & USART_SR_TXE) && (HLW811X_UART_INSTANCE->CR1 & USART_CR1_TXEIE))
    {
        static u8 s_tx_idx = 0;
        static u8 s_tx_total = 0;

        if (g_hw_tx_busy && s_tx_idx == 0 && s_tx_total == 0) {
            s_tx_total = g_hlw811x_tx_len;
            s_tx_idx = 0;
        }

        if (s_tx_idx < s_tx_total)
        {
            HLW811X_UART_INSTANCE->DR = g_hlw811x_tx_buf[s_tx_idx];
            s_tx_idx++;
        }
        else
        {
            HLW811X_UART_INSTANCE->CR1 &= ~USART_CR1_TXEIE;
            g_hw_tx_busy = 0;
            g_hw_tx_timeout = 0;
            s_tx_idx = 0;
            s_tx_total = 0;
/*             g_hlw811x_tx_busy = 0;          /* 通知协议层发送完成 */
            g_hlw811x_tx_timeout = 0;
        }
    }

    /*-------------------- RXNE 接收中断处理 --------------------*/
    if (sr & USART_SR_RXNE)
    {
        unsigned char data = HLW811X_UART_INSTANCE->DR;

        if (g_hlw811x_rx_idx < g_hlw811x_rx_len)
        {
            g_hlw811x_rx_buf[g_hlw811x_rx_idx] = data;
            g_hlw811x_rx_idx++;
            g_hlw811x_rx_ing = 1;

            if (g_hlw811x_rx_idx >= g_hlw811x_rx_len)
            {
/*                 g_hlw811x_rx_done = 1;      /* 接收完成 */
            }
        }
        else
        {
            g_hlw811x_rx_idx = 0;
        }
    }
}

/*============================================================
 * 发送超时处理函数
 *============================================================*/

void HLW811X_HW_TxTimeoutIRQ(void)
{
    if (g_hw_tx_timeout > 0) {
        g_hw_tx_timeout--;
        if (g_hw_tx_timeout == 0 && g_hw_tx_busy) {
            HLW811X_UART_INSTANCE->CR1 &= ~USART_CR1_TXEIE;
            g_hw_tx_busy = 0;
            g_hlw811x_tx_busy = 0;
            g_hlw811x_tx_timeout = 0;
#if HLW811X_LOG_ENABLE
            HLW811X_HW_Print("[HLW811X] TX timeout! Force abort\r\n");
#endif
        }
    }
}
