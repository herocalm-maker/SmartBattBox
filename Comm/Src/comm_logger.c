/**
 * @file    comm_logger.c
 * @brief   日志通信 (Comm 层)
 */

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include "bsp_uart.h"
#include "comm_logger.h"
#include "comm_ringbuf.h"


#define MAX_DBG_NUM            3
#define DEBUG_BUFF_LEN         256

/* UART1 RX 环形缓冲 (ISR 推入, 任务弹出) */
#define LOGGER_RX_BUF_SIZE     256

static volatile uint8_t dbg_num = 0;
static uint8_t s_log_buff[MAX_DBG_NUM][DEBUG_BUFF_LEN] = {0};
/* USER CODE BEGIN 0 */
static uint8_t s_uart1_rx_buf[256] = {0};  /* UART1 RX DMA 缓冲区 (bsp_uart 层) */
static uint8_t s_logger_rx_storage[LOGGER_RX_BUF_SIZE];  /* 应用层 RX 环形缓冲 */
static comm_ringbuf_t s_logger_rx_ring;
static bool s_logger_ready = false;




/* 日志级别前缀 */
static const char *s_level_prefix[] = {
  "",           /* LOG_NONE */
  "[Verbose]:", /* LOG_VERBOSE */
  "[Debug]:",   /* LOG_DEBUG */
  "[Info]:",    /* LOG_INFO */
  "[Warn]:",    /* LOG_WARN */
  "[Error]:"    /* LOG_ERROR */
};

static log_level_enum s_log_level = LOG_INFO;




/*********************************************************************
 * @fn      Debug_Uart_Send
 * @brief   调试串口发送数据
 * @param   pdata   数据指针
 * @param   nbyte   数据长度
 * @return  bool    true=成功
 ********************************************************************/
static bool Debug_Uart_Send(uint8_t *pdata, uint16_t nbyte)
{
  /* 统一走 UART1, 30 tick 发送超时上限, 防止日志阻塞调用任务 */
  return bsp_uart_send(BSP_UART_1, pdata, nbyte, 30u);
}

/*====================================================================
 * UART1 RX 回调 (ISR 上下文)
 *====================================================================*/

/**
 * @brief  UART1 RX 回调 (ISR 上下文)
 * @note   由 bsp_uart IDLE 中断触发, 将接收数据推入环形缓冲.
 *         单生产者 (ISR) / 单消费者 (AppMainTask), 不关中断.
 * @param  port  UART 端口号
 * @param  buf   接收数据指针
 * @param  len   接收数据长度
 */
static void logger_uart1_rx_cb(bsp_uart_port_t port, uint8_t *buf, uint16_t len)
{
    (void)port;
    if (!s_logger_ready)
    {
        return;
    }
    comm_ringbuf_push(&s_logger_rx_ring, buf, len);
}

/*********************************************************************
 * @fn      logger_rx_read
 * @brief   从 UART1 RX 缓冲读取数据 (非阻塞)
 * @param   buf  输出缓冲区
 * @param   len  期望读取长度
 * @return  uint16_t 实际读取字节数
 ********************************************************************/
uint16_t logger_rx_read(uint8_t *buf, uint16_t len)
{
    if (!s_logger_ready || buf == NULL || len == 0)
    {
        return 0;
    }
    return comm_ringbuf_pop(&s_logger_rx_ring, buf, len);
}

/*********************************************************************
 * @fn      logger_rx_available
 * @brief   查询 UART1 RX 缓冲当前数据字节数
 * @return  uint16_t 可用字节数
 ********************************************************************/
uint16_t logger_rx_available(void)
{
    if (!s_logger_ready)
    {
        return 0;
    }
    return comm_ringbuf_size(&s_logger_rx_ring);
}

/*********************************************************************
 * @fn      logger_out
 * @brief   带级别前缀的日志输出
 * @param   lv    日志级别
 * @param   fmt   格式化字符串
 * @return  void
 ********************************************************************/
void logger_out(log_level_enum lv, const char *fmt, ...)
{
  va_list args;
  int len = 0, titlelen = 0;
  uint8_t index = 0;
  const char *prefix;

  /* 级别过滤 */
  /* LOG_NONE 视为强制输出, 不参与级别比较; 低于当前全局级别的日志直接丢弃 */
  if((lv != LOG_NONE)&&(lv < s_log_level))
    return;

  if (fmt == NULL)
    return;

  /* 3 块缓冲轮转: DMA 发送期间的新日志写入其他缓冲, 避免发送中的数据被覆盖 */
  index = dbg_num;
  dbg_num = (dbg_num + 1) % MAX_DBG_NUM;

  prefix = s_level_prefix[lv];
  titlelen = strlen(prefix);

  memset(&s_log_buff[index][0], 0, DEBUG_BUFF_LEN);
  memcpy(&s_log_buff[index][0], prefix, titlelen);
  len = titlelen;

  va_start(args, fmt);
  /* 格式化内容接在前缀之后; 长度预留 1 字节保证 NUL 结尾, 超长由 vsnprintf 截断 */
  len += vsnprintf((char *)&s_log_buff[index][0] + titlelen,
                   DEBUG_BUFF_LEN - titlelen - 1, fmt, args);
  va_end(args);

  /* 追加换行 */
  if (len < DEBUG_BUFF_LEN - 3) {
    s_log_buff[index][len++] = '\r';
    s_log_buff[index][len++] = '\n';
    s_log_buff[index][len] = 0;
  }

  Debug_Uart_Send(&s_log_buff[index][0], len);
}


/*********************************************************************
 * @fn      logger_init
 * @brief   初始化日志模块 (设置默认级别, 注册 UART1 RX 回调)
 * @return  void
 ********************************************************************/
void logger_init(uint32_t baudrate)
{
  bsp_uart_config_t uart1_cfg = {0};

  /* 初始化 RX 环形缓冲 */
  comm_ringbuf_init(&s_logger_rx_ring, s_logger_rx_storage, LOGGER_RX_BUF_SIZE);
  /* 先置就绪再注册回调 (bsp_uart_init 内完成注册), 避免初始化期间到达的 RX 数据被回调丢弃 */
  s_logger_ready = true;

  uart1_cfg.baudrate    = baudrate;
  uart1_cfg.rx_cb       = logger_uart1_rx_cb;  /* 注册 RX 回调 */
  uart1_cfg.rx_buf      = s_uart1_rx_buf;
  uart1_cfg.rx_buf_size = sizeof(s_uart1_rx_buf);
  bsp_uart_init(BSP_UART_1, &uart1_cfg);

  s_log_level = LOG_INFO;  /* 默认全局级别: INFO (Verbose/Debug 被过滤) */
}
