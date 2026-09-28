/**
 * @file    hlw8112_protocol.h
 * @brief   HLW8112/HLW8110 UART 协议层接口
 * @details 封装 UART 命令帧拼装、校验和计算、缓冲区管理。
 *          缓冲区变量为全局 extern，供 hlw8112_hw.c 的 ISR 直接访问。
 *
 * 全局变量:
 *   g_hlw811x_tx_buf[10] — 发送缓冲区
 *   g_hlw811x_rx_buf[10] — 接收缓冲区（ISR 写入）
 *   g_hlw811x_tx_len      — 发送数据长度
 *   g_hlw811x_rx_len      — 期望接收长度（含校验和）
 *   g_hlw811x_rx_idx      — 接收索引（ISR 维护）
 *   g_hlw811x_rx_done     — 接收完成标志（ISR 置 1）
 *   g_hlw811x_tx_timeout  — 发送超时计数器
 *
 * @version V1.1 全局变量方案
 * @date    2026-06-05
 */

#ifndef __HLW8112_PROTOCOL_H
#define __HLW8112_PROTOCOL_H

#include "hlw8112_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================
 * 全局缓冲区变量 extern（定义在 hlw8112_protocol.c）
 *============================================================*/
extern u8  g_hlw811x_tx_buf[10];
extern u8  g_hlw811x_rx_buf[10];
extern u8  g_hlw811x_tx_len;
extern u8  g_hlw811x_rx_len;
extern u8  g_hlw811x_rx_idx;
extern volatile u8  g_hlw811x_rx_done;
extern volatile u8  g_hlw811x_rx_ing;
extern volatile u8  g_hlw811x_tx_busy;
extern volatile u8  g_hlw811x_tx_timeout;

/*============================================================
 * 校验和计算
 *============================================================*/
u8 HLW811X_Proto_CheckSum_Write(const u8 *tx_buf, u8 tx_len);
u8 HLW811X_Proto_CheckSum_Read(const u8 *tx_buf, const u8 *rx_buf, u8 rx_len);

/*============================================================
 * 协议函数（操作全局缓冲区）
 *============================================================*/
void HLW811X_Proto_ClearRxBuf(void);
void HLW811X_Proto_WriteRegEN(void);
void HLW811X_Proto_WriteRegDIS(void);
void HLW811X_Proto_SetChannelA(u8 channel);
void HLW811X_Proto_ReadReg(u8 reg_addr, u8 reg_len);
void HLW811X_Proto_WriteReg(u8 reg_addr, u8 reg_len, u32 data);
void HLW811X_Proto_Reset(void);

u8  HLW811X_Proto_IsRxDone(void);
u8  HLW811X_Proto_GetRxData(u8 *out_buf, u8 reg_len);
void HLW811X_Proto_ClearTxBuf(void);
u8  HLW811X_Proto_IsTxBusy(void);

#ifdef __cplusplus
}
#endif

/* #endif /* __HLW8112_PROTOCOL_H */
