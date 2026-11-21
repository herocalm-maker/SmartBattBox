/**
 * @file    sys_alarm.h
 * @brief   统一系统告警位图 (Module 层系统契约)
 *
 * 设计目标:
 *   - 取代 App 层本地 ALM_* 与 Module 层对外暴露的 BAT_FAULT_* 双套位图
 *   - 只保留面向"人机/上层决策"的统一告警位
 *   - mod_battery 内部故障状态机 (BAT_FAULT_*) 保留，由 battery 快照折叠为
 *     本位图 (alarm_bits) 供上层消费，上层不再直接读 BAT_FAULT_*
 *   - 消费方: App 层 (app_main_task / app_alarm / 显示)
 *
 * 分类 (2026-11 重新定义, 与 App 层 ERR 码一一对应):
 *   - 通讯/数据 (01~03): BAT_COMM / METER_COMM / BAT_INVALID —— 仅真能检测的通讯异常,
 *     持续多次失败才置位 (继电器互锁为命令位, 非硬件反馈, 不纳入)
 *   - 电池保护 (10~12): BAT_OVP / BAT_UVP / BAT_OCP
 *   - 温度 (20~22)    : BAT_OTP / INV_OTP / CHG_OTP
 *   - 提示/辅助位     : LOW_SOC(13) / STORAGE(04) / AC_INPUT(40) / BAT_INVALID
 *   码表见 App/Inc/app_alarm.h, 映射与优先级见 App/Src/app_alarm.c
 */
#ifndef SYS_ALARM_H
#define SYS_ALARM_H

#ifdef __cplusplus
extern "C" {
#endif

/* ERROR 类: 通讯硬故障 (连续多次失败才置位) —— ERR01/02 */
#define SYS_ALARM_BAT_COMM       (1u << 11) /* MAX17263 通讯失败 (连续 ≥10 次轮询) */
#define SYS_ALARM_METER_COMM     (1u << 12) /* HLW8110 通讯失败 (输入/输出通道持续无数据) */

/* ALARM 类: 温度/电量提示 —— ERR20/21/22/13 */
#define SYS_ALARM_BAT_OTP        (1u << 2)  /* 电池过温 -> ERR20 */
#define SYS_ALARM_INV_OTP        (1u << 4)  /* 逆变过温 -> ERR22 */
#define SYS_ALARM_CHG_OTP        (1u << 5)  /* 充电过温 -> ERR21 */
#define SYS_ALARM_LOW_SOC        (1u << 10) /* 保留: 低 SoC 不再折叠告警 (ERR13 已停用) */

/* 电池保护位 —— ERR10/11/12 */
#define SYS_ALARM_BAT_OVP        (1u << 0)  /* 电池过压 -> ERR10 */
#define SYS_ALARM_BAT_UVP        (1u << 1)  /* 电池欠压 -> ERR11 */
#define SYS_ALARM_BAT_OCP        (1u << 3)  /* 电池过流 -> ERR12 */

/* 其它 */
#define SYS_ALARM_AC_INPUT       (1u << 6)  /* AC 输入异常 -> ERR40 */
#define SYS_ALARM_BAT_INVALID    (1u << 8)  /* 电池数据无效 -> ERR03 */
#define SYS_ALARM_STORAGE        (1u << 9)  /* 外部存储故障 -> ERR04 */

#ifdef __cplusplus
}
#endif
#endif /* SYS_ALARM_H */
