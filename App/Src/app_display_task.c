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
#include "mod_temp.h"

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

/* TextTime 节流: 3s 检测一次, 分钟值变化才写屏 (TTE/TTF 取整抖动过滤) */
#define TEXT_TIME_CHECK_MS 3000u
static uint16_t s_time_min_shown = 0;
static bool     s_time_shown_valid = false;
static uint32_t s_time_check_ms = 0;

/* TextHZ 节流: 1s 检测一次, 频率值变化才写屏 */
#define TEXT_HZ_CHECK_MS 1000u
static uint16_t s_hz_shown = 0;
static bool     s_hz_shown_valid = false;
static uint32_t s_hz_check_ms = 0;

/*====================================================================
 * 显示刷新
 *====================================================================*/

/**
 * @brief TextTime 更新: 仅充电/放电时显示剩余时间, 其余情况清空。
 * @param now   当前 tick。
 * @param force true=通路事件触发立即判定(跳过节流); false=按 3s 节流。
 * @note  充电显示 TTF(离充满剩余), 放电显示 TTE(可用时间); 快照有效但未成熟显示 0 兜底;
 *        分钟值变化才写屏, 过滤 TTE/TTF 取整抖动。
 */
static void text_time_update(uint32_t now, bool force)
{
    if (!force && (uint32_t)(now - s_time_check_ms) < TEXT_TIME_CHECK_MS)
    {
        return;
    }
    s_time_check_ms = now;

    BATT_INFO_T snapshot;
    BATT_INFO_T *bat = battery_get_snapshot(&snapshot) ? &snapshot : NULL;
    bool time_visible = (s_path == PATH_CHARGE || s_path == PATH_CHARGE_BYPASS ||
                         s_path == PATH_INVERT);
    bool chg = (s_path == PATH_CHARGE || s_path == PATH_CHARGE_BYPASS);
    char txt[16];
    uint16_t v = 0;
    bool ok = false;
    if (time_visible && chg)
    {
        if (bat && bat->ttf_valid)
        {
            v = bat->time_to_full_min;
            ok = true;
        }
        else if (bat)
        {
            v = 0; /* 快照有效但 TTF 未成熟: 显示 0 兜底 */
            ok = true;
        }
    }
    else if (time_visible)
    {
        if (bat && bat->tte_valid)
        {
            v = bat->time_to_empty_min;
            ok = true;
        }
        else if (bat)
        {
            v = 0; /* 快照有效但 TTE 未成熟: 显示 0 兜底 */
            ok = true;
        }
    }
    if (ok)
    {
        if (!s_time_shown_valid || v != s_time_min_shown)
        {
            s_time_min_shown = v;
            s_time_shown_valid = true;
            snprintf(txt, sizeof txt, "%uMIN", (unsigned)v);
            drv_dwin_set_text(VP_TEXT_TIME, txt);
        }
    }
    else
    {
        s_time_shown_valid = false; /* 离开充/放: 立即清空 */
        drv_dwin_set_text(VP_TEXT_TIME, "");
    }
}

/**
 * @brief TextHZ 更新: 逆变输出时显示频率(0Hz 也写 0), 不输出写空。
 * @param now   当前 tick。
 * @param force true=通路事件触发立即判定(跳过节流); false=按 1s 节流。
 * @note  频率四舍五入取整, 值变化才写屏; 停止输出时立即清空一次。
 */
static void text_hz_update(uint32_t now, bool force)
{
    if (!force && (uint32_t)(now - s_hz_check_ms) < TEXT_HZ_CHECK_MS)
    {
        return;
    }
    s_hz_check_ms = now;

    METER_INFO_T *m = metering_get_info();
    char txt[8];
    if (s_path == PATH_INVERT && m && m->online)
    {
        uint16_t v = (uint16_t)(m->vout_freq_hz + 0.5f);
        if (!s_hz_shown_valid || v != s_hz_shown)
        {
            s_hz_shown = v;
            s_hz_shown_valid = true;
            snprintf(txt, sizeof txt, "%u", (unsigned)v);
            drv_dwin_set_text(VP_TEXT_HZ, txt);
        }
    }
    else if (s_hz_shown_valid)
    {
        s_hz_shown_valid = false; /* 停止输出: 立即清空 */
        drv_dwin_set_text(VP_TEXT_HZ, "");
    }
}

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

    /* --- 电量条: 静态图标(0x2030) 与 动画(0x2032) 互斥 ---
     * 充电: 动画播放(0x2032=1) + 底层静态失能(0x2030=0)
     * 逆变输出/静止: 动画停止(0x2032=0) + 底层显示 soc/10 格静态电量 */
    bool anim_play = (s_path == PATH_CHARGE || s_path == PATH_CHARGE_BYPASS);
    drv_dwin_set_value(VP_ANIM_BATT, anim_play ? ICON_ON : ICON_OFF);
    drv_dwin_set_value(VP_BATT_BAR,  anim_play ? ICON_OFF : (int16_t)(soc / 10));

    /* --- 图标: 消息驱动 + 充满强制关充电 / 温度固定位 --- */
    drv_dwin_set_value(VP_ICON_CHG,  bat_full ? ICON_OFF : s_icon_chg);
    drv_dwin_set_value(VP_ICON_OUT,  s_icon_out);
    drv_dwin_set_value(VP_ICON_HZ,   s_icon_hz);
    drv_dwin_set_value(VP_ICON_UPS,  s_icon_ups);
    /* --- 温度图标 (0x200E): 任一温度 >= 风扇开启阈值(35℃) 或 OTP 故障
     * 告警 -> 显示 8 (与 fan_manage 开启条件同步, 温度过高即有显示) --- */
    const TEMP_INFO_T *t = mod_temp_get();
    bool temp_hot = (t != NULL &&
                     (t->chg_temp >= BOARD_PROT_FAN_ON_TH ||
                      t->bat_temp >= BOARD_PROT_FAN_ON_TH ||
                      t->inv_temp >= BOARD_PROT_FAN_ON_TH)) ||
                    app_alarm_temp_high();
    drv_dwin_set_value(VP_ICON_TEMP, temp_hot ? ICON_TEMP_ON : ICON_OFF);
    /* --- VarIconTime (0x2018): 仅充电/放电(逆变输出)时显示图标 13, 其余隐藏 --- */
    bool time_visible = (s_path == PATH_CHARGE || s_path == PATH_CHARGE_BYPASS ||
                         s_path == PATH_INVERT);
    drv_dwin_set_value(VP_ICON_TIME, time_visible ? ICON_TIME_ON : ICON_OFF);
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

    uint32_t now = HAL_GetTick();

    /* 周期路径: TextTime/TextHZ 按节流刷新数值
     * (通路变化时的即时显示/清空由 EVT_DISP_ICON 消息 force 触发) */
    text_time_update(now, false);
    text_hz_update(now, false);
}

/*====================================================================
 * 任务入口
 *====================================================================*/

#define DISPLAY_TICK_MS 100
#define REFRESH_DIV 5 /* 无消息时约500ms；消息提前唤醒会缩短计数周期。 */

/* 文本控件初始化清空: 开机覆盖屏端默认文本 (如 TextTime 默认 "888HMIN")
 * 空格数取控件 VP 到下一占用 VP 的安全间隔 (≤ 间隔, 不越界相邻变量) */
static void display_init_clear_text(void)
{
    char sp[VP_TEXT_ERR_LEN];
    memset(sp, ' ', sizeof(sp));          /* ERR 6 字符: 0x2050~0x2052 */
    drv_dwin_set_text(VP_TEXT_ERR, sp);
    char spa[VP_TEXT_ALARM_LEN];
    memset(spa, ' ', sizeof(spa));        /* ALARM 8 字符: 0x2060~0x2063 */
    drv_dwin_set_text(VP_TEXT_ALARM, spa);
    char spt[VP_TEXT_TIME_LEN];
    memset(spt, ' ', sizeof(spt));        /* TIME 9 字符: 0x2036~0x203A */
    drv_dwin_set_text(VP_TEXT_TIME, spt);
    char sph[VP_TEXT_HZ_LEN];
    memset(sph, ' ', sizeof(sph));        /* HZ 2 字符: 0x2030 */
    drv_dwin_set_text(VP_TEXT_HZ, sph);
}

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

    /* 开机首刷: 覆盖屏端默认文本 (888HMIN 等), 并立即整屏刷新 */
    display_init_clear_text();
    display_refresh();

    for (;;)
    {
        /* 消息等待: 显示队列阻塞等待 (图标/通路事件) */
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
                    /* 通路变化: 立即更新 TextTime/TextHZ 的显示/清空, 不等节流周期 */
                    text_time_update(HAL_GetTick(), true);
                    text_hz_update(HAL_GetTick(), true);
                    break;
                }

                default:
                    break;
            }
        }

        /* 数值刷新: 按计数周期整屏刷新 (消息驱动图标, 周期驱动数值) */
        if (++refresh_cnt >= REFRESH_DIV)
        {
            refresh_cnt = 0;
            display_refresh();
        }

        /* 心跳: 监控定时器检测任务存活 */
        bsp_wdg_task_heartbeat(WDG_TASK_DISP);
    }
}
