/**
 * @file    app_protect.h
 * @brief   统一告警 + 保护对象 (App 层)
 *
 * 收敛全系统告警的单一合并入口 (app_protect_merge) 与读取入口
 * (app_protect_alarm_get / 兼容的 alarm_get).
 * 告警位定义见 Module/Inc/sys_alarm.h.
 *
 * 说明: 保护阈值判定与动作触发 (关机/限功率) 仍由 AppMainTask 编排,
 *       本模块只负责告警位图的合并与读取 (单一事实来源).
 */
#ifndef APP_PROTECT_H
#define APP_PROTECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "sys_alarm.h"

void     app_protect_init(void);
uint32_t app_protect_alarm_get(void);   /* 读取当前合并后告警位图 */
void     app_protect_merge(uint32_t bits);   /* 单一合并入口: 按位 OR 写入 */
void     app_protect_unmerge(uint32_t bits); /* 单一清除入口: 按位 AND ~bits */
void     app_protect_clear(void);        /* 清除全部告警 (复位/启动用) */

/* 兼容入口: 原 app_main_task 对外 alarm_get() 收敛到本模块 */
uint32_t alarm_get(void);

#ifdef __cplusplus
}
#endif
#endif /* APP_PROTECT_H */
