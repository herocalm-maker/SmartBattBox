/**
 * @file app_main_task.c
 * @brief 主任务拥有通路状态机，负责保护、按键和工作指示。
 * @note 逆变启动：PD4等待500ms，再开PD7等待100ms，最后开PD0。
 *       继电器软件状态仅表示输出命令，不代表触点反馈。
 */
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "main.h"
#include "app_main_task.h"
#include "project_param.h"
#include "app_display_task.h"
#include "mod_battery.h"
#include "battery_config.h"
#include "mod_metering.h"
#include "mod_key.h"
#include "mod_temp.h"
#include "drv_buzzer.h"
#include "drv_led.h"
#include "drv_fan.h"
#include "drv_relaysw.h"
#include "bsp_wdg.h"
#include "comm_logger.h"
#include "Board.h"

#define ALM_BAT_OVP (1u << 1)
#define ALM_BAT_UVP (1u << 2)
#define ALM_BAT_OTP (1u << 3)
#define ALM_INV_OTP (1u << 6)
#define ALM_CHG_OTP (1u << 7)
#define ALM_AC_INPUT (1u << 8)
#define ALM_RELAY (1u << 9)
#define ALM_BAT_INVALID (1u << 10)

/* Bench-validation defaults, not verified contact/charger/inverter timings. */
#define PATH_RELEASE_MS 50u
#define PATH_CONTACTOR_RELEASE_MS 100u
#define PATH_PRECHARGE_MS 500u
#define PATH_INVERTER_START_MS 100u
#define PATH_CHARGER_START_MS 1500u
#define PATH_INPUT_WAIT_MS 5000u

typedef enum
{
    PHASE_IDLE,
    PHASE_RELEASE_AC,
    PHASE_RELEASE_DC,
    PHASE_START_CHARGE,
    PHASE_PRECHARGE,
    PHASE_START_INVERTER,
    PHASE_STOP_CHARGE
} path_phase_t;
static energy_path_t s_cur_path = PATH_NONE;
static energy_path_t s_target_path = PATH_NONE;
static path_phase_t s_phase = PHASE_IDLE;
static uint32_t s_phase_since;
static bool s_charge_requested;
static bool s_output_requested;
static bool s_shutting_down;
static bool s_shutdown_complete;
static uint32_t s_dc_release_ms = PATH_RELEASE_MS;
static volatile SYS_STATUS_T s_state = SYS_STATUS_IDLE;
static volatile uint32_t s_alarm;
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
 * @brief 读取当前累计的告警位图。
 * @return 告警位掩码，0表示无已记录告警。
 */
uint32_t alarm_get(void)
{
    return s_alarm;
}

/**
 * @brief 判断目标通路是否包含充电。
 * @param path 待检查或提交的能量通路。
 * @return true：包含充电；false：不包含充电。
 */
static bool path_charges(energy_path_t path)
{
    return path == PATH_CHARGE || path == PATH_CHARGE_BYPASS;
}

/**
 * @brief 判断目标通路是否依赖有效市电输入。
 * @param path 待检查或提交的能量通路。
 * @return true：需要市电；false：不需要市电。
 */
static bool path_needs_ac(energy_path_t path)
{
    return path == PATH_BYPASS || path_charges(path);
}

/**
 * @brief 按充电或放电方向选择电池保护位。
 * @param path 待检查或提交的能量通路。
 * @return 当前方向需要检查的电池故障位掩码。
 */
static uint32_t battery_path_fault_mask(energy_path_t path)
{
    return path_charges(path) ? (BAT_FAULT_OV | BAT_FAULT_OC_CHG)
                              : (BAT_FAULT_UV | BAT_FAULT_OC_DIS);
}

/**
 * @brief 更新系统状态，并在变化时请求刷新显示。
 * @param state 新的系统运行状态。
 */
static void state_set(SYS_STATUS_T state)
{
    if (s_state != state)
    {
        logger_print(LOG_INFO, "ST %d -> %d", (int)s_state, (int)state);
        s_state = state;
        s_ui_dirty = true;
    }
}

/**
 * @brief 非阻塞发送继电器命令对应的图标状态，队列满时留待重试。
 */
static void display_publish(void)
{
    if (!s_ui_dirty)
    {
        return;
    }
    ucMESSAGE_t msg = {0};
    msg.MsgID = EVT_DISP_ICON;
    msg.Length = 2;
    /* Actual commands, not requests: charger may still be starting. */
    msg.Paras.Value[0] = drv_relay_is_on(RELAY_CHG_OUT) ? 1 : 0;
    msg.Paras.Value[1] = (drv_relay_is_on(RELAY_AC_CTRL) || drv_relay_is_on(RELAY_INV_OUT)) ? 2 : 0;
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
    s_charge_requested = s_output_requested = false;
    s_battery_trip_pending = false;
    path_commit(PATH_NONE); /* Also cancels every delayed close operation. */
}

/**
 * @brief 记录告警、关闭通路并进入故障状态。
 * @param alarm 本次新增的告警位掩码。
 */
static void enter_fault(uint32_t alarm)
{
    s_alarm |= alarm;
    power_abort();
    state_set(SYS_STATUS_FAULT);
    drv_led_on(LED_ALM);
    drv_buzzer_short_sound(3);
    logger_print(LOG_ERROR, "FAULT: alarm=0x%lX", (unsigned long)s_alarm);
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
    enter_fault(ALM_RELAY);
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
        if (!battery_get_snapshot(&bat) || (bat.active_faults & battery_path_fault_mask(path)))
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
    drv_relay_all_off();
    /* 全部输出已撤销；后续阶段仅等待释放，再判断是否开启目标通路。 */
    s_cur_path = PATH_NONE;
    s_phase = PHASE_RELEASE_AC;
    s_phase_since = HAL_GetTick();
    state_set(SYS_STATUS_SWITCHING);
    s_ui_dirty = true;
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
        logger_print(LOG_WARN, "PATH %d rejected: source/battery/temperature", (int)path);
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
    s_alarm |= ALM_AC_INPUT;
    power_abort();
    drv_buzzer_short_sound(2);
    logger_print(LOG_WARN, "AC input invalid/unknown: routes off; ACOUT required to restart");
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
            logger_print(LOG_WARN, "BAT checkpoint failed");
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
        if (elapsed < PATH_RELEASE_MS)
        {
            return;
        }
        drv_relay_off(RELAY_CHG_IN);
        path_commit(PATH_BYPASS);
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
        }
        else
        {
            if (!relay_on(RELAY_INV_PRO))
            {
                return;
            }
            s_phase = PHASE_PRECHARGE;
            logger_print(LOG_INFO, "INV: PD4 ON, wait 500ms before PD7");
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
        s_phase = PHASE_START_INVERTER;
        s_phase_since = HAL_GetTick();
        logger_print(LOG_INFO, "INV: PD7 ON, PD4 held ON, wait 100ms before PD0");
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
            logger_print(LOG_INFO, "INV: PD0 output ON");
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
    s_charge_requested = s_output_requested = false;
    s_battery_trip_pending = false;
    path_begin_stop(PATH_NONE);
    logger_print(LOG_INFO, "ONOFF: 3s hold, stopping routes before power off");
    /* 保留当前关机顺序：给提示音留出时间，再撤销电源保持。 */
    drv_buzzer_short_sound(1);
    osDelay(300);
    DEV_POWER_OFF();
    osDelay(500);
}

/**
 * @brief 处理ACIN/ACOUT切换请求，支持启动过程中取消。
 * @param charge true表示ACIN；false表示ACOUT。
 */
static void handle_path_key(bool charge)
{
    ac_input_state_t ac = mod_metering_get_ac_input().state;
    if (s_state == SYS_STATUS_FAULT || s_shutting_down)
    {
        return;
    }

    /* Toggle the accepted request, including startup, not just closed contacts.
     * An OFF press is never rejected because input detection is unavailable. */
    if (charge ? s_charge_requested : s_output_requested)
    {
        bool keep_charge = !charge && s_charge_requested && ac == AC_INPUT_PRESENT;
        bool keep_output = charge && s_output_requested && ac == AC_INPUT_PRESENT;
        energy_path_t target = keep_charge ? PATH_CHARGE : (keep_output ? PATH_BYPASS : PATH_NONE);
        if (path_request(target, ac))
        {
            s_charge_requested = keep_charge;
            s_output_requested = keep_output;
            drv_buzzer_short_sound(1);
        }
        return;
    }
    if (ac == AC_INPUT_UNKNOWN || (charge && ac != AC_INPUT_PRESENT))
    {
        logger_print(LOG_WARN, "KEY rejected: AC input unavailable");
        if (charge)
        {
            drv_buzzer_reject_sound();
        }
        else
        {
            drv_buzzer_short_sound(2);
        }
        return;
    }
    /* ACIN ON retains the original charge-only policy; ACOUT ON preserves charge. */
    bool want_charge = charge || (ac == AC_INPUT_PRESENT && s_charge_requested);
    energy_path_t target =
        charge ? PATH_CHARGE
               : (ac == AC_INPUT_PRESENT ? (want_charge ? PATH_CHARGE_BYPASS : PATH_BYPASS)
                                         : PATH_INVERT);
    if (path_request(target, ac))
    {
        s_charge_requested = want_charge;
        s_output_requested = !charge;
        s_alarm &= ~ALM_AC_INPUT;
        drv_buzzer_short_sound(1);
    }
}

/**
 * @brief 分发按键事件：ONOFF仅响应长按，AC键仅响应按下。
 * @param key_code 按键编码，包含按下或长按标志。
 */
static void _key_event_handler(uint8_t key_code)
{
    switch (key_code)
    {
        case KEY_LONG_VALUE_ONOFF:
            handle_onoff_key();
            break;
        /* Scanner emits both press and long events. AC keys act only on press. */
        case KEY_VALUE_ACIN:
            handle_path_key(true);
            break;
        case KEY_VALUE_ACOUT:
            handle_path_key(false);
            break;
        default:
            break;
    }
}
/* Thermal control follows active/starting routes, including charge+bypass.
 * Fans continue cooling hot components after route changes and faults. */
/**
 * @brief 处理风扇回差控制及当前通路的温度、电池保护。
 */
static void work_status_process(void)
{
    const TEMP_INFO_T *t = mod_temp_get();
    if (t)
    {
        if (!s_fan_aux_on && t->chg_temp >= BOARD_PROT_FAN_ON_TH)
        {
            drv_fan_on(FAN_AUX);
            s_fan_aux_on = true;
        }
        else if (s_fan_aux_on && t->chg_temp <= BOARD_PROT_FAN_OFF_TH)
        {
            drv_fan_off(FAN_AUX);
            s_fan_aux_on = false;
        }
        if (!s_fan_main_on &&
            (t->inv_temp >= BOARD_PROT_FAN_ON_TH || t->bat_temp >= BOARD_PROT_FAN_ON_TH))
        {
            drv_fan_on(FAN_MAIN);
            s_fan_main_on = true;
        }
        else if (s_fan_main_on && t->inv_temp <= BOARD_PROT_FAN_OFF_TH &&
                 t->bat_temp <= BOARD_PROT_FAN_OFF_TH)
        {
            drv_fan_off(FAN_MAIN);
            s_fan_main_on = false;
        }
    }
    if (s_state == SYS_STATUS_FAULT || s_shutting_down)
    {
        return;
    }
    bool charge = path_charges(s_target_path) || drv_relay_is_on(RELAY_CHG_IN);
    bool invert = s_target_path == PATH_INVERT || drv_relay_is_on(RELAY_INV_PRO) ||
                  drv_relay_is_on(RELAY_INV_IN);
    if (t)
    {
        if (s_target_path != PATH_NONE && t->bat_temp >= BOARD_PROT_BAT_TMP_FAULT_TH)
        {
            enter_fault(ALM_BAT_OTP);
            return;
        }
        if (charge && t->chg_temp >= BOARD_PROT_CHG_TMP_FAULT_TH)
        {
            enter_fault(ALM_CHG_OTP);
            return;
        }
        if (invert && t->inv_temp >= BOARD_PROT_INV_TMP_FAULT_TH)
        {
            enter_fault(ALM_INV_OTP);
            return;
        }
    }
    if (charge || invert)
    {
        BATT_INFO_T bat = {0};
        bool good = battery_get_snapshot(&bat);
        uint32_t severe =
            bat.active_faults & battery_path_fault_mask(charge ? PATH_CHARGE : PATH_INVERT);
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
                enter_fault(ALM_BAT_INVALID | ((severe & BAT_FAULT_OV) ? ALM_BAT_OVP : 0) |
                            ((severe & BAT_FAULT_UV) ? ALM_BAT_UVP : 0));
                return;
            }
        }
        else
        {
            s_battery_trip_pending = false;
        }
    }
    else
    {
        s_battery_trip_pending = false;
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
    param_init(); /* Legacy AC threshold settings no longer select power routes. */
    drv_fan_off(FAN_AUX);
    drv_fan_off(FAN_MAIN);
    drv_led_off(LED_ALM);
    mod_key_init();
    mod_key_register_callback(_key_event_handler);
    logger_print(
        LOG_INFO,
        "Power controller: AC=90..300V, PD4 -> 500ms -> PD7 -> 100ms -> PD0; PD4 held ON");
    for (;;)
    {
        /* Apply source/fault changes before accepting a new key request. */
        work_status_process();
        power_process();
        mod_key_poll_handler();
        work_led_process();
        display_publish();
        bsp_wdg_task_heartbeat(WDG_TASK_MAIN);
        osDelay(20);
    }
}
