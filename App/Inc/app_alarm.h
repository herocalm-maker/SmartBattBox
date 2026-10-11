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

/* ERROR(故障) 位掩码: 仅通讯硬故障 (MAX17263 / HLW8110 持续通讯失败) */
#define APP_ALARM_ERR_MASK (SYS_ALARM_BAT_COMM | SYS_ALARM_METER_COMM)

/* ALARM(报警) 位掩码: 温度类为主 + 低电量提示 */
#define APP_ALARM_ALM_MASK (SYS_ALARM_BAT_OTP | SYS_ALARM_INV_OTP | \
                            SYS_ALARM_CHG_OTP | SYS_ALARM_LOW_SOC)

/* ---- 告警位图持有 ---- */
void     app_alarm_merge(uint32_t bits);    /* 单一合并入口: 按位 OR */
void     app_alarm_unmerge(uint32_t bits);  /* 单一清除入口: 按位 AND ~bits */
uint32_t app_alarm_get(void);               /* 读取当前合并后告警位图 */

/* ---- 故障/报警码派生 (供显示拼接 ERROR/ALARM 文本) ---- */
uint8_t  app_alarm_error(void);             /* 最高优先级 ERROR 码 (1..7); 0=无 */
uint8_t  app_alarm_alarm(void);             /* 最高优先级 ALARM 码 (1..4); 0=无 */
uint8_t  app_alarm_err_code(void);          /* 统一 ERR 码 (1..11); 0=无 (ERROR 1-7 优先, ALARM +7 -> 8-11) */
bool     app_alarm_temp_high(void);         /* 任一过温告警置位 */

#ifdef __cplusplus
}
#endif
#endif /* __APP_ALARM_H__ */
