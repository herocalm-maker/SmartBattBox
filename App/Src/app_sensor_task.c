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
#include "app_main_task.h" /* 充电/判满状态诊断查询 */

/* Module 层 */
#include "mod_battery.h"
#include "mod_temp.h"

/* BSP 层 */
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
        if (bat && bat->valid)
        {
            logger_out(LOG_INFO,
                       "[BAT] soc: %u%%, soh: %u%%, vol: %lumV, cur: %ldmA, avgcur: %ldmA",
                       bat->soc, bat->soh, (unsigned long)bat->voltage_mv, (long)bat->current_ma,
                       (long)bat->avg_current_ma);
            logger_out(LOG_INFO,
                       "[BAT] ttf: %u min, tte: %u min, cycles: %u",
                       bat->ttf_valid ? bat->time_to_full_min : 0,
                       bat->tte_valid ? bat->time_to_empty_min : 0,
                       bat->cycles_raw);
            logger_out(LOG_INFO,
                       "[BAT] repcap: %lumAh, fullcap: %lumAh, temp: %d.%dC, vfsoc: %u%%",
                       (unsigned long)bat->repcap_mah, (unsigned long)bat->fullcap_mah,
                       bat->temp_deg / 10, bat->temp_deg % 10, (unsigned)bat->vfsoc);
            /* 充电/判满状态:
             *   mode  工作模式 (IDLE/CHARGE_BYPASS/INVERT/SHUTDOWN)
             *   chg   充电状态机 (IDLE/STARTING/RUNNING/STOPPING)
             *   need  补电请求; blk 判满停充后的冷却剩余秒
             *   full  fq=芯片FStat.FQ(bit7); latch/app=判满停充锁存; fstat 原始位
             *   avgcur 参与判满 B 路径(涓流)与 TTF/充放电方向判定 */
            logger_out(LOG_INFO,
                       "[BAT] mode: %s, chg: %s, need: %u, blk: %lus, full: fq=%u latch=%u app=%u, fstat: 0x%04X",
                       work_mode_name(), charge_state_name(), (unsigned)charge_need_active(),
                       (unsigned long)charge_restart_block_left_s(),
                       (unsigned)bat->full_qualified, (unsigned)charge_full_active(),
                       (unsigned)battery_full_active(), (unsigned)bat->fstat_raw);
        }
        else
        {
            logger_out(
                LOG_WARN,
                "[BAT] INVALID error=%d (%s) faults=0x%08lX history=0x%08lX last_sample_ms=%lu",
                snapshot.last_error, drv_max17263_error_string(snapshot.last_error),
                (unsigned long)snapshot.active_faults, (unsigned long)snapshot.latched_faults,
                (unsigned long)snapshot.timestamp_ms);
            /* 数据无效时也给出充电/判满状态, 便于判断"没充电"是数据问题还是策略问题 */
            logger_out(LOG_WARN,
                       "[BAT] mode: %s, chg: %s, need: %u, blk: %lus, full: latch=%u (battery data invalid)",
                       work_mode_name(), charge_state_name(), (unsigned)charge_need_active(),
                       (unsigned long)charge_restart_block_left_s(),
                       (unsigned)charge_full_active());
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
    uint8_t temp_cnt = 0;

    /* 初始化电池采集模块。 */
    mod_battery_init();
    /* 初始化温度采集模块。 */
    mod_temp_init();

    for (;;)
    {
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

        osDelay(SENSOR_TICK_MS);   /* 周期轮询 (空转队列已移除) */
    }
}


