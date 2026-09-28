/**
 * @file    bsp_uart.h
 * @brief   UART 外设驱动接口
 *
 * 每个串口拥有独立的 MX_USARTx_UART_Init() 函数, 也可通过
 * bsp_uart_init(port, cfg) 统一入口初始化.
 *
 * RX: DMA+IDLE → 回调 (ISR 上下文, 上层自行推入 ringbuf)
 * TX: 阻塞 DMA (HAL_UART_Transmit)
 *
 * TX-only 端口: rx_buf=NULL, rx_cb=NULL 即可跳过 RX 配置.
 */
#ifndef __BSP_UART_H__
#define __BSP_UART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

/* =====================================================================
 *  端口枚举
 * ===================================================================== */
typedef enum {
    BSP_UART_1 = 0,   /* USART1 -> 调试/log */
    BSP_UART_2,        /* USART2 -> HLW8110 U8 输入 */
    BSP_UART_3,        /* USART3 -> HLW8110 U9 输出 */
    BSP_UART_4,        /* UART4  -> 串口屏 DWIN */
    BSP_UART_6,        /* USART6 -> 蓝牙 PB-03F */
    BSP_UART_MAX
} bsp_uart_port_t;

/* =====================================================================
 *  RX 回调类型 (IDLE 触发, ISR 上下文)
 * ===================================================================== */
typedef void (*bsp_uart_rx_cb_t)(bsp_uart_port_t port, uint8_t *buf, uint16_t len);

/* =====================================================================
 *  数据格式枚举
 * ===================================================================== */
typedef enum {
    BSP_UART_DATA_7B = 7,
    BSP_UART_DATA_8B = 8,
    BSP_UART_DATA_9B = 9,
} bsp_uart_data_bits_t;

typedef enum {
    BSP_UART_PARITY_NONE = 0,
    BSP_UART_PARITY_EVEN,
    BSP_UART_PARITY_ODD,
} bsp_uart_parity_t;

typedef enum {
    BSP_UART_STOPBITS_1 = 0,
    BSP_UART_STOPBITS_2,
} bsp_uart_stopbits_t;

/**
 * @brief  UART 配置参数
 * @note   TX-only: rx_buf=NULL, rx_buf_size=0, rx_cb=NULL
 */
typedef struct {
    uint32_t  baudrate;           /**< 波特率 */
    bsp_uart_data_bits_t data_bits;/**< 数据位: 7/8/9 (init 自动根据 parity 计算 HAL WordLength) */
    bsp_uart_parity_t   parity;   /**< 校验: NONE/EVEN/ODD */
    bsp_uart_stopbits_t stop_bits;/**< 停止位: 1 或 2 */
    bsp_uart_rx_cb_t rx_cb;       /**< 接收回调函数 (IDLE 触发, ISR 上下文; TX-only 时为 NULL) */
    uint8_t  *rx_buf;             /**< RX DMA 缓冲区 (TX-only 时为 NULL) */
    uint16_t  rx_buf_size;        /**< RX 缓冲区大小 */
} bsp_uart_config_t;



extern bool bsp_uart_init(bsp_uart_port_t port, const bsp_uart_config_t *cfg);

extern bool bsp_uart_deinit(bsp_uart_port_t port);

extern bool bsp_uart_send(bsp_uart_port_t port, const uint8_t *data,uint16_t len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_UART_H__ */
