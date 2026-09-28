/**
 * @file    comm_str.h
 * @brief   通用字符串工具 (Comm 层)
 *
 * 提供 AT 指令解析、串口协议解析等场景的共用字符串函数，
 * 避免各驱动层重复实现。
 */
#ifndef __COMM_STR_H__
#define __COMM_STR_H__

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>
#include <stdbool.h>

/*--------------------------------------------------------------
 * comm_str_find: 在 buf 中查找子串 sub, 返回首次出现偏移, -1=未找到
 *--------------------------------------------------------------*/
extern int16_t comm_str_find(const uint8_t *buf, uint16_t len, const char *sub);

/*--------------------------------------------------------------
 * comm_str_extract: 提取响应中 "+PREFIX:" 到 "\r\n" 之间的值
 *   buf       - 数据缓冲
 *   len       - 数据长度
 *   prefix    - 前缀 (如 "+NAME:" / "+VERSION:" / "+MAC:")
 *   out       - 输出缓冲
 *   maxlen    - 输出缓冲最大长度 (含 NUL)
 *   return    - true=成功
 *--------------------------------------------------------------*/
extern bool comm_str_extract(const uint8_t *buf, uint16_t len,const char *prefix, char *out, uint16_t maxlen);

/*--------------------------------------------------------------
 * comm_str_startswith: 判断 buf 是否以 prefix 开头
 *--------------------------------------------------------------*/
extern bool     comm_str_startswith(const uint8_t *buf, uint16_t len, const char *prefix);

#ifdef __cplusplus
}
#endif
#endif /* __COMM_STR_H__ */
