/**
 * @file    drv_dwin.h
 * @brief   迪文 T5L 串口屏驱动 (Driver 层)
 *
 * 硬件连接:
 *   UART4_TX = PA0, UART4_RX = PA1  (115200 bps, 经 SP3232E 转 RS232)
 *
 * 协议: DGUS II (5A A5 帧格式)
 *   帧头: 5A A5
 *   长度: 1 byte (指令+数据总字节数)
 *   指令: 82=写寄存器, 83=读寄存器
 *   数据: 大端格式
 *
 * DGUS VP 地址分配:
 *   0084     页面切换 (2 bytes)
 *   1000+    文本/数值变量区
 *
 * 数据流 (参考 drv_hlw8110):
 *   TX: bsp_uart_send(port, frame)  → DWIN 屏
 *   RX: DWIN 屏 (触摸/寄存器回传) → UART IDLE ISR → comm_ringbuf_push → drv_dwin_read 读取
 */
#ifndef __DRV_DWIN_H__
#define __DRV_DWIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include "bsp_uart.h"
#include "comm_ringbuf.h"

/*============================================================================
 * DGUS 帧常量
 *============================================================================*/

#define DWIN_FRAME_HEAD1         0x5A
#define DWIN_FRAME_HEAD2         0xA5
#define DWIN_CMD_WRITE           0x82       /* 写 VP 寄存器 */
#define DWIN_CMD_READ            0x83       /* 读 VP 寄存器 */
#define DWIN_TX_MAX              128        /* 单帧最大发送长度 */
#define DWIN_TIMEOUT_MS          50         /* 发送超时 */
#define DWIN_READ_TIMEOUT_MS     50         /* 读 VP 等待应答超时 */
 
/* DGUS VP 地址定义 */
#define DWIN_VP_PAGE             0x0084     /* 页面切换 */
#define DWIN_VP_TEXT_BASE        0x1000     /* 文本变量起始 */
#define DWIN_VP_VALUE_BASE       0x2000     /* 数值变量起始 */

/* RX 缓冲区尺寸 */
#define DWIN_RX_BUF_SIZE         64         /* UART DMA 单帧接收缓冲 */
#define DWIN_RING_BUF_SIZE       512        /* 触摸/回传数据环形缓冲 */

/*============================================================================
 * 页面号定义
 *   DGUS 的页面号 = bmp 文件名前缀的数字 (0.bmp → 页 0, 1.bmp → 页 1)。
 *   屏端上电默认显示页 0, 因此建议让"空白启动页"占用页 0:
 *     0.bmp = 启动/空白页,  1.bmp = 主界面
 *   若屏端实际是反的(0=主界面), 把下面两个值对调即可, 其它代码不用改。
 *============================================================================*/

#define DWIN_PAGE_BOOT   0u   /* 启动/空白页 (上电默认显示, 遮住控件组态初值) */
#define DWIN_PAGE_MAIN   1u   /* 主界面 (运行界面) */
#define DWIN_PAGE_COUNT  2u   /* 工程页面总数 (仅用于越界保护) */

/*============================================================================
 * 设备实例 (单屏, 驱动内部静态持有)
 *============================================================================*/
typedef struct {
    bsp_uart_port_t     port;                          /* 绑定的 UART 端口 */
    uint8_t             cur_page;                      /* 当前页面 (软件跟踪) */
    uint8_t             tx_buf[DWIN_TX_MAX];           /* 格式化文本暂存 */
    comm_ringbuf_t      rx_ring;                       /* 触摸/回传数据环形缓冲 */
    uint8_t             ring_storage[DWIN_RING_BUF_SIZE];
    uint8_t             uart_rx_buf[DWIN_RX_BUF_SIZE]; /* UART RX DMA 缓冲 */
} dwin_t;

/*============================================================================
 * API
 *============================================================================*/

/* 初始化: 绑定 UART 端口 (含 bsp_uart_init), 背光 80% + 切 boot 页 */
extern bool drv_dwin_init(bsp_uart_port_t port);
/* 反初始化: 释放 UART 端口 */
extern void drv_dwin_deinit(void);
/* 读取触摸/寄存器回传数据 (从环形缓冲弹出, 无数据返回 0) */
extern uint16_t drv_dwin_read(uint8_t *buf, uint16_t len);

/* ---- 页面控制 ---- */
extern void drv_dwin_set_page(uint8_t page);
/* get page */
extern uint8_t drv_dwin_get_page(void);

/* 文本显示 */
/* ---- 文本显示 (vp_addr 为完整 VP 地址) ---- */
extern void drv_dwin_set_text(uint16_t vp_addr, const char *text);
/* 按"1 字符 = 1 word(2 字节)"写 ASCII 文本: 用于编码为 GBK 的文本控件,
 * DGUS 这类控件每个字符占 1 个 word, 高字节为 0, 不足部分补空格。 */
extern void drv_dwin_set_text_word(uint16_t vp_addr, const char *text, uint8_t chars);
/* 同上, 但低字节在前 {code, 0x00} —— 仅用于文本格式探测 */
extern void drv_dwin_set_text_word_le(uint16_t vp_addr, const char *text, uint8_t chars);
/* set text format */
extern void drv_dwin_set_text_format(uint16_t vp_addr, const char *fmt, ...);

/* 数值显示 */
/* ---- 数值显示 (16bit 整数, vp_addr 为完整 VP 地址) ---- */
extern void drv_dwin_set_value(uint16_t vp_addr, int16_t val);
/* set value u16 */
extern void drv_dwin_set_value_u16(uint16_t vp_addr, uint16_t val);

/* ---- 背光 ---- */
extern void drv_dwin_set_brightness(uint8_t percent);

/* ---- 原始写 VP 寄存器 ---- */
extern void drv_dwin_write_vp(uint16_t vp_addr, const uint8_t *data, uint16_t len);

/**
 * @brief 读 VP 寄存器 (指令 83), 同步等待屏端应答。
 * @param vp_addr 变量地址
 * @param count   读取字数 1~16
 * @param out     出参: count 个 16bit 值
 * @return true=成功; false=超时/未初始化/帧格式错误
 * @note  阻塞至多 DWIN_READ_TIMEOUT_MS, 仅供调试/自检, 不得用于周期刷新路径。
 */
extern bool drv_dwin_read_vp(uint16_t vp_addr, uint8_t count, uint16_t *out);

/* 帧结束标记 */
/* ---- 帧结束标记 (部分控件需要 FF FF FF 结束) ---- */
extern void drv_dwin_send_end(void);

#ifdef __cplusplus
}
#endif
#endif /* __DRV_DWIN_H__ */
