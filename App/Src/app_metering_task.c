/**
 * @file    app_metering_task.c
 * @brief   电能计量任务 — HLW8110 双路采集 (输入/输出)
 *
 * MeteringTask 职责:
 *   - 初始化双路 HLW8110 (USART2=输入, USART3=输出)
 *   - 周期调用 mod_metering_handler() 读取电压/电流/功率/电能
 *   - 异常检测 + 自动恢复
 */
#include "main.h"
#include "app_metering_task.h"
#include "mod_metering.h"
#include "app_alarm.h"
#include "bsp_wdg.h"
#include "comm_logger.h"
#include "comm_str.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"


#define METERING_POLL_MS          100 /* 轮询周期 (mod_metering_handler 内部有 1s 节流) */
#define METERING_LOG_TICKS       (10000 / METERING_POLL_MS) /* 10s 打印一次 */



/*====================================================================
 * 消息发送接口
 *====================================================================*/
/**
 * @brief 向计量任务队列发送消息，最多等待3000个RTOS tick。
 * @param msg 待发送消息，调用方需提供有效指针。
 * @return true：消息入队；false：队列满。
 */
bool Send_Msg_AppMeteringTask(ucMESSAGE_t *msg)
{
    BaseType_t err;
    err = xQueueSend(xMeteringQueueHandle, msg, 3000);
    if (err == errQUEUE_FULL)
    {
        return false;
    }
    return true;
}


/**
 * @brief 按调用次数打印双路计量数据，使用整数拆分显示小数。
 */
static void metering_tim_process(void)
{
    static uint32_t log_cnt = 0;
    /* 10s 周期打印计量数据 (整数拆分避免 nano.specs 无 %f 支持) */
    if (++log_cnt >= METERING_LOG_TICKS)
    {
        log_cnt = 0;
        METER_INFO_T *m = metering_get_info();
        if (m)
        {
            int32_t vin_v_i = (int32_t)(m->vin_v);
            int32_t vin_v_d = comm_str_frac(m->vin_v, 10);
            int32_t vin_i_i = (int32_t)(m->vin_i);
            int32_t vin_i_d = comm_str_frac(m->vin_i, 100);
            int32_t vin_p_i = (int32_t)(m->vin_p_w);
            int32_t vin_p_d = comm_str_frac(m->vin_p_w, 10);
            int32_t vout_v_i = (int32_t)(m->vout_v);
            int32_t vout_v_d = comm_str_frac(m->vout_v, 10);
            int32_t vout_i_i = (int32_t)(m->vout_i);
            int32_t vout_i_d = comm_str_frac(m->vout_i, 100);
            int32_t vout_p_i = (int32_t)(m->vout_p_w);
            int32_t vout_p_d = comm_str_frac(m->vout_p_w, 10);
            int32_t vin_f_i = (int32_t)(m->vin_freq_hz);
            int32_t vin_f_d = comm_str_frac(m->vin_freq_hz, 10);
            int32_t vout_f_i = (int32_t)(m->vout_freq_hz);
            int32_t vout_f_d = comm_str_frac(m->vout_freq_hz, 10);

            logger_out(
                LOG_INFO,
                "[METER] sta=%u,VIN:%d.%dV %d.%02dA %d.%dW %d.%dHz | sta=%u,VOUT:%d.%dV %d.%02dA %d.%dW %d.%dHz | AC:%d",
                m->in_ok,
                vin_v_i, vin_v_d, vin_i_i, vin_i_d, vin_p_i, vin_p_d,
                vin_f_i, vin_f_d,
                m->out_ok,
                vout_v_i, vout_v_d, vout_i_i, vout_i_d,
                vout_p_i, vout_p_d, vout_f_i, vout_f_d, m->ac_in_present);
        }
    }
}

/*====================================================================
 * 任务入口
 *====================================================================*/

/**
 * @brief 计量任务入口：初始化采样配置并周期调用双路采集。
 * @param arg 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
void MeteringTask(void *arg)
{
    /* 注入板级采样配置 (K1/K2 + PGA_A=1), 必须在 init 之前 */
    mod_metering_apply_board_config();

    /* 初始化计量模块 (双路 HLW8110) */
    mod_metering_init();

    /* init 完成后回读校验关键寄存器 (PGA_A=1) */
    mod_metering_verify_syscon();

    for (;;)
    {
        /* 读双路 HLW8110 数据 + 异常检测 */
        mod_metering_handler();

        /* 通讯失败告警: 任一通道持续无数据 → 置 METER_COMM (ERROR 02);
         * 双通道恢复后清除. (仅置告警位, 动作由 app_main_task 按通道语义执行) */
        METER_INFO_T *m = metering_get_info();
        if (m)
        {
            if (m->in_ok == 0u || m->out_ok == 0u)
            {
                app_alarm_merge(SYS_ALARM_METER_COMM);
            }
            else
            {
                app_alarm_unmerge(SYS_ALARM_METER_COMM);
            }
        }

        /* 日志周期按任务循环次数累计。 */
        metering_tim_process();

        /* 心跳 — 监控定时器检测任务存活 */
        bsp_wdg_task_heartbeat(WDG_TASK_METERING);

        osDelay(METERING_POLL_MS);   /* 100ms 轮询节拍 (空转队列已移除) */
    }
}
