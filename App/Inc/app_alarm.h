/**
 * @file    app_alarm.h
 * @brief   统一告警 + 故障码定义 (App 层)
 *
 * 单一码表设计: app_alarm.c 内一张按优先级排列的映射表, 查出最高优先级码,
 * 屏上统一显示 "ERRxx" (两位十进制)。新增告警只需在表里加一行并分配码。
 *
 * 码段划分 (2026-11 重新定义):
 *   01 ~ 09  通讯/数据/存储硬故障  —— 停机锁死或降级
 *   10 ~ 19  电池保护              —— 过压/欠压/过流/低电量
 *   20 ~ 29  温度保护              —— 电池/充电/逆变过温
 *   40 ~ 49  输入/市电             —— 掉电、越窗
 *   88       保留                  —— 自检/全段显示 (不参与位图)
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

/*====================================================================
 * ERR 码定义 (屏幕 TextError 显示 "ERRxx")
 *====================================================================*/

/* ---- 01~09 通讯/数据/存储 ---- */
#define APP_ERR_BAT_COMM      1u   /* 电池电量计(MAX17263)通讯失败, ≥10 次轮询 */
#define APP_ERR_METER_COMM    2u   /* 计量芯片(HLW8110)通讯失败, 通道持续无数据 */
#define APP_ERR_BAT_INVALID   3u   /* 电池数据无效 (快照无效/方向故障位) */
#define APP_ERR_STORAGE       4u   /* 存储/参数异常 (NVM/参数区) */

/* ---- 10~19 电池保护 ---- */
#define APP_ERR_BAT_OVP      10u   /* 电池过压 -> 停充 */
#define APP_ERR_BAT_UVP      11u   /* 电池欠压 -> 停放电 */
#define APP_ERR_BAT_OCP      12u   /* 电池过流 -> 停机 */
#define APP_ERR_LOW_SOC      13u   /* 保留: 电量过低不显示 ERR 码 (2026-11-15 需求) */

/* ---- 20~29 温度保护 ---- */
#define APP_ERR_BAT_OTP      20u   /* 电池 NTC ≥60℃ -> 停充 + 停交流输出 (可恢复) */
#define APP_ERR_CHG_OTP      21u   /* 充电器 ≥75℃ -> 停机锁死 */
#define APP_ERR_INV_OTP      22u   /* 逆变器 ≥75℃ -> 停机锁死 */

/* ---- 40~49 输入/市电 ---- */
#define APP_ERR_AC_INPUT     40u   /* 市电异常 (掉电/越窗) -> 转逆变 */

/* ---- 保留 ---- */
#define APP_ERR_SELFTEST     88u   /* 自检/全段显示, 不参与告警位图 */

/* ---- 告警位图持有 ---- */
void     app_alarm_merge(uint32_t bits);    /* 单一合并入口: 按位 OR */
void     app_alarm_unmerge(uint32_t bits);  /* 单一清除入口: 按位 AND ~bits */
uint32_t app_alarm_get(void);               /* 读取当前合并后告警位图 */

/* ---- 码派生 ---- */
/**
 * @brief 取当前最高优先级的统一 ERR 码 (供屏幕 TextError 显示)。
 * @return 码值 (见上方 APP_ERR_*); 0 = 无异常。
 * @note  查表顺序即优先级: 通讯/数据 > 电池保护 > 温度 > 存储 > 低电量 > 市电。
 */
uint8_t  app_alarm_err_code(void);

/**
 * @brief 任一过温告警是否置位 (电池/充电/逆变)。
 * @return true = 存在 OTP 告警 (显示任务据此点亮温度图标)。
 */
bool     app_alarm_temp_high(void);

/**
 * @brief 判断某个 ERR 码是否属于"停机锁死"类 (需断电重启恢复)。
 * @param code ERR 码
 * @return true = 锁死类; false = 可恢复/提示类。
 */
bool     app_alarm_code_is_latched(uint8_t code);

#ifdef __cplusplus
}
#endif
#endif /* __APP_ALARM_H__ */
