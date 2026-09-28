/**
 * @file    app_display_task.c
 * @brief   显示任务 — 串口屏 (DWIN T5L) 驱动
 *
 * 单页显示 (0.bmp):
 *   - 数值: battery(0x2002) / inputpower(0x2022) / outputpower(0x2026)
 *   - 图标: Icon1/2显示通路状态，Icon3(0x2008)显示市电接入，其余灭
 *   - 文本: Texterror(0x2030)
 */
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* App layer */
#include "app_display_task.h"
#include "app_main_task.h"

/* Module 层 */
#include "mod_battery.h"
#include "mod_metering.h"

/* BSP 层 */
#include "drv_dwin.h"
#include "bsp_wdg.h"
#include "Board.h"

/* Comm */
#include "comm_logger.h"

static const uint16_t s_icon_vp[] = {
    VP_ICON1, VP_ICON2, VP_ICON3, VP_ICON4, VP_ICON5,
    VP_ICON6, VP_ICON7, VP_ICON8, VP_ICON9, VP_ICON_PWR,
};

#define ICON_AC_INPUT_PRESENT 11u /* 屏端Icon3的市电接入图标索引。 */

#define ICON_COUNT (sizeof(s_icon_vp) / sizeof(s_icon_vp[0]))

/*====================================================================
 * 显示刷新
 * 图标: EVT_DISP_ICON 消息驱动 (按键处理时推送, 立即更新)
 * 数值: 500ms 周期刷新 (battery/metering 真实数据)
 *====================================================================*/

/* 图标状态 (EVT_DISP_ICON 消息写入) */
static uint8_t s_icon1_val = ICON_OFF;
static uint8_t s_icon2_val = ICON_OFF;

/**
 * @brief 刷新串口屏图标、电池SOC及输入输出功率。
 */
static void display_refresh(void)
{
    /* 图标: 消息驱动状态 */
    drv_dwin_set_value(VP_ICON1, s_icon1_val);
    drv_dwin_set_value(VP_ICON2, s_icon2_val);

    /* 跟随输入HLW8110的有效市电状态，独立于ACIN/ACOUT按键。
     * 90～300V确认有效时显示11；无输入、读取异常或数据过期时显示0。 */
    ac_input_snapshot_t ac_input = mod_metering_get_ac_input();
    drv_dwin_set_value(VP_ICON3,
                       ac_input.state == AC_INPUT_PRESENT ? ICON_AC_INPUT_PRESENT : ICON_OFF);

    /* Icon4及之后的未使用图标关闭，避免覆盖Icon3的市电状态。 */
    for (uint8_t i = 3; i < ICON_COUNT; i++)
    {
        drv_dwin_set_value(s_icon_vp[i], ICON_OFF);
    }

    /* 数值显示: 电池 SOC + 输入/输出功率 (真实数据) */
    BATT_INFO_T snapshot;
    BATT_INFO_T *bat = battery_get_snapshot(&snapshot) ? &snapshot : NULL;
    METER_INFO_T *m = metering_get_info();
    drv_dwin_set_text(0x2030, bat ? "" : "BAT DATA INVALID");
    drv_dwin_set_value(VP_BATTERY, (int16_t)(bat ? bat->soc : 0));
    drv_dwin_set_value(VP_INPUT_POWER, (int16_t)(m ? m->vin_p_w : 0));
    drv_dwin_set_value(VP_OUTPUT_POWER, (int16_t)(m ? m->vout_p_w : 0));
}

/*====================================================================
 * 任务入口
 *====================================================================*/

#define DISPLAY_TICK_MS 100
#define REFRESH_DIV 5 /* 无消息时约500ms；消息提前唤醒会缩短计数周期。 */

/**
 * @brief 向显示任务队列发送消息，不等待队列空间。
 * @param msg 待发送消息，调用方需提供有效指针。
 * @return true：消息入队；false：队列满。
 */
bool Send_Msg_AppDisplayTask(ucMESSAGE_t *msg)
{
    BaseType_t err;
    err = xQueueSend(xDisplayQueueHandle, msg, 0);
    if (err == errQUEUE_FULL)
    {
        return false;
    }
    return true;
}

/**
 * @brief 显示任务入口：响应图标消息，并按循环计数刷新数值。
 * @param argument 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
void DisplayTask(void *argument)
{
    uint32_t uwRet = 0;
    ucMESSAGE_t Msg = {0};
    uint32_t refresh_cnt = 0;

    display_refresh();

    for (;;)
    {
        uwRet = xQueueReceive(xDisplayQueueHandle, &Msg, pdMS_TO_TICKS(DISPLAY_TICK_MS));
        if (uwRet == pdPASS)
        {
            switch (Msg.MsgID)
            {
                /* 按键图标更新 (ACIN/ACOUT 通路切换), 立即整屏刷新 */
                case EVT_DISP_ICON:
                {
                    s_icon1_val = Msg.Paras.Value[0];
                    s_icon2_val = Msg.Paras.Value[1];
                    display_refresh();
                    break;
                }

                case 0:
                    break;

                default:
                    break;
            }
        }

        if (++refresh_cnt >= REFRESH_DIV)
        {
            refresh_cnt = 0;
            display_refresh();
        }

        /* 心跳 — 监控定时器检测任务存活 */
        bsp_wdg_task_heartbeat(WDG_TASK_DISP);
    }
}
