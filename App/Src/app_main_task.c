/**
 * @file app_main_task.c
 * @brief 主任务拥有通路状态机，负责保护、按键和工作指示。
 * @note 逆变启动：PD4预充500ms → PD7吸合 → 100ms后PD0输出 (PD4保持)。
 *       正常停充/停逆变按 1.5s 顺序断电(先断输出端, 延时后断输入端/主接触器)。
 *       继电器软件状态仅表示输出命令，不代表触点反馈。
 *       充满检测：MAX17263 FStat.FQ 置位时自动停止充电 (BY_BYPASS 保留AC直通)。
 *       按键: AC IN=旁路开关; AC OUT=逆变(有AC维持旁路); 充电自动(220V稳定2s后)。
 */
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "main.h"
#include "app_main_task.h"
#include "project_param.h"
#include "app_display_task.h"
#include "mod_battery.h"
#include "sys_alarm.h"
#include "battery_config.h"
#include "mod_metering.h"
#include "mod_key.h"
#include "mod_temp.h"
#include "bsp_wdg.h"
#include "comm_logger.h"
#include "Board.h"
#include "drv_relaysw.h"
#include "drv_fan.h"
#include "drv_led.h"
#include "drv_buzzer.h"
#include "app_alarm.h"


/* 时序: 依据需求文档 (充电1.5s / 逆变预充1.5s-直连0.5s-输出1.5s / 顺序断电1.5s) */
#define PATH_RELEASE_MS 50u
#define PATH_CONTACTOR_RELEASE_MS 100u
#define PATH_CHARGER_START_MS 1500u      /* 充电: CHG_IN 后等充电器稳定再闭合 CHG_OUT */
#define PATH_PRECHARGE_MS 1500u          /* 逆变: PD4 预充 1.5s 后吸合 PD7 (需求文档) */
#define PATH_DC_FAST_MS 500u             /* 逆变: PD7 后 0.5s 断开 PD4, 电池直连 (需求文档) */
#define PATH_INVERTER_START_MS 1500u     /* 逆变: 直连后 1.5s 开 PD0 输出 (需求文档) */
#define PATH_STOP_CHARGE_TAIL_MS 1500u   /* 停充: CHG_OUT 断开后 1500ms 再断 CHG_IN */
#define PATH_STOP_INV_TAIL_MS 1500u      /* 停逆变: PD0 断开后 1500ms 再断 PD7/PD4 */
#define PATH_INPUT_WAIT_MS 5000u
#define AC_STABLE_MS 2000u               /* 自动充电: 220V 稳定 2s 后启充 (防抖) */
#define INV_IDLE_STOP_MS 60000u          /* 逆变: 无输出功率 60s 自动停 */
#define INV_IDLE_PWR_W 1                 /* 无输出功率判定阈值, W */

typedef enum
{
    PHASE_IDLE,
    PHASE_RELEASE_AC,
    PHASE_RELEASE_DC,
    PHASE_START_CHARGE,
    PHASE_PRECHARGE,
    PHASE_START_DC,          /* 逆变: PD7 后 0.5s 断 PD4 直连 (需求文档三段时序) */
    PHASE_START_INVERTER,
    PHASE_STOP_CHARGE,
    PHASE_STOP_CHARGE_TAIL,
    PHASE_STOP_INV_TAIL
} path_phase_t;

/* 通路语义判定 (原 mod_acswitch 迁入, 纯逻辑无驱动依赖) */
static bool path_charges(energy_path_t path)
{
    return path == PATH_CHARGE || path == PATH_CHARGE_BYPASS;
}
/* 通路语义判定: 是否需要市电输入 (旁路或充电) */
static bool path_needs_ac(energy_path_t path)
{
    return path == PATH_BYPASS || path_charges(path);
}


static energy_path_t s_cur_path = PATH_NONE;
static energy_path_t s_target_path = PATH_NONE;
static path_phase_t s_phase = PHASE_IDLE;
static uint32_t s_phase_since;
static bool s_output_requested; /* 旁路/逆变输出请求 (AC IN/AC OUT 键 toggle) */
static bool s_need_charge;     /* 自动充电生效标志 (auto_charge_update 维护) */
static uint32_t s_inv_idle_since; /* 逆变无功率计时起点, 0=未计时 */
static bool s_shutting_down;
static bool s_shutdown_complete;
static uint32_t s_dc_release_ms = PATH_RELEASE_MS;
static volatile SYS_STATUS_T s_state = SYS_STATUS_IDLE;
static bool s_fan_aux_on, s_fan_main_on;
static bool s_ui_dirty = true;
static bool s_battery_trip_pending;
static uint32_t s_battery_trip_since;

/**
 * @brief 读取当前系统运行状态。
 * @return 当前系统状态枚举值。
 */
SYS_STATUS_T state_get(void)
{
    return s_state;
}

/**
 * @brief 按充电或放电方向选择电池保护位。
 * @param path 待检查或提交的能量通路。
 * @return 当前方向需要检查的电池故障位掩码。
 */
static uint32_t battery_path_fault_mask(energy_path_t path)
{
    return path_charges(path) ? (SYS_ALARM_BAT_OVP | SYS_ALARM_BAT_OCP)
                              : (SYS_ALARM_BAT_UVP | SYS_ALARM_BAT_OCP);
}

/**
 * @brief 更新系统状态，并在变化时请求刷新显示。
 * @param state 新的系统运行状态。
 */
static void state_set(SYS_STATUS_T state)
{
    if (s_state != state)
    {
        logger_out(LOG_INFO, "ST %d -> %d", (int)s_state, (int)state);
        s_state = state;
        s_ui_dirty = true;
    }
}

/**
 * @brief 非阻塞发送通路状态+图标消息给显示任务，队列满时留待重试。
 * @note  图标值由目标通路语义派生 (充电/逆变/旁路); 充电图标充满后由显示端强制关闭。
 */
static void display_publish(void)
{
    if (!s_ui_dirty)
    {
        return;
    }
    ucMESSAGE_t msg = {0};
    msg.MsgID = EVT_DISP_ICON;
    msg.Length = 5;
    msg.Paras.Value[0] = (uint8_t)s_target_path;                       /* 通路状态 (TextTime/动画依据) */
    msg.Paras.Value[1] = path_charges(s_target_path) ? ICON_CHG_ON : ICON_OFF;
    msg.Paras.Value[2] = (s_target_path == PATH_INVERT) ? ICON_OUT_ON : ICON_OFF;
    msg.Paras.Value[3] = (s_target_path == PATH_INVERT) ? ICON_HZ_ON  : ICON_OFF;
    msg.Paras.Value[4] = (s_target_path == PATH_BYPASS ||
                          s_target_path == PATH_CHARGE_BYPASS) ? ICON_UPS_ON : ICON_OFF;
    if (Send_Msg_AppDisplayTask(&msg))
    {
        s_ui_dirty = false;
    }
}

/**
 * @brief 结束通路切换，提交当前通路及对应运行状态。
 * @param path 待检查或提交的能量通路。
 */
static void path_commit(energy_path_t path)
{
    s_cur_path = s_target_path = path;
    s_phase = PHASE_IDLE;
    s_ui_dirty = true;
    switch (path)
    {
        case PATH_CHARGE:
            state_set(SYS_STATUS_CHARGE);
            break;
        case PATH_CHARGE_BYPASS:
            state_set(SYS_STATUS_CHARGE_BYPASS);
            break;
        case PATH_BYPASS:
            state_set(SYS_STATUS_BYPASS);
            break;
        case PATH_INVERT:
            state_set(SYS_STATUS_OUTPOWER);
            break;
        default:
            state_set(SYS_STATUS_IDLE);
            break;
    }
}

/**
 * @brief 立即关闭全部通路并撤销请求，取消后续延时启动。
 */
static void power_abort(void)
{
    drv_relay_all_off();
    s_output_requested = false;
    s_battery_trip_pending = false;
    path_commit(PATH_NONE); /* Also cancels every delayed close operation. */
}

/**
 * @brief 记录告警、关闭通路并进入故障状态。
 * @param alarm 本次新增的告警位掩码。
 */
static void enter_fault(uint32_t alarm)
{
    app_alarm_merge(alarm);
    power_abort();
    state_set(SYS_STATUS_FAULT);
    drv_led_on(LED_ALM);
    drv_buzzer_short_sound(3);
    logger_out(LOG_ERROR, "FAULT: alarm=0x%lX", (unsigned long)app_alarm_get());
}

/**
 * @brief 通过驱动互锁开启一路输出，失败时进入继电器故障状态。
 * @param id 继电器输出编号。
 * @return true：命令已接受；false：互锁拒绝并已进入故障。
 */
static bool relay_on(relay_id_enum id)
{
    if (drv_relay_on(id))
    {
        s_ui_dirty = true;
        return true;
    }
    enter_fault(SYS_ALARM_RELAY);
    return false;
}

/**
 * @brief 检查输入来源、电池数据和温度是否允许开启目标通路。
 * @param path 待检查或提交的能量通路。
 * @param ac 输入计量模块提供的市电状态。
 * @return true：允许；false：条件不满足。
 */
static bool path_permitted(energy_path_t path, ac_input_state_t ac)
{
    const TEMP_INFO_T *t = mod_temp_get();
    if (path == PATH_NONE)
    {
        return true;
    }
    if (path_needs_ac(path) && ac != AC_INPUT_PRESENT)
    {
        return false;
    }
    if (path == PATH_INVERT && ac != AC_INPUT_ABSENT)
    {
        return false;
    }
    if (!t || t->bat_temp >= BOARD_PROT_BAT_TMP_FAULT_TH)
    {
        return false;
    }
    if (path_charges(path) && t->chg_temp >= BOARD_PROT_CHG_TMP_FAULT_TH)
    {
        return false;
    }
    if (path == PATH_INVERT && t->inv_temp >= BOARD_PROT_INV_TMP_FAULT_TH)
    {
        return false;
    }
    if (path_charges(path) || path == PATH_INVERT)
    {
        BATT_INFO_T bat;
        if (!battery_get_snapshot(&bat) || (bat.alarm_bits & battery_path_fault_mask(path)))
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief 立即撤销输出命令，并进入目标通路的释放等待阶段。
 * @param target 释放等待后要进入的目标通路。
 */
static void path_begin_stop(energy_path_t target)
{
    s_target_path = target;
    drv_relay_off(RELAY_INV_OUT);
    drv_relay_off(RELAY_AC_CTRL);
    drv_relay_off(RELAY_CHG_OUT);
    /* 顺序断电: 输出端已断, 输入端/主接触器按 1.5s 尾段延时断开, 防反灌/拉弧.
     * 紧急路径(power_abort)仍立即全断. */
    s_cur_path = PATH_NONE;
    s_phase_since = HAL_GetTick();
    state_set(SYS_STATUS_SWITCHING);
    s_ui_dirty = true;
    if (drv_relay_is_on(RELAY_INV_IN))
    {
        s_phase = PHASE_STOP_INV_TAIL;
    }
    else if (drv_relay_is_on(RELAY_CHG_IN))
    {
        s_phase = PHASE_STOP_CHARGE_TAIL;
    }
    else
    {
        s_phase = PHASE_RELEASE_AC;
    }
}
/* A request never sleeps. Stable CHARGE <-> CHARGE_BYPASS preserves charging. */
/**
 * @brief 接收通路请求；稳定充电与旁路组合的切换保留独立通路。
 * @param path 待检查或提交的能量通路。
 * @param ac 输入计量模块提供的市电状态。
 * @return true：请求已接受；false：状态或保护条件拒绝。
 */
static bool path_request(energy_path_t path, ac_input_state_t ac)
{
    if (s_state == SYS_STATUS_FAULT || s_shutting_down)
    {
        return false;
    }
    if (path == s_target_path)
    {
        return true;
    }
    /* 关闭或减少通路不应被新启动的许可检查阻挡。 */
    bool reducing =
        path == PATH_NONE || (s_phase == PHASE_IDLE && s_cur_path == PATH_CHARGE_BYPASS &&
                              (path == PATH_CHARGE || path == PATH_BYPASS));
    if (!reducing && !path_permitted(path, ac))
    {
        logger_out(LOG_WARN, "PATH %d rejected: source/battery/temperature", (int)path);
        return false;
    }
    s_battery_trip_pending = false;
    if (s_phase == PHASE_IDLE && s_cur_path == PATH_CHARGE_BYPASS && path == PATH_BYPASS)
    {
        /* Turning ACIN off must not interrupt an independently enabled ACOUT. */
        drv_relay_off(RELAY_CHG_OUT);
        s_target_path = PATH_BYPASS;
        s_cur_path = PATH_NONE;
        s_phase = PHASE_STOP_CHARGE;
        s_phase_since = HAL_GetTick();
        state_set(SYS_STATUS_SWITCHING);
        s_ui_dirty = true;
        return true;
    }
    if (s_phase == PHASE_IDLE && path_charges(s_cur_path) && path_charges(path))
    {
        if (path == PATH_CHARGE_BYPASS)
        {
            if (!relay_on(RELAY_AC_CTRL))
            {
                return false;
            }
        }
        else
        {
            drv_relay_off(RELAY_AC_CTRL);
        }
        path_commit(path);
        return true;
    }
    path_begin_stop(path);
    return true;
}

/**
 * @brief 输入失效或未知时撤销通路，等待用户再次开启。
 */
static void input_abort(void)
{
    app_alarm_merge(SYS_ALARM_AC_INPUT);
    power_abort();
    drv_buzzer_short_sound(2);
    logger_out(LOG_WARN, "AC input invalid/unknown: routes off; ACOUT required to restart");
}
/* 关机标志阻止新的启动；此流程仅在MCU仍有供电时继续执行。 */
/**
 * @brief 在关机请求后仍有供电时推进释放等待、数据保存和断电。
 */
static void shutdown_process(void)
{
    if (s_shutdown_complete)
    {
        return;
    }
    uint32_t elapsed = (uint32_t)(HAL_GetTick() - s_phase_since);
    if (s_phase == PHASE_RELEASE_AC && elapsed >= PATH_RELEASE_MS)
    {
        drv_relay_all_off();
        s_phase = PHASE_RELEASE_DC;
        s_phase_since = HAL_GetTick();
    }
    else if (s_phase == PHASE_RELEASE_DC && elapsed >= PATH_CONTACTOR_RELEASE_MS)
    {
        power_abort();
        s_shutdown_complete = true;
        display_publish();
        if (!mod_battery_checkpoint(2000))
        {
            logger_out(LOG_WARN, "BAT checkpoint failed");
        }
        DEV_POWER_OFF();
    }
}
/* Returns false on UNKNOWN while waiting, or on a rejected/aborted startup. */
/**
 * @brief 复查逆变启动条件；输入未知时等待，超时后撤销启动。
 * @param ac 输入计量模块提供的市电状态。
 * @param elapsed 当前阶段已持续的毫秒数。
 * @return true：可进入下一阶段；false：等待中或启动已撤销。
 */
static bool inverter_stage_ready(ac_input_state_t ac, uint32_t elapsed)
{
    if (ac == AC_INPUT_UNKNOWN)
    {
        if (elapsed >= PATH_INPUT_WAIT_MS)
        {
            input_abort();
        }
        return false;
    }
    if (!path_permitted(PATH_INVERT, ac))
    {
        power_abort();
        return false;
    }
    return true;
}
/* Invoked every main-loop tick, also while keys/charger startup are pending. */
/**
 * @brief 推进充电、逆变和旁路的非阻塞通路状态机。
 *
 * 每 20ms 由主循环调用; 依赖 s_phase + s_phase_since 计时推进, 不阻塞等待.
 * 阶段语义:
 *   PHASE_RELEASE_AC       - 撤销全部输出后等待继电器释放 (PATH_RELEASE_MS)
 *   PHASE_RELEASE_DC       - 按是否仍带逆变输入决定释放时长, 再按目标通路开启
 *   PHASE_START_CHARGE     - 开充电回路 + 可选旁路 (CHG_IN→1500ms→CHG_OUT)
 *   PHASE_PRECHARGE      - 逆变预充 (PD4), 1500ms 后吸合 PD7
 *   PHASE_START_DC       - PD7 后 500ms 断开 PD4 (电池直连逆变器输入)
 *   PHASE_START_INVERTER - 直连后 1500ms 开 PD0 输出, 完成时 path_commit
 *   PHASE_STOP_CHARGE      - 停充保留旁路: CHG_OUT 已断, 1.5s 后断 CHG_IN 提交旁路
 *   PHASE_STOP_CHARGE_TAIL - 顺序断电: 1.5s 后断 CHG_IN 再进机械释放
 *   PHASE_STOP_INV_TAIL    - 顺序断电: 1.5s 后断 PD7/PD4 再进机械释放
 * 安全: 每次进入/推进前都过 path_permitted() 复查 (输入/电池/温度).
 */
static void power_process(void)
{
    if (s_shutting_down)
    {
        shutdown_process();
        return;
    }
    if (s_state == SYS_STATUS_FAULT)
    {
        return;
    }
    ac_input_state_t ac = mod_metering_get_ac_input().state;
    /* 市电状态变化(接入/掉电/未知)时立即标记 UI 脏, 主循环 display_publish 即时发
     * EVT_DISP_ICON 刷新 ICON3(220V 图标), 不等数值 500ms 刷新周期. */
    static ac_input_state_t s_last_ac_state = AC_INPUT_UNKNOWN;
    if (ac != s_last_ac_state)
    {
        s_last_ac_state = ac;
        s_ui_dirty = true;
    }
    if (path_needs_ac(s_target_path) && ac != AC_INPUT_PRESENT)
    {
        input_abort();
        return;
    }
    /* 输入恢复时优先转旁路，避免继续推进逆变启动。 */
    if (s_output_requested && s_target_path == PATH_INVERT && ac == AC_INPUT_PRESENT)
    {
        if (!path_request(PATH_BYPASS, ac))
        {
            power_abort();
        }
        return;
    }
    if (s_phase == PHASE_IDLE)
    {
        return;
    }
    uint32_t elapsed = (uint32_t)(HAL_GetTick() - s_phase_since);
    if (s_phase == PHASE_STOP_CHARGE)
    {
        /* 停充保留旁路: CHG_OUT 已断, 1.5s 后再断 CHG_IN 并提交旁路 */
        if (elapsed < PATH_STOP_CHARGE_TAIL_MS)
        {
            return;
        }
        drv_relay_off(RELAY_CHG_IN);
        path_commit(PATH_BYPASS);
        return;
    }
    if (s_phase == PHASE_STOP_CHARGE_TAIL)
    {
        /* 顺序断电尾段: 1.5s 后断充电输入, 再进入机械释放 */
        if (elapsed < PATH_STOP_CHARGE_TAIL_MS)
        {
            return;
        }
        drv_relay_off(RELAY_CHG_IN);
        s_phase = PHASE_RELEASE_AC;
        s_phase_since = HAL_GetTick();
        return;
    }
    if (s_phase == PHASE_STOP_INV_TAIL)
    {
        /* 顺序断电尾段: 1.5s 后断主接触器与预充, 再进入机械释放 */
        if (elapsed < PATH_STOP_INV_TAIL_MS)
        {
            return;
        }
        drv_relay_off(RELAY_INV_IN);
        drv_relay_off(RELAY_INV_PRO);
        s_phase = PHASE_RELEASE_AC;
        s_phase_since = HAL_GetTick();
        return;
    }
    if (s_phase == PHASE_RELEASE_AC)
    {
        if (elapsed < PATH_RELEASE_MS)
        {
            return;
        }
        s_dc_release_ms =
            drv_relay_is_on(RELAY_INV_IN) ? PATH_CONTACTOR_RELEASE_MS : PATH_RELEASE_MS;
        drv_relay_all_off(); /* PD4 off before PD7; no residual precharge feed. */
        s_phase = PHASE_RELEASE_DC;
        s_phase_since = HAL_GetTick();
        return;
    }
    if (s_phase == PHASE_RELEASE_DC)
    {
        if (elapsed < s_dc_release_ms)
        {
            return;
        }
        if (s_target_path == PATH_INVERT && ac == AC_INPUT_UNKNOWN)
        {
            if (elapsed >= PATH_INPUT_WAIT_MS)
            {
                input_abort();
            }
            return;
        }
        if (!path_permitted(s_target_path, ac))
        {
            power_abort();
            return;
        }
        if (s_target_path == PATH_NONE)
        {
            path_commit(PATH_NONE);
            return;
        }
        if (s_target_path == PATH_BYPASS)
        {
            if (relay_on(RELAY_AC_CTRL))
            {
                path_commit(PATH_BYPASS);
            }
            return;
        }
        if (path_charges(s_target_path))
        {
            if (!relay_on(RELAY_CHG_IN))
            {
                return;
            }
            s_phase = PHASE_START_CHARGE;
            s_ui_dirty = true;
            display_publish(); /* 充电启动: 立即刷新充电图标, 不等通路 commit */
            logger_out(LOG_INFO, "CHG: CHG_IN ON, charge 1500ms before CHG_OUT");
        }
        else
        {
            if (!relay_on(RELAY_INV_PRO))
            {
                return;
            }
            s_phase = PHASE_PRECHARGE;
            s_ui_dirty = true;
            display_publish(); /* PD4 预充开启: 立即刷新逆变图标, 不等通路 commit */
            logger_out(LOG_INFO, "INV: PD4 ON, precharge 1500ms before PD7");
        }
        s_phase_since = HAL_GetTick();
        return;
    }
    if (s_phase == PHASE_START_CHARGE)
    {
        if (elapsed < PATH_CHARGER_START_MS)
        {
            return;
        }
        if (!path_permitted(s_target_path, ac))
        {
            power_abort();
            return;
        }
        if (!relay_on(RELAY_CHG_OUT))
        {
            return;
        }
        if (s_target_path == PATH_CHARGE_BYPASS && !relay_on(RELAY_AC_CTRL))
        {
            return;
        }
        path_commit(s_target_path);
    }
    else if (s_phase == PHASE_PRECHARGE)
    {
        if (elapsed < PATH_PRECHARGE_MS || !inverter_stage_ready(ac, elapsed))
        {
            return;
        }
        if (!relay_on(RELAY_INV_IN))
        {
            return;
        }
        s_phase = PHASE_START_DC;
        s_phase_since = HAL_GetTick();
        logger_out(LOG_INFO, "INV: PD7 ON, wait 500ms then PD4 off (direct connect)");
    }
    else if (s_phase == PHASE_START_DC)
    {
        if (elapsed < PATH_DC_FAST_MS || !inverter_stage_ready(ac, elapsed))
        {
            return;
        }
        drv_relay_off(RELAY_INV_PRO); /* 预充结束: 断开 PD4, 电池直连逆变器输入 */
        s_phase = PHASE_START_INVERTER;
        s_phase_since = HAL_GetTick();
        logger_out(LOG_INFO, "INV: PD4 OFF (direct), wait 1500ms before PD0");
    }
    else if (s_phase == PHASE_START_INVERTER)
    {
        if (elapsed < PATH_INVERTER_START_MS || !inverter_stage_ready(ac, elapsed))
        {
            return;
        }
        if (relay_on(RELAY_INV_OUT))
        {
            path_commit(PATH_INVERT);
            logger_out(LOG_INFO, "INV: PD0 output ON");
        }
    }
}

/**
 * @brief 处理ONOFF长按：停止通路、短响提示并按现有延时断电。
 */
static void handle_onoff_key(void)
{
    if (s_shutting_down)
    {
        return;
    }
    s_shutting_down = true;
    s_shutdown_complete = false;
    s_output_requested = false;
    s_battery_trip_pending = false;
    /* 关机: 立即全断, 不等顺序断电尾段 (MCU 即将断电) */
    drv_relay_all_off();
    path_begin_stop(PATH_NONE);
    mod_battery_led_mask(0); /* 关机: 熄灭 MAX17263 电量 LED (开机 init 恢复 SOC 模式) */
    logger_out(LOG_INFO, "ONOFF: 3s hold, stopping routes before power off");
    /* 保留当前关机顺序：给提示音留出时间，再撤销电源保持。 */
    drv_buzzer_short_sound(1);
    osDelay(300);
    DEV_POWER_OFF();
    osDelay(500);
}

/**
 * @brief 合成目标通路: 自动充电 + 输出请求 (有 AC 旁路 / 无 AC 逆变)。
 * @param ac 输入计量状态。
 * @param want_output 输出请求 (用户 AC OUT 键 toggle)。
 * @param need_charge 自动充电生效 (220V 稳定且未充满)。
 * @return 应保持的目标通路。
 */
static energy_path_t resolve_target_path(ac_input_state_t ac, bool want_output, bool need_charge)
{
    if (ac != AC_INPUT_PRESENT)
    {
        return want_output ? PATH_INVERT : PATH_NONE; /* 无AC: 仅逆变请求有效 */
    }
    if (need_charge && want_output)
    {
        return PATH_CHARGE_BYPASS; /* 旁路 + 自动充电 */
    }
    if (need_charge)
    {
        return PATH_CHARGE;
    }
    return want_output ? PATH_BYPASS : PATH_NONE;
}

/**
 * @brief 处理 AC OUT 键 (唯一 AC 功能键): 有 220V 时旁路直接输出,
 *        无 220V 时电池逆变输出 (互斥, 由 resolve_target_path 合成)。
 * @note 充电由自动策略(auto_charge_update)维护, 接入 220V 自动启充/充满自停。
 */
static void handle_path_key(void)
{
    ac_input_state_t ac = mod_metering_get_ac_input().state;
    if (s_state == SYS_STATUS_FAULT || s_shutting_down)
    {
        return;
    }
    if (ac == AC_INPUT_UNKNOWN)
    {
        logger_out(LOG_WARN, "KEY rejected: AC input unavailable");
        drv_buzzer_short_sound(2);
        return;
    }
    /* 候选输出请求取反; 目标由自动充电策略合成, path_request 成功才落定. */
    bool new_out = !s_output_requested;
    energy_path_t target = resolve_target_path(ac, new_out, s_need_charge);
    if (path_request(target, ac))
    {
        s_output_requested = new_out;
        app_alarm_unmerge(SYS_ALARM_AC_INPUT);
        drv_buzzer_short_sound(1);
        display_publish(); /* 通路变化: 即时刷新继电器图标, 不等 500ms 周期 */
    }
}

/**
 * @brief 自动充电策略: 220V 稳定 2s 且未充满时启用充电, 与输出请求合成目标。
 * @note 每 20ms tick 调用; 防抖计时在 AC 波动时清零; 充满/无AC 自动收敛停充。
 */
static void auto_charge_update(void)
{
    if (s_state == SYS_STATUS_FAULT || s_shutting_down)
    {
        return;
    }
    ac_input_state_t ac = mod_metering_get_ac_input().state;
    static bool s_ac_stable;
    static uint32_t s_ac_stable_since;
    static ac_input_state_t s_ac_log = AC_INPUT_UNKNOWN; /* 诊断: AC 状态变化日志 */
    static bool s_chg_log = false;                       /* 诊断: need_charge 变化日志 */

    if (ac != s_ac_log)
    {
        s_ac_log = ac;
        logger_out(LOG_INFO, "auto chg: AC state=%d", (int)ac);
    }

    if (ac == AC_INPUT_PRESENT)
    {
        if (!s_ac_stable)
        {
            s_ac_stable = true;
            s_ac_stable_since = HAL_GetTick();
        }
        if ((uint32_t)(HAL_GetTick() - s_ac_stable_since) >= AC_STABLE_MS)
        {
            BATT_INFO_T bat = {0};
            bool snap_ok = battery_get_snapshot(&bat);
            s_need_charge = snap_ok && !bat.full_qualified;
            if (s_need_charge != s_chg_log)
            {
                s_chg_log = s_need_charge;
                logger_out(LOG_INFO, "auto chg: need=%d ok=%d soc=%u fq=%d",
                           (int)s_need_charge, (int)snap_ok, bat.soc, (int)bat.full_qualified);
            }
        }
    }
    else
    {
        s_ac_stable = false;
        s_need_charge = false;
    }

    energy_path_t target = resolve_target_path(ac, s_output_requested, s_need_charge);
    if (target != s_target_path)
    {
        path_request(target, ac); /* 内部短路; 成功后主循环 display_publish 同步图标 */
    }
}

/**
 * @brief 逆变输出看门狗: 无负载(60s)或输出计量离线时自动停止输出。
 * @note  - 输出 HLW8110 离线(检测不到输出) -> 立即关断
 *        - vout_p_w < 1W 连续 60s -> 自动关断
 *        - 关断即清除 s_output_requested, 不会自动重启, 等下次 AC OUT 键触发
 */
static void inverter_idle_watchdog(void)
{
    if (s_state == SYS_STATUS_FAULT || s_shutting_down)
    {
        return;
    }
    if (s_target_path != PATH_INVERT)
    {
        return;
    }
    METER_INFO_T *m = metering_get_info();
    uint32_t now = HAL_GetTick();
    ac_input_state_t ac = mod_metering_get_ac_input().state;

    /* 输出计量离线: 检测不到输出 -> 立即关断输出并清除请求 */
    if (!m || !m->online)
    {
        s_inv_idle_since = 0;
        logger_out(LOG_WARN, "INV meter offline: output off");
        s_output_requested = false;
        if (path_request(PATH_NONE, ac))
        {
            drv_buzzer_short_sound(2);
            display_publish();
        }
        return;
    }
    if (m->vout_p_w >= INV_IDLE_PWR_W)
    {
        s_inv_idle_since = 0;
        return;
    }
    if (s_inv_idle_since == 0)
    {
        s_inv_idle_since = now;
    }
    if ((uint32_t)(now - s_inv_idle_since) >= INV_IDLE_STOP_MS)
    {
        s_inv_idle_since = 0;
        logger_out(LOG_WARN, "INV idle %us no load: auto output off", INV_IDLE_STOP_MS / 1000u);
        s_output_requested = false;
        if (path_request(PATH_NONE, ac))
        {
            drv_buzzer_short_sound(2);
            display_publish();
        }
    }
}

/**
 * @brief 分发按键事件：ONOFF仅响应长按，AC键仅响应按下。
 * @param key_code 按键编码，包含按下或长按标志。
 */
static void app_main_task_key_event_handler(uint8_t key_code)
{
    switch (key_code)
    {
        case KEY_LONG_VALUE_ONOFF:
            handle_onoff_key();
            break;
        /* Scanner emits both press and long events. AC key acts only on press.
         * 仅保留 ACOUT: 充电由接入 220V 自动触发, ACIN 不再响应. */
        case KEY_VALUE_ACOUT:
            handle_path_key();
            break;
        default:
            break;
    }
}
/* ---- work_status_process 拆分子流程 (三段独立职责) ---- */

/* 风扇滞回: 超温开, 回落到阈值下关 (温度无效则保持当前, 不误动作)
 * 主风扇: 电池/充电器温度超阈值开启 (需求: bat/chg 任一 >= 35℃)
 * 辅风扇: 逆变器温度超阈值开启 (逆变/旁路输出时逆变器工作发热) */
static void fan_hysteresis(const TEMP_INFO_T *t)
{
    if (!t)
    {
        return;
    }
    if (!s_fan_main_on &&
        (t->bat_temp >= BOARD_PROT_FAN_ON_TH || t->chg_temp >= BOARD_PROT_FAN_ON_TH))
    {
        drv_fan_on(FAN_MAIN);
        s_fan_main_on = true;
    }
    else if (s_fan_main_on && t->bat_temp <= BOARD_PROT_FAN_OFF_TH &&
             t->chg_temp <= BOARD_PROT_FAN_OFF_TH)
    {
        drv_fan_off(FAN_MAIN);
        s_fan_main_on = false;
    }

    if (!s_fan_aux_on && t->inv_temp >= BOARD_PROT_FAN_ON_TH)
    {
        drv_fan_on(FAN_AUX);
        s_fan_aux_on = true;
    }
    else if (s_fan_aux_on && t->inv_temp <= BOARD_PROT_FAN_OFF_TH)
    {
        drv_fan_off(FAN_AUX);
        s_fan_aux_on = false;
    }
}

/* 温度保护: 按当前通路方向检查电池/充电/逆变过温, 触发即进故障 */
static bool thermal_protect(const TEMP_INFO_T *t, bool charge, bool invert)
{
    if (!t)
    {
        return false;
    }
    if (s_target_path != PATH_NONE && t->bat_temp >= BOARD_PROT_BAT_TMP_FAULT_TH)
    {
        enter_fault(SYS_ALARM_BAT_OTP);
        return true;
    }
    if (charge && t->chg_temp >= BOARD_PROT_CHG_TMP_FAULT_TH)
    {
        enter_fault(SYS_ALARM_CHG_OTP);
        return true;
    }
    if (invert && t->inv_temp >= BOARD_PROT_INV_TMP_FAULT_TH)
    {
        enter_fault(SYS_ALARM_INV_OTP);
        return true;
    }
    return false;
}

/* 电池保护: 数据无效或方向相关故障位, 延时跳闸进故障 */
static bool battery_protect(bool charge, bool invert)
{
    if (!charge && !invert)
    {
        s_battery_trip_pending = false;
        return false;
    }
    BATT_INFO_T bat = {0};
    bool good = battery_get_snapshot(&bat);
    uint32_t severe = bat.alarm_bits & battery_path_fault_mask(charge ? PATH_CHARGE : PATH_INVERT);
    if (!good || severe)
    {
        if (!s_battery_trip_pending)
        {
            s_battery_trip_pending = true;
            s_battery_trip_since = HAL_GetTick();
        }
        /* Startup must never complete using invalid battery data. */
        if (s_phase != PHASE_IDLE ||
            (uint32_t)(HAL_GetTick() - s_battery_trip_since) >= BATTERY_ACTION_DELAY_MS)
        {
            enter_fault(SYS_ALARM_BAT_INVALID | (severe & SYS_ALARM_BAT_OVP) |
                        (severe & SYS_ALARM_BAT_UVP));
            return true;
        }
    }
    else
    {
        s_battery_trip_pending = false;
    }
    return false;
}

/* 充满自动断充: FStat.FQ 上升沿检测; 仅在稳定充电态触发一次.
 * CHARGE_BYPASS 时仅停充电保留 AC 直通输出; FQ 清零(转放电)后可再次触发. */
static void full_charge_auto_stop(void)
{
    if (s_state == SYS_STATUS_FAULT || s_shutting_down || s_phase != PHASE_IDLE ||
        !path_charges(s_target_path))
    {
        return;
    }
    BATT_INFO_T bat = {0};
    static bool s_full_seen;
    if (!battery_get_snapshot(&bat))
    {
        return;
    }
    if (!bat.full_qualified)
    {
        s_full_seen = false;
        return;
    }
    if (s_full_seen)
    {
        return; /* 已触发过, 等待 FQ 清零后允许下一次 */
    }
    s_full_seen = true;
    energy_path_t target = (s_target_path == PATH_CHARGE_BYPASS) ? PATH_BYPASS : PATH_NONE;
    logger_out(LOG_INFO, "BAT full (FQ): auto stop charging -> path %d", (int)target);
    ac_input_state_t ac = mod_metering_get_ac_input().state;
    if (path_request(target, ac))
    {
        drv_buzzer_short_sound(1);
        display_publish();
    }
}

/* Thermal control follows active/starting routes, including charge+bypass.
 * Fans continue cooling hot components after route changes and faults. */
/**
 * @brief 处理风扇回差控制及当前通路的温度、电池保护。
 *
 * 按顺序执行: 风扇滞回(始终) → 温度保护 → 电池保护 → 充满自动断充; 任一路进故障即返回.
 */
static void work_status_process(void)
{
    const TEMP_INFO_T *t = mod_temp_get();

    /* 1. 风扇滞回: 始终生效, 故障/关机后仍继续散热 */
    fan_hysteresis(t);

    if (s_state == SYS_STATUS_FAULT || s_shutting_down)
    {
        return;
    }

    bool charge = path_charges(s_target_path) || drv_relay_is_on(RELAY_CHG_IN);
    bool invert = s_target_path == PATH_INVERT || drv_relay_is_on(RELAY_INV_PRO) ||
                  drv_relay_is_on(RELAY_INV_IN);

    /* 2. 温度保护: 按通路方向过温即跳故障 */
    if (thermal_protect(t, charge, invert))
    {
        return;
    }

    /* 3. 电池保护: 数据无效/方向故障位, 延时跳闸 */
    if (battery_protect(charge, invert))
    {
        return;
    }

    /* 4. 充满自动断充: FQ 上升沿触发, 停止充电 (BY_BYPASS 保留 AC 直通) */
    full_charge_auto_stop();

    drv_led_off(LED_ALM);
}
/* Powered MCU heartbeat, independent of the selected AC route. No delays. */
/**
 * @brief 非阻塞刷新工作灯：绿灯1秒亮灭，蓝灯每5秒快闪3次。
 */
static void work_led_process(void)
{
    static bool initialized;
    static uint32_t green_since, blue_since;
    uint32_t now = HAL_GetTick();
    if (!initialized)
    {
        green_since = blue_since = now;
        initialized = true;
    }
    if (s_shutting_down)
    {
        drv_led_off(LED_SIG);
        drv_led_off(LED_SIG2);
        return;
    }
    uint32_t green_elapsed = (uint32_t)(now - green_since);
    uint32_t blue_elapsed = (uint32_t)(now - blue_since);
    /* Advance periodic anchors to preserve phase and handle tick rollover. */
    green_since += (green_elapsed / 2000u) * 2000u;
    blue_since += (blue_elapsed / 5000u) * 5000u;
    if ((green_elapsed % 2000u) < 1000u)
    {
        drv_led_on(LED_SIG);
    }
    else
    {
        drv_led_off(LED_SIG);
    }
    /* 每个5秒周期的前600ms为三组100ms亮/100ms灭。 */
    uint32_t blue_phase = blue_elapsed % 5000u;
    if (blue_phase < 600u && (blue_phase % 200u) < 100u)
    {
        drv_led_on(LED_SIG2);
    }
    else
    {
        drv_led_off(LED_SIG2);
    }
}

/**
 * @brief 主任务入口：循环处理保护、通路、按键、指示灯及显示。
 * @param arg 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
void AppMainTask(void *arg)
{
    (void)arg;
    param_init(); /* Legacy AC threshold settings no longer select power routes. */
    drv_fan_off(FAN_AUX);
    drv_fan_off(FAN_MAIN);
    drv_led_off(LED_ALM);
    mod_key_init();
    mod_key_register_callback(app_main_task_key_event_handler);
    logger_out(LOG_INFO,
               "Power controller: auto-charge(AC 2s stable), PD4->500ms->PD7->100ms->PD0 (PD4 held)");
    for (;;)
    {
        /* Apply source/fault changes before accepting a new key request. */
        work_status_process();
        power_process();
        auto_charge_update();
        inverter_idle_watchdog();
        mod_key_poll_handler();
        work_led_process();
        display_publish();
        bsp_wdg_task_heartbeat(WDG_TASK_MAIN);
        osDelay(20);
    }
}
