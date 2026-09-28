/**
 * @file    drv_pb03f.h
 * @brief   PB-03F BLE 模块驱动 (Driver 层)
 *
 * 硬件连接:
 *   UART6_TX = PC6, UART6_RX = PC7  (115200, 无 DMA)
 *   BLE_WKUP = PA2  (高电平唤醒, 低电平待机)
 *   PB4 连接状态检测 (WAKEUP IO 复用)
 *
 * 参考: Doc/pb系列模块_alimesh_at指令手册v0.1.pdf
 *
 * AT 指令框架 (参考 AppLora 模式):
 *   - at_send_command() 发送AT指令并等待响应
 *   - URC 回调表 + at_oob_match() 分发异步事件
 *   - Ring buffer 缓存 UART RX 数据
 *
 * 工作模式:
 *   AT_MODE:   发送 AT 指令控制模块 (配置/查询)
 *   TRANSPARENT: 蓝牙连接后自动进入透传, 数据透明收发
 */
#ifndef __DRV_PB03F_H__
#define __DRV_PB03F_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "bsp_uart.h"          /* UART_HandleTypeDef (BSP RX 回调签名) */

/*============================================================================
 * 配置常量
 *============================================================================*/

#define BLE_RX_BUF_SIZE        512u   /* 透传 RX 缓冲 */
#define BLE_AT_RESP_SIZE       256u   /* AT 响应缓冲 */
#define BLE_AT_TIMEOUT_MS      2000u  /* AT 指令默认超时 */
#define BLE_NAME_MAX_LEN       16u    /* 蓝牙名称最大长度 */
#define BLE_MAC_STR_LEN        12u    /* MAC 地址字符串长度 (无冒号) */
#define BLE_CMD_BUF_SIZE       128u   /* AT 指令构建缓冲 */

/*============================================================================
 * 枚举定义
 *============================================================================*/

typedef enum {
    BLE_MODE_AT          = 0,
    BLE_MODE_TRANSPARENT = 1,
} ble_mode_enum;

typedef enum {
    BLE_STATE_DISCONNECTED = 0,
    BLE_STATE_CONNECTED    = 1,
} ble_state_enum;

typedef enum {
    BLE_PKT_NONE     = 0,
    BLE_PKT_RAW      = 1,
    BLE_PKT_ALIGENIE = 2,
} ble_pkt_type_enum;

typedef enum {
    AT_RESULT_NONE    = 0,
    AT_RESULT_OK      = 1,
    AT_RESULT_FAIL    = 2,
    AT_RESULT_ERROR   = 3,
    AT_RESULT_TIMEOUT = 4,
} at_result_enum;

/*============================================================================
 * URC 回调类型
 *============================================================================*/

struct ble_info;
typedef int (*ble_urc_cb_t)(struct ble_info *info, uint8_t *data, uint16_t len);

typedef struct {
     const char   *prefix;     /* URC 前缀字符串 */
     ble_urc_cb_t  cb_func;   /* 回调函数 */
} ble_urc_item_t;

/*============================================================================
 * BLE 模块信息结构 (不透明句柄的内部定义)
 *============================================================================*/

typedef struct ble_info {
    /* RX 缓冲 */
    uint8_t  rx_buf[BLE_RX_BUF_SIZE];
    volatile uint16_t rx_len;
    volatile bool     rx_ready;

    /* AT 响应缓冲 */
    uint8_t  at_resp[BLE_AT_RESP_SIZE];
    volatile uint16_t at_resp_len;
    volatile bool     at_resp_done;

    /* AT 指令构建缓冲 */
    char     cmd_buf[BLE_CMD_BUF_SIZE];

    /* 工作模式 */
    volatile ble_mode_enum mode;

    /* 连接状态 (通过 WAKEUP IO 检测) */
    volatile ble_state_enum conn_state;

    /* FreeRTOS 信号量 */
    void *rx_sem;      /* 透传 RX 完成信号量 */
    void *at_sem;      /* AT 响应完成信号量 */

    /* 配置进度 */
    uint8_t  cfg_step;
    uint8_t  cfg_retry;
    uint32_t cfg_waittick;
} ble_info_t;

/*============================================================================
 * API 函数声明
 *============================================================================*/

/* ---- 生命周期 ---- */
extern void drv_pb03f_init(void);
/* drv_pb03f_deinit */
extern void drv_pb03f_deinit(void);

/* ---- 电源/唤醒 ---- */
extern void drv_pb03f_wakeup(void);
/* sleep */
extern void drv_pb03f_sleep(void);

/* ---- 模式切换 ---- */
extern ble_mode_enum drv_pb03f_get_mode(void);
/* enter transparent */
extern bool drv_pb03f_enter_transparent(void);
/* exit transparent */
extern bool drv_pb03f_exit_transparent(void);

/* ---- 数据收发 ---- */
extern bool     drv_pb03f_send(const uint8_t *data, uint16_t len);
/* start rx */
extern void     drv_pb03f_start_rx(void);
/* take rx */
extern uint16_t drv_pb03f_take_rx(uint8_t *out, uint16_t maxlen);
/* wait rx */
extern bool     drv_pb03f_wait_rx(uint32_t timeout_ms);

/* 回调 */
/* ---- RX 回调 (中断上下文) ---- */
extern void drv_pb03f_on_rx_done(uint16_t len);

/* ---- 包类型判断 ---- */
extern ble_pkt_type_enum drv_pb03f_pkt_type(const uint8_t *buf, uint16_t len);
uint16_t drv_pb03f_extract_aligenie(const uint8_t *buf, uint16_t len,
                                    char *out, uint16_t maxlen);

/* 指令 */
/* ---- AT 指令 (基础) ---- */
extern bool drv_pb03f_at_test(void);
/* reset */
extern bool drv_pb03f_reset(void);
/* restore */
extern bool drv_pb03f_restore(void);

/* 指令 */
/* ---- AT 指令 (查询) ---- */
extern bool drv_pb03f_get_version(char *out, uint16_t maxlen);
/* get name */
extern bool drv_pb03f_get_name(char *out, uint16_t maxlen);
/* get mac */
extern bool drv_pb03f_get_mac(char *out, uint16_t maxlen);
/* get state */
extern bool drv_pb03f_get_state(ble_state_enum *out);

/* 指令 */
/* ---- AT 指令 (设置) ---- */
extern bool drv_pb03f_set_name(const char *name);
/* set uart baud */
extern bool drv_pb03f_set_uart_baud(uint32_t baud);
/* set aligenie */
extern bool drv_pb03f_set_aligenie(const char *pid, const char *mac, const char *secret);

/* ---- 调试 ---- */
extern uint16_t drv_pb03f_take_at_resp(uint8_t *out, uint16_t maxlen);
/* at on rx event */
extern void drv_pb03f_at_on_rx_event(uint16_t len);

/* ---- 连接状态 ---- */
extern ble_state_enum drv_pb03f_get_conn_state(void);

/* 内部使用 */
/* ---- 内部使用 (BSP RX 回调) ---- */
extern void drv_pb03f_bsp_rx_cb(bsp_uart_port_t port, uint8_t *buff, uint16_t len);

#ifdef __cplusplus
}
#endif
#endif /* __DRV_PB03F_H__ */
