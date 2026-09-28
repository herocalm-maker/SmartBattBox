/**
 * @file    bsp_wdg.h
 * @brief   看门狗通用外设接口 (BSP 层)
 *
 * 设计目标:
 *   - 屏蔽 HAL_IWDG 细节, 上层只通过 bsp_wdg_refresh 喂狗
 *   - 上层只需指定超时时间 (ms), 内部自动计算 prescaler/reload
 *
 * 注意:
 *   必须在超时前周期调用 bsp_wdg_refresh, 否则系统复位.
 */
#ifndef __BSP_WDG_H__
#define __BSP_WDG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "Board.h"   /* BOARD_WDG_ENABLE */
#include "main.h"    /* IWDG_HandleTypeDef, HAL IWDG 常量 */

#if BOARD_WDG_ENABLE

#include <stdbool.h>
#include <stdint.h>



#define BOARD_IWDG_TIMEOUT_MS           8190u  /* 看门狗超时时间 (ms), 内部自动计算 prescaler/reload */


typedef struct {
    uint32_t timeout_ms;   // 超时时间 (毫秒), 内部自动计算 prescaler/reload
} bsp_wdg_config_t;

/** 任务 ID 枚举 (与 RTOS 任务一一对应) */
typedef enum {
    WDG_TASK_MAIN  = 0,    /* AppMainTask   */
    WDG_TASK_SENSOR,       /* SensorTask    */
    WDG_TASK_BLE,          /* BleTask       */
    WDG_TASK_DISP,         /* DisplayTask   */
    WDG_TASK_METERING,     /* MeteringTask  */
    WDG_TASK_COUNT
} wdg_task_id_t;


/* =====================================================================
 *  公开 API
* ===================================================================== */
extern bool bsp_wdg_init(const bsp_wdg_config_t *cfg);
extern void bsp_wdg_refresh(void);
extern void bsp_wdg_task_heartbeat(wdg_task_id_t id);
extern uint32_t bsp_wdg_get_task_cnt(wdg_task_id_t id);
extern void bsp_wdg_monitor_tick(void);
extern void bsp_wdg_monitor_init(void);

#endif

#ifdef __cplusplus
}
#endif
#endif /* __BSP_WDG_H__ */
