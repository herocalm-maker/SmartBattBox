/**
 * @file app_main_task.c
 * @brief 应用层主任务: 工作状态机(保护/充电策略/通路切换/稳态管理) + 风扇 + 按键与指示。
 * @note 逆变启动三段时序: PD4预充1.5s → PD7吸合 → 0.5s后PD4断(电池直连) → 1.5s后PD0输出。
 *       停充/停逆变按 1.5s 顺序断电(先断输出端, 延时后断输入端/主接触器)。
 *       继电器软件状态仅表示输出命令，不代表触点反馈。
 *       充满检测: MAX17263 FStat.FQ 置位时自动停止充电 (CHARGE_BYPASS 保留AC直通)。
 *       按键: 仅 AC OUT=输出开关(有220V→旁路 / 无→逆变); 充电由接入220V自动触发;
 *             AC IN 已在应用层屏蔽。主循环 20ms 节拍。
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
    PHASE_STOP_CHARGE,       /* 停充尾段: CHG_OUT 已断, 1.5s 后断 CHG_IN (keep_bypass 决定提交旁路或进释放) */
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
static bool s_need_charge;     /* 自动充电生效标志 (charge_strategy_update 维护) */
static uint32_t s_inv_idle_since; /* 逆变无功率计时起点, 0=未计时 */
static bool s_shutting_down;
static bool s_shutdown_complete;
static uint32_t s_dc_release_ms = PATH_RELEASE_MS;
static volatile SYS_STATUS_T s_state = SYS_STATUS_IDLE;
static bool s_fan_aux_on, s_fan_main_on;
static bool s_ui_dirty = true;
static bool s_battery_trip_pending;
static uint32_t s_battery_trip_since;
static bool s_stop_chg_keep_bypass; /* 停充尾段提交目标: true=保留旁路(PATH_BYPASS), false=进机械释放 */

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

/* 通路活跃判定: 目标通路或实测继电器任一成立 (过渡期保护仍生效) */
static bool path_active_charge(void)
{
    return path_charges(s_target_path) || drv_relay_is_on(RELAY_CHG_IN);
}
static bool path_active_invert(void)
{
    return s_target_path == PATH_INVERT || drv_relay_is_on(RELAY_INV_PRO) ||
           drv_relay_is_on(RELAY_INV_IN);
}

/**
 * @brief 电池方向有效性判定: 快照有效且无方向故障位。
 * @param path 待检查或提交的能量通路。
 * @param bat 出参: 电池快照 (供故障码细分)。
 * @param faults 出参: 方向故障位掩码 (快照无效时为 0)。
 * @return true: 快照有效; false: 快照无效。
 */
static bool battery_path_ok(energy_path_t path, BATT_INFO_T *bat, uint32_t *faults)
{
    if (!battery_get_snapshot(bat))
    {
        *faults = 0;
        return false;
    }
    *faults = bat->alarm_bits & battery_path_fault_mask(path);
    return true;
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
 * @brief 记录告警、立即全断并进入故障状态 (不经过 path_commit, 避免冗余 IDLE 状态日志)。
 * @param alarm 本次新增的告警位掩码。
 */
static void enter_fault(uint32_t alarm)
{
    app_alarm_merge(alarm);
    drv_relay_all_off();
    s_output_requested = false;
    s_battery_trip_pending = false;
    s_cur_path = s_target_path = PATH_NONE;
    s_phase = PHASE_IDLE;
    s_ui_dirty = true;
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
 * @brief 稳定充电状态下的组合切换: 充电 <-> 充电+旁路 不重启充电回路。
 * @param path 目标通路 (PATH_CHARGE 或 PATH_CHARGE_BYPASS)。
 * @return true: 已直接提交; false: 继电器失败 (已进故障)。
 * @note 由 path_request 在 "PHASE_IDLE 且 cur/目标均为充电通路" 时调用。
 */
static bool path_request_charge_switch(energy_path_t path)
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
        uint32_t faults;
        if (!battery_path_ok(path, &bat, &faults) || faults != 0)
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
        s_stop_chg_keep_bypass = false;
        s_phase = PHASE_STOP_CHARGE;
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
        s_stop_chg_keep_bypass = true;
        s_phase = PHASE_STOP_CHARGE;
        s_phase_since = HAL_GetTick();
        state_set(SYS_STATUS_SWITCHING);
        s_ui_dirty = true;
        return true;
    }
    if (s_phase == PHASE_IDLE && path_charges(s_cur_path) && path_charges(path))
    {
        return path_request_charge_switch(path);
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
 * @brief 按当前阶段推进通路状态机 (由 work_status_process 每 tick 调用)。
 * @param ac 输入计量模块提供的市电状态。
 * @param elapsed 当前阶段已持续的毫秒数。
 * @note 各阶段按计时 + path_permitted/inverter_stage_ready 复查推进, 不阻塞。
 */
static void power_phase_process(ac_input_state_t ac, uint32_t elapsed)
{
    switch (s_phase)
    {
    case PHASE_STOP_CHARGE:
        /* 停充尾段: CHG_OUT 已断, 1.5s 后断 CHG_IN; keep_bypass 时提交旁路, 否则进机械释放 */
        if (elapsed < PATH_STOP_CHARGE_TAIL_MS)
        {
            break;
        }
        drv_relay_off(RELAY_CHG_IN);
        if (s_stop_chg_keep_bypass)
        {
            s_stop_chg_keep_bypass = false;
            path_commit(PATH_BYPASS);
        }
        else
        {
            s_phase = PHASE_RELEASE_AC;
            s_phase_since = HAL_GetTick();
        }
        break;

    case PHASE_STOP_INV_TAIL:
        /* 顺序断电尾段: 1.5s 后断主接触器与预充, 再进入机械释放 */
        if (elapsed < PATH_STOP_INV_TAIL_MS)
        {
            break;
        }
        drv_relay_off(RELAY_INV_IN);
        drv_relay_off(RELAY_INV_PRO);
        s_phase = PHASE_RELEASE_AC;
        s_phase_since = HAL_GetTick();
        break;

    case PHASE_RELEASE_AC:
        if (elapsed < PATH_RELEASE_MS)
        {
            break;
        }
        s_dc_release_ms =
            drv_relay_is_on(RELAY_INV_IN) ? PATH_CONTACTOR_RELEASE_MS : PATH_RELEASE_MS;
        drv_relay_all_off(); /* PD4 off before PD7; no residual precharge feed. */
        s_phase = PHASE_RELEASE_DC;
        s_phase_since = HAL_GetTick();
        break;

    case PHASE_RELEASE_DC:
        if (elapsed < s_dc_release_ms)
        {
            break;
        }
        if (s_target_path == PATH_INVERT && ac == AC_INPUT_UNKNOWN)
        {
            if (elapsed >= PATH_INPUT_WAIT_MS)
            {
                input_abort();
            }
            break;
        }
        if (!path_permitted(s_target_path, ac))
        {
            power_abort();
            break;
        }
        if (s_target_path == PATH_NONE)
        {
            path_commit(PATH_NONE);
            break;
        }
        if (s_target_path == PATH_BYPASS)
        {
            if (relay_on(RELAY_AC_CTRL))
            {
                path_commit(PATH_BYPASS);
            }
            break;
        }
        if (path_charges(s_target_path))
        {
            if (!relay_on(RELAY_CHG_IN))
            {
                break;
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
                break;
            }
            s_phase = PHASE_PRECHARGE;
            s_ui_dirty = true;
            display_publish(); /* PD4 预充开启: 立即刷新逆变图标, 不等通路 commit */
            logger_out(LOG_INFO, "INV: PD4 ON, precharge 1500ms before PD7");
        }
        s_phase_since = HAL_GetTick();
        break;

    case PHASE_START_CHARGE:
        if (elapsed < PATH_CHARGER_START_MS)
        {
            break;
        }
        if (!path_permitted(s_target_path, ac))
        {
            power_abort();
            break;
        }
        if (!relay_on(RELAY_CHG_OUT))
        {
            break;
        }
        if (s_target_path == PATH_CHARGE_BYPASS && !relay_on(RELAY_AC_CTRL))
        {
            break;
        }
        path_commit(s_target_path);
        break;

    case PHASE_PRECHARGE:
        if (elapsed < PATH_PRECHARGE_MS || !inverter_stage_ready(ac, elapsed))
        {
            break;
        }
        if (!relay_on(RELAY_INV_IN))
        {
            break;
        }
        s_phase = PHASE_START_DC;
        s_phase_since = HAL_GetTick();
        logger_out(LOG_INFO, "INV: PD7 ON, wait 500ms then PD4 off (direct connect)");
        break;

    case PHASE_START_DC:
        if (elapsed < PATH_DC_FAST_MS || !inverter_stage_ready(ac, elapsed))
        {
            break;
        }
        drv_relay_off(RELAY_INV_PRO); /* 预充结束: 断开 PD4, 电池直连逆变器输入 */
        s_phase = PHASE_START_INVERTER;
        s_phase_since = HAL_GetTick();
        logger_out(LOG_INFO, "INV: PD4 OFF (direct), wait 1500ms before PD0");
        break;

    case PHASE_START_INVERTER:
        if (elapsed < PATH_INVERTER_START_MS || !inverter_stage_ready(ac, elapsed))
        {
            break;
        }
        if (relay_on(RELAY_INV_OUT))
        {
            path_commit(PATH_INVERT);
            logger_out(LOG_INFO, "INV: PD0 output ON");
        }
        break;

    default:
        break;
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
 * @note 充电由自动策略(charge_strategy_update)维护, 接入 220V 自动启充/充满自停。
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
 * @brief 充电策略: 220V 稳定 2s 且未充满时置 need_charge (由 work_status_process 调用)。
 * @param ac 输入计量模块提供的市电状态。
 * @note 防抖计时在 AC 波动(掉电再接入)时清零; 充满/无AC 自动收敛;
 *       故障/关机门控由 work_status_process 外部保证。
 */
static void charge_strategy_update(ac_input_state_t ac)
{
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
 * @brief 逆变稳态管理: 无负载(60s)或输出计量离线时自动停止输出。
 * @note  - 输出 HLW8110 离线(检测不到输出) -> 立即关断
 *        - vout_p_w < 1W 连续 60s -> 自动关断
 *        - 关断即清除 s_output_requested, 不会自动重启, 等下次 AC OUT 键触发
 *        - 仅由 work_status_process 稳态分派(逆变态)调用, 状态/通路门控外部保证
 */
static void inverter_idle_watchdog(void)
{
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

/*********************************************************************
 * @fn      fan_manage
 * @brief   风扇独立管理: 超温开启, 回落到阈值下关闭 (滞回)。
 * @param   t 温度快照 (bat/chg/inv 三路 NTC), 可为 NULL。
 * @note    主风扇: bat/chg 任一 >= FAN_ON_TH 开启; 辅风扇: inv >= FAN_ON_TH 开启;
 *          均低于 FAN_OFF_TH 关闭; 温度无效(t==NULL)保持现状不误动作;
 *          由主循环始终调用, 故障/关机后仍继续散热。
 * @return  none
*********************************************************************/
static void fan_manage(const TEMP_INFO_T *t)
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

/*********************************************************************
 * @fn      thermal_protect
 * @brief   温度保护: 按当前通路方向检查电池/充电/逆变过温。
 * @param   t 温度快照 (可能为 NULL)。
 * @param   charge 充电方向活跃 (目标通路或实测继电器)。
 * @param   invert 逆变方向活跃 (目标通路或实测继电器)。
 * @return  true: 已进入故障; false: 无过温。
 * @note    电池过温仅在有通路时检查; 充电/逆变过温按方向分别触发 OTP。
*********************************************************************/
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

/*********************************************************************
 * @fn      battery_protect
 * @brief   电池保护: 数据无效或方向相关故障位, 延时跳闸进故障。
 * @param   charge 充电方向活跃。
 * @param   invert 逆变方向活跃。
 * @return  true: 已进入故障; false: 正常或仍在延时计时。
 * @note    稳态延时 BATTERY_ACTION_DELAY_MS 跳闸防误断;
 *          启动中(PHASE 非 IDLE)立即跳闸 —— 禁止使用无效电池数据完成起动。
*********************************************************************/
static bool battery_protect(bool charge, bool invert)
{
    if (!charge && !invert)
    {
        s_battery_trip_pending = false;
        return false;
    }
    BATT_INFO_T bat = {0};
    uint32_t severe = 0;
    bool good = battery_path_ok(charge ? PATH_CHARGE : PATH_INVERT, &bat, &severe);
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

/*********************************************************************
 * @fn      full_charge_auto_stop
 * @brief   充满自动断充: MAX17263 FStat.FQ 上升沿检测, 仅在稳定充电态触发一次。
 * @note    CHARGE_BYPASS 时仅停充电, 保留 AC 直通输出; FQ 清零(转放电)后可再次触发;
 *          由 work_status_process 稳态分派(充电/充电+旁路态)调用。
 * @return  none
*********************************************************************/
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

/**
 * @brief 工作状态机: 统一管理保护、充电策略、通路切换与稳态附加管理。
 *
 * 阶段流水 (每 20ms, 主循环调用):
 *   门控(关机→shutdown_process / 故障→保持全断)
 *   → 温度/电池方向保护 (启动中立即, 稳态延时跳闸)
 *   → AC 掉电守卫 (需市电的通路在 220V 丢失/未知时立即停止)
 *   → 充电策略 (AC 稳定 2s → need_charge)
 *   → 目标通路合成+切换 (输出请求+充电策略; AC 恢复自动转旁路)
 *   → PHASE 时序推进 (预充/直连/输出/停充尾段)
 *   → 稳态附加管理 (按工作态分派: 充电→充满停充 / 逆变→空闲看门狗 / 空闲→无)
 * 工作态由 s_target_path 派生, 不新增状态变量。
 */
static void work_status_process(void)
{
    if (s_shutting_down)
    {
        shutdown_process();      /* 关机态: 推进释放等待/数据保存/断电 */
        return;
    }
    if (s_state == SYS_STATUS_FAULT)
    {
        return;                  /* 故障态: 保持全断, 等按键/复位 */
    }

    ac_input_state_t ac = mod_metering_get_ac_input().state;
    /* AC 状态变化立即标记 UI, 主循环 display_publish 即时刷新 220V 图标 */
    static ac_input_state_t s_last_ac_state = AC_INPUT_UNKNOWN;
    if (ac != s_last_ac_state)
    {
        s_last_ac_state = ac;
        s_ui_dirty = true;
    }

    /* 异常监督: 温度/电池方向保护 (启动中立即, 稳态延时跳闸) */
    const TEMP_INFO_T *t = mod_temp_get();
    bool charge = path_active_charge();
    bool invert = path_active_invert();
    if (thermal_protect(t, charge, invert) || battery_protect(charge, invert))
    {
        return;
    }

    /* AC 掉电守卫: 充电/旁路通路在 220V 丢失或未知时立即停止, 等下次按键 */
    if (path_needs_ac(s_target_path) && ac != AC_INPUT_PRESENT)
    {
        input_abort();
        return;
    }

    /* 充电策略: 220V 稳定 2s 且未充满 → need_charge (波动清零防抖) */
    charge_strategy_update(ac);

    /* 目标通路合成+切换: 输出请求 + 充电策略
     * AC present 时 resolve 天然合成旁路/充电+旁路 → 输入恢复自动转旁路 */
    energy_path_t target = resolve_target_path(ac, s_output_requested, s_need_charge);
    if (target != s_target_path)
    {
        path_request(target, ac); /* 拒绝/失败由保护与故障状态接管 */
    }

    /* 通路时序推进 (PHASE 状态机: 预充/直连/输出/停充尾段) */
    if (s_phase != PHASE_IDLE)
    {
        uint32_t elapsed = (uint32_t)(HAL_GetTick() - s_phase_since);
        power_phase_process(ac, elapsed);
        return;
    }

    /* 稳态附加管理: 按工作态分派 (风扇已独立到主循环, 此处不再涉及) */
    switch (s_target_path)
    {
    case PATH_CHARGE:
    case PATH_CHARGE_BYPASS:
        full_charge_auto_stop();  /* 充电/充电+旁路态: 充满自动停充 */
        break;
    case PATH_INVERT:
        inverter_idle_watchdog(); /* 逆变态: 无负载 60s / 计量离线自动停 */
        break;
    default:
        break;                    /* 空闲态: 无附加管理 */
    }
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
    /* 参数初始化: 读取 Flash 参数 (Legacy AC 阈值不再参与通路选择, 由状态机接管) */
    param_init();
    /* 外设安全初态: 上电先关风扇/告警灯, 避免残留状态 */
    drv_fan_off(FAN_AUX);
    drv_fan_off(FAN_MAIN);
    drv_led_off(LED_ALM);
    /* 按键: 初始化扫描, 注册事件回调 (仅 ONOFF 长按 / ACOUT 按下) */
    mod_key_init();
    mod_key_register_callback(app_main_task_key_event_handler);
    logger_out(LOG_INFO,
               "Power controller: auto-charge(AC 2s stable), INV PD4->1.5s->PD7->0.5s->PD4off->1.5s->PD0");
    for (;;)
    {
        /* 风扇: 独立管理, 始终执行 (故障/关机后仍散热) */
        fan_manage(mod_temp_get());
        /* 工作状态机: 保护 + 充电策略 + 通路切换 + 稳态管理 */
        work_status_process();
        /* 按键: 轮询扫描, 事件经回调分发 (仅 ONOFF 长按 / ACOUT 按下) */
        mod_key_poll_handler();
        /* 指示灯: 非阻塞刷新工作灯 (绿灯 1s 亮灭 / 蓝灯 5s 快闪) */
        work_led_process();
        /* 显示: 通路/状态有变化时向显示任务发送图标消息 */
        display_publish();
        /* 看门狗: 主任务心跳喂狗 */
        bsp_wdg_task_heartbeat(WDG_TASK_MAIN);
        osDelay(20); /* 20ms 任务节拍 */
    }
}
