/**
 * @file    app_sensor_task.h
 * @brief   传感器任务 — 电池电量计 (MAX17263) + NTC 温度
 *
 * SensorTask 职责:
 *   - mod_battery_handler() — MAX17263 电量计 (周期由模块内部管理)
 *   - mod_temp_update()     — NTC 温度采样 (500ms 周期)
 *   注: 按键扫描已移至 AppMainTask; 计量已移至 MeteringTask
 */
#ifndef __APP_SENSOR_TASK_H__
#define __APP_SENSOR_TASK_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "main.h"

/*====================================================================
 * API
 *====================================================================*/

/**
 * @brief 传感器任务入口：处理电池采集、温度更新和诊断日志。
 * @param arg 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
extern void SensorTask(void *arg);

#ifdef __cplusplus
}
#endif
#endif /* __APP_SENSOR_TASK_H__ */
