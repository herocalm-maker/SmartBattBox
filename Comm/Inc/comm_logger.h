/**
 * @file    comm_logger.h
 * @brief   日志通信 (Comm 层)
 *
 * 功能:
 *   1. 旋转静态缓冲池, ISR/Task 均可安全调用
 *   2. 带级别前缀 (@Debug>: @Info>: @Warn>: @Error>:)
 *   3. 便捷宏: log_info()/log_warn()/log_error()/log_debug()
 */
#ifndef __COMM_LOGGER_H__
#define __COMM_LOGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>



/*====================================================================
 * 日志级别枚举 (阈值过滤: >= 设定级别即输出)
 *====================================================================*/
typedef enum {
  LOG_NONE    = 0,
  LOG_VERBOSE = 1,
  LOG_DEBUG   = 2,
  LOG_INFO    = 3,
  LOG_WARN    = 4,
  LOG_ERROR   = 5
} log_level_enum;

/*====================================================================
 * 日志 API (带级别前缀, 自动追加 \r\n)
 *====================================================================*/

/* 初始化日志模块 */
extern void logger_init(uint32_t baudrate);
/* 带级别前缀的日志输出 */
extern void logger_out(log_level_enum lv, const char *fmt, ...);

/*====================================================================
 * UART1 RX API (接收命令/数据, 供 app_main_task 解析)
 *====================================================================*/

/* 从 UART1 RX 缓冲读取数据 (非阻塞, 返回实际读取字节数) */
extern uint16_t logger_rx_read(uint8_t *buf, uint16_t len);
/* 查询 UART1 RX 缓冲当前数据字节数 */
extern uint16_t logger_rx_available(void);

/*====================================================================
 * 便捷宏 (小写, 避免与枚举 LOG_INFO 等冲突)
 *
 *   log_info("voltage: %dV", v);
 *   → 输出: @Info>: voltage: 220V\r\n
 *====================================================================*/
#define log_print(fmt, ...)    logger_out(LOG_NONE,   fmt, ##__VA_ARGS__)
#define log_verbose(fmt, ...)  logger_out(LOG_VERBOSE, fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...)    logger_out(LOG_DEBUG,   fmt, ##__VA_ARGS__)
#define log_info(fmt, ...)     logger_out(LOG_INFO,    fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...)     logger_out(LOG_WARN,    fmt, ##__VA_ARGS__)
#define log_error(fmt, ...)    logger_out(LOG_ERROR,   fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
#endif /* __COMM_LOGGER_H__ */
