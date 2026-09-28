/**
 * @file    sys_alarm.h
 * @brief   统一系统告警位图 (Module 层系统契约)
 *
 * 设计目标:
 *   - 取代 App 层本地 ALM_* 与 Module 层对外暴露的 BAT_FAULT_* 双套位图
 *   - 只保留面向"人机/上层决策"的统一告警位
 *   - mod_battery 内部故障状态机 (BAT_FAULT_*) 保留，由 battery 快照折叠为
 *     本位图 (alarm_bits) 供上层消费，上层不再直接读 BAT_FAULT_*
 *   - 消费方: App 层 (app_main_task / app_protect / mod_hmi / 显示)
 */
#ifndef SYS_ALARM_H
#define SYS_ALARM_H

#ifdef __cplusplus
extern "C" {
#endif

/* 统一告警位图，任意组合可按位或 */
#define SYS_ALARM_BAT_OVP      (1u << 0)  /* 电池过压 */
#define SYS_ALARM_BAT_UVP      (1u << 1)  /* 电池欠压 */
#define SYS_ALARM_BAT_OTP      (1u << 2)  /* 电池过温 */
#define SYS_ALARM_BAT_OCP      (1u << 3)  /* 电池过流 */
#define SYS_ALARM_INV_OTP      (1u << 4)  /* 逆变过温 */
#define SYS_ALARM_CHG_OTP      (1u << 5)  /* 充电过温 */
#define SYS_ALARM_AC_INPUT     (1u << 6)  /* AC 输入异常 */
#define SYS_ALARM_RELAY        (1u << 7)  /* 继电器故障 */
#define SYS_ALARM_BAT_INVALID  (1u << 8)  /* 电池数据无效 */
#define SYS_ALARM_STORAGE      (1u << 9)  /* 外部存储故障 */
#define SYS_ALARM_LOW_SOC      (1u << 10) /* 低 SoC */

#ifdef __cplusplus
}
#endif
#endif /* SYS_ALARM_H */
