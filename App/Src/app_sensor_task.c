/**
 * @file    app_sensor_task.c
 * @brief   传感器任务 — 电池(库仑计) + ADC
 *
 * SensorTask 职责:
 *   - mod_battery_handler()    — MAX17263 电量计 + 库仑积分 (采样周期由模块内部管理)
 *   - bsp_adc 温度/电压读取    — 滤波 + 标定
 *   注: 按键扫描已移至 AppMainTask (20ms循环延时)
 */

#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* App layer */
#include "app_sensor_task.h"

/* Module 层 */
#include "mod_battery.h"
#include "mod_temp.h"

/* BSP 层 */
#include "bsp_adc.h"
#include "bsp_wdg.h"

/* Comm */
#include "comm_logger.h"

/*====================================================================
 * 任务入口
 *====================================================================*/

#define SENSOR_TICK_MS 20                         /* 20ms 基础周期 */
#define SENSOR_LOG_TICKS (10000 / SENSOR_TICK_MS) /* 10s 打印一次 */

/**
 * @brief 按调用次数打印电池和温度诊断信息。
 */
static void sensor_tim_process(void)
{
    static uint32_t log_cnt = 0;

    /* 10s 周期打印电池 + 温度数据；MAX17263使用芯片内部温度。 */
    if (++log_cnt >= SENSOR_LOG_TICKS)
    {
        log_cnt = 0;
        BATT_INFO_T snapshot;
        BATT_INFO_T *bat = battery_get_snapshot(&snapshot) ? &snapshot : NULL;
        if (bat)
        {
            int16_t t_abs = bat->temp_deg < 0 ? -bat->temp_deg : bat->temp_deg;
            logger_out(LOG_INFO,
                         "[BAT] SOC:%u%% %lumV %ldmA Cap:%lu/%lu DieT:%s%d.%dC SOHcap:%u%%",
                         bat->soc, (unsigned long)bat->voltage_mv, (long)bat->current_ma,
                         (unsigned long)bat->repcap_mah, (unsigned long)bat->fullcap_mah,
                         bat->temp_deg < 0 ? "-" : "", t_abs / 10, t_abs % 10, bat->soh);
        }
        else
        {
            logger_out(
                LOG_WARN,
                "[BAT] INVALID error=%d (%s) faults=0x%08lX history=0x%08lX last_sample_ms=%lu",
                snapshot.last_error, drv_max17263_error_string(snapshot.last_error),
                (unsigned long)snapshot.active_faults, (unsigned long)snapshot.latched_faults,
                (unsigned long)snapshot.timestamp_ms);
        }
        const TEMP_INFO_T *t = mod_temp_get();
        if (t)
        {
            int16_t chg_abs = t->chg_temp < 0 ? -t->chg_temp : t->chg_temp;
            int16_t bat_abs = t->bat_temp < 0 ? -t->bat_temp : t->bat_temp;
            int16_t inv_abs = t->inv_temp < 0 ? -t->inv_temp : t->inv_temp;
            logger_out(LOG_INFO, "[TEMP] CHG:%s%d.%d BAT:%s%d.%d INV:%s%d.%dC",
                         t->chg_temp < 0 ? "-" : "", chg_abs / 10, chg_abs % 10,
                         t->bat_temp < 0 ? "-" : "", bat_abs / 10, bat_abs % 10,
                         t->inv_temp < 0 ? "-" : "", inv_abs / 10, inv_abs % 10);
        }
    }
}

/**
 * @brief 传感器任务入口：处理电池采集、温度更新和诊断日志。
 * @param argument 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
void SensorTask(void *argument)
{
    uint32_t uwRet = 0;
    ucMESSAGE_t Msg = {0};
    uint8_t temp_cnt = 0;

    /* 初始化电池采集模块。 */
    mod_battery_init();
    /* 初始化温度采集模块。 */
    mod_temp_init();

    for (;;)
    {
        uwRet = xQueueReceive(xSensorQueueHandle, &Msg, pdMS_TO_TICKS(SENSOR_TICK_MS));
        if (uwRet == pdPASS)
        {
            switch (Msg.MsgID)
            {
                case 0:
                    break;

                default:
                    break;
            }
        }

        /* 1. 电池电量信息采集计量  */
        mod_battery_handler();

        /* 2. ADC温度采样 (500ms 周期 = 25 × 20ms) */
        if (++temp_cnt >= 25)
        {
            temp_cnt = 0;
            mod_temp_update();
        }

        /* 日志周期按任务循环次数累计。 */
        sensor_tim_process();

        /* 通知看门狗本轮任务已完成。 */
        bsp_wdg_task_heartbeat(WDG_TASK_SENSOR);
    }
}


