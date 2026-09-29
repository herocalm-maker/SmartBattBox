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


#define MAX_DBG_NUM            3
#define DEBUG_BUFF_LEN         256

static volatile uint8_t dbg_num = 0;
static uint8_t s_log_buff[MAX_DBG_NUM][DEBUG_BUFF_LEN] = {0};
/* USER CODE BEGIN 0 */
static uint8_t s_uart1_rx_buf[256] = {0};  /* UART1 RX 缓冲区 */




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
  return bsp_uart_send(BSP_UART_1, pdata, nbyte, 30u);
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
  if((lv != LOG_NONE)&&(lv < s_log_level))
    return;

  if (fmt == NULL)
    return;

  index = dbg_num;
  dbg_num = (dbg_num + 1) % MAX_DBG_NUM;

  prefix = s_level_prefix[lv];
  titlelen = strlen(prefix);

  memset(&s_log_buff[index][0], 0, DEBUG_BUFF_LEN);
  memcpy(&s_log_buff[index][0], prefix, titlelen);
  len = titlelen;

  va_start(args, fmt);
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
 * @brief   初始化日志模块 (设置默认级别)
 * @return  void
 ********************************************************************/
void logger_init(uint32_t baudrate)
{
  bsp_uart_config_t uart1_cfg = {0};

  uart1_cfg.baudrate    = baudrate;
  uart1_cfg.rx_cb       = NULL;
  uart1_cfg.rx_buf      = s_uart1_rx_buf;
  uart1_cfg.rx_buf_size = sizeof(s_uart1_rx_buf);
  bsp_uart_init(BSP_UART_1, &uart1_cfg);
  
  s_log_level = LOG_INFO;
}
