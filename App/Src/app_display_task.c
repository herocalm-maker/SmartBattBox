/**
 * @file    app_display_task.c
 * @brief   显示任务 — 串口屏 (DWIN T5L) 驱动
 *
 * 单页显示 (0.bmp 新界面):
 *   - 数值: 电量(0x2002) / 输入功率(0x2014) / 输出功率(0x2016)
 *   - 电量条: 底层静态图标(0x2030, soc/10 共 0~10 格) + 上层动画(0x2032, 充/放播放)
 *   - 图标: 充电(0x2004) / 逆变输出(0x200A) / 频率(0x200C) / 温度(0x200E) /
 *           时间(0x2018 固定13) / 蓝牙(0x2036 灭) / 旁路UPS(0x2038)
 *   - 文本: 异常(0x2010 "ERRxx") / 过载(0x2012 "OVERLOAD") /
 *           剩余时间(0x2020 "%uMIN") / 输出频率(0x2034)
 */
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* App layer */
#include "app_display_task.h"
#include "app_main_task.h"
#include "app_alarm.h"

/* Module 层 */
#include "mod_battery.h"
#include "mod_metering.h"

/* BSP 层 */
#include "drv_dwin.h"
#include "bsp_wdg.h"
#include "Board.h"

/* Comm */
#include "comm_logger.h"

/*====================================================================
 * 显示状态
 * 图标/通路: EVT_DISP_ICON 消息驱动 (按键/通路切换时推送, 立即更新)
 * 数值/文本: display_refresh 500ms 周期刷新 (真实数据)
 *====================================================================*/

/* 图标状态 (EVT_DISP_ICON 消息写入) */
static uint8_t s_icon_chg = ICON_OFF; /* 0x2004 充电 */
static uint8_t s_icon_out = ICON_OFF; /* 0x200A 逆变输出 */
static uint8_t s_icon_hz  = ICON_OFF; /* 0x200C 输出频率 */
static uint8_t s_icon_ups = ICON_OFF; /* 0x2038 旁路 (市电直通) */

/* 当前目标通路 (TextTime 充/放选择 + 动画使能依据) */
static energy_path_t s_path = PATH_NONE;

/*====================================================================
 * 显示刷新
 *====================================================================*/

/**
 * @brief 刷新串口屏全部显示项 (电量条/动画/图标/数值/文本)。
 * @note  500ms 周期调用; 图标由消息驱动, 此处仅叠加充满强制关/温度固定位。
 */
static void display_refresh(void)
{
    /* 电池快照: 充满(FStat.FQ)时充电图标强制关闭 + SOC 显示 100 */
    BATT_INFO_T snapshot;
    BATT_INFO_T *bat = battery_get_snapshot(&snapshot) ? &snapshot : NULL;
    METER_INFO_T *m = metering_get_info();
    bool bat_full = bat && bat->full_qualified;
    uint8_t soc = bat_full ? 100 : (bat ? bat->soc : 0);
    char txt[40];

    /* --- 电量条: 底层静态图标 0~10 格 (0%=0, 10%=1 ... 100%=10) --- */
    drv_dwin_set_value(VP_BATT_BAR, (int16_t)(soc / 10));

    /* --- 电量动画 (0x2032 使能): 充电/逆变播放, 其余停止 ---
     * 动画内容(充电流水 ICL 11-20 / 放电闪烁 21-22)由屏端组态固定;
     * 如需运行时切换需屏端提供动画参数可写 VP (见 VP_ANIM_BATT_CFG 说明)。 */
    uint8_t anim_en = (s_path == PATH_CHARGE || s_path == PATH_CHARGE_BYPASS ||
                       s_path == PATH_INVERT) ? ICON_ON : ICON_OFF;
    drv_dwin_set_value(VP_ANIM_BATT, (int16_t)anim_en);

    /* --- 图标: 消息驱动 + 充满强制关充电 / 温度固定位 --- */
    drv_dwin_set_value(VP_ICON_CHG,  bat_full ? ICON_OFF : s_icon_chg);
    drv_dwin_set_value(VP_ICON_OUT,  s_icon_out);
    drv_dwin_set_value(VP_ICON_HZ,   s_icon_hz);
    drv_dwin_set_value(VP_ICON_UPS,  s_icon_ups);
    drv_dwin_set_value(VP_ICON_TEMP, app_alarm_temp_high() ? ICON_TEMP_ON : ICON_OFF);
    drv_dwin_set_value(VP_ICON_TIME, ICON_TIME_ON); /* 固定显示 */
    drv_dwin_set_value(VP_ICON_BLE,  ICON_OFF);     /* 暂不显示 */

    /* --- 数值: 电量 / 输入功率 / 输出功率 --- */
    drv_dwin_set_value(VP_BATT_SOC,        (int16_t)soc);
    drv_dwin_set_value(VP_DATA_INPUT_PWR,  (int16_t)(m ? m->vin_p_w  : 0));
    drv_dwin_set_value(VP_DATA_OUTPUT_PWR, (int16_t)(m ? m->vout_p_w : 0));

    /* --- TextError (0x2010): 统一 ERR 码 "ERRxx", 正常写空 --- */
    uint8_t code = app_alarm_err_code();
    if (code != 0u)
    {
        snprintf(txt, sizeof txt, "ERR%02u", code);
    }
    else
    {
        txt[0] = '\0';
    }
    drv_dwin_set_text(VP_TEXT_ERR, txt);

    /* --- TextAlarm (0x2012): 输出过载显示 "OVERLOAD", 正常写空 ---
     * 过载阈值 = 额定功率×1.1 (Board.h BOARD_PWR_OVERLOAD_W) */
    bool overload = (m != NULL) && m->online && (m->vout_p_w > BOARD_PWR_OVERLOAD_W);
    drv_dwin_set_text(VP_TEXT_ALARM, overload ? "OVERLOAD" : "");

    /* --- TextTime (0x2020): 充电显示离充满时间 / 逆变显示可用时间 (纯分钟) --- */
    txt[0] = '\0';
    if (s_path == PATH_CHARGE || s_path == PATH_CHARGE_BYPASS)
    {
        if (bat && bat->ttf_valid && bat->time_to_full_min > 0)
        {
            snprintf(txt, sizeof txt, "%uMIN", (unsigned)bat->time_to_full_min);
        }
    }
    else if (s_path == PATH_INVERT)
    {
        if (bat && bat->tte_valid && bat->time_to_empty_min > 0)
        {
            snprintf(txt, sizeof txt, "%uMIN", (unsigned)bat->time_to_empty_min);
        }
    }
    drv_dwin_set_text(VP_TEXT_TIME, txt);

    /* --- TextHZ (0x2034): 逆变输出时显示频率 (取整), 不输出写空 --- */
    txt[0] = '\0';
    if (s_path == PATH_INVERT && m && m->online && (m->vout_freq_hz >= 1.0f))
    {
        snprintf(txt, sizeof txt, "%u", (unsigned)(m->vout_freq_hz + 0.5f));
    }
    drv_dwin_set_text(VP_TEXT_HZ, txt);
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
 * @brief 显示任务入口：响应通路/图标消息，并按循环计数刷新数值。
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
                /* 通路状态+图标更新 (ACIN/ACOUT 通路切换), 立即整屏刷新 */
                case EVT_DISP_ICON:
                {
                    s_path = (energy_path_t)Msg.Paras.Value[0];
                    s_icon_chg = Msg.Paras.Value[1];
                    s_icon_out = Msg.Paras.Value[2];
                    s_icon_hz  = Msg.Paras.Value[3];
                    s_icon_ups = Msg.Paras.Value[4];
                    refresh_cnt = REFRESH_DIV;
                    break;
                }

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
