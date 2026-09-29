/**
 * @file    app_faultcode.h
 * @brief   故障码 / 报警码生成 (App 层)
 *
 * 从统一告警位图 (sys_alarm, 由 app_protect 持有) 派生两类独立编码:
 *   - ERROR 码 (故障, 1..7)  : 过压/欠压/过温/过流/继电器, 需保护处理
 *   - ALARM 码 (报警, 1..4)  : AC 异常/数据无效/存储/低电量, 提示性
 * ERROR 与 ALARM 各自独立编号, 供显示任务拼接文本 (ERROR: XX, ALARM: XX).
 */
#ifndef APP_FAULTCODE_H
#define APP_FAULTCODE_H

#include <stdbool.h>
#include <stdint.h>
#include "sys_alarm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ERROR(故障) 位掩码: 影响安全/需保护停机 */
#define FAULT_ERR_MASK (SYS_ALARM_BAT_OVP | SYS_ALARM_BAT_UVP | SYS_ALARM_BAT_OTP | \
                        SYS_ALARM_BAT_OCP | SYS_ALARM_INV_OTP | SYS_ALARM_CHG_OTP | \
                        SYS_ALARM_RELAY)

/* ALARM(报警) 位掩码: 提示性, 可继续运行 */
#define FAULT_ALM_MASK (SYS_ALARM_AC_INPUT | SYS_ALARM_BAT_INVALID | \
                        SYS_ALARM_STORAGE | SYS_ALARM_LOW_SOC)

/**
 * @brief 读取当前最高优先级 ERROR 故障码 (1..7).
 * @return 故障码; 0 = 当前无 ERROR.
 */
uint8_t app_faultcode_get_error(void);

/**
 * @brief 读取当前最高优先级 ALARM 报警码 (1..4).
 * @return 报警码; 0 = 当前无 ALARM.
 */
uint8_t app_faultcode_get_alarm(void);

/**
 * @brief 温度高判定: 任一过温告警 (电池/逆变/充电) 触发.
 * @return true = 任一 OTP 告警置位 (显示任务据此将 0x2017 图标置 8).
 */
bool app_faultcode_temp_high(void);

#ifdef __cplusplus
}
#endif
#endif /* APP_FAULTCODE_H */
