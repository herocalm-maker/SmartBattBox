/**
 * @file    app_alarm.h
 * @brief   统一告警 + 故障码派生 (App 层)
 *
 * 合并原 app_protect (告警位图持有) 与 app_faultcode (ERROR/ALARM 码派生):
 *   - app_alarm_merge/unmerge: 上报告警/清除 (单一合并入口)
 *   - app_alarm_get          : 读取当前合并后位图
 *   - app_alarm_error/alarm  : 派生最高优先级 ERROR/ALARM 码 (各自独立编号)
 *   - app_alarm_temp_high    : 任一过温告警判定
 * 告警位定义见 Module/Inc/sys_alarm.h.
 */
#ifndef __APP_ALARM_H__
#define __APP_ALARM_H__

#include <stdbool.h>
#include <stdint.h>
#include "sys_alarm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ERROR(故障) 位掩码: 影响安全/需保护停机 */
#define APP_ALARM_ERR_MASK (SYS_ALARM_BAT_OVP | SYS_ALARM_BAT_UVP | SYS_ALARM_BAT_OTP | \
                            SYS_ALARM_BAT_OCP | SYS_ALARM_INV_OTP | SYS_ALARM_CHG_OTP | \
                            SYS_ALARM_RELAY)

/* ALARM(报警) 位掩码: 提示性, 可继续运行 */
#define APP_ALARM_ALM_MASK (SYS_ALARM_AC_INPUT | SYS_ALARM_BAT_INVALID | \
                            SYS_ALARM_STORAGE | SYS_ALARM_LOW_SOC)

/* ---- 告警位图持有 ---- */
void     app_alarm_merge(uint32_t bits);    /* 单一合并入口: 按位 OR */
void     app_alarm_unmerge(uint32_t bits);  /* 单一清除入口: 按位 AND ~bits */
uint32_t app_alarm_get(void);               /* 读取当前合并后告警位图 */

/* ---- 故障/报警码派生 (供显示拼接 ERROR/ALARM 文本) ---- */
uint8_t  app_alarm_error(void);             /* 最高优先级 ERROR 码 (1..7); 0=无 */
uint8_t  app_alarm_alarm(void);             /* 最高优先级 ALARM 码 (1..4); 0=无 */
bool     app_alarm_temp_high(void);         /* 任一过温告警置位 */

#ifdef __cplusplus
}
#endif
#endif /* __APP_ALARM_H__ */
