/**
 * @file    comm_str.c
 * @brief   通用字符串工具实现 (Comm 层)
 */
#include "comm_str.h"
#include <string.h>

/**
 * @brief  在缓冲区中查找子串 (二进制安全, 不依赖 NUL 结尾)
 * @param  buf  数据缓冲区
 * @param  len  数据长度
 * @param  sub  待查找子串 (NUL 结尾)
 * @return 首次出现的字节偏移, -1=未找到或参数非法
 */
int16_t comm_str_find(const uint8_t *buf, uint16_t len, const char *sub)
{
    uint16_t slen = (uint16_t)strlen(sub);
    if (slen == 0 || slen > len || buf == NULL || sub == NULL) return -1;
    for (uint16_t i = 0; i + slen <= len; i++) {
        if (memcmp(&buf[i], sub, slen) == 0) {
            return (int16_t)i;
        }
    }
    return -1;
}

/**
 * @brief  提取前缀到行尾之间的字段值 (AT 响应解析用)
 * @note   值域为 prefix 之后到 '\r'/'\n'/缓冲区末尾之间, 拷贝到 out 并 NUL 结尾.
 *         典型: buf="+NAME:SmartBox\r\n" prefix="+NAME:" → out="SmartBox"
 * @param  buf     数据缓冲区
 * @param  len     数据长度
 * @param  prefix  字段前缀 (如 "+NAME:" / "+VERSION:" / "+MAC:")
 * @param  out     输出缓冲区
 * @param  maxlen  输出缓冲区大小 (含 NUL)
 * @return true=成功 false=前缀未找到/值为空/缓冲区不足
 */
bool comm_str_extract(const uint8_t *buf, uint16_t len,const char *prefix, char *out, uint16_t maxlen)
{
    if (buf == NULL || prefix == NULL || out == NULL || maxlen == 0)
    	return false;

    int16_t idx = comm_str_find(buf, len, prefix);
    if (idx < 0) return false;

    uint16_t start = (uint16_t)idx + (uint16_t)strlen(prefix);
    uint16_t end = start;
    while (end < len && buf[end] != '\r' && buf[end] != '\n') {
        end++;
    }
    uint16_t copy_len = end - start;
    if (copy_len == 0 || copy_len >= maxlen) return false;

    memcpy(out, &buf[start], copy_len);
    out[copy_len] = '\0';
    return true;
}

/**
 * @brief  判断缓冲区是否以指定前缀开头 (二进制安全)
 * @note   典型: URC 行匹配 (如 "+EVENT:BLE_CONNECTED" / "+DATA=")
 * @param  buf     数据缓冲区
 * @param  len     数据长度
 * @param  prefix  前缀 (NUL 结尾)
 * @return true=以 prefix 开头 false=否或参数非法
 */
bool comm_str_startswith(const uint8_t *buf, uint16_t len, const char *prefix)
{
    if (buf == NULL || prefix == NULL)
    	return false;
    uint16_t plen = (uint16_t)strlen(prefix);
    if (plen > len)
    	return false;
    return memcmp(buf, prefix, plen) == 0;
}

/**
 * @brief  取浮点值绝对值小数部分 (打印浮点整数拆分用)
 * @note   nano.specs 无 %f, 打印浮点统一拆成 "整数部分 + 小数部分"。
 *         小数部分 = round(|v| * scale) % scale, 恒非负, 避免负数取模出现负号。
 * @param  v      浮点值 (正负均可)
 * @param  scale  倍率: 10=1位小数, 100=2位, 1000=3位
 * @return 小数部分 (0 .. scale-1)
 */
uint16_t comm_str_frac(float v, uint16_t scale)
{
    int32_t n = (int32_t)(v * (float)scale + 0.5f);
    if (n < 0) n = -n;
    return (uint16_t)(n % (int32_t)scale);
}
