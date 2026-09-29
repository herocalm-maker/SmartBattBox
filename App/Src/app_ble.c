/**
 * @file    app_ble.c
 * @brief   BLE 通信模块
 *
 * BleTask 职责:
 *   - 拉高 PB-03F BLE 唤醒引脚并接收任务消息
 *   - 消息命令处理当前为预留分支
 *   - 提供JSON状态上报接口，任务循环尚未调用该接口
 */
#include "main.h"
#include "app_ble.h"
#include "app_main_task.h"
#include "app_protect.h"
#include "drv_pb03f.h"
#include "mod_battery.h"
#include "mod_metering.h"
#include "comm_logger.h"
#include "comm_str.h"
#include "bsp_wdg.h"
#include "bsp_io.h"
#include "Board.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include <string.h>
#include <stdio.h>

/* JSON 缓冲 */
#define JSON_BUF_SIZE 256
static char s_json_buf[JSON_BUF_SIZE];

/*====================================================================
 * 状态上报
 *====================================================================*/

/**
 * @brief 读取电池和计量状态，组装JSON并通过BLE串口发送。
 * @note 使用静态发送缓冲区，调用方应避免并发调用。
 */
void app_ble_report_status(void)
{
    BATT_INFO_T snapshot;
    BATT_INFO_T *bat = battery_get_snapshot(&snapshot) ? &snapshot : NULL;
    METER_INFO_T *m = metering_get_info();

    int len = snprintf(s_json_buf, sizeof(s_json_buf), "{\"battery_valid\":%u", bat != NULL);

    if (bat)
    {
        len += snprintf(s_json_buf + len, sizeof(s_json_buf) - len,
                        ",\"soc\":%u,\"vbat\":%lu,\"cur\":%ld,\"die_temp\":%d", bat->soc,
                        (unsigned long)bat->voltage_mv, (long)bat->current_ma, bat->temp_deg);
    }
    if (m && m->online)
    {
        len += snprintf(s_json_buf + len, sizeof(s_json_buf) - len, ",");
        len += snprintf(s_json_buf + len, sizeof(s_json_buf) - len,
                        "\"vin\":%d.%u,\"vout\":%d.%u,\"pin\":%d,\"pout\":%d", (int)m->vin_v,
                        comm_str_frac(m->vin_v, 10), (int)m->vout_v,
                        comm_str_frac(m->vout_v, 10), (int)m->vin_p_w,
                        (int)m->vout_p_w);
    }
    len += snprintf(s_json_buf + len, sizeof(s_json_buf) - len, ",\"state\":%d,\"alm\":%lu}",
                    (int)state_get(), (unsigned long)app_protect_alarm_get());

    drv_pb03f_send((const uint8_t *)s_json_buf, len);
}

/*====================================================================
 * BLE 任务
 *====================================================================*/

/**
 * @brief 向BLE任务队列发送消息，最多等待3000个RTOS tick。
 * @param msg 待发送消息，调用方需提供有效指针。
 * @return true：消息入队；false：队列满。
 */
bool Send_Msg_AppBleTask(ucMESSAGE_t *msg)
{
    BaseType_t err;
    err = xQueueSend(xBleQueueHandle, msg, 3000);
    if (err == errQUEUE_FULL)
    {
        return false;
    }
    return true;
}

/**
 * @brief BLE任务入口：拉高唤醒引脚，接收预留消息并上报任务心跳。
 * @param arg 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
void BleTask(void *arg)
{
    uint32_t uwRet = 0;
    ucMESSAGE_t Msg = {0};

    bsp_gpio_write(BOARD_BLE_WKUP_PORT, BOARD_BLE_WKUP_PIN, GPIO_PIN_SET);
    osDelay(10);

    for (;;)
    {
        uwRet = xQueueReceive(xBleQueueHandle, &Msg, 100);
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

        /* 心跳 — 监控定时器检测任务存活 */
        bsp_wdg_task_heartbeat(WDG_TASK_BLE);
    }
}
