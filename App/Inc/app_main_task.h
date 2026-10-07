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

#ifdef __cplusplus
}
#endif
#endif /* __APP_MAIN_TASK_H__ */
