/**
 * @file    app_main_task.h
 * @brief   主任务 (AppMainTask) — 状态机 + 继电器控制 + 参数管理
 *
 * 职责:
 *   - 系统状态机 (CHARGE/OUTPOWER/FAULT)
 *   - AC 通路管理 (继电器安全切换)
 *   - 热管理 (风扇 + 温度报警)
 *   - Flash 参数 (project_param.h) 管理
 *   - 对外提供: state_get(); 告警位图读取见 app_alarm.h (app_alarm_get)
 */
#ifndef __APP_MAIN_TASK_H__
#define __APP_MAIN_TASK_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>
#include "main.h"
#include "app_alarm.h"   /* ERR 码定义 (APP_ERR_*) */

/*====================================================================
 * 系统状态
 *====================================================================*/
typedef enum
{
    SYS_STATUS_CHARGE,
    SYS_STATUS_OUTPOWER,
    SYS_STATUS_FAULT,
    SYS_STATUS_IDLE,
    SYS_STATUS_BYPASS,
    SYS_STATUS_CHARGE_BYPASS,
    SYS_STATUS_SWITCHING,
    SYS_STATUS_MAX
} SYS_STATUS_T;

/*====================================================================
 * 事件类型 (跨任务消息)
 *====================================================================*/
typedef enum
{
    EVT_NONE = 0,
    EVT_DISP_ICON /* DisplayTask: 通路状态+图标更新 (Value[0]=通路, [1]=充电, [2]=逆变输出, [3]=频率, [4]=旁路UPS) */
} evt_type_t;

/**
 * @brief 主任务入口：循环处理保护、通路、按键、指示灯及显示。
 * @param arg 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
extern void AppMainTask(void *arg);

/* 状态/告警查询 */
/**
 * @brief 读取当前系统运行状态。
 * @return 当前系统状态枚举值。
 */
extern SYS_STATUS_T state_get(void);

/**
 * @brief 判满锁存查询 (停充时置位, 新一轮充电开始时清除)。
 * @return true: 上一次停充是因为判满。
 */
extern bool charge_full_active(void);

/**
 * @brief 产品级"满"判定 (供显示/BLE 使用)。
 * @return true: 上一次停充是因为判满 (SOC 到 100%)。
 * @note  SOC 数字始终显示真实值; 该标志只用于关充电图标/满图标。
 */
extern bool battery_full_active(void);

/*====================================================================
 * 诊断查询 (周期日志用, 只读)
 *====================================================================*/
/**
 * @brief 工作模式名称。
 * @return 静态字符串: IDLE / CHARGE_BYPASS / INVERT / SHUTDOWN。
 */
extern const char *work_mode_name(void);

/**
 * @brief 充电状态机名称。
 * @return 静态字符串: IDLE / STARTING / RUNNING / STOPPING。
 */
extern const char *charge_state_name(void);

/**
 * @brief 当前是否存在补电请求 (充电策略结论)。
 * @return true: 已判定可充电。
 */
extern bool charge_need_active(void);

/**
 * @brief 充满停充后的重启封锁剩余秒数。
 * @return 剩余秒; 0 表示未封锁。
 */
extern uint32_t charge_restart_block_left_s(void);

/*====================================================================
 * 瞬时提示码 (显示 TextError "ERRxx")
 * 注: 低电量不再刷 ERR 码 (2026-11-15 需求), 仅蜂鸣提示。
 *====================================================================*/

/**
 * @brief 显示一个瞬时提示码, 到时自动消失。
 * @param code 提示码 (1~99)
 * @param hold_ms 保持时间, ms
 */
extern void app_ui_show_code(uint8_t code, uint32_t hold_ms);

/**
 * @brief 读取当前瞬时提示码。
 * @return 提示码; 0=无。优先级低于真实告警码 (app_alarm_err_code)。
 */
extern uint8_t app_ui_code_get(void);

/*====================================================================
 * 过载声光报警 (输出功率 > BOARD_PWR_ALARM_W)
 *====================================================================*/
/**
 * @brief 当前是否处于过载告警 (输出功率超告警门限且输出通路运行中)。
 * @return true: 需要声光报警。
 */
extern bool app_ui_overload_alarm(void);

/**
 * @brief 过载声光节奏相位 (2.0s 周期, 前 1.0s 为报警相位)。
 * @return true: 报警相位 (报警灯亮 / OVERLOAD 文本显示)。
 * @note  报警灯与蜂鸣器由主任务使用, 显示任务据此同步 OVERLOAD 文本闪动。
 */
extern bool app_ui_overload_phase(void);

/*====================================================================
 * 风扇状态查询 (供显示任务驱动风扇动画/温度显示)
 *====================================================================*/
/* app_ui_fan_state() 返回值位定义 */
#define APP_UI_FAN_MAIN  (1u << 0)  /* 主风扇运行 (电池/充电回路) */
#define APP_UI_FAN_AUX   (1u << 1)  /* 辅风扇运行 (逆变器) */

/**
 * @brief 当前是否有任一散热风扇在运行。
 * @return true: 主风扇或辅风扇已开启。
 */
extern bool app_ui_fan_running(void);

/**
 * @brief 当前各风扇的运行状态位图。
 * @return APP_UI_FAN_MAIN / APP_UI_FAN_AUX 的按位或; 0 = 全部停止。
 */
extern uint8_t app_ui_fan_state(void);

/*====================================================================
 * 通路活跃查询 (供显示任务判断"输入功率"是否有效)
 *====================================================================*/
/**
 * @brief 当前是否正在给整机/电池充电 (CHG_IN 已吸合)。
 * @return true: 充电状态机处于 STARTING / RUNNING。
 * @note  不能与 battery_full_active() 混用: 后者只表示"上次停充因为判满"。
 */
extern bool app_ui_charging(void);

/**
 * @brief 当前旁路输出是否活跃 (AC_CTRL 已吸合)。
 * @return true: 旁路状态机处于 STARTING / RUNNING。
 */
extern bool app_ui_bypass_active(void);

#ifdef __cplusplus
}
#endif
#endif /* __APP_MAIN_TASK_H__ */
