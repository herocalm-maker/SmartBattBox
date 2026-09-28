/**
 * @file    hlw8112_protocol.c
 * @brief   HLW8112/HLW8110 UART 协议层实现（全局变量方案）
 * @details 缓冲区用全局变量（非 static），hw.c ISR 通过 extern 直接访问。
 *          操作这些变量的函数封装了命令帧拼装和校验和计算。
 *
 * @version V1.1 全局变量方案
 * @date    2026-06-05
 */

#include "hlw8112_protocol.h"
#include "hlw8112_regs.h"
/* #include "hlw8112_hw.h"         /* HLW811X_HW_SendData */

/*============================================================
 * 全局缓冲区变量定义
 *============================================================*/
u8  g_hlw811x_tx_buf[10];
u8  g_hlw811x_rx_buf[10];
u8  g_hlw811x_tx_len;
u8  g_hlw811x_rx_len;
u8  g_hlw811x_rx_idx;
volatile u8  g_hlw811x_rx_done;
volatile u8  g_hlw811x_rx_ing;
volatile u8  g_hlw811x_tx_busy;
volatile u8  g_hlw811x_tx_timeout;

/*============================================================
 * 校验和计算
 *============================================================*/

u8 HLW811X_Proto_CheckSum_Write(const u8 *tx_buf, u8 tx_len)
{
    u8 i, cs = 0;
    for (i = 0; i < (tx_len - 1); i++) cs += tx_buf[i];
    return ~cs;
}

u8 HLW811X_Proto_CheckSum_Read(const u8 *tx_buf, const u8 *rx_buf, u8 rx_len)
{
    u8 i, cs = 0;
    for (i = 0; i < (rx_len - 1); i++) cs += rx_buf[i];
    cs = cs + tx_buf[0] + tx_buf[1];
    return ~cs;
}

/*============================================================
 * 缓冲区操作
 *============================================================*/

void HLW811X_Proto_ClearRxBuf(void)
{
    u8 i;
    for (i = 0; i < 10; i++) g_hlw811x_rx_buf[i] = 0;
    g_hlw811x_rx_idx = 0;
    g_hlw811x_rx_done = 0;
    g_hlw811x_rx_ing = 0;
    HLW811X_HW_ClearRxBuf();
}

void HLW811X_Proto_ClearTxBuf(void)
{
    u8 i;
    for (i = 0; i < 10; i++) g_hlw811x_tx_buf[i] = 0;
    g_hlw811x_tx_len = 0;
    g_hlw811x_tx_busy = 0;
    g_hlw811x_tx_timeout = 0;
}

u8 HLW811X_Proto_IsTxBusy(void)
{
    return g_hlw811x_tx_busy;
}

/*============================================================
 * 命令函数
 *============================================================*/

void HLW811X_Proto_WriteRegEN(void)
{
    g_hlw811x_tx_buf[0] = HLW811X_CMD_PREAMBLE;
    g_hlw811x_tx_buf[1] = HLW811X_CMD_OP_WRITE_EN;
    g_hlw811x_tx_buf[2] = HLW811X_CMD_SUB_ENABLE;
    g_hlw811x_tx_buf[3] = HLW811X_Proto_CheckSum_Write(g_hlw811x_tx_buf, 4);
    g_hlw811x_tx_len = 4;
    g_hlw811x_rx_len = 0;
    g_hlw811x_tx_busy = 1;
    HLW811X_HW_SendData(g_hlw811x_tx_buf, g_hlw811x_tx_len);
}

void HLW811X_Proto_WriteRegDIS(void)
{
    g_hlw811x_tx_buf[0] = HLW811X_CMD_PREAMBLE;
    g_hlw811x_tx_buf[1] = HLW811X_CMD_OP_WRITE_EN;
    g_hlw811x_tx_buf[2] = HLW811X_CMD_SUB_DISABLE;
    g_hlw811x_tx_buf[3] = HLW811X_Proto_CheckSum_Write(g_hlw811x_tx_buf, 4);
    g_hlw811x_tx_len = 4;
    g_hlw811x_rx_len = 0;
    g_hlw811x_tx_busy = 1;
    HLW811X_HW_SendData(g_hlw811x_tx_buf, g_hlw811x_tx_len);
}

void HLW811X_Proto_SetChannelA(u8 channel)
{
    g_hlw811x_tx_buf[0] = HLW811X_CMD_PREAMBLE;
    g_hlw811x_tx_buf[1] = HLW811X_CMD_OP_WRITE_EN;
    g_hlw811x_tx_buf[2] = (channel == 0) ? HLW811X_CMD_SUB_CH_A : HLW811X_CMD_SUB_CH_B;
    g_hlw811x_tx_buf[3] = HLW811X_Proto_CheckSum_Write(g_hlw811x_tx_buf, 4);
    g_hlw811x_tx_len = 4;
    g_hlw811x_rx_len = 0;
    g_hlw811x_tx_busy = 1;
    HLW811X_HW_SendData(g_hlw811x_tx_buf, g_hlw811x_tx_len);
}

void HLW811X_Proto_ReadReg(u8 reg_addr, u8 reg_len)
{
    HLW811X_Proto_ClearRxBuf();
    g_hlw811x_tx_buf[0] = HLW811X_CMD_PREAMBLE;
    g_hlw811x_tx_buf[1] = reg_addr;
    g_hlw811x_tx_len = 2;
/*     g_hlw811x_rx_len = reg_len + 1;       /* +1 for checksum */
    g_hlw811x_tx_busy = 1;
    HLW811X_HW_SendData(g_hlw811x_tx_buf, g_hlw811x_tx_len);
}

void HLW811X_Proto_WriteReg(u8 reg_addr, u8 reg_len, u32 data)
{
    u8 i;
    g_hlw811x_tx_buf[0] = HLW811X_CMD_PREAMBLE;
    g_hlw811x_tx_buf[1] = reg_addr | HLW811X_CMD_READ_MASK;
    for (i = 0; i < reg_len; i++) {
        g_hlw811x_tx_buf[i + 2] = (u8)((data >> ((reg_len - 1 - i) * 8)) & 0xFF);
    }
    g_hlw811x_tx_len = 3 + reg_len;
    g_hlw811x_rx_len = 0;
    g_hlw811x_tx_buf[g_hlw811x_tx_len - 1] = HLW811X_Proto_CheckSum_Write(g_hlw811x_tx_buf, g_hlw811x_tx_len);
    g_hlw811x_tx_busy = 1;
    HLW811X_HW_SendData(g_hlw811x_tx_buf, g_hlw811x_tx_len);
}

void HLW811X_Proto_Reset(void)
{
    g_hlw811x_tx_buf[0] = HLW811X_CMD_PREAMBLE;
    g_hlw811x_tx_buf[1] = HLW811X_CMD_OP_WRITE_EN;
    g_hlw811x_tx_buf[2] = HLW811X_CMD_SUB_RESET;
    g_hlw811x_tx_buf[3] = HLW811X_Proto_CheckSum_Write(g_hlw811x_tx_buf, 4);
    g_hlw811x_tx_len = 4;
    g_hlw811x_rx_len = 0;
    g_hlw811x_tx_busy = 1;
    HLW811X_HW_SendData(g_hlw811x_tx_buf, g_hlw811x_tx_len);
}

/*============================================================
 * 接收状态与数据读取
 *============================================================*/

u8 HLW811X_Proto_IsRxDone(void)
{
    return g_hlw811x_rx_done;
}

u8 HLW811X_Proto_GetRxData(u8 *out_buf, u8 reg_len)
{
    u8 cs_calc;
    u8 rx_total_len = reg_len + 1;

    if (g_hlw811x_rx_len != rx_total_len) {
        HLW811X_Proto_ClearRxBuf();
        return 0;
    }
    cs_calc = HLW811X_Proto_CheckSum_Read(g_hlw811x_tx_buf, g_hlw811x_rx_buf, rx_total_len);
    if (g_hlw811x_rx_buf[rx_total_len - 1] != cs_calc) {
        HLW811X_Proto_ClearRxBuf();
        return 0;
    }
    {
        u8 i;
        for (i = 0; i < reg_len; i++) out_buf[i] = g_hlw811x_rx_buf[i];
    }
    HLW811X_Proto_ClearRxBuf();
    return 1;
}
