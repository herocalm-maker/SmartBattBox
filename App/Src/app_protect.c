/**
 * @file    app_protect.c
 * @brief   统一告警 + 保护对象实现 (App 层)
 *
 * 系统级告警位图 s_alarm 的唯一持有者. 各模块/任务通过 app_protect_merge()
 * 上报告警, 通过 app_protect_alarm_get() / alarm_get() 读取.
 */
#include "app_protect.h"

static volatile uint32_t s_alarm;

void app_protect_init(void)
{
    s_alarm = 0;
}

uint32_t app_protect_alarm_get(void)
{
    return s_alarm;
}

void app_protect_merge(uint32_t bits)
{
    s_alarm |= bits;
}

void app_protect_unmerge(uint32_t bits)
{
    s_alarm &= ~bits;
}

void app_protect_clear(void)
{
    s_alarm = 0;
}

uint32_t alarm_get(void)
{
    return s_alarm;
}
