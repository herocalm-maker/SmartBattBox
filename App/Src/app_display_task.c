/**
 * @file    app_display_task.c
 * @brief   显示任务 — 串口屏 (DWIN T5L) 驱动
 *
 * 单页显示 (0.bmp 界面, 组态 2026-11-15):
 *   - 数值: 电量(艺术字 0x5100) / 输入功率(0x5104) / 输出功率(0x510A)
 *   - 电量条: 静态图标 0x2070, soc/10 → 0~10 格 + 充电动画(0x2040)
 *   - 时间: 艺术字 0x2106 (单位 0.1h) + 时间图标 0x2102 + 单位图标 0x2120
 *   - 温度: 艺术字 0x2130 (整数 ℃) 超阈值转红 + 单位图标 0x2140 + 超温动画 0x2150
 *   - 频率: 艺术字 0x2100 + 频率图标 0x2006
 *   - 图标: 充电(0x2002) / 逆变输出(0x2004) / 旁路UPS(0x2008) / 蓝牙(0x200A)
 *   - 文本: 异常(0x2050 "ERRxx") / 过载(0x2060 "OVERLOAD" 2.0s 闪动)
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
#include "drv_pb03f.h"   /* 蓝牙连接状态 (决定 BLE 图标显隐) */
#include "bsp_wdg.h"
#include "Board.h"

/* Comm */
#include "comm_logger.h"

/*====================================================================
 * 显示状态
 * 图标/通路: EVT_DISP_ICON 消息驱动 (按键/通路切换时推送, 立即更新)
 * 数值/文本: display_refresh 500ms 周期刷新 (真实数据)
 *====================================================================*/

/* 时间数值节流: 500ms 检测一次 (整屏刷新约 1.2s, 节流不再叠加额外延迟);
 * 只在 显隐/数值 变化时写屏, 不产生多余报文。 */
#define TEXT_TIME_CHECK_MS 500u

/* 频率数值节流: 1s 检测一次, 值变化才写屏 */
#define TEXT_HZ_CHECK_MS 1000u

/* 负载在位判定: 交流输出功率 > 1W 视为接了负载 (与主任务空载判据一致) */
#define TEXT_TIME_LOAD_W 1.0f

/* ArtisticVarTime 数值上限 (整数小时): 999h, 防显示越界 */
#define TIME_HOURS_MAX   999u

/* 时间显示防抖与单位迟滞:
 *   1) 无效后连续满 TIME_HIDE_HOLD_MS 才真正隐藏 —— TTF 有效性靠
 *      "平均电流 >50mA" 的硬阈值, 充电电流擦边时每 3s 会翻转一次;
 *      快照新鲜度阈值(3500ms)与采样周期(3000ms)也只有 500ms 余量,
 *      不做防抖就会"灭一下、亮一下"。
 *   2) 单位在 55~59min 保持上一次的档位, 避免 60min 附近来回跳 MIN/H。 */
#define TIME_HIDE_HOLD_MS   3000u
#define TIME_UNIT_HYST_MIN  55u

/* 温度数值分色开关:
 *   0 = 按屏端默认颜色显示 (当前红色字库未提供, 默认关闭)
 *   1 = 超阈值 (>=35℃) 切红色 ICL 库, 回落到 <=30℃ 切回白色。
 * 打开前需确认 LIB_TEMP_WHITE / LIB_TEMP_RED 库号与屏端字库一致。 */
#define TEMP_VALUE_COLOR_ENABLE 0


/**
 * @brief 显示任务状态结构体: 统一管理图标、通路、节流状态 */
typedef struct
{
    /* 图标状态 (EVT_DISP_ICON 消息写入) */
    uint8_t icon_chg;  /* 0x2002 充电 */
    uint8_t icon_out;  /* 0x2004 逆变输出 */
    uint8_t icon_hz;   /* 0x2006 输出频率 */
    uint8_t icon_ups;  /* 0x2008 旁路 (市电直通) */

    /* 时间数值节流 (ArtisticVarTime, 单位 0.1h) */
    uint32_t time_check_ms;   /* 上次检测时刻 */

    /* 频率数值节流 (ArtisticVarHZ) */
    struct {
        uint16_t hz_shown;    /* 已写入的数值 0xFFFF=尚未写过 */
        uint32_t check_ms;
    } hz;

    /* 电量分色当前档位 (0=白 1=橙 2=红 0xFE=空白库隐藏 0xFF=尚未初始化) */
    uint8_t soc_level;
} display_state_t;

static display_state_t s_disp = {
    .icon_chg = ICON_OFF,
    .icon_out = ICON_OFF,
    .icon_hz = ICON_OFF,
    .icon_ups = ICON_OFF,
    .time_check_ms = 0,
    .hz = {0xFFFFu, 0},
    .soc_level = 0xFFu,
};

/* 电量显示"空白"哨兵: s_disp.soc_level 专用 (0/1/2 = 白/橙/红正常色) */
#define SOC_LEVEL_BLANK   0xFEu

/* 时间显示已写入屏的状态 0xFF/0xFFFF 表示未知, 首次必写一次 */
static uint16_t s_time_last_val = 0xFFFFu;   /* 已写入的数值 (分钟或小时) */
static uint8_t  s_time_icon_on  = 0xFFu;     /* 0x2102 时间图标: 0/1 */
static uint8_t  s_time_unit_cur = 0xFFu;     /* 0x2120 单位图标: 0/1(MIN)/2(H) */
static bool     s_time_shown;                /* 当前是否处于显示态 (防抖用) */
static uint32_t s_time_invalid_since;        /* 连续无效起始时刻, 0=当前有效 */
static bool     s_time_use_hour;             /* 单位档位 (带迟滞) */

/* 电池温度超阈值状态 (5℃ 迟滞, 与 fan_manage 一致):
 * 由 art_temp_update() 维护, display_refresh() 用于驱动温度动画。 */
static bool s_temp_hot;

/* 温度数值已写入屏的状态 (变化或每 2s 重申) */
static int16_t  s_temp_last_deg = INT16_MIN;
static uint32_t s_temp_last_ms;

/**
 * @brief 设置时间图标 (0x2102) 与单位图标 (0x2120)。
 * @param show true=显示; false=隐藏 (两个图标都写 0)。
 * @param unit show=true 时写入的单位图标: ICON_UNIT_H(3) / ICON_UNIT_MIN(4)。
 * @note  只在状态变化时写屏, 避免每 500ms 重复发帧。 */
static void time_chrome_set(bool show, uint8_t unit)
{
    static uint32_t s_chrome_ms;   /* 上次重申时刻 */
    uint32_t now = HAL_GetTick();
    uint8_t icon = show ? (uint8_t)ICON_TIME_ON : (uint8_t)ICON_OFF;
    uint8_t u    = show ? unit : (uint8_t)ICON_UNIT_OFF;
    bool refresh = (uint32_t)(now - s_chrome_ms) >= 5000u;

    if (icon != s_time_icon_on)
    {
        s_time_icon_on = icon;
        drv_dwin_set_value(VP_ICON_TIME, (int16_t)icon);
    }
    else if (refresh)
    {
        drv_dwin_set_value(VP_ICON_TIME, (int16_t)icon);   /* 每 5s 重申, 防屏端复位丢失 */
    }

    if (u != s_time_unit_cur)
    {
        s_time_unit_cur = u;
        drv_dwin_set_value(VP_ICON_TIME_UNIT, (int16_t)u);
        /* 现场诊断: 0=隐藏 1=MIN 2=H */
        logger_out(LOG_INFO, "time unit -> %u (0x2120)", (unsigned)u);
    }
    else if (refresh)
    {
        drv_dwin_set_value(VP_ICON_TIME_UNIT, (int16_t)u);  /* 每 5s 重申 */
    }

    if (refresh)
    {
        s_chrome_ms = now;
    }
}

/*====================================================================
 * ArtisticVar* 数值显示 (切 ICL 库: 库号在 SP+4 高字节, 低字节=显示模式 0 透明)
 *   ArtisticVarTime: SP 0x5900 → 0x5904
 *       59_num_18_36.icl (0x3B) 正常 / 60_num_18_36_empty.icl (0x3C) 空白
 *   ArtisticVarHZ  : SP 0x5600 → 0x5604, 同样 0x3B / 0x3C
 *====================================================================*/
static uint8_t s_time_val_vis = 0xFFu;   /* 0=空白 1=正常 0xFF=未知 */
static uint8_t s_hz_vis       = 0xFFu;   /* 频率数值 0=隐藏 1=显示 0xFF=未知 */

/**
 * @brief 用切 ICL 库的方式显示/隐藏 ArtisticVarTime 的数值。
 * @param on true=切 51_num_24_48 (显示); false=切 58_num_24_48_empty (隐藏)。
 * @note  仅在状态变化时写屏。 */
static void time_value_set_visible(bool on)
{
    uint8_t want = on ? 1u : 0u;
    if (s_time_val_vis == want)
    {
        return;
    }
    s_time_val_vis = want;
    drv_dwin_set_value(VP_ART_TIME_LIB,
                       (int16_t)((uint16_t)(on ? LIB_TIME_NORMAL : LIB_TIME_BLANK) << 8));
}

/**
 * @brief 应用 ArtisticVarHZ 的显示/隐藏 (切 ICL 库, 带状态缓存不重复发帧)。
 * @param on true=显示 (51_num_24_48); false=隐藏 (58_num_24_48_empty)。
 * @note  0xFF 为"未知"初值, 保证上电第一拍一定执行一次隐藏;
 *        屏端不支持回读, 正常库号直接写死 (LIB_HZ_NORMAL)。 */
static void hz_display_set(bool on)
{
    uint8_t want = on ? 1u : 0u;
    if (s_hz_vis == want)
    {
        return;
    }
    s_hz_vis = want;
    drv_dwin_set_value(VP_ART_HZ_LIB,
                       (int16_t)((uint16_t)(on ? LIB_HZ_NORMAL : LIB_HZ_BLANK) << 8));
}

/*====================================================================
 * 显示刷新
 *====================================================================*/

/**
 * @brief ArtisticVarTime 更新: 充电显示距充满, 逆变输出显示电池可用时间。
 * @param now   当前 tick。
 * @param force true=通路事件触发立即判定(跳过节流); false=按 500ms 节流。
 * @note  屏端 2026-11-15 组态: 数值 ArtisticVarTime(0x2106) + 时间图标 VarIconTime(0x2102)
 *        + 单位图标 VARIconUnit(0x2120)。显示规则:
 *          1) 充电中                         → 距充满剩余(TTF);
 *          2) 未充电 + 逆变输出 + 已接负载   → 电池可用时间 (TTE);
 *          3) 旁路输出(市电在用) / 无输出    → 隐藏 (数值归零 + 两个图标灭);
 *          4) 估算未成熟 (TTF/TTE 无效)      → 隐藏。
 *        数值 <1h 显示分钟 (单位图标 4=MIN); >=1h 显示整数小时
 *        (单位图标 3=H, 四舍五入: 90min→2h), 小时上限 999。
 *        只有取到有效时间才点亮三件套 (数值 + 时间图标 + 单位图标);
 *        无效 (旁路/无输出/估算未成熟) 时三者同时隐藏。
 *        估算由 mod_battery 自算 (10 样本中值 + 一阶低通), 单位分钟。 */
static void art_time_update(uint32_t now, bool force)
{
    if (!force && (uint32_t)(now - s_disp.time_check_ms) < TEXT_TIME_CHECK_MS)
    {
        return;
    }
    s_disp.time_check_ms = now;

    BATT_INFO_T snapshot;
    BATT_INFO_T *bat = battery_get_snapshot(&snapshot) ? &snapshot : NULL;
    METER_INFO_T *m = metering_get_info();

    /* 充电图标亮=充电(TTF); 逆变图标亮=无 220V 的输出; 有载=输出功率 >1W */
    bool chg = (s_disp.icon_chg == ICON_CHG_ON);
    bool inverting = (s_disp.icon_out == ICON_OUT_ON);
    bool load_on = (m != NULL) && m->out_ok && (m->vout_p_w > TEXT_TIME_LOAD_W);

    uint16_t min_val = UINT16_MAX;  /* UINT16_MAX = 无有效值 → 隐藏 */

    if (chg)
    {
        /* 规则 1: 充电 → 距充满剩余 */
        if (bat != NULL && bat->ttf_valid)
        {
            min_val = bat->time_to_full_min;
        }
    }
    else if (inverting && load_on && bat != NULL && bat->tte_valid)
    {
        /* 规则 2: 无 220V + 逆变输出 + 已接负载 → 电池可用时间 */
        min_val = bat->time_to_empty_min;
    }

    if (min_val == UINT16_MAX)
    {
        /* 无效/不适用: 已经显示过的, 连续无效满 TIME_HIDE_HOLD_MS 才真正隐藏;
         * 否则 TTF 有效性(平均电流>50mA 硬阈值)或快照新鲜度抖动时,
         * 三件套会"灭一下、亮一下"。 */
        if (s_time_shown)
        {
            if (s_time_invalid_since == 0u)
            {
                s_time_invalid_since = now;
            }
            if ((uint32_t)(now - s_time_invalid_since) < TIME_HIDE_HOLD_MS)
            {
                return;   /* 防抖窗口内: 保持当前显示不动 */
            }
            s_time_invalid_since = 0;
            s_time_shown = false;
        }
        /* 隐藏 (time_chrome_set / time_value_set_visible 内部有状态缓存),
         * 重复调用不会重复发帧; 上电首拍仍会写一次以确保初始态正确。 */
        time_chrome_set(false, ICON_UNIT_OFF);
        time_value_set_visible(false);
        /* 切库后补写一次数值强制重绘: 部分 DGUS 版本切库不会立刻重画,
         * 会把上一次的数字留在屏上 (空白库下这次写入不可见) */
        if (s_time_last_val != 0u)
        {
            s_time_last_val = 0u;
            drv_dwin_set_value(VP_ART_TIME, 0);
        }
        return;
    }

    /* 有效: 清防抖计数 */
    s_time_invalid_since = 0;
    s_time_shown = true;

    /* 单位档位带迟滞: >=60min 用小时, <55min 用分钟, 55~59 保持上一次;
     * 避免剩余时间在 60min 附近让 MIN/H 来回跳。 */
    if (min_val >= 60u)
    {
        s_time_use_hour = true;
    }
    else if (min_val < TIME_UNIT_HYST_MIN)
    {
        s_time_use_hour = false;
    }

    uint16_t value;
    uint8_t  unit;
    if (s_time_use_hour)
    {
        uint32_t hours = ((uint32_t)min_val + 30u) / 60u;
        if (hours > TIME_HOURS_MAX)
        {
            hours = TIME_HOURS_MAX;
        }
        value = (uint16_t)hours;
        unit  = ICON_UNIT_H;
    }
    else
    {
        value = min_val;
        unit  = ICON_UNIT_MIN;
    }

    /* 显示: 时间图标 + 单位图标(H/MIN), 数值切回正常库; 恢复显示时强制写一次数值 */
    bool was_hidden = (s_time_val_vis == 0u);
    time_chrome_set(true, unit);
    time_value_set_visible(true);
    if (was_hidden || value != s_time_last_val)
    {
        s_time_last_val = value;
        drv_dwin_set_value(VP_ART_TIME, (int16_t)value);
    }
}

/**
 * @brief ArtisticVarHZ 更新: 逆变输出或旁路时显示频率, 只显示 50/60 两档, 默认 50。
 * @param now   当前 tick。
 * @param force true=通路事件触发立即判定(跳过节流); false=按 1s 节流。
 * @note  实测频率就近取档 (<55Hz → 50, >=55Hz → 60); 计量无效/读数为 0
 *        时按默认 50 显示; 值变化才写屏; 停止输出时写 0 清空。
 *        屏端 2026-11-15 组态: 数值 ArtisticVarHZ(0x2100), 图标 VarIconHZ(0x2006)。 */
static void art_hz_update(uint32_t now, bool force)
{
    if (!force && (uint32_t)(now - s_disp.hz.check_ms) < TEXT_HZ_CHECK_MS)
    {
        return;
    }
    s_disp.hz.check_ms = now;

    METER_INFO_T *m = metering_get_info();
    /* 逆变输出或旁路时才有频率; 其余情况 (含上电首拍) 一律隐藏 */
    bool has_output = (s_disp.icon_out == ICON_OUT_ON || s_disp.icon_ups == ICON_SIDE_ON);

    if (!has_output)
    {
        hz_display_set(false);
        /* 切库后补写一次数值强制重绘 (空白库下不可见) */
        if (s_disp.hz.hz_shown != 0u)
        {
            s_disp.hz.hz_shown = 0u;
            drv_dwin_set_value(VP_ART_HZ, 0);
        }
        return;
    }

    uint8_t prev = s_hz_vis;
    hz_display_set(true);

    /* 频率只显示 50/60 两档: 实测值就近取档, 无有效读数默认 50 */
    float f = (m != NULL && m->online) ? m->vout_freq_hz : 0.0f;
    uint16_t v = (f >= 55.0f) ? 60u : 50u;
    /* 刚从隐藏恢复时强制写一次 (隐藏期间数值可能被写 0 顶替) */
    if (prev != 1u || v != s_disp.hz.hz_shown)
    {
        s_disp.hz.hz_shown = v;
        drv_dwin_set_value(VP_ART_HZ, (int16_t)v);
    }
}

/**
 * @brief ArtisticVarTemp 更新: 显示电池 NTC 温度 (整数 ℃)。
 * @note  屏端 2026-11-15 组态 ArtisticVarTemp(0x2130), SP 0x5800。
 *        颜色: 默认不切库, 用屏端组态色 (红色字库暂未提供);
 *        需要在超阈值转红时把 TEMP_VALUE_COLOR_ENABLE 改成 1。
 *        阈值滞回 (>=35℃ 置位 / <=30℃ 复位) 仍在维护 s_temp_hot,
 *        供 display_refresh() 驱动温度动画 (0x2150) 使用。
 *        负温度在艺术字控件上无法显示负号, 统一钳到 0。
 *        温度模块未完成首次采样 (t->valid == false) 时不写屏 ——
 *        否则会上电瞬间把 0.0℃ 当成有效温度显示出来。
 *        数值变化或每 2s 重申一次才写屏 (防止屏端丢帧后一直停在旧值)。 */
static void art_temp_update(void)
{
#if TEMP_VALUE_COLOR_ENABLE
    static uint8_t s_last_lib;
#endif

    const TEMP_INFO_T *t = mod_temp_get();
    if (t == NULL || !t->valid)
    {
        return;   /* 上电初期 ADC 还没采过: 不写 0, 等有效数据 */
    }

    /* 阈值滞回: bat / chg 任一 >=35℃ 置位, 两者都 <=30℃ 复位 (与主风扇一致)。
     * 说明: 逆变器 NTC(inv) 不参与动画 —— 按产品定义温度动画只提示电池/充电回路;
     *       若以后要包含 inv, 在 hot/cool 里加上 t->inv_temp 即可。
     * 温度数值仍只显示电池 NTC (按产品定义)。 */
    bool hot = (t->bat_temp >= BOARD_PROT_FAN_ON_TH) ||
               (t->chg_temp >= BOARD_PROT_FAN_ON_TH);
    bool cool = (t->bat_temp <= BOARD_PROT_FAN_OFF_TH) &&
                (t->chg_temp <= BOARD_PROT_FAN_OFF_TH);
    bool prev = s_temp_hot;
    if (hot)
    {
        s_temp_hot = true;
    }
    else if (cool)
    {
        s_temp_hot = false;
    }
    if (s_temp_hot != prev)
    {
        /* 现场诊断: 动画开关时打印三路原始温度 (单位 0.1℃) */
        logger_out(LOG_INFO, "TEMP anim %s (0.1C): bat=%d chg=%d inv=%d",
                   s_temp_hot ? "ON" : "OFF",
                   (int)t->bat_temp, (int)t->chg_temp, (int)t->inv_temp);
    }

    /* 0.1℃ -> 整数摄氏度, 四舍五入; 负温度钳到 0 */
    int deg = (t->bat_temp >= 0) ? ((t->bat_temp + 5) / 10) : 0;

    /* 变化即写; 数值不变也每 2s 重申一次, 防止屏端丢帧后停在组态初值 */
    uint32_t now = HAL_GetTick();
    if ((int16_t)deg != s_temp_last_deg ||
        (uint32_t)(now - s_temp_last_ms) >= 2000u)
    {
        s_temp_last_deg = (int16_t)deg;
        s_temp_last_ms = now;
        drv_dwin_set_value(VP_ART_TEMP, (int16_t)deg);
    }

#if TEMP_VALUE_COLOR_ENABLE
    uint8_t lib = s_temp_hot ? LIB_TEMP_RED : LIB_TEMP_WHITE;
    if (lib != s_last_lib)
    {
        s_last_lib = lib;
        drv_dwin_set_value(VP_ART_TEMP_LIB, (int16_t)((uint16_t)lib << 8));
    }
#endif
}

/**
 * @brief 超温动画 AnimiconTermp(0x2150) 的写入: 边沿触发 + 每 5s 重申一次。
 * @note  屏端组态: 停止=0 / 开始=1 / 循环播放。
 *        只在状态翻转时写, 避免每 500ms 重发把动画反复拉回第一帧;
 *        同时每 5s 重申一次, 屏端复位或页面重载后能自动恢复。 */

static void temp_anim_update(void)
{
    static uint8_t  s_sent = 0xFFu;    /* 上次写入值 */
    static uint32_t s_sent_ms;         /* 上次写入时刻 */

    uint8_t want = s_temp_hot ? ANIM_TEMP_ON : ANIM_TEMP_OFF;
    uint32_t now = HAL_GetTick();

    if (want == s_sent && (uint32_t)(now - s_sent_ms) < 5000u)
    {
        return;
    }
    s_sent = want;
    s_sent_ms = now;
    drv_dwin_set_value(VP_ANIM_TEMP, (int16_t)want);
}

/**
 * @brief 风扇动画 AnimationFan(0x510E) 的写入: 边沿触发 + 每 5s 重申一次。
 * @note  屏端组态: 停止=0 / 开始=1 / 图标 1~2 / 单帧 15 DGUS 周期 / 循环播放。
 *        不能每 500ms 重发: 2 帧一轮约 600ms, 重发会把动画反复拉回起始帧,
 *        视觉上就像卡在第 1 帧不动。
 *        每 5s 重申一次, 屏端复位/页面重载后能自愈。 */
static void fan_anim_update(void)
{
    static uint8_t  s_sent = 0xFFu;    /* 上次写入值 */
    static uint32_t s_sent_ms;         /* 上次写入时刻 */

    uint8_t want = app_ui_fan_running() ? ANIM_FAN_ON : ANIM_FAN_OFF;
    uint32_t now = HAL_GetTick();

    if (want == s_sent && (uint32_t)(now - s_sent_ms) < 5000u)
    {
        return;
    }
    if (want != s_sent)
    {
        /* 现场诊断: 区分"固件没发"还是"屏端不播" */
        logger_out(LOG_INFO, "fan anim -> %u (0x510E)", (unsigned)want);
    }
    s_sent = want;
    s_sent_ms = now;
    drv_dwin_set_value(VP_ANIM_FAN, (int16_t)want);
}

/**
 * @brief 刷新串口屏全部显示项 (电量/动画/图标/数值/文本)。
 * @note  500ms 周期调用; 图标由消息驱动, 此处仅叠加充满强制关/温度固定位。
 *        上电后 BOOT_BURST_MS 内会清空"已写入"缓存并加快刷新。
 *        屏端 T5L 上电比 MCU 慢 1~2s, 此前的帧会丢失, 屏会先显示组态初值 0;
 *        清缓存后可保证屏一就绪就在 200ms 内被正确内容覆盖。 */
#define BOOT_BURST_MS    8000u

static void display_cache_invalidate(void)
{
    s_disp.soc_level   = 0xFFu;
    s_disp.hz.hz_shown = 0xFFFFu;
    s_time_icon_on     = 0xFFu;
    s_time_unit_cur    = 0xFFu;
    s_time_last_val    = 0xFFFFu;
    s_time_val_vis     = 0xFFu;   /* 时间数值库: 必须重置, 否则切页后屏恢复
                                   * 组态默认(正常)库, 而我们以为已隐藏, 残留 0 */
    s_hz_vis           = 0xFFu;
    s_temp_last_deg    = INT16_MIN;
}

/* 文本控件写入日志: 只在内容变化时打印一条, 避免 500ms 周期刷屏。
 * 内容形如: TEXT ERR vp=0x2050 len=6 "" hex=[]              (清空)
 *           TEXT ERR vp=0x2050 len=6 "ERR20" hex=[45 52 ...] (显示)
 * 用途: 屏端是黑盒, 用串口日志核对固件实际发出去的字符与长度。 */
static void text_tx_log(const char *tag, const char *text, uint8_t chars, uint16_t vp)
{
    /* 初值用非空哨兵: 上电第一次写"空文本"(清屏) 也要打一条日志,
     * 否则无法从串口确认开机清空动作是否发出 */
    static char s_last_err[16]   = { 1 };
    static char s_last_alarm[16] = { 1 };

    char *last = (vp == VP_TEXT_ERR) ? s_last_err : s_last_alarm;
    const char *s = (text != NULL) ? text : "";

    if (strncmp(last, s, sizeof(s_last_err) - 1u) == 0)
    {
        return;   /* 内容未变: 不重复打印 */
    }
    strncpy(last, s, sizeof(s_last_err) - 1u);
    last[sizeof(s_last_err) - 1u] = '\0';

    static const char hx[] = "0123456789ABCDEF";
    char hex[48];
    uint8_t n = 0;
    for (uint8_t i = 0u; i < chars && s[i] != '\0' && n < 44u; i++)
    {
        uint8_t c = (uint8_t)s[i];
        hex[n++] = hx[c >> 4];
        hex[n++] = hx[c & 0x0Fu];
        hex[n++] = ' ';
    }
    hex[n] = '\0';

    /* wr = 按偶数补齐后实际写入的字节数 (0x82 写 VP 按字寻址, 奇数帧会被屏丢弃) */
    uint16_t wr = chars;
    if (wr & 1u)
    {
        wr++;
    }

    logger_out(LOG_INFO, "TEXT %s vp=0x%04X len=%u wr=%u \"%s\" hex=[%s]",
               tag, (unsigned)vp, (unsigned)chars, (unsigned)wr, s, hex);
}

static void display_refresh(void)
{
    /* 上电窗口: 每拍强制重发, 抢在屏端显示组态初值之前/之后尽快纠正 */
    static uint32_t s_boot_ms;
    static bool s_boot_init;
    if (!s_boot_init)
    {
        s_boot_init = true;
        s_boot_ms = HAL_GetTick();
    }
    bool boot_win = (uint32_t)(HAL_GetTick() - s_boot_ms) < BOOT_BURST_MS;
    if (boot_win)
    {
        display_cache_invalidate();
    }

    /* 文本控件前景色: 描述指针 + 3 = RGB565 (已实测确认)。
     * 上电 3s 后首次写入(屏启动完成、属性区初始化之后), 之后每 10s 重申一次,
     * 防止屏端因页面刷新/重新初始化把属性区复位导致颜色丢失。 */
    static const uint16_t s_text_sp[] = { VP_TEXT_ERR_SP, VP_TEXT_ALARM_SP };
    static uint32_t s_err_color_next;
    static bool s_err_color_logged;
    uint32_t now_ms = HAL_GetTick();
    if (now_ms >= 3000u && (int32_t)(now_ms - s_err_color_next) >= 0)
    {
        s_err_color_next = now_ms + 10000u;
        for (uint8_t i = 0; i < sizeof(s_text_sp) / sizeof(s_text_sp[0]); i++)
        {
            drv_dwin_set_value((uint16_t)(s_text_sp[i] + TEXT_SP_COLOR_OFS),
                               (int16_t)TEXT_COLOR_RED);
        }
        if (!s_err_color_logged)
        {
            s_err_color_logged = true;
            logger_out(LOG_INFO, "text color -> red (ERR %04X, ALARM %04X)",
                       VP_TEXT_ERR_COLOR, VP_TEXT_ALARM_COLOR);
        }
    }

    /* 电池快照: "充满"统一由 battery_full_active() 判定 (FQ/锁存 且 SOC>=90),
     * 单看标志会在锁存残留时把 3% 的电池显示成 100%(现场踩过)。SOC 数字始终
     * 用真实值; 仅满充时把取整到 99% 的 RepSOC 补成 100%。 */
    BATT_INFO_T snapshot;
    BATT_INFO_T *bat = battery_get_snapshot(&snapshot) ? &snapshot : NULL;
    METER_INFO_T *m = metering_get_info();
    uint8_t soc = bat ? bat->soc : 0;                /* 真实 SOC, 不再被"满"覆盖 */
    bool bat_full = bat && battery_full_active();
    if (bat_full && (unsigned)soc >= 99u)
    {
        soc = 100u;
    }
    char txt[40];

    /* --- 电量条: 静态图标(0x2070) 与动画(0x2040) 互斥 ---
     * 充电: 动画播放(0x2040=1~10) + 底层静态失能(0x2070=0)
     * 逆变输出/静止: 动画停止(0x2040=0) + 底层显示 soc/10 格静态电量 */
    bool anim_play = (s_disp.icon_chg == ICON_CHG_ON);
    /* 写入 1 触发循环播放 (帧 0→1→...→10→0), 写入 0 停止 */
    drv_dwin_set_value(VP_ANIM_BATT, anim_play ? 1 : 0);
    drv_dwin_set_value(VP_BATT_BAR,  anim_play ? ICON_OFF : (int16_t)(soc / 10));

    /* --- 图标: 消息驱动 + 充满强制关充电 --- */
    uint8_t chg_icon = bat_full ? ICON_OFF : s_disp.icon_chg;
    drv_dwin_set_value(VP_ICON_CHG,  chg_icon);
    drv_dwin_set_value(VP_ICON_OUT,  s_disp.icon_out);
    drv_dwin_set_value(VP_ICON_HZ,   s_disp.icon_hz);
    drv_dwin_set_value(VP_ICON_SIDE, s_disp.icon_ups);

    /* --- 温度: 数值 (art_temp_update 内部写) ---
     * 单位图标 VARIconTempUnit(0x2140) 屏端默认已显示, 开机写一次即可 (见 DisplayTask) */
    art_temp_update();
    /* --- 超温动画 AnimiconTermp(0x2150): 屏端 停止值=0 / 开始值=1 --- */
    temp_anim_update();

    /* --- 蓝牙图标 (0x200A): 仅在 BLE 已连接时显示 12, 未连接则隐藏 --- */
    bool ble_on = (drv_pb03f_get_conn_state() == BLE_STATE_CONNECTED);
    drv_dwin_set_value(VP_ICON_BLE, ble_on ? ICON_BLE_ON : ICON_OFF);

    /* --- 风扇动画 (0x510E): 有风扇运行则播放, 边沿触发避免反复重启 --- */
    fan_anim_update();

    /* --- 数值: 电量 / 输入功率 / 输出功率 (艺术字变量, 滤波值防显示跳变) ---
     * 电量分色: 数值 + SP+4 切 ICL 库 (空白 0x28 / 白 0x35 / 橙 0x36 / 红 0x37)
     *   >= 20% 白 / 10~20% 橙 / < 10% 红; 无有效快照时切空白库隐藏 */
    if (bat == NULL)
    {
        /* 电池快照无效 (上电初期 MAX17263 尚未出数 / 通讯异常):
         * 切 40_num_72_170_empty 空白库, 让屏上连"0"都不显示 ——
         * 否则屏端组态的初始值会露出一个白色 0。
         * 有效数据到达后由下面分支切回 53/54/55 并按 SOC 分色。
         * 注意: 这里不写数值 (VP 0x5100), 只切库。 */
        if (s_disp.soc_level != SOC_LEVEL_BLANK)
        {
            s_disp.soc_level = SOC_LEVEL_BLANK;
            drv_dwin_set_value(VP_BATT_SOC_LIB,
                               (int16_t)((uint16_t)LIB_BATT_EMPTY << 8));
            /* 切库后补写一次数值强制重绘 (空白库下不可见) */
            drv_dwin_set_value(VP_BATT_SOC, 0);
        }
    }
    else
    {
        uint8_t soc_level = 0;  /* 0=白 1=橙 2=红 */
        if ((unsigned)soc < BOARD_SOC_COLOR_RED_PCT)
        {
            soc_level = 2;
        }
        else if ((unsigned)soc < BOARD_SOC_COLOR_ORANGE_PCT)
        {
            soc_level = 1;
        }

        if (soc_level != s_disp.soc_level)
        {
            /* 档位变化: 切 ICL 库 (SP+4 高字节), 低字节 0x00 = 透明显示模式 */
            uint16_t lib = (soc_level == 0u) ? LIB_BATT_WHITE
                         : (soc_level == 1u) ? LIB_BATT_ORANGE
                                             : LIB_BATT_RED;
            drv_dwin_set_value(VP_BATT_SOC_LIB, (int16_t)(lib << 8));
            drv_dwin_set_value(VP_SOC_COLOR_LEVEL, (int16_t)soc_level);
            s_disp.soc_level = soc_level;
        }
        /* 数值每拍刷新 (颜色由上面的库号决定) */
        drv_dwin_set_value(VP_BATT_SOC, (int16_t)soc);
    }

    /* --- 输入功率 (0x5104): 只有"充电"或"旁路输出"时才有意义 ---
     *   STARTING/RUNNING 的充电或旁路 → 显示实测滤波值
     *   其余 (停充/空闲/无市电/通道失效) → 显示 0
     * 注意: 判据用状态机活跃位, 不能用 battery_full_active() —— 后者停充后仍为 1。 */
    bool in_pwr_valid = (app_ui_charging() || app_ui_bypass_active()) &&
                        (m != NULL) && (m->in_ok != 0u);
    drv_dwin_set_value(VP_DATA_INPUT_PWR,  (int16_t)(in_pwr_valid ? m->vin_p_w_f : 0));

    /* --- 输出功率 (0x510A): 主任务在输出停止时会清零输出通道数据, 此处直接写 --- */
    drv_dwin_set_value(VP_DATA_OUTPUT_PWR, (int16_t)(m ? m->vout_p_w_f : 0));

    /* --- TextError (0x2050) / TextAlarm (0x2060) ---
     * 屏端: FONT0_ID=1 (0_DWIN_ASC 字库), FONT1_ID=0, 8x16, GBK, 1 字节/字符。
     * 文本控件按"文本长度"固定渲染, 写入长度必须为偶数 (奇数帧会被屏丢弃)。
     * 注意: 屏端"初始值"要留空/空格, 否则上电会先露出占位文字。 */
    uint8_t code = app_alarm_err_code();
    if (code == 0u)
    {
        code = app_ui_code_get();
    }
    if (code != 0u)
    {
        snprintf(txt, sizeof txt, "ERR%02u", code);
    }
    else
    {
        txt[0] = '\0';
    }
    text_tx_log("ERR", txt, VP_TEXT_ERR_LEN, VP_TEXT_ERR);
    drv_dwin_set_text(VP_TEXT_ERR, txt);

    /* TextAlarm: 过载告警 (>500W) 时 "OVERLOAD" 按 2.0s 周期闪动 (亮1s/灭1s),
     * 与蜂鸣器/报警灯同相。TextError(0x2050,Y400) 与 TextAlarm(0x2060,Y440)
     * 是两行独立控件, 互不影响。 */
    bool overload_show = app_ui_overload_alarm() && app_ui_overload_phase();
    text_tx_log("ALARM", overload_show ? "OVERLOAD" : "", VP_TEXT_ALARM_LEN, VP_TEXT_ALARM);
    drv_dwin_set_text(VP_TEXT_ALARM, overload_show ? "OVERLOAD" : "");

    uint32_t now = HAL_GetTick();

    /* 周期路径: 时间/频率数值按节流刷新
     * (通路变化时的即时显示/清空由 EVT_DISP_ICON 消息 force 触发) */
    art_time_update(now, boot_win);
    art_hz_update(now, boot_win);
}

/*====================================================================
 * 任务入口
 *====================================================================*/

#define DISPLAY_TICK_MS 100
#define REFRESH_DIV 5 /* 无消息时约 500ms；消息提前唤醒会缩短计数周期。 */
#define BOOT_REFRESH_DIV 2 /* 上电窗口内 200ms 刷一次，尽快覆盖屏端初值 */

/* 启动页时序 (0.bmp = 空白页, 1.bmp = 主界面):
 *   屏端上电需要 0.5~2s 才显示画面, 这段时间会先画出主页面控件的组态初值 (0)。
 *   上电先停在空白页, 主界面控件的 VP 值在后台照常刷新;
 *   屏端就绪后切回主界面, 此时显示的已经是实时值。 */
#define BOOT_PAGE_MS        1200u  /* 空白页停留时长 (就绪后尽快切走) */
#define BOOT_PAGE_RETRY_MS  600u   /* 空白页重申时刻 (屏端上电慢, 首帧可能丢失) */
#define MAIN_PAGE_RETRY_MS  800u   /* 主界面补发间隔 (1.2s 静默未就绪时兜底) */

/* 切页并记录时间戳: 便于用串口日志核对各阶段耗时 */
static void disp_page_set(uint8_t page, const char *tag)
{
    logger_out(LOG_INFO, "DISP: page %u (%s) at t=%ums",
               (unsigned)page, tag, (unsigned)HAL_GetTick());
    drv_dwin_set_page(page);
}

/**
 * @fn      display_init_clear_text
 * @brief   开机将文本控件写入空格, 覆盖屏端默认显示。
 * @note    仅上电调用一次; 若不清空, 屏端默认文本会与后续刷新结果混显。
 *          2026-11-15 组态只剩 TextError / TextAlarm 两个文本控件,
 *          时间/频率/温度已改为艺术字变量 (上电由 art_*_update 写 0)。 */
static void display_init_clear_text(void)
{
    /* 写满空格, 覆盖屏端组态里配的初始文本 (如 "12345") */
    char sp[VP_TEXT_ERR_LEN + 1u];
    memset(sp, ' ', VP_TEXT_ERR_LEN);
    sp[VP_TEXT_ERR_LEN] = '\0';         /* 显式结尾, 避免 strlen 越界 */
    drv_dwin_set_text(VP_TEXT_ERR, sp);

    char spa[VP_TEXT_ALARM_LEN + 1u];
    memset(spa, ' ', VP_TEXT_ALARM_LEN);
    spa[VP_TEXT_ALARM_LEN] = '\0';
    drv_dwin_set_text(VP_TEXT_ALARM, spa);
}

/**
 * @brief 向显示任务队列发送消息，不等待队列空间。
 * @param msg 待发送消息，调用方需提供有效指针。
 * @return true：消息入队；false：队列满。 */
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
 * @note 由RTOS调用，任务循环不返回。 */
void DisplayTask(void *argument)
{
    uint32_t uwRet = 0;
    ucMESSAGE_t Msg = {0};
    uint32_t refresh_cnt = 0;
    uint32_t boot_ms;
    uint32_t page_retry_ms = 0;
    uint32_t main_page_ms = 0;
    uint8_t  main_retry_cnt = 0;
    bool main_page_shown = false;

    /* 上电先切到启动页 (1.bmp): 遮住主页面控件在屏端上电阶段显示的组态初值。
     * 主页面的 VP 值在下面照常刷新 (不可见), 切页后立即就是正确内容。 */
    disp_page_set(DWIN_PAGE_BOOT, "boot");

    /* 开机首刷: 覆盖屏端默认文本 (888HMIN 等), 并立即整屏刷新 */
    display_init_clear_text();
    /* 温度单位图标 VARIconTempUnit(0x2140): 屏端默认已显示, 开机只写一次 */
    drv_dwin_set_value(VP_ICON_TEMP_UNIT, ICON_TEMP_UNIT_ON);
    display_refresh();
    boot_ms = HAL_GetTick();

    for (;;)
    {
        /* 消息等待: 显示队列阻塞等待 (图标/通路事件) */
        uwRet = xQueueReceive(xDisplayQueueHandle, &Msg, pdMS_TO_TICKS(DISPLAY_TICK_MS));
        if (uwRet == pdPASS)
        {
            switch (Msg.MsgID)
            {
                /* 图标更新 (工作模式/通路切换), 立即整屏刷新 */
                case EVT_DISP_ICON:
                {
                    /* Value[0] = work_mode_t (未使用), Value[1..4] = 图标状态 */
                    s_disp.icon_chg = Msg.Paras.Value[1];
                    s_disp.icon_out = Msg.Paras.Value[2];
                    s_disp.icon_hz  = Msg.Paras.Value[3];
                    s_disp.icon_ups = Msg.Paras.Value[4];
                    refresh_cnt = REFRESH_DIV+1;
                    /* 图标变化: 立即更新时间/频率的显示与清空, 不等节流周期 */
                    art_time_update(HAL_GetTick(), true);
                    art_hz_update(HAL_GetTick(), true);
                    break;
                }

                default:
                    break;
            }
        }

        /* 启动页时序: BOOT_PAGE_RETRY_MS 重申一次空白页 (屏端上电慢, 首帧可能丢失);
         * 满 BOOT_PAGE_MS 后切回主界面并全量重发一次 */
        uint32_t page_now = HAL_GetTick();
        if (!main_page_shown)
        {
            if (page_retry_ms == 0u &&
                (uint32_t)(page_now - boot_ms) >= BOOT_PAGE_RETRY_MS)
            {
                page_retry_ms = page_now;
                disp_page_set(DWIN_PAGE_BOOT, "boot-retry");
            }
            if ((uint32_t)(page_now - boot_ms) >= BOOT_PAGE_MS)
            {
                main_page_shown = true;
                main_page_ms = page_now;
                disp_page_set(DWIN_PAGE_MAIN, "main");
                display_cache_invalidate();   /* 切页后强制全量重发一次 */
                display_refresh();
                refresh_cnt = 0;
            }
        }
        else if (main_retry_cnt < 2u &&
                 (uint32_t)(page_now - main_page_ms) >=
                     (MAIN_PAGE_RETRY_MS * (uint32_t)(main_retry_cnt + 1u)))
        {
            /* 屏端若比 BOOT_PAGE_MS 更晚就绪, 那一帧会丢 → 补发 2 次,
             * 避免停在空白页出不来 */
            main_retry_cnt++;
            disp_page_set(DWIN_PAGE_MAIN, "main-retry");
        }

        /* 数值刷新: 上电窗口内 200ms 一次, 之后 500ms 一次
         * (消息驱动图标, 周期驱动数值) */
        uint32_t div = ((uint32_t)(HAL_GetTick() - boot_ms) < BOOT_BURST_MS)
                           ? (uint32_t)BOOT_REFRESH_DIV
                           : (uint32_t)REFRESH_DIV;
        if (++refresh_cnt >= div)
        {
            refresh_cnt = 0;
            display_refresh();
        }

        /* 心跳: 监控定时器检测任务存活 */
        bsp_wdg_task_heartbeat(WDG_TASK_DISP);
    }
}
