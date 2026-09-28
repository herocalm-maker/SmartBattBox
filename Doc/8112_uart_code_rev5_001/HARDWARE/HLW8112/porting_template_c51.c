/**
 * @file    hlw8112_hw_c51.c
 * @brief   HLW8112/HLW8110 硬件抽象层实现 — Keil C51 平台模板
 * @details 此为 C51 平台的移植模板，工程师需根据实际硬件连接修改此文件。
 *          将此文件重命名为 hlw8112_hw.c 放入工程即可。
 *
 *          假设硬件连接：
 *            EN   → P1^0
 *            CS   → P1^1
 *            SCLK → P1^2
 *            UART → 使用串口1 (TXD=P3^1, RXD=P3^0)
 *
 * @note    重构后（2026-06-05）：缓冲区为全局变量（定义在 hlw8112_protocol.c），
 *          HW层 ISR 直接操作 g_hlw811x_rx_buf / g_hlw811x_rx_idx / g_hlw811x_rx_done。
 * @note    C51 平台特点：
 *          - 无 <stdint.h>，类型由 hlw8112_port.h 自动适配
 *          - int = 16bit，注意类型转换
 *          - float 为软件模拟，代码体积较大
 * @version V4.0 适配重构架构
 * @date    2026-06-05
 */

#include "hlw8112_hw.h"
/* #include "hlw8112_protocol.h"       /* g_hlw811x_rx_buf, g_hlw811x_rx_len, g_hlw811x_rx_idx, g_hlw811x_rx_done */

/*============================================================
 * C51 平台头文件
 *============================================================*/
/* #include <reg52.h>          /* C51 寄存器定义，根据芯片型号调整 */
/* #include <intrins.h>        /* _nop_() 等内联函数 */
/* #include <stdio.h>          /* 仅当使用 printf 时需要 */

/*============================================================
 * C51 本地 GPIO 引脚定义 (sbit 位寻址)
 *============================================================*/
sbit IO_HLW8112_EN   = P1^0;
sbit IO_HLW8112_CS   = P1^1;
sbit IO_HLW8112_SCLK = P1^2;

/*============================================================
 * GPIO 控制函数实现
 *============================================================*/

void HLW811X_HW_SetEN(u8 level)
{
    IO_HLW8112_EN = (level != 0) ? 1 : 0;
}

void HLW811X_HW_SetCS(u8 level)
{
    IO_HLW8112_CS = (level != 0) ? 1 : 0;
}

void HLW811X_HW_SetSCLK(u8 level)
{
    IO_HLW8112_SCLK = (level != 0) ? 1 : 0;
}

/*============================================================
 * 延时函数实现 (依赖于晶振频率)
 *============================================================*/

void HLW811X_HW_DelayMs(u16 ms)
{
    u16 i, j;
    for (i = 0; i < ms; i++)
    {
        for (j = 0; j < 120; j++)
        {
            _nop_();
        }
    }
}

/*============================================================
 * 调试输出函数 (C51 平台)
 *============================================================*/

#if HLW811X_LOG_ENABLE

void HLW811X_HW_Print(const char *fmt, ...)
{
#if 0
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
#else
    const char *p = fmt;
    while (*p)
    {
        while (!TI);
        TI = 0;
        SBUF = *p++;
    }
#endif
}

/* #endif /* HLW811X_LOG_ENABLE */

/*============================================================
 * 硬件初始化
 *============================================================*/

void HLW811X_HW_Init(void)
{
/*     /* 设置 GPIO 为推挽输出 */
/*     /* P1^0, P1^1, P1^2 默认上电为准双向口，可直接使用 */

/*     /* 初始化 UART (方式1: 8位UART, 9600bps) */
/*     SCON = 0x50;    /* 方式1, 8位UART, 允许接收 */
/*     TMOD = 0x20;    /* 定时器1, 方式2, 8位自动重装 */
/*     TH1  = 0xFD;    /* 9600bps @ 12MHz */
    TL1  = 0xFD;
    TR1  = 1;

    ES = 1;
    EA = 1;
}

/*============================================================
 * UART 数据收发
 *============================================================*/

void HLW811X_HW_SendData(const u8 *data, u16 len)
{
    u16 i;
    for (i = 0; i < len; i++)
    {
        SBUF = data[i];
        while (!TI);
        TI = 0;
    }
}

void HLW811X_HW_ClearRxBuf(void)
{
/*     /* 协议层 HLW811X_Proto_ClearRxBuf() 管理软件缓冲区 */
/*     /* 硬件层无需额外操作 */
}

u8 HLW811X_HW_IsTxBusy(void)
{
/*     return 0;   /* C51 查询模式发送，无需忙标志 */
}

/*============================================================
 * C51 串口中断服务函数 — 直接操作全局缓冲区
 *============================================================*/

void UART_ISR(void) interrupt 4
{
    if (RI)
    {
        RI = 0;
        {
            if (g_hlw811x_rx_idx < g_hlw811x_rx_len)
            {
                g_hlw811x_rx_buf[g_hlw811x_rx_idx] = SBUF;
                g_hlw811x_rx_idx++;
                g_hlw811x_rx_ing = 1;

                if (g_hlw811x_rx_idx >= g_hlw811x_rx_len)
                {
                    g_hlw811x_rx_done = 1;
                }
            }
            else
            {
                g_hlw811x_rx_idx = 0;
            }
        }
    }

    if (TI)
    {
        TI = 0;
    }
}

/*============================================================
 * 发送超时处理
 *============================================================*/

void HLW811X_HW_TxTimeoutIRQ(void)
{
/*     /* C51 查询发送模式无需超时管理 */
}
