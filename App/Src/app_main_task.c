/**
 * @file app_main_task.c
 * @brief 应用层主任务: 工作状态机(保护/充电策略/通路切换/稳态管理) + 风扇 + 按键与指示。
 * @note 逆变启动三段时序: PD4预充1.5s → PD7吸合 → 0.5s后PD4断(电池直连) → 1.5s后PD0输出。
 *       停充/停逆变按 1.5s 顺序断电(先断输出端, 延时后断输入端/主接触器)。
 *       继电器软件状态仅表示输出命令，不代表触点反馈。
 *       充满检测: MAX17263 FStat.FQ 置位时自动停止充电; FQ 未置位时由"电压达满充+
 *             taper电流"兜底判定停充 (CHARGE_BYPASS 保留AC直通)。
 *       按键: 仅 AC OUT=输出开关(有220V→旁路 / 无→逆变); 充电由接入220V自动触发;
 *             AC IN 已在应用层屏蔽。主循环 20ms 节拍。
 *       输出: 插入 220V 自动打开旁路输出, 与充电并行, 充满停充后旁路保持;
 *             按 AC OUT 可关闭, 且本段 220V 会话内不再自动重开; 拔掉 220V 后
 *             旁路请求失效 (逆变输出只能按键启动); 旁路/逆变空载 50min 自动关输出
 *             (只关输出, 不关机; 再开需按 AC OUT)。
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
#include <string.h>
#include <stdlib.h>



/*====================================================================
 * 工作模式枚举
 *====================================================================*/
typedef enum
{
    WORK_MODE_IDLE,              /* 空闲（无输出） */
    WORK_MODE_CHARGE_BYPASS,     /* 充电+旁路输出（有220V接入） */
    WORK_MODE_INVERT,            /* 逆变输出（无220V接入） */
    WORK_MODE_SHUTDOWN,          /* 关机 */
} work_mode_t;

/*====================================================================
 * 各功能子状态机枚举
 *====================================================================*/

/* 充电时序状态机 */
typedef enum
{
    CHARGE_STATE_IDLE,           /* 空闲 */
    CHARGE_STATE_STARTING,       /* 启动中 (CHG_IN → 1.5s → CHG_OUT) */
    CHARGE_STATE_RUNNING,        /* 稳定充电 */
    CHARGE_STATE_STOPPING,       /* 停止中 (CHG_OUT → 1.5s → CHG_IN) */
} charge_state_t;

/* 旁路输出时序状态机 */
typedef enum
{
    BYPASS_STATE_IDLE,           /* 空闲 */
    BYPASS_STATE_STARTING,       /* 启动中 (AC_CTRL) */
    BYPASS_STATE_RUNNING,        /* 稳定旁路 */
    BYPASS_STATE_STOPPING,       /* 停止中 (AC_CTRL) */
} bypass_state_t;

/* 逆变输出时序状态机 */
typedef enum
{
    INVERT_STATE_IDLE,           /* 空闲 */
    INVERT_STATE_PRECHARGE,      /* PD4 预充 (1.5s) */
    INVERT_STATE_DC_CONNECT,     /* PD7 吸合 + 0.5s 后断 PD4 */
    INVERT_STATE_OUTPUT,         /* 等待 PD0 输出 (1.5s) */
    INVERT_STATE_RUNNING,        /* 稳定输出 */
    INVERT_STATE_STOPPING,       /* 停止中 (顺序断电 1.5s) */
} invert_state_t;

/*====================================================================
 * 时序常量
 *====================================================================*/
#define PATH_CHARGER_START_MS        1500u   /* 充电: CHG_IN 后等充电器稳定再闭合 CHG_OUT */
#define PATH_PRECHARGE_MS            1500u   /* 逆变: PD4 预充 1.5s 后吸合 PD7 */
#define PATH_DC_FAST_MS              500u    /* 逆变: PD7 后 0.5s 断开 PD4, 电池直连 */
#define PATH_INVERTER_START_MS       1500u   /* 逆变: 直连后 1.5s 开 PD0 输出 */
#define PATH_STOP_CHARGE_TAIL_MS     1500u   /* 停充: CHG_OUT 断开后 1500ms 再断 CHG_IN */
#define PATH_STOP_INV_TAIL_MS        1500u   /* 停逆变: PD0 断开后 1500ms 再断 PD7/PD4 */
#define AC_STABLE_MS                 2000u   /* 自动充电: 220V 稳定 2s 后启充 (防抖) */
#define AC_IDLE_STOP_MS              3000000u /* 交流输出(旁路/逆变): 无负载持续 50min 自动关闭 */
#define AC_IDLE_PWR_W                1.0f     /* 无负载判定阈值 W (输出功率 > 1W 视为接了负载) */
#define IDLE_SHUTDOWN_MS             3600000u /* 整机: 无带载供电且未充电持续 1h 自动关机 */
#define INV_MIN_SOC_PCT              1u      /* 逆变输出电量门限: SOC<1%(即0%) 不允许/自动关断, SOC>=1% 允许 */
/* 逆变输出单体欠压保护 (2026-11-21 新增, 替代过低的 2.5V 门限):
 *   软停: 单体 <= 2.80V → 自动关闭逆变输出 (3 声提示), 回升到 2.85V 允许再开
 *   硬停: 单体 <= 2.70V → 立即关断并锁存, 必须回升到 2.85V 才解除
 * 背景: 现场实测放电到整包 20.67V(2.58V/单体) 才靠 SOC=0 关断, LFP 已深度过放。 */
#define INV_CELL_SOFT_MV             2800u   /* 软停门限: 单体 <= 2.80V 关输出 */
#define INV_CELL_HARD_MV             2700u   /* 硬停门限: 单体 <= 2.70V 关断并锁存 */
#define INV_CELL_RESUME_MV           3000u   /* 解除门限: 单体 >= 3.00V 才允许重新开启
                                              * (2.85V 太低: 静置 3.03V 带载 25A 会跌到 2.79V,
                                              *  2.85V 起机瞬间就击穿软停甚至硬停) */
#define INV_UV_RESTART_HOLD_MS       120000u /* 欠压/低电量停输出后 2 分钟内拒绝重启 */
#define UI_CODE_HOLD_MS              10000u  /* 瞬时提示码 (ERRxx) 显示时长, ms */
#define OVERLOAD_BEEP_PERIOD_MS      2000u   /* 过载声光报警周期 */
#define OVERLOAD_BEEP_HALF_MS        1000u   /* 报警相位时长: 1.0s 亮/响 + 1.0s 静默 */
/* 判满口径 (产品定义, 2026-10-25):
 *   启动: 有 220V 且 SOC<100% 且不在停充冷却期
 *   判满: SOC=100% 时, FQ=1 且 TTF=0 → 立即停充;
 *         若 SOC=100% 但 FQ/TTF 未同时满足 → 保持 5min 后强制停充
 *   冷却: 停充后 10min 内不再启动 (避免插着 220V 反复充电)
 *   拔枪: 充电中 220V 丢失立即断充 (mode_decide 切 INVERT 时停止充电) */
#define CHARGE_FULL_SOC              100u     /* 判满 SOC 门槛: 报告 SOC 到 100% 才停充 */
/* 停充后重新充电的门限 (双条件, 见 need_charge 判定):
 *   SOC <= 97% 且 整包电压 < 27.2V (=3.40V/单体) 才补电。
 * 为什么需要: 停充后电池电压弛豫, 库仑计按 OCV 重算 SOC, 会从 100% 掉到 99%,
 * 老门限 (SOC<100) 判定为"要充电", 于是每 10min 补电 2~3 秒就停 (继电器反复吸合)。
 * LFP 的 OCV-SOC 曲线在 20~90% 极平 (3.30~3.35V), 单靠电压估不准 SOC,
 * 但用它做"是否真的离开满电平台"的迟滞判据是可靠的。 */
#define CHARGE_RESTART_SOC           97u      /* 允许重启的 SOC 上限: <= 该值才重新充电 */
/* 电压门限带迟滞 (27.16V 进入 / 27.24V 退出), 避免在 27.2V 附近抖动导致 need 反复翻转。
 * 实测: 无迟滞时电压在 27196~27214mV 间波动, 9 秒内 need 翻转两次。 */
#define CHARGE_RESTART_V_ENTER_MV    27160u   /* 允许重启: 整包电压 < 27.16V (3.395V/单体) */
#define CHARGE_RESTART_V_EXIT_MV     27240u   /* 撤销重启: 整包电压 > 27.24V (3.405V/单体) */
/* AC 掉电恢复后"来电补满"窗口: 该窗口内重启充电不受电压条件限制
 * (SOC 门限仍生效)。窗口过期后回到正常双条件判定。 */
#define AC_BACK_TOPUP_MS             600000u  /* AC 恢复后 10min 内可免电压条件补电 */
#define CHARGE_FULL_FORCE_MS         300000u  /* SOC=100 但 FQ/TTF 未同时满足 → 5min 后强制停 */
#define CHARGE_FULL_V_MV             3600u    /* 物理兜底: 单体电压门槛 3600mV (=28.8V/8) */
#define CHARGE_FULL_I_MA             500      /* 物理兜底: 库仑计平均电流门槛, mA */
#define CHARGE_FULL_V_I_HOLD_MS      3600000u /* 物理兜底: 电压+TTF+电流三条件连续保持 1h */
#define PHYS_BREAK_TOLERANCE_MS      120000u  /* 物理兜底: 条件短暂不满足的容错窗口
                                              * (taper 段 avgI 在 500mA 附近抖动, 10s 不够,
                                              *  实测 30s 一次 re-bulk 会把 1h 计时反复清零) */
#define CHARGE_FULL_PHYS_REARM_SOC   95u      /* 物理兜底判满后, SOC 低于该值才允许再充 */
#define CHARGE_RESTART_BLOCK_MS      600000u  /* 停充后 10min 冷却, 期间不重新启动充电 */

/*====================================================================
 * 主任务状态结构体
 *====================================================================*/
typedef struct
{
    /* === 顶层状态 === */
    work_mode_t mode;                       /* 当前工作模式 */

    /* === 各功能独立状态机 === */
    charge_state_t charge_state;            /* 充电状态 */
    bypass_state_t bypass_state;            /* 旁路状态 */
    invert_state_t invert_state;            /* 逆变状态 */

    /* === 计时器 === */
    uint32_t charge_phase_since;            /* 充电阶段起始时间 */
    uint32_t bypass_phase_since;            /* 旁路阶段起始时间 */
    uint32_t invert_phase_since;            /* 逆变阶段起始时间 */

    /* === 标志位 (9 位, 用 uint16_t 承载避免跨存储单元) === */
    struct {
        uint16_t ac_out_requested : 1;      /* AC OUT 键请求 */
        uint16_t need_charge : 1;           /* 自动充电生效 */
        uint16_t charge_full : 1;           /* 充满兜底判定锁存 (FQ 未置位时按电压+taper判满) */
        uint16_t fault_active : 1;          /* 故障锁死 */
        uint16_t bat_otp : 1;               /* 电池 NTC 过温降载 (停充+停输出, 可恢复) */
        uint16_t fan_aux_on : 1;            /* 辅风扇运行 */
        uint16_t fan_main_on : 1;           /* 主风扇运行 */
        uint16_t ui_dirty : 1;              /* UI 需刷新 */
        uint16_t battery_trip_pending : 1;  /* 电池跳闸待处理 */
    } flags;

    /* === 其他计时器 === */
    uint32_t out_idle_since;                /* 交流输出空载计时起点 (旁路/逆变共用) */
    uint32_t battery_trip_since;            /* 电池跳闸起算时间戳 */
} main_task_state_t;

static main_task_state_t s_state = {
    .mode = WORK_MODE_IDLE,
    .charge_state = CHARGE_STATE_IDLE,
    .bypass_state = BYPASS_STATE_IDLE,
    .invert_state = INVERT_STATE_IDLE,
    .charge_phase_since = 0,
    .bypass_phase_since = 0,
    .invert_phase_since = 0,
    .flags = {0},
    .out_idle_since = 0,
    .battery_trip_since = 0,
};

/* 过载声光报警状态: 由 output_power_supervise() 每拍刷新, 显示任务经
 * app_ui_overload_alarm() 只读消费 (跨任务, 故加 volatile)。 */
static volatile bool s_overload_alarm;
/* 过载蜂鸣相位沿检测: 只在报警相位上升沿触发一次短响 */
static bool s_overload_phase_prev;
/* 低电量告警状态: 软停/硬停/SOC<1% 停输出或按键被拒时置位, 充电或逆变重启后清除。
 * 显示任务经 app_ui_lowbat() 只读消费 (跨任务, 故加 volatile)。 */
static volatile bool s_ui_lowbat;
/* 逆变欠压状态: 硬停锁存 + 最近一次欠压停输出时刻 (供冷却判定/按键提示复用) */
static bool     s_uv_hard;
static uint32_t s_uv_stop_ms;
/* 充满停充后的重启封锁状态: full_charge_auto_stop 置位, charge_strategy_update
 * 消费; 220V 拔出再插入立即解除, 或 10min 超时自动解除。
 * 定义在 s_state 之后、所有使用点之前, 供 charge_restart_block_left_s() 只读查询。 */
static uint32_t s_chg_block_since;
static bool s_chg_block;
/* 物理兜底判满锁存 (电压+TTF+电流 持续 1h): 置位后需 SOC<CHARGE_FULL_PHYS_REARM_SOC
 * 或拔插 220V 才解除, 防止"芯片 SOC 到不了 100%"时每 10min 反复补电。 */
static bool s_phys_full;

/* 瞬时提示码 (显示 ERRxx): 用于"按键被拒/电量不足不允许启动"等一次性提示,
 * 到时自动消失; 优先级低于 app_alarm_err_code() 的真实告警码。 */
static uint8_t s_ui_code;
static uint32_t s_ui_code_until;

/**
 * @brief 显示一个瞬时提示码 (ERRxx), 到时自动消失。
 * @param code 提示码 (1~99)
 * @param hold_ms 保持时间, ms
 */
void app_ui_show_code(uint8_t code, uint32_t hold_ms)
{
    taskENTER_CRITICAL();
    s_ui_code = code;
    s_ui_code_until = HAL_GetTick() + hold_ms;
    taskEXIT_CRITICAL();
}

/**
 * @brief 读取当前瞬时提示码 (超时自动清零)。
 * @return 提示码; 0=当前无提示。
 */
uint8_t app_ui_code_get(void)
{
    uint8_t code = 0;
    uint32_t now = HAL_GetTick();
    taskENTER_CRITICAL();
    if (s_ui_code != 0u && (int32_t)(now - s_ui_code_until) < 0)
    {
        code = s_ui_code;
    }
    else
    {
        s_ui_code = 0;
    }
    taskEXIT_CRITICAL();
    return code;
}

/**
 * @brief 当前是否处于过载告警 (输出功率超告警门限且输出通路运行中)。
 * @return true: 需要声光报警。
 */
bool app_ui_overload_alarm(void)
{
    return s_overload_alarm;
}

/**
 * @brief 低电量告警 (屏 TextAlarm 显示 "LOWBAT")。
 * @return true: 电池欠压/低电量导致逆变停输出, 且尚未恢复。
 * @note  置位: 软停 / 硬停 / SOC<1% 停输出时;
 *        清除: 重新开始充电, 或逆变重新进入 RUNNING。
 */
bool app_ui_lowbat(void)
{
    return s_ui_lowbat;
}

/**
 * @brief 过载声光节奏相位 (2.0s 周期, 前 1.0s 为报警相位)。
 * @return true: 报警相位。
 */
bool app_ui_overload_phase(void)
{
    return (HAL_GetTick() % OVERLOAD_BEEP_PERIOD_MS) < OVERLOAD_BEEP_HALF_MS;
}

/**
 * @brief 当前是否有任一散热风扇在运行。
 * @return true: 主风扇或辅风扇已开启。
 * @note  跨任务只读 (显示任务消费); 位域按单字节访问, 不会读到撕裂值。
 */
bool app_ui_fan_running(void)
{
    return s_state.flags.fan_main_on || s_state.flags.fan_aux_on;
}

/**
 * @brief 当前各风扇的运行状态位图。
 * @return APP_UI_FAN_MAIN / APP_UI_FAN_AUX 的按位或; 0 = 全部停止。
 * @note  显示任务据此决定温度文本取哪几路 NTC 的最大值。
 */
uint8_t app_ui_fan_state(void)
{
    uint8_t m = 0u;
    if (s_state.flags.fan_main_on)
    {
        m |= APP_UI_FAN_MAIN;
    }
    if (s_state.flags.fan_aux_on)
    {
        m |= APP_UI_FAN_AUX;
    }
    return m;
}

/**
 * @brief 当前是否在给整机/电池充电 (CHG_IN 已吸合, 状态机启动或运行)。
 * @return true: 充电状态机处于 STARTING / RUNNING。
 * @note  跨任务只读。显示层用它判断"输入功率是否有效"。
 *        注意与 battery_full_active() 区分: 后者只表示"上次停充是因为判满",
 *        停充后仍为 true, 不能用来判断"当前是否在充电"。
 */
bool app_ui_charging(void)
{
    return (s_state.charge_state == CHARGE_STATE_STARTING ||
            s_state.charge_state == CHARGE_STATE_RUNNING);
}

/**
 * @brief 当前旁路输出是否活跃 (AC_CTRL 已吸合)。
 * @return true: 旁路状态机处于 STARTING / RUNNING。
 * @note  跨任务只读。与 app_ui_charging() 一起决定输入功率是否有效:
 *        两者都不活跃时, 220V 入口的功率对用户没有意义, 显示 0。
 */
bool app_ui_bypass_active(void)
{
    return (s_state.bypass_state == BYPASS_STATE_STARTING ||
            s_state.bypass_state == BYPASS_STATE_RUNNING);
}

/* 通路语义判定 (原 mod_acswitch 迁入, 纯逻辑无驱动依赖) */
/**
 * @fn      path_charges
 * @brief   判定能量通路是否属于充电方向。
 * @param   path 能量通路枚举。
 * @return  true: 充电方向 (PATH_CHARGE / PATH_CHARGE_BYPASS)。
 */
static bool path_charges(energy_path_t path)
{
    return path == PATH_CHARGE || path == PATH_CHARGE_BYPASS;
}

/**
 * @brief 读取当前系统运行状态（实时派生）。
 * @return 当前系统状态枚举值。
 */
SYS_STATUS_T state_get(void)
{
    if (s_state.flags.fault_active)
    {
        return SYS_STATUS_FAULT;
    }
    if (s_state.mode == WORK_MODE_SHUTDOWN)
    {
        return SYS_STATUS_SWITCHING;
    }
    switch (s_state.mode)
    {
        case WORK_MODE_CHARGE_BYPASS:
            if (s_state.charge_state == CHARGE_STATE_RUNNING)
                return SYS_STATUS_CHARGE_BYPASS;
            else
                return SYS_STATUS_BYPASS;
        case WORK_MODE_INVERT:
            return SYS_STATUS_OUTPOWER;
        default:
            return SYS_STATUS_IDLE;
    }
}

/* ---------------- 诊断查询: 供周期日志打印充电/满电状态 ---------------- */

/**
 * @brief 工作模式名称 (日志用)。
 * @return 静态字符串: IDLE/CHARGE_BYPASS/INVERT/SHUTDOWN/?
 */
const char *work_mode_name(void)
{
    switch (s_state.mode)
    {
        case WORK_MODE_IDLE:
            return "IDLE";
        case WORK_MODE_CHARGE_BYPASS:
            return "CHARGE_BYPASS";
        case WORK_MODE_INVERT:
            return "INVERT";
        case WORK_MODE_SHUTDOWN:
            return "SHUTDOWN";
        default:
            return "?";
    }
}

/**
 * @brief 充电状态机名称 (日志用)。
 * @return 静态字符串: IDLE/STARTING/RUNNING/STOPPING/?
 */
const char *charge_state_name(void)
{
    switch (s_state.charge_state)
    {
        case CHARGE_STATE_IDLE:
            return "IDLE";
        case CHARGE_STATE_STARTING:
            return "STARTING";
        case CHARGE_STATE_RUNNING:
            return "RUNNING";
        case CHARGE_STATE_STOPPING:
            return "STOPPING";
        default:
            return "?";
    }
}

/**
 * @brief 当前是否有充电请求 (charge_strategy_update 的结论)。
 * @return true: 有补电请求。
 */
bool charge_need_active(void)
{
    return s_state.flags.need_charge != 0;
}

/**
 * @brief 充满停充后的重启封锁剩余时间。
 * @return 剩余秒数; 0 表示当前未封锁。
 * @note   只读, 不消费封锁状态 (真正的清除在 charge_restart_blocked 内)。
 */
uint32_t charge_restart_block_left_s(void)
{
    uint32_t elapsed;
    if (!s_chg_block)
    {
        return 0;
    }
    elapsed = (uint32_t)(HAL_GetTick() - s_chg_block_since);
    if (elapsed >= CHARGE_RESTART_BLOCK_MS)
    {
        return 0;
    }
    return (CHARGE_RESTART_BLOCK_MS - elapsed) / 1000u + 1u;
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

/* 通路活跃判定: 根据工作模式判断 */
/**
 * @fn      path_active_charge
 * @brief   判定充电通路是否处于活跃阶段 (STARTING/RUNNING)。
 * @return  true: 当前为充电模式且充电子状态机已启动或运行中。
 */
static bool path_active_charge(void)
{
    return s_state.mode == WORK_MODE_CHARGE_BYPASS &&
           (s_state.charge_state == CHARGE_STATE_STARTING ||
            s_state.charge_state == CHARGE_STATE_RUNNING);
}
/**
 * @fn      path_active_invert
 * @brief   判定逆变通路是否处于活跃阶段 (预充至运行的任一子状态)。
 * @return  true: 当前为逆变模式且子状态机处于 PRECHARGE/DC_CONNECT/OUTPUT/RUNNING。
 * @note    不含 STOPPING: 停止阶段保护检查按非活跃处理, 不再触发方向性跳闸。
 */
static bool path_active_invert(void)
{
    return s_state.mode == WORK_MODE_INVERT &&
           (s_state.invert_state == INVERT_STATE_PRECHARGE ||
            s_state.invert_state == INVERT_STATE_DC_CONNECT ||
            s_state.invert_state == INVERT_STATE_OUTPUT ||
            s_state.invert_state == INVERT_STATE_RUNNING);
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
 * @brief 非阻塞发送通路状态+图标消息给显示任务，队列满时留待重试。
 * @note  图标值由工作模式派生; 充电图标充满后由显示端强制关闭。
 */
static void display_publish(void)
{
    if (!s_state.flags.ui_dirty)
    {
        return;
    }

    ucMESSAGE_t msg = {0};
    msg.MsgID = EVT_DISP_ICON;
    msg.Length = 5;

    /* 图标状态: 启动过程中即亮, 不等 RUNNING */
    bool charging = (s_state.charge_state == CHARGE_STATE_STARTING ||
                     s_state.charge_state == CHARGE_STATE_RUNNING);
    bool inverting = (s_state.invert_state == INVERT_STATE_PRECHARGE ||
                      s_state.invert_state == INVERT_STATE_DC_CONNECT ||
                      s_state.invert_state == INVERT_STATE_OUTPUT ||
                      s_state.invert_state == INVERT_STATE_RUNNING);
    bool bypassing = (s_state.bypass_state == BYPASS_STATE_STARTING ||
                      s_state.bypass_state == BYPASS_STATE_RUNNING);

    msg.Paras.Value[0] = (uint8_t)s_state.mode;
    msg.Paras.Value[1] = charging ? ICON_CHG_ON : ICON_OFF;
    msg.Paras.Value[2] = inverting ? ICON_OUT_ON : ICON_OFF;   /* VarIconOutput: 仅逆变输出 */
    msg.Paras.Value[3] = (inverting || bypassing) ? ICON_HZ_ON : ICON_OFF;  /* VarIconHZ: 逆变或旁路 */
    msg.Paras.Value[4] = bypassing ? ICON_SIDE_ON : ICON_OFF;   /* VarIconUPS: 仅旁路 */

    if (Send_Msg_AppDisplayTask(&msg))
    {
        s_state.flags.ui_dirty = false;
    }
}

/**
 * @brief 立即关闭全部通路并撤销请求。
 * @note  同时复位过载告警: 通路已断, 声光报警无意义 (由下一次监督重新判定)。
 */
static void power_abort(void)
{
    drv_relay_all_off();
    s_state.flags.ac_out_requested = false;
    s_state.flags.battery_trip_pending = false;
    s_state.mode = WORK_MODE_IDLE;
    s_state.charge_state = CHARGE_STATE_IDLE;
    s_state.bypass_state = BYPASS_STATE_IDLE;
    s_state.invert_state = INVERT_STATE_IDLE;
    s_state.flags.ui_dirty = true;
    s_overload_alarm = false;
}

/**
 * @brief 记录告警、立即全断并进入故障状态。
 * @param alarm 本次新增的告警位掩码。
 */
static void enter_fault(uint32_t alarm)
{
    app_alarm_merge(alarm);
    drv_relay_all_off();
    s_state.flags.ac_out_requested = false;
    s_state.flags.battery_trip_pending = false;
    s_state.mode = WORK_MODE_IDLE;
    s_state.charge_state = CHARGE_STATE_IDLE;
    s_state.bypass_state = BYPASS_STATE_IDLE;
    s_state.invert_state = INVERT_STATE_IDLE;
    s_state.flags.ui_dirty = true;
    s_state.flags.fault_active = true;
    s_overload_alarm = false;   /* 故障态优先, 不再播报过载 */
    drv_led_on(LED_ALM);
    drv_buzzer_short_sound(3);
    logger_out(LOG_ERROR, "FAULT: alarm=0x%lX", (unsigned long)app_alarm_get());
}

/**
 * @brief 继电器业务互锁判定 (纯逻辑, 不操作硬件)。
 * @param id 待吸合的继电器编号。
 * @return true：允许吸合；false：违反互锁/顺序规则。
 * @note 依据命令位 (drv_relay_is_on) 判定, 非硬件反馈; 规则:
 *   1) 市电侧 (AC_CTRL/CHG_IN/CHG_OUT) 与逆变侧 (INV_OUT/INV_PRO/INV_IN) 互斥;
 *   2) 启动顺序: PD7(INV_IN) 需 PD4(INV_PRO) 先合; PD0(INV_OUT) 需 PD7 保持
 *      (三段时序中 PD4 已在 PD0 前断开); PD3(CHG_OUT) 需 PD2(CHG_IN) 先合。
 */
static bool relay_interlock_ok(relay_id_enum id)
{
    /* 市电侧吸合: 逆变侧必须全断 */
    if ((id == RELAY_AC_CTRL || id == RELAY_CHG_IN || id == RELAY_CHG_OUT) &&
        (drv_relay_is_on(RELAY_INV_OUT) || drv_relay_is_on(RELAY_INV_PRO) ||
         drv_relay_is_on(RELAY_INV_IN)))
    {
        return false;
    }
    /* 逆变侧吸合: 市电侧必须全断 */
    if ((id == RELAY_INV_OUT || id == RELAY_INV_PRO || id == RELAY_INV_IN) &&
        (drv_relay_is_on(RELAY_AC_CTRL) || drv_relay_is_on(RELAY_CHG_IN) ||
         drv_relay_is_on(RELAY_CHG_OUT)))
    {
        return false;
    }
    /* 启动顺序依赖 */
    if (id == RELAY_INV_IN && !drv_relay_is_on(RELAY_INV_PRO))
    {
        return false;
    }
    if (id == RELAY_INV_OUT && !drv_relay_is_on(RELAY_INV_IN))
    {
        return false;
    }
    if (id == RELAY_CHG_OUT && !drv_relay_is_on(RELAY_CHG_IN))
    {
        return false;
    }
    return true;
}

/**
 * @brief 按业务互锁开启一路输出。
 * @param id 继电器输出编号。
 * @return true：命令已接受；false：互锁拒绝/ID 无效。
 * @note 互锁判定基于命令位, 非硬件反馈——软件判断不了继电器真实状态,
 *       故拒绝不视为故障, 仅记录日志, 由调用方安全收敛。
 */
static bool relay_on(relay_id_enum id)
{
    if (!relay_interlock_ok(id))
    {
        logger_out(LOG_WARN, "relay %d on rejected (interlock)", (int)id);
        return false;
    }
    if (drv_relay_on(id))
    {
        s_state.flags.ui_dirty = true;
        return true;
    }
    return false;
}

/*====================================================================
 * 新模式决策与各功能状态机
 *====================================================================*/

/**
 * @brief 模式决策: 根据 AC 输入状态切换工作模式 (CHARGE_BYPASS ↔ INVERT)。
 * @param ac 当前市电输入状态 (PRESENT/ABSENT/UNKNOWN)。
 * @note  模式切换逻辑:
 *          - AC_PRESENT        → WORK_MODE_CHARGE_BYPASS (充电+旁路输出)
 *          - AC_ABSENT/UNKNOWN → WORK_MODE_INVERT (逆变输出)
 *        UNKNOWN 表示输入计量不可信 (读失败/时效超期), 无法确认市电是否在位,
 *        按"无市电"处理: 市电侧继电器必须全断, 不允许把不可信的市电直通到输出端。
 *        切换时先停止当前模式的通路 (逆变/充电), 再设置新模式;
 *        具体通路启停由对应的状态机 (charge/bypass/invert_state_process) 执行。
 *        输出请求 ac_out_requested 的置位/清除不在此处, 由
 *        ac_output_session_update() 按 220V 插拔边沿统一处理。
 */
static void mode_decide(ac_input_state_t ac)
{
    if (s_state.flags.fault_active)
        return;

    if (ac == AC_INPUT_PRESENT)
    {
        /* 有220V → CHARGE_BYPASS 模式 */
        if (s_state.mode != WORK_MODE_CHARGE_BYPASS)
        {
            /* 停止逆变（如果正在运行） */
            if (s_state.mode == WORK_MODE_INVERT && s_state.invert_state != INVERT_STATE_IDLE)
            {
                drv_relay_off(RELAY_INV_OUT);
                s_state.invert_state = INVERT_STATE_STOPPING;
                s_state.invert_phase_since = HAL_GetTick();
            }
            s_state.mode = WORK_MODE_CHARGE_BYPASS;
            s_state.flags.ui_dirty = true;
        }
    }
    else
    {
        /* 无220V (ABSENT) 或输入不可信 (UNKNOWN) → INVERT 模式 */
        if (s_state.mode != WORK_MODE_INVERT)
        {
            /* 停止充电和旁路（如果正在运行） */
            if (s_state.mode == WORK_MODE_CHARGE_BYPASS)
            {
                if (s_state.charge_state != CHARGE_STATE_IDLE)
                {
                    drv_relay_off(RELAY_CHG_OUT);
                    s_state.charge_state = CHARGE_STATE_STOPPING;
                    s_state.charge_phase_since = HAL_GetTick();
                }
                if (s_state.bypass_state != BYPASS_STATE_IDLE)
                {
                    drv_relay_off(RELAY_AC_CTRL);
                    s_state.bypass_state = BYPASS_STATE_IDLE;
                }
            }
            s_state.mode = WORK_MODE_INVERT;
            s_state.flags.ui_dirty = true;
        }
    }
}

/*====================================================================
 * 220V 会话管理: 插入自动开旁路输出, 拔出失效
 *====================================================================*/

/**
 * @brief 按 220V 插拔边沿管理旁路输出的自动请求。
 * @param ac 当前市电输入状态 (PRESENT/ABSENT/UNKNOWN)。
 * @note  产品定义 (2026-11-18 修订): 旁路/逆变输出随 220V 插拔自动跟随 ——
 *          AC OUT 开  → 拔掉 220V 自动转逆变输出; 重插 220V 自动旁路直通 (UPS 式);
 *          AC OUT 关  → 拔插 220V 都只给电池充电, 输出必须按键开启;
 *          冷启动 s_out_intent 初值 true → 插着 220V 开机会自动旁路输出。
 *        充电始终由 charge_strategy_update 独立触发, 与输出状态无关。
 *        规则:
 *          1) 220V 撤除边沿把 ac_out_requested 记入 s_out_intent, 且"不清请求"
 *             (请求保持 → 逆变状态机自动接管, 见 invert_state_process);
 *          2) 220V 接入上升沿只判定一次: s_out_intent 为真才自动旁路;
 *          3) 用户按 AC OUT 关闭后, 本段会话内不再自动重开 (直到拔出重插);
 *          4) 保护动作清请求 (过温降载/过载停机/空载自停) 同样不自动重开;
 *          5) AC_OUT 开启但电量 <1% 时, 逆变启动被拒绝并清请求 (3 声提示)。
 *        必须边沿触发: 若每拍按"市电在位"无条件置位, 会把用户关断与保护关断
 *        立刻覆盖, 造成继电器反复吸合、空载自停失效。
 */
static void ac_output_session_update(ac_input_state_t ac)
{
    static ac_input_state_t s_prev_ac = AC_INPUT_UNKNOWN; /* 上一拍市电状态 (边沿检测) */
    static bool s_auto_done;                              /* 本段 220V 会话已自动置位 */
    static bool s_out_intent = true;                      /* 初值 true: 冷启动插着 220V 自动旁路 */

    /* ---- 220V 撤除边沿: 结束会话, 记录输出意图 ---- */
    if (s_prev_ac == AC_INPUT_PRESENT && ac != AC_INPUT_PRESENT)
    {
        s_auto_done = false;
        /* 记住掉电前 AC OUT 的状态: 决定重插 220V 时是否自动旁路输出 */
        s_out_intent = s_state.flags.ac_out_requested;

        /* AC OUT 开着 → 保持请求不变, 由 mode_decide() 切 INVERT 模式、
         * invert_state_process() 自动走预充→合闸, 无缝转逆变输出;
         * AC OUT 关着 → 请求本来就为假, 无需处理。 */
        if (s_state.flags.ac_out_requested)
        {
            logger_out(LOG_INFO, "ACOUT: 220V lost, auto switch to inverter");
        }
    }

    /* ---- 220V 接入边沿: 仅当掉电前 AC OUT 为"开"时自动旁路; 否则只充电 ----
     *   AC OUT 开  → 重插 220V 自动旁路输出 (UPS 式直通)
     *   AC OUT 关  → 重插 220V 只给电池充电, 输出必须按键开启
     * 充电始终由 charge_strategy_update 独立触发, 与输出状态无关。 */
    if (ac == AC_INPUT_PRESENT && !s_auto_done)
    {
        s_auto_done = true; /* 本段会话只判定一次 */
        if (!s_state.flags.fault_active && !s_state.flags.bat_otp && s_out_intent)
        {
            s_state.flags.ac_out_requested = true;
            s_state.flags.ui_dirty = true;
            logger_out(LOG_INFO, "ACOUT: 220V present, bypass auto on (AC OUT was ON)");
        }
        else
        {
            logger_out(LOG_INFO, "ACOUT: 220V present, charging only (AC OUT off)");
        }
    }

    s_prev_ac = ac;
}

/**
 * @brief 充电状态机: 管理充电通路继电器的时序启停。
 * @param ac 当前市电输入状态 (保留供未来扩展)。
 * @note  状态流转:
 *          IDLE ──(need_charge & CHG_IN 吸合)──► STARTING ──(1.5s)──► RUNNING
 *            ▲                                                           │
 *            └──(1.5s 延时后断 CHG_IN)── STOPPING ◄──(停止条件)─────────┘
 *        - IDLE→STARTING: 先吸合 CHG_IN, 等充电器稳定 1.5s
 *        - STARTING→RUNNING: 再吸合 CHG_OUT, 开始充电
 *        - RUNNING→STOPPING: 外部触发停止 (如充满/故障)
 *        - STOPPING→IDLE: 先断 CHG_OUT, 等 1.5s 后再断 CHG_IN (顺序断电)
 */
static void charge_state_process(ac_input_state_t ac)
{
    uint32_t elapsed = HAL_GetTick() - s_state.charge_phase_since;

    switch (s_state.charge_state)
    {
        case CHARGE_STATE_IDLE:
            /* 充电启动入口: 市电模式 + SOC<100% (且不在停充冷却期) 才启动;
             * 先吸合 CHG_IN 给充电器上电 */
            if (s_state.mode == WORK_MODE_CHARGE_BYPASS && s_state.flags.need_charge)
            {
                if (relay_on(RELAY_CHG_IN))
                {
                    /* 新一轮充电开始: 清掉上一次的判满锁存 (显示随之退出"满") */
                    if (s_state.flags.charge_full)
                    {
                        s_state.flags.charge_full = false;
                        logger_out(LOG_INFO, "BAT full latch cleared at charge start");
                    }
                    s_state.charge_state = CHARGE_STATE_STARTING;
                    s_state.charge_phase_since = HAL_GetTick();
                    s_state.flags.ui_dirty = true;
                    logger_out(LOG_INFO, "CHG: CHG_IN ON, start 1500ms");
                }
            }
            break;

        case CHARGE_STATE_STARTING:
            /* CHG_IN 吸合后等 1.5s 让充电器上电稳定, 再闭合 CHG_OUT 接通电池,
             * 避免充电器输出未稳时接入电池 */
            if (elapsed >= PATH_CHARGER_START_MS)
            {
                if (relay_on(RELAY_CHG_OUT))
                {
                    s_state.charge_state = CHARGE_STATE_RUNNING;
                    s_state.flags.ui_dirty = true;
                    mod_metering_reset_out_no_data();
                    logger_out(LOG_INFO, "CHG: CHG_OUT ON, running");
                }
            }
            break;

        case CHARGE_STATE_RUNNING:
            /* 稳定充电: 充满检测由 full_charge_auto_stop (FStat.FQ) 触发停止,
             * 本状态机不重复判断 */
            break;

        case CHARGE_STATE_STOPPING:
            /* 顺序断电: CHG_OUT 已先断(脱离电池), 等 1.5s 让充电器输出卸载,
             * 最后才断 CHG_IN —— 避免充电器带载时直接断市电输入侧 */
            if (elapsed >= PATH_STOP_CHARGE_TAIL_MS)
            {
                drv_relay_off(RELAY_CHG_IN);
                s_state.charge_state = CHARGE_STATE_IDLE;
                s_state.flags.ui_dirty = true;
                logger_out(LOG_INFO, "CHG: stopped");
            }
            break;
    }
    (void)ac;
}

/**
 * @brief 旁路状态机: 管理旁路输出继电器 (AC_CTRL) 的启停。
 * @param ac 当前市电输入状态 (保留供未来扩展)。
 * @note  状态流转:
 *          IDLE ──(ac_out_requested & AC_CTRL 吸合)──► RUNNING
 *            ▲                                           │
 *            └──(ac_out_requested=false)────────────────┘
 *        - 旁路启动无延时, 吸合 AC_CTRL 即进入 RUNNING
 *        - RUNNING 态检测 ac_out_requested, 为 false 时立即断开并返回 IDLE
 *        - STOPPING 为过渡态, 强制断开 AC_CTRL 并返回 IDLE
 */
static void bypass_state_process(ac_input_state_t ac)
{
    switch (s_state.bypass_state)
    {
        case BYPASS_STATE_IDLE:
            /* 旁路启动入口: 市电模式下有输出请求即吸合 AC_CTRL, 市电直接输出;
             * 无预充时序 (旁路仅闭合一路继电器, 无浪涌问题) */
            if (s_state.mode == WORK_MODE_CHARGE_BYPASS && s_state.flags.ac_out_requested)
            {
                if (relay_on(RELAY_AC_CTRL))
                {
                    s_state.bypass_state = BYPASS_STATE_RUNNING;
                    s_state.flags.ui_dirty = true;
                    mod_metering_reset_out_no_data();
                    logger_out(LOG_INFO, "BYPASS: AC_CTRL ON, running");
                }
            }
            break;

        case BYPASS_STATE_STARTING:
            /* 旁路启动很快，直接进入 RUNNING */
            s_state.bypass_state = BYPASS_STATE_RUNNING;
            s_state.flags.ui_dirty = true;
            break;

        case BYPASS_STATE_RUNNING:
            /* 稳定旁路: 用户取消输出请求时立即断开 (市电掉电由 mode_decide 兜底) */
            if (!s_state.flags.ac_out_requested)
            {
                drv_relay_off(RELAY_AC_CTRL);
                s_state.bypass_state = BYPASS_STATE_IDLE;
                s_state.flags.ui_dirty = true;
                logger_out(LOG_INFO, "BYPASS: AC_CTRL OFF, idle");
            }
            break;

        case BYPASS_STATE_STOPPING:
            /* 兜底: 强制断开 AC_CTRL 回 IDLE (通常由模式切换直接置入) */
            drv_relay_off(RELAY_AC_CTRL);
            s_state.bypass_state = BYPASS_STATE_IDLE;
            s_state.flags.ui_dirty = true;
            break;
    }
    (void)ac;
}

/**
 * @brief 逆变状态机: 管理逆变输出通路的三段时序启停。
 * @param ac 当前市电输入状态 (保留供未来扩展)。
 * @note  状态流转 (启动三段时序):
 *          IDLE ──(ac_out_requested & PD4 吸合)──► PRECHARGE (1.5s 预充)
 *            │
 *            ├──(1.5s 到 & PD7 吸合)──► DC_CONNECT (0.5s 直连)
 *            │
 *            ├──(0.5s 到 & PD4 断开)──► OUTPUT (1.5s 等待)
 *            │
 *            ├──(1.5s 到 & PD0 吸合)──► RUNNING (稳定输出)
 *            │                              │
 *            │                              ▼
 *            ◄──(1.5s 顺序断电)── STOPPING ◄──(ac_out_requested=false)
 *
 *        启动时序: PD4(预充) → 1.5s → PD7(吸合) → 0.5s → PD4(断开, 电池直连) → 1.5s → PD0(输出)
 *        停止时序: PD0(断开) → 1.5s → PD7+PD4(断开)
 *        关键: 市电侧与逆变侧继电器互斥, 由 relay_interlock_ok() 保证。
 */
/**
 * @brief 取电池等效单体电压 (mV), 0 表示数据无效。
 * @param bat 电池快照 (可为 NULL)
 * @note  优先 cell_voltage_mv (等效平均单体), 回退到 AvgVCell。
 */
static uint32_t inv_cell_mv(const BATT_INFO_T *bat)
{
    if (bat == NULL)
    {
        return 0u;
    }
    return bat->cell_voltage_mv ? bat->cell_voltage_mv : bat->avg_cell_voltage_mv;
}

/**
 * @brief 逆变输出门限检查: SOC >= 1% 且 单体未处于欠压软停区 才允许开启/维持逆变。
 * @param bat 输出: 本次读到的电池快照 (可为 NULL)
 * @return true: 允许逆变; false: 电量不足 / 单体欠压未恢复 / 数据无效。
 * @note  傍路输出不经此判定 (接 220V 时不受电量限制)。
 */
static bool invert_soc_allow(BATT_INFO_T *bat)
{
    if (!bat || !battery_get_snapshot(bat))
    {
        return false; /* 数据无效: 不允许启动逆变, 由保护逻辑处理 */
    }
    if ((unsigned)bat->soc < INV_MIN_SOC_PCT)
    {
        return false;
    }
    uint32_t cell = inv_cell_mv(bat);
    if ((cell != 0u) && (cell < INV_CELL_RESUME_MV))
    {
        return false; /* 单体欠压: 未回升到解除门限前不允许启动 */
    }
    return true;
}

/**
 * @brief 欠压停输出后的重启冷却剩余秒数。
 * @return 0: 无冷却; >0: 剩余秒数 (向上取整)。
 */
static uint32_t inv_uv_cooldown_left_s(void)
{
    if (s_uv_stop_ms == 0u)
    {
        return 0u;
    }
    uint32_t el = (uint32_t)(HAL_GetTick() - s_uv_stop_ms);
    if (el >= INV_UV_RESTART_HOLD_MS)
    {
        return 0u;
    }
    return (INV_UV_RESTART_HOLD_MS - el) / 1000u + 1u;
}

static void invert_state_process(ac_input_state_t ac)
{
    uint32_t elapsed = HAL_GetTick() - s_state.invert_phase_since;

    /* 冷却期结束或已开始充电: 清除冷却与低电量告警 */
    if (s_uv_stop_ms != 0u)
    {
        if ((uint32_t)(HAL_GetTick() - s_uv_stop_ms) >= INV_UV_RESTART_HOLD_MS)
        {
            s_uv_stop_ms = 0u;
        }
    }
    if (s_state.flags.need_charge)
    {
        s_uv_stop_ms = 0u;      /* 开始充电: 允许充电完成后立即使用 */
        s_ui_lowbat  = false;
    }
    else if (s_state.mode == WORK_MODE_INVERT && s_state.invert_state == INVERT_STATE_IDLE)
    {
        /* 无市电且空闲: 电池本来就低(上电/软停后)也要在屏幕上提示 -> LOWBAT 闪烁 */
        BATT_INFO_T lb = {0};
        if (battery_get_snapshot(&lb))
        {
            uint32_t lc = inv_cell_mv(&lb);
            if (((lc != 0u) && (lc < INV_CELL_SOFT_MV)) || ((unsigned)lb.soc < 3u))
            {
                s_ui_lowbat = true;
            }
        }
    }

    /* 硬停锁存解除: 单体回升到解除门限以上 */
    if (s_uv_hard)
    {
        BATT_INFO_T r = {0};
        uint32_t cell = battery_get_snapshot(&r) ? inv_cell_mv(&r) : 0u;
        if ((cell != 0u) && (cell >= INV_CELL_RESUME_MV))
        {
            s_uv_hard = false;
            logger_out(LOG_INFO, "INV: UV hard latch cleared (cell=%lumV >= %lumV)",
                       (unsigned long)cell, (unsigned long)INV_CELL_RESUME_MV);
        }
    }

    switch (s_state.invert_state)
    {
        case INVERT_STATE_IDLE:
            /* 逆变启动入口: 仅在逆变模式(无市电)且用户按下输出键时开始三段时序;
             * 第一步吸合 PD4, 电池经预充电阻给逆变器母线电容充电。
             * 电量必须 >= INV_MIN_SOC_PCT(1%) 才允许启动 (220V 掉电自动转逆变时同样受此限制)。 */
            if (s_state.mode == WORK_MODE_INVERT && s_state.flags.ac_out_requested)
            {
                /* 欠压/低电量停输出后的冷却: 期间拒绝重启, 避免"刚回弹就开、一开就跌穿" */
                uint32_t cd_s = inv_uv_cooldown_left_s();
                if (cd_s != 0u)
                {
                    s_state.flags.ac_out_requested = false;
                    s_ui_lowbat = true;
                    logger_out(LOG_WARN, "INV start rejected: UV cooldown %lus left, beep 3",
                               (unsigned long)cd_s);
                    drv_buzzer_short_sound(3);
                    display_publish();
                    break;
                }
                BATT_INFO_T bat = {0};
                if (!invert_soc_allow(&bat))
                {
                    uint32_t cell = inv_cell_mv(&bat);
                    s_state.flags.ac_out_requested = false;
                    logger_out(LOG_WARN, "INV start rejected: soc=%u%% (<%u%%) cell=%lumV (<%lumV), beep 3",
                               (unsigned)bat.soc, (unsigned)INV_MIN_SOC_PCT,
                               (unsigned long)cell, (unsigned long)INV_CELL_RESUME_MV);
                    /* 低电量/欠压不刷 ERR 码 (按需求), 只用 3 声蜂鸣 + 屏 LOWBAT */
                    s_ui_lowbat = true;
                    drv_buzzer_short_sound(3);
                    display_publish();
                    break;
                }
                if (relay_on(RELAY_INV_PRO))
                {
                    s_state.invert_state = INVERT_STATE_PRECHARGE;
                    s_state.invert_phase_since = HAL_GetTick();
                    s_state.flags.ui_dirty = true;
                    logger_out(LOG_INFO, "INV: PD4 ON, precharge 1500ms");
                }
            }
            break;

        case INVERT_STATE_PRECHARGE:
            /* 预充 1.5s: 经电阻限流给母线电容预充电, 避免直接吸合主继电器产生浪涌电流
             * (浪涌过大既会烧蚀触点, 也可能触发电池过流保护); 到时后吸合 PD7 主继电器 */
            if (elapsed >= PATH_PRECHARGE_MS)
            {
                if (relay_on(RELAY_INV_IN))
                {
                    s_state.invert_state = INVERT_STATE_DC_CONNECT;
                    s_state.invert_phase_since = HAL_GetTick();
                    s_state.flags.ui_dirty = true;
                    logger_out(LOG_INFO, "INV: PD7 ON, wait 500ms");
                }
            }
            break;

        case INVERT_STATE_DC_CONNECT:
            /* PD7 吸合稳定 0.5s 后断开 PD4, 摘除预充电阻, 电池经 PD7 直连逆变器
             * (预充电阻持续通流会发热, 直连降低回路损耗); 之后等母线电压稳定 */
            if (elapsed >= PATH_DC_FAST_MS)
            {
                drv_relay_off(RELAY_INV_PRO);
                s_state.invert_state = INVERT_STATE_OUTPUT;
                s_state.invert_phase_since = HAL_GetTick();
                s_state.flags.ui_dirty = true;
                logger_out(LOG_INFO, "INV: PD4 OFF (direct), wait 1500ms");
            }
            break;

        case INVERT_STATE_OUTPUT:
            /* 直连后再等 1.5s 让逆变器母线电压/控制电路上电稳定, 才吸合 PD0 接通输出端,
             * 防止逆变器未就绪时对外输出异常电压 */
            if (elapsed >= PATH_INVERTER_START_MS)
            {
                if (relay_on(RELAY_INV_OUT))
                {
                    s_state.invert_state = INVERT_STATE_RUNNING;
                    s_state.flags.ui_dirty = true;
                    s_ui_lowbat = false;   /* 逆变已正常输出: 清除低电量告警 */
                    mod_metering_reset_out_no_data();
                    logger_out(LOG_INFO, "INV: PD0 ON, running");
                }
            }
            break;

        case INVERT_STATE_RUNNING:
        {
            /* 稳定输出: 用户取消 / 空载60s / 过载5s / 通道失效 都会撤销 ac_out_requested。
             * 保护 (按优先级):
             *   单体 <= 2.70V → 硬停 (立即关断 + 锁存, 回升到 2.85V 才解除)
             *   单体 <= 2.80V → 软停 (关输出, 可恢复; 重新开启需 >= 2.85V)
             *   SOC  < 1%    → 关输出 (原低电量逻辑)
             * 傍路输出不受此限制。 */
            BATT_INFO_T bat = {0};
            uint32_t cell = 0u;
            bool snap_ok = battery_get_snapshot(&bat);
            if (snap_ok)
            {
                cell = inv_cell_mv(&bat);
            }

            if (snap_ok && (cell != 0u) && (cell <= INV_CELL_HARD_MV))
            {
                s_uv_hard = true;
                s_uv_stop_ms = HAL_GetTick();
                s_ui_lowbat = true;
                logger_out(LOG_ERROR, "INV: cell=%lumV <= %lumV (hard UV), output off & latched, beep 3",
                           (unsigned long)cell, (unsigned long)INV_CELL_HARD_MV);
                s_state.flags.ac_out_requested = false;
                drv_buzzer_short_sound(3);
                display_publish();
            }
            else if (snap_ok && (cell != 0u) && (cell <= INV_CELL_SOFT_MV))
            {
                s_uv_stop_ms = HAL_GetTick();
                s_ui_lowbat = true;
                logger_out(LOG_WARN, "INV: cell=%lumV <= %lumV (soft UV), output off, beep 3",
                           (unsigned long)cell, (unsigned long)INV_CELL_SOFT_MV);
                s_state.flags.ac_out_requested = false;
                drv_buzzer_short_sound(3);
                display_publish();
            }
            else if (snap_ok && (unsigned)bat.soc < INV_MIN_SOC_PCT)
            {
                s_uv_stop_ms = HAL_GetTick();
                s_ui_lowbat = true;
                logger_out(LOG_WARN, "INV: soc=%u%% < %u%%, auto output off (low battery), beep 3",
                           (unsigned)bat.soc, (unsigned)INV_MIN_SOC_PCT);
                s_state.flags.ac_out_requested = false;
                drv_buzzer_short_sound(3); /* 低电量自动关闭逆变: 3 声提示 */
                display_publish();
            }
            if (!s_state.flags.ac_out_requested)
            {
                drv_relay_off(RELAY_INV_OUT);
                s_state.invert_state = INVERT_STATE_STOPPING;
                s_state.invert_phase_since = HAL_GetTick();
                s_state.flags.ui_dirty = true;
                logger_out(LOG_INFO, "INV: PD0 OFF, stopping");
            }
            break;
        }

        case INVERT_STATE_STOPPING:
            /* 顺序断电: PD0 已先断, 再等 1.5s 让逆变器卸载/母线放电,
             * 最后才断 PD7+PD4 主回路 —— 避免逆变器带载时硬断直流主回路产生拉弧 */
            if (elapsed >= PATH_STOP_INV_TAIL_MS)
            {
                drv_relay_off(RELAY_INV_IN);
                drv_relay_off(RELAY_INV_PRO);
                s_state.invert_state = INVERT_STATE_IDLE;
                s_state.flags.ui_dirty = true;
                logger_out(LOG_INFO, "INV: stopped");
            }
            break;
    }
    (void)ac;
}

/*====================================================================
 * 按键处理
 *====================================================================*/

/**
 * @brief 关闭整机: 断全部通路 → 熄灭指示 → 保存数据 → 切断主电源。
 * @param reason 关机原因 (日志用)
 * @note  唯一的断电出口, 两条触发路径共用: ONOFF 长按、1h 低功耗自动关机。
 *        本函数含阻塞式 checkpoint (≤2s), 只能在非中断上下文调用。
 */
static void system_shutdown(const char *reason)
{
    if (s_state.mode == WORK_MODE_SHUTDOWN)
    {
        return;
    }

    power_abort();  /* 断全部继电器 + 清输出请求 + 各子状态机复位 */
    s_state.mode = WORK_MODE_SHUTDOWN;
    s_state.flags.ui_dirty = true;
    display_publish();          /* 立即更新界面 */
    mod_battery_led_mask(0);    /* 熄灭 MAX17263 电量 LED */
    drv_buzzer_short_sound(1);
    logger_out(LOG_INFO, "shutdown: %s", reason);

    if (!mod_battery_checkpoint(2000))
    {
        logger_out(LOG_WARN, "BAT checkpoint failed");
    }
    DEV_POWER_OFF();
}

/**
 * @brief 处理 ONOFF 长按：关机。
 */
static void handle_onoff_key(void)
{
    system_shutdown("ONOFF long press");
}

/**
 * @brief 处理 AC OUT 键: 切换输出请求。
 */
static void handle_path_key(void)
{
    /* 故障/关机/电池过温降载: 3 声拒绝 */
    if (s_state.flags.fault_active || s_state.flags.bat_otp ||
        s_state.mode == WORK_MODE_SHUTDOWN)
    {
        if (s_state.flags.fault_active || s_state.flags.bat_otp)
        {
            logger_out(LOG_WARN, "KEY rejected: %s",
                       s_state.flags.fault_active ? "fault" : "battery over-temp");
            drv_buzzer_short_sound(3);
            display_publish();
        }
        return;
    }

    ac_input_state_t ac = mod_metering_get_ac_input().state;
    if (ac == AC_INPUT_UNKNOWN)
    {
        logger_out(LOG_WARN, "KEY rejected: AC input unavailable");
        drv_buzzer_short_sound(2);
        return;
    }

    /* 开启输出前: 若将走逆变通路(无 220V) 且 电量<=INV_MIN_SOC_PCT, 拒绝开启;
     * 傍路输出(有 220V) 不受电量限制 */
    if (!s_state.flags.ac_out_requested && ac == AC_INPUT_ABSENT)
    {
        BATT_INFO_T bat = {0};
        if (!invert_soc_allow(&bat))
        {
            uint32_t cell = inv_cell_mv(&bat);
            uint32_t cd_s = inv_uv_cooldown_left_s();
            if (cd_s != 0u)
            {
                logger_out(LOG_WARN, "ACOUT rejected: UV cooldown %lus left (cell=%lumV), beep 3",
                           (unsigned long)cd_s, (unsigned long)cell);
            }
            else
            {
                logger_out(LOG_WARN, "ACOUT rejected: soc=%u%% (<%u%%) cell=%lumV (<%lumV), beep 3",
                           (unsigned)bat.soc, (unsigned)INV_MIN_SOC_PCT,
                           (unsigned long)cell, (unsigned long)INV_CELL_RESUME_MV);
            }
            /* 低电量不刷 ERR 码 (按需求), 只用 3 声蜂鸣 + 屏 LOWBAT 提示原因 */
            s_ui_lowbat = true;
            drv_buzzer_short_sound(3);
            display_publish();
            return;
        }
    }

    /* 切换输出请求 */
    s_state.flags.ac_out_requested = !s_state.flags.ac_out_requested;
    app_alarm_unmerge(SYS_ALARM_AC_INPUT);
    drv_buzzer_short_sound(1);
    s_state.flags.ui_dirty = true;
    display_publish();  /* 立即更新界面 */
    logger_out(LOG_INFO, "ACOUT: output %s", s_state.flags.ac_out_requested ? "ON" : "OFF");
}

/* 充满停充后的重启封锁状态定义见文件上方 (s_state 之后)。 */

/**
 * @brief 充满停充后的重启封锁检查与维护。
 * @param ac          当前市电状态。
 * @param ac_recycle  本拍检测到 220V 拔出(ABSENT)后重新插入(PRESENT)的边沿。
 * @return true: 当前应封锁充电启动。
 * @note 判满停充后 10min 内不重启充电 (避免插着 220V 反复充停;
 *       反复充停); 用户拔插 220V 视为主动要求重新充电, 立即解除封锁。
 */
static bool charge_restart_blocked(ac_input_state_t ac, bool ac_recycle)
{
    (void)ac;
    if (!s_chg_block)
    {
        return false;
    }
    if (ac_recycle ||
        (uint32_t)(HAL_GetTick() - s_chg_block_since) >= CHARGE_RESTART_BLOCK_MS)
    {
        s_chg_block = false;
        if (ac_recycle)
        {
            s_phys_full = false; /* 拔插 220V 视为用户要求重新充电 */
        }
        logger_out(LOG_INFO, "auto chg: restart block %s",
                   ac_recycle ? "cleared (AC re-inserted)" : "expired (10min)");
        return false;
    }
    return true;
}

/**
 * @brief 产品级"满"判定: 报告 SOC 是否已达 100%。
 * @param bat 电池快照 (可 NULL)。
 * @return true: SOC 已到 CHARGE_FULL_SOC (100%)。
 * @note  判满以 SOC=100% 为准; FQ/TTF 只用于"是否立即停"的确认, 见 full_charge_auto_stop()。
 */
static bool battery_soc_full(const BATT_INFO_T *bat)
{
    return bat && (unsigned)bat->soc >= CHARGE_FULL_SOC;
}

/**
 * @brief 充电策略: 220V 稳定 2s 且未充满时置 need_charge (由 work_status_process 调用)。
 * @param ac 输入计量模块提供的市电状态。
 * @note 防抖计时在 AC 波动(掉电再接入)时清零; 充满/无AC 自动收敛;
 *       判满停充后 10min 封锁重启 (拔插 220V 解除);
 *       故障/关机门控由 work_status_process 外部保证。
 */
static void charge_strategy_update(ac_input_state_t ac)
{
    static bool s_ac_stable;
    static uint32_t s_ac_stable_since;
    static ac_input_state_t s_ac_prev = AC_INPUT_UNKNOWN; /* 上一拍 AC 状态 (拔插边沿检测) */
    static ac_input_state_t s_ac_log = AC_INPUT_UNKNOWN; /* 诊断: AC 状态变化日志 */
    static bool s_chg_log = false;                       /* 诊断: need_charge 变化日志 */
    static uint32_t s_ac_back_ms;                        /* AC 掉电恢复时刻 (0=无), 用于补满豁免 */
    static bool s_v_low;                                 /* 电压迟滞状态: 已进入低位区 */

    static bool s_ac_had_present;                         /* 曾经检测到 PRESENT (区分"回电"与冷启动) */

    /* 来电判定: 只要上一拍不是 PRESENT、且曾经见过 PRESENT, 本次回到 PRESENT 就算"回电"。
     * 不能写成 (s_ac_prev == ABSENT && ac == PRESENT): HLW8110 在 VIN=0 时会先报
     * UNKNOWN(0) 再报 ABSENT(1), 实测两次插电都是 UNKNOWN -> PRESENT, 导致
     * "来电补满"窗口从来不打开 (日志 acback 恒为 0)。
     * s_ac_had_present 保证冷启动首次 PRESENT 不算回电。 */
    bool ac_recycle = (ac == AC_INPUT_PRESENT) &&
                      (s_ac_prev != AC_INPUT_PRESENT) &&
                      s_ac_had_present;
    s_ac_prev = ac;
    if (ac == AC_INPUT_PRESENT)
    {
        s_ac_had_present = true;
    }
    if (ac_recycle)
    {
        s_ac_back_ms = HAL_GetTick();   /* 记录来电时刻, 开启补满窗口 */
        s_phys_full = false;            /* 来电视为用户要求补满: 清物理满锁存 */
    }

    if (ac != s_ac_log)
    {
        s_ac_log = ac;
        logger_out(LOG_INFO, "auto chg: AC state=%d", (int)ac);
    }

    if (ac == AC_INPUT_PRESENT)
    {
        bool stable;
        bool blocked;

        if (!s_ac_stable)
        {
            s_ac_stable = true;
            s_ac_stable_since = HAL_GetTick();
        }
        stable = (uint32_t)(HAL_GetTick() - s_ac_stable_since) >= AC_STABLE_MS;
        blocked = charge_restart_blocked(ac, ac_recycle); /* 每拍调用以处理到期解除 */

        if (stable && !blocked)
        {
            BATT_INFO_T bat = {0};
            bool snap_ok = battery_get_snapshot(&bat);
            /* 物理兜底判满后, 只有 SOC 明显回落 (或 220V 拔插) 才允许再充,
             * 否则 SOC 卡在 99% 时会每 10min 反复补电 */
            if (s_phys_full && (!snap_ok || (unsigned)bat.soc < CHARGE_FULL_PHYS_REARM_SOC))
            {
                s_phys_full = false;
                logger_out(LOG_INFO, "BAT phys-full rearmed: soc=%u%% < %u%%", (unsigned)bat.soc,
                           (unsigned)CHARGE_FULL_PHYS_REARM_SOC);
            }
            /* 电压门限迟滞: 低于 27.16V 置位, 高于 27.24V 清零。
             * 没有迟滞时, 采样噪声 (实测 18mV) 会让 need_charge 在门限附近反复翻转。 */
            if (snap_ok)
            {
                if (bat.voltage_mv < CHARGE_RESTART_V_ENTER_MV)
                {
                    s_v_low = true;
                }
                else if (bat.voltage_mv > CHARGE_RESTART_V_EXIT_MV)
                {
                    s_v_low = false;
                }
            }
            /* 接入 220V 后:
             *   来电补满窗口内 → 不管 SOC / 电压, 直接充 (AC 掉电恢复必须补满)
             *   窗口外         → SOC<=97% 且 电压已进入低位 (迟滞, 见上)
             * 另需无物理满锁存且电池未过温。 */
            bool ac_came_back = (s_ac_back_ms != 0u) &&
                                ((uint32_t)(HAL_GetTick() - s_ac_back_ms) < AC_BACK_TOPUP_MS);
            s_state.flags.need_charge = snap_ok &&
                                        (ac_came_back ||
                                         ((unsigned)bat.soc <= CHARGE_RESTART_SOC && s_v_low)) &&
                                        !s_phys_full && !s_state.flags.bat_otp;
            if (s_state.flags.need_charge != s_chg_log)
            {
                s_chg_log = s_state.flags.need_charge;
                logger_out(LOG_INFO, "auto chg: need=%d ok=%d soc=%u vol=%umV vlow=%d acback=%d fq=%d fb=%d",
                           (int)s_state.flags.need_charge, (int)snap_ok, bat.soc,
                           (unsigned)bat.voltage_mv, (int)s_v_low, (int)ac_came_back,
                           (int)bat.full_qualified, (int)s_state.flags.charge_full);
            }
            if (s_state.flags.need_charge)
            {
                s_ac_back_ms = 0u;   /* 补满窗口只对来电后第一次判定生效 */
            }
        }
        else
        {
            /* 未稳定或冷却期内: 必须显式撤销请求。若沿用上一拍的 true, STOPPING→IDLE
             * 后会立刻重新吸合继电器, 10min 冷却形同虚设 (B 路径停充时就是这种情形)。 */
            s_state.flags.need_charge = false;
            s_chg_log = false;
        }
    }
    else
    {
        s_ac_stable = false;
        s_state.flags.need_charge = false;
        s_chg_log = false;
    }
    /* 本函数只更新 s_state.flags.need_charge; 通路合成(resolve)与请求(path_request)
     * 统一由 work_status_process() 单点执行, 避免同 tick 重复切换。 */
}

/**
 * @brief 输出通道失效检查: 通道通讯失败 (out_ok == false) → 立即停止输出
 * @param m 输出计量快照
 * @note 适用范围: 有输出请求时 (bypass 或 inverter 子状态机非 IDLE)
 */
static void output_channel_check(const METER_INFO_T *m)
{
    if (m == NULL || !m->out_ok)
    {
        /* 检查是否有输出通路激活 (bypass 或 inverter 子状态机非 IDLE) */
        bool output_active = (s_state.bypass_state != BYPASS_STATE_IDLE ||
                              s_state.invert_state != INVERT_STATE_IDLE);
        if (output_active)
        {
            s_state.out_idle_since = 0;
            logger_out(LOG_ERROR, "OUT meter no data: stop output (bypass=%d invert=%d)",
                       s_state.bypass_state, s_state.invert_state);
            s_state.flags.ac_out_requested = false;
            drv_buzzer_short_sound(2);
            display_publish();
        }
        return;
    }
}

/**
 * @brief 交流输出通路是否处于稳定运行 (旁路或逆变)。
 * @return true: 正在对外供电。
 */
static bool output_running(void)
{
    return (s_state.bypass_state == BYPASS_STATE_RUNNING ||
            s_state.invert_state == INVERT_STATE_RUNNING);
}

/**
 * @brief 输出空载检查: 输出功率 <= 1W 持续 50min → 自动关闭交流输出。
 * @param m 输出计量快照 (可 NULL)
 * @note  旁路与逆变共用同一门限 (二者互斥, 不会同时运行): 空载满 50min 关输出。
 *        本函数只做"输出管理", 不触发关机; 关输出后清请求, 再开必须按 AC OUT
 *        (220V 会话里也不会被自动重开)。
 *        接上负载 (>1W) 即复位计时; 只有连续 50min 无载才动作。
 */
static void output_idle_check(const METER_INFO_T *m)
{
    if (!output_running() || m == NULL || !m->out_ok || m->vout_p_w > AC_IDLE_PWR_W)
    {
        s_state.out_idle_since = 0;
        return;
    }

    uint32_t now = HAL_GetTick();
    if (s_state.out_idle_since == 0)
    {
        s_state.out_idle_since = now;
        return;
    }

    if ((uint32_t)(now - s_state.out_idle_since) >= AC_IDLE_STOP_MS)
    {
        s_state.out_idle_since = 0;
        logger_out(LOG_WARN, "AC out idle %umin no load: auto output off",
                   AC_IDLE_STOP_MS / 60000u);
        s_state.flags.ac_out_requested = false;
        drv_buzzer_short_sound(2);
        display_publish();
    }
}

/**
 * @brief 输出功率分级监督: 两级过载 + 声光报警。
 * @param m 输出计量快照 (可 NULL)
 * @note  判据统一用原始功率 vout_p_w (非滤波值), 保证响应速度:
 *          > 500W            → 声光报警 (蜂鸣 2.0s 一次 / 报警灯 / 屏 OVERLOAD)
 *          > 700W  持续 30s  → 长时过载, 关闭交流输出
 *          > 1000W 持续 3s   → 严重过载, 立即关闭交流输出
 *        每一级的计时在功率回落到门限以下时立即清零, 不累计。
 *        告警状态经 app_ui_overload_alarm() 供显示/报警灯/蜂鸣器共用;
 *        停机后清除请求, 等下次按键, 不自动重启。
 */
static void output_power_supervise(const METER_INFO_T *m)
{
    static uint32_t s_soft_since; /* >700W  起算时间戳, 0 = 当前未超限 */
    static uint32_t s_hard_since; /* >1000W 起算时间戳, 0 = 当前未超限 */

    if (!output_running() || m == NULL || !m->out_ok)
    {
        s_soft_since = 0;
        s_hard_since = 0;
        s_overload_alarm = false;
        return;
    }

    uint32_t now = HAL_GetTick();
    float p = m->vout_p_w;

    s_overload_alarm = (p > (float)BOARD_PWR_ALARM_W);

    /* 两级计时: 功率回落到门限以下立即清零 */
    if (p > (float)BOARD_PWR_TRIP_W)
    {
        if (s_soft_since == 0)
        {
            s_soft_since = now;
        }
    }
    else
    {
        s_soft_since = 0;
    }

    if (p > (float)BOARD_PWR_HARD_W)
    {
        if (s_hard_since == 0)
        {
            s_hard_since = now;
        }
    }
    else
    {
        s_hard_since = 0;
    }

    bool hard_trip = (s_hard_since != 0) &&
                     ((uint32_t)(now - s_hard_since) >= BOARD_PWR_HARD_HOLD_MS);
    bool soft_trip = (s_soft_since != 0) &&
                     ((uint32_t)(now - s_soft_since) >= BOARD_PWR_TRIP_HOLD_MS);
    if (!hard_trip && !soft_trip)
    {
        return;
    }

    if (hard_trip)
    {
        logger_out(LOG_WARN, "OUT power %dW > %uW for %us: output off (hard)",
                   (int)p, (unsigned)BOARD_PWR_HARD_W,
                   (unsigned)(BOARD_PWR_HARD_HOLD_MS / 1000u));
    }
    else
    {
        logger_out(LOG_WARN, "OUT power %dW > %uW for %us: output off (overload)",
                   (int)p, (unsigned)BOARD_PWR_TRIP_W,
                   (unsigned)(BOARD_PWR_TRIP_HOLD_MS / 1000u));
    }

    s_soft_since = 0;
    s_hard_since = 0;
    s_state.flags.ac_out_requested = false;
    drv_buzzer_short_sound(2);
    display_publish();
}

/**
 * @brief 输出监督: 通道失效 → 立即停止; 空载 50min → 自动停止。
 * @param m 输出计量快照 (可 NULL)
 * @note  过载分级由 output_power_supervise 独立处理。
 */
static void output_supervise(const METER_INFO_T *m)
{
    output_channel_check(m);
    output_idle_check(m);
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

    /* 主风扇: bat/chg 任一超阈值开启，均低于阈值关闭 */
    bool main_on_cond = (t->bat_temp >= BOARD_PROT_FAN_ON_TH ||
                         t->chg_temp >= BOARD_PROT_FAN_ON_TH);
    bool main_off_cond = (t->bat_temp <= BOARD_PROT_FAN_OFF_TH &&
                          t->chg_temp <= BOARD_PROT_FAN_OFF_TH);

    if (!s_state.flags.fan_main_on && main_on_cond)
    {
        drv_fan_on(FAN_MAIN);
        s_state.flags.fan_main_on = true;
    }
    else if (s_state.flags.fan_main_on && main_off_cond)
    {
        drv_fan_off(FAN_MAIN);
        s_state.flags.fan_main_on = false;
    }

    /* 辅风扇: inv 超阈值开启，低于阈值关闭 */
    bool aux_on_cond = (t->inv_temp >= BOARD_PROT_FAN_ON_TH);
    bool aux_off_cond = (t->inv_temp <= BOARD_PROT_FAN_OFF_TH);

    if (!s_state.flags.fan_aux_on && aux_on_cond)
    {
        drv_fan_on(FAN_AUX);
        s_state.flags.fan_aux_on = true;
    }
    else if (s_state.flags.fan_aux_on && aux_off_cond)
    {
        drv_fan_off(FAN_AUX);
        s_state.flags.fan_aux_on = false;
    }
}

/*********************************************************************
 * @fn      battery_over_temp_guard
 * @brief   电池 NTC 过温降载: >=60℃ 停充 + 停交流输出, <=55℃ 自动恢复。
 * @param   t 温度快照 (可 NULL)。
 * @return  none
 * @note    1) 与 enter_fault 的锁死故障区分: 过温属可恢复工况, 用独立标志
 *             降载而不锁死, 避免用户必须断电重启才能继续使用;
 *          2) 停充走既有顺序断电 (先断 CHG_OUT, 1.5s 后断 CHG_IN);
 *          3) 停输出只撤请求, 由旁路/逆变状态机按各自时序安全断电;
 *          4) 降载期间 need_charge 由 charge_strategy_update 强制为 false,
 *             ACOUT 按键在 handle_path_key 中被拒绝;
 *          5) 温度快照无效时保持现状, 不误动作。
 *********************************************************************/
static void battery_over_temp_guard(const TEMP_INFO_T *t)
{
    if (t == NULL)
    {
        return;
    }

    if (!s_state.flags.bat_otp && t->bat_temp >= BOARD_PROT_BAT_TMP_STOP_TH)
    {
        s_state.flags.bat_otp = true;
        app_alarm_merge(SYS_ALARM_BAT_OTP);   /* 界面显示 ERR20 (码表 APP_ERR_BAT_OTP=20) */

        /* 停充: 进入顺序断电尾段 */
        if (s_state.charge_state == CHARGE_STATE_STARTING ||
            s_state.charge_state == CHARGE_STATE_RUNNING)
        {
            drv_relay_off(RELAY_CHG_OUT);
            s_state.charge_state = CHARGE_STATE_STOPPING;
            s_state.charge_phase_since = HAL_GetTick();
        }
        /* 停交流输出: 撤请求, 由旁路/逆变状态机顺序断电 */
        s_state.flags.ac_out_requested = false;
        s_state.flags.need_charge = false;
        s_state.flags.ui_dirty = true;
        display_publish();
        drv_buzzer_short_sound(3);
        logger_out(LOG_WARN, "BAT NTC %d.%dC >= %d.%dC: stop charge & AC out, beep 3",
                   t->bat_temp / 10, t->bat_temp % 10,
                   BOARD_PROT_BAT_TMP_STOP_TH / 10, BOARD_PROT_BAT_TMP_STOP_TH % 10);
    }
    else if (s_state.flags.bat_otp && t->bat_temp <= BOARD_PROT_BAT_TMP_RESUME_TH)
    {
        s_state.flags.bat_otp = false;
        app_alarm_unmerge(SYS_ALARM_BAT_OTP);
        logger_out(LOG_INFO, "BAT NTC %d.%dC <= %d.%dC: over-temp released",
                   t->bat_temp / 10, t->bat_temp % 10,
                   BOARD_PROT_BAT_TMP_RESUME_TH / 10, BOARD_PROT_BAT_TMP_RESUME_TH % 10);
    }
}

/*********************************************************************
 * @fn      thermal_protect
 * @brief   温度保护: 按当前通路方向检查充电/逆变过温并跳闸锁死。
 * @param   t 温度快照 (可能为 NULL)。
 * @param   charge 充电方向活跃 (目标通路或实测继电器)。
 * @param   invert 逆变方向活跃 (目标通路或实测继电器)。
 * @return  true: 已进入故障; false: 无过温或仍在延时计时。
 * @note    1) 超异常阈值后须持续 THERMAL_FAULT_DELAY_MS 才 enter_fault,
 *             期间回落即复位计时 —— 防止瞬态过温误停机;
 *          2) 风扇启动阈值 < 异常阈值: 超风扇值仅开对应风扇, 由
 *             fan_manage 独立管理 (本函数不负责风扇);
 *          3) 电池过温由 battery_over_temp_guard 以可恢复降载方式处理,
 *             本函数只负责充电器/逆变器的锁死型过温。
*********************************************************************/
#define THERMAL_FAULT_DELAY_MS 10000u /* 过温持续 10s 才跳闸 (防抖) */
static bool thermal_protect(const TEMP_INFO_T *t, bool charge, bool invert)
{
    static bool s_thermal_trip_pending;   /* 过温持续时间未到: true */
    static uint32_t s_thermal_trip_since; /* 过温起算时间戳 */

    if (!t)
    {
        s_thermal_trip_pending = false;
        return false;
    }
    /* 判定当前超限方向 (优先级: 充电 > 逆变);
     * 电池过温由 battery_over_temp_guard 以可恢复降载方式单独处理 */
    uint32_t trip_bit = 0u;
    if (charge && t->chg_temp >= BOARD_PROT_CHG_TMP_FAULT_TH)
    {
        trip_bit = SYS_ALARM_CHG_OTP;
    }
    else if (invert && t->inv_temp >= BOARD_PROT_INV_TMP_FAULT_TH)
    {
        trip_bit = SYS_ALARM_INV_OTP;
    }

    if (trip_bit == 0u)
    {
        /* 温度回落到异常阈值下: 复位计时, 不跳闸 */
        s_thermal_trip_pending = false;
        return false;
    }
    if (!s_thermal_trip_pending)
    {
        s_thermal_trip_pending = true;
        s_thermal_trip_since = HAL_GetTick();
    }
    if ((uint32_t)(HAL_GetTick() - s_thermal_trip_since) >= THERMAL_FAULT_DELAY_MS)
    {
        enter_fault(trip_bit);
        return true;
    }
    return false; /* 超异常阈值但未满 10s: 仅持续计时, 风扇已开启 */
}

/*********************************************************************
 * @fn      battery_protect
 * @brief   电池保护: 数据无效或方向相关故障位, 延时跳闸进故障。
 * @param   charge 充电方向活跃。
 * @param   invert 逆变方向活跃。
 * @return  true: 已进入故障; false: 正常或仍在延时计时。
 * @note    稳态 (RUNNING/STOPPING) 连续无效满 BATTERY_ACTION_DELAY_MS 才跳闸防误断;
 *          启动中 (CHARGE STARTING / BYPASS STARTING / INVERT PRECHARGE、DC_CONNECT、
 *          OUTPUT) 立即跳闸 —— 禁止使用无效电池数据完成起动。
*********************************************************************/
static bool battery_protect(bool charge, bool invert)
{
    if (!charge && !invert)
    {
        s_state.flags.battery_trip_pending = false;
        return false;
    }
    BATT_INFO_T bat = {0};
    uint32_t severe = 0;
    bool good = battery_path_ok(charge ? PATH_CHARGE : PATH_INVERT, &bat, &severe);
    if (!good || severe)
    {
        if (!s_state.flags.battery_trip_pending)
        {
            s_state.flags.battery_trip_pending = true;
            s_state.battery_trip_since = HAL_GetTick();
        }
        /* 启动中 (STARTING/PRECHARGE/DC_CONNECT/OUTPUT) 立即跳闸: 禁止用无效
         * 电池数据完成起动; 稳态 (RUNNING/STOPPING) 必须连续无效满
         * BATTERY_ACTION_DELAY_MS 才跳闸 —— 单次采样失败或芯片 DNR 不再直接停机。 */
        bool starting = (s_state.charge_state == CHARGE_STATE_STARTING ||
                         s_state.bypass_state == BYPASS_STATE_STARTING ||
                         s_state.invert_state == INVERT_STATE_PRECHARGE ||
                         s_state.invert_state == INVERT_STATE_DC_CONNECT ||
                         s_state.invert_state == INVERT_STATE_OUTPUT);
        if (starting ||
            (uint32_t)(HAL_GetTick() - s_state.battery_trip_since) >= BATTERY_ACTION_DELAY_MS)
        {
            /* 跳闸掩码: 数据无效 + 方向性电池保护位 (OVP/UVP/OCP) 一并上报,
             * 使界面能显示具体原因 (ERR10/11/12) 而不是笼统的 ERR03 */
            enter_fault(SYS_ALARM_BAT_INVALID |
                        (severe & (SYS_ALARM_BAT_OVP | SYS_ALARM_BAT_UVP | SYS_ALARM_BAT_OCP)));
            return true;
        }
    }
    else
    {
        s_state.flags.battery_trip_pending = false;
    }
    return false;
}

/**
 * @brief 旧的"电压+taper 2h / 零电流 3min"兜底判满已废弃。
 * @note  2026-10-25 产品定义改为: SOC=100% 判满, FQ/TTF 仅用于是否立即停的确认,
 *        见 full_charge_auto_stop()。此函数名保留为空实现, 避免外部历史引用失效。
 */
static void charge_full_fallback_update(void)
{
}


/**
 * @brief 判满锁存查询 (停充时置位, 开始新一轮充电时清除)。
 * @return true: 上一次停充是因为判满。
 */
bool charge_full_active(void)
{
    return s_state.flags.charge_full;
}

/**
 * @brief 产品级"满"判定 (供显示/BLE 使用)。
 * @return true: 上一次停充是因为判满 (SOC 到 100%)。
 * @note  显示侧只用它关"充电图标"并把 99→100 补齐; SOC 数字始终用真实值,
 *        不会被强制成 100% (现场 3% 显示 100% 的问题已修)。
 */
bool battery_full_active(void)
{
    return s_state.flags.charge_full != 0;
}

/**
 * @brief 停充公共动作: 置满锁存 + 10min 冷却 + 断 CHG_OUT。
 * @param reason 触发原因 (日志用): "FQ+TTF" / "forced5min" / "V+I 1h"
 * @param bat 有效电池快照
 */
static void charge_stop_full(const char *reason, const BATT_INFO_T *bat)
{
    s_state.flags.charge_full = true;
    s_chg_block = true;
    s_chg_block_since = HAL_GetTick();
    logger_out(LOG_INFO,
               "BAT FULL (%s): soc=%u%% fq=%u ttf=%umin vfsoc=%u%% avgI=%ldmA cellAvg=%lumV, block %lus",
               reason, (unsigned)bat->soc, (unsigned)bat->full_qualified,
               bat->ttf_valid ? (unsigned)bat->time_to_full_min : 0u, (unsigned)bat->vfsoc,
               (long)bat->avg_current_ma,
               (unsigned long)(bat->avg_cell_voltage_mv ? bat->avg_cell_voltage_mv : bat->cell_voltage_mv),
               (unsigned long)(CHARGE_RESTART_BLOCK_MS / 1000u));

    drv_relay_off(RELAY_CHG_OUT);
    s_state.charge_state = CHARGE_STATE_STOPPING;
    s_state.charge_phase_since = HAL_GetTick();
    drv_buzzer_short_sound(1);
    s_state.flags.ui_dirty = true;
}

/*********************************************************************
 * @fn      full_charge_auto_stop
 * @brief   判满停充。两条路径:
 *          A) SOC 到 100%: FQ=1 且 TTF 剩余=0 → 立即停; 否则保持 5min 强制停。
 *          B) 物理兜底 (覆盖 SOC 到不了 100% 的 EZ+LFP 场景):
 *             AvgVCell ≥ 3600mV(=28.8V/8) 且 |AvgCurrent| ≤ 500mA, 连续保持 1h
 *             → 判满停充, 并置物理满锁存 (SOC<95% 才允许再充)。
 *             注: 不要求 TTF=0, 因为本工程 TTF=0 与 SOC=100 等价, 会让 B 永远无法触发。
 * @note    停充后统一进入 CHARGE_RESTART_BLOCK_MS 冷却; 拔插 220V 立即解除。
*********************************************************************/
static void full_charge_auto_stop(void)
{
    static bool s_force_pending;   /* A: SOC=100 但确认条件未满足, 强制停计时中 */
    static uint32_t s_force_since;
    static bool s_phys_pending;    /* B: 物理兜底三条件满足, 保持计时中 */
    static uint32_t s_phys_since;
    static uint32_t s_phys_break_since; /* B: 条件中断起始时刻 (0=当前满足), 用于容错 */

    if (s_state.flags.fault_active || s_state.mode != WORK_MODE_CHARGE_BYPASS ||
        s_state.charge_state != CHARGE_STATE_RUNNING)
    {
        s_force_pending = false;
        s_phys_pending = false;
        s_phys_break_since = 0u;
        return;
    }

    BATT_INFO_T bat = {0};
    if (!battery_get_snapshot(&bat))
        return;                    /* 数据无效: 不判满, 交给保护逻辑 */

    bool ttf_zero = (!bat.ttf_valid) || (bat.time_to_full_min == 0u);

    /* ---- 路径 A: SOC 到 100% ---- */
    if (battery_soc_full(&bat))
    {
        s_phys_pending = false;
        if (bat.full_qualified && ttf_zero)
        {
            s_force_pending = false;
            charge_stop_full("FQ+TTF", &bat);
            return;
        }
        if (!s_force_pending)
        {
            s_force_pending = true;
            s_force_since = HAL_GetTick();
            logger_out(LOG_INFO, "BAT soc=100%% but fq=%u ttf=%umin; force stop in %lus",
                       (unsigned)bat.full_qualified,
                       bat.ttf_valid ? (unsigned)bat.time_to_full_min : 0u,
                       (unsigned long)(CHARGE_FULL_FORCE_MS / 1000u));
            return;
        }
        if ((uint32_t)(HAL_GetTick() - s_force_since) >= CHARGE_FULL_FORCE_MS)
        {
            s_force_pending = false;
            charge_stop_full("forced5min", &bat);
        }
        return;
    }

    /* ---- 路径 B: 物理兜底 (SOC 到不了 100% 时) ---- */
    s_force_pending = false;
    {
        uint32_t cell_mv = bat.avg_cell_voltage_mv ? bat.avg_cell_voltage_mv : bat.cell_voltage_mv;
        /* 电流取绝对值窗: 末端板载自耗会让净电流为小负值(充电已完成), 也算"涓流";
         * 但大电流放电(边充边用/充电器失效)必须排除 */
        bool i_ok = (bat.avg_current_ma <= CHARGE_FULL_I_MA) &&
                    (bat.avg_current_ma >= -CHARGE_FULL_I_MA);
        /* 这里刻意不要求 TTF=0: 本工程 TTF=(FullCapRep-RepCap)/I, TTF=0 与 SOC=100 等价,
         * 加上它会让 B 路径永远只能在 SOC=100 时触发, 失去"SOC 到不了 100"的兜底意义。 */
        bool phys_ok = (cell_mv >= CHARGE_FULL_V_MV) && i_ok;
        if (!phys_ok)
        {
            /* 容错: 充电器 re-bulk 时瞬时电流会冲高 (实测 avgI 859mA > 500mA)、
             * 单体电压也会抖。一拍的越界不应把已经跑了很久的 1h 计时整个清零,
             * 否则 B 路径可能永远触发不了。连续不满足 PHYS_BREAK_TOLERANCE_MS 才作废。 */
            uint32_t now = HAL_GetTick();
            if (s_phys_break_since == 0u)
            {
                s_phys_break_since = (now != 0u) ? now : 1u;   /* 0 保留为"满足"标记 */
            }
            else if ((uint32_t)(now - s_phys_break_since) >= PHYS_BREAK_TOLERANCE_MS)
            {
                s_phys_break_since = 0u;
                s_phys_pending = false;
            }
            return;
        }
        s_phys_break_since = 0u;   /* 条件恢复, 中断计时清零 */
        if (!s_phys_pending)
        {
            s_phys_pending = true;
            s_phys_since = HAL_GetTick();
            logger_out(LOG_INFO, "BAT soc=%u%% but cellAvg=%lumV avgI=%ldmA ttf=%umin; phys full in %lus",
                       (unsigned)bat.soc, (unsigned long)cell_mv, (long)bat.avg_current_ma,
                       bat.ttf_valid ? (unsigned)bat.time_to_full_min : 0u,
                       (unsigned long)(CHARGE_FULL_V_I_HOLD_MS / 1000u));
            return;
        }
        if ((uint32_t)(HAL_GetTick() - s_phys_since) >= CHARGE_FULL_V_I_HOLD_MS)
        {
            s_phys_pending = false;
            s_phys_full = true;
            charge_stop_full("V+I 1h", &bat);
        }
    }
}

/**
 * @brief 工作状态机主流程: 统一管理保护、模式决策、各功能状态机及输出监督。
 * @note  执行顺序 (20ms 周期调用):
 *          1. 电池过温降载 (NTC >=60℃ 停充+停输出, 可恢复)
 *          2. 保护检查 (充电/逆变过温、电池故障) → 异常则 enter_fault 并返回
 *          3. (旧)充满兜底判定已废弃 → 判满由 full_charge_auto_stop() 按 SOC=100% 处理
 *          4. 充电策略更新 → 220V 稳定 2s 且 SOC<100% 且不在冷却期 → need_charge
 *          5. 模式决策 → 市电在位 CHARGE_BYPASS / 掉电 INVERT
 *          6. 220V 会话 → 插入自动开旁路输出; 拔出清输出请求
 *          7. 各功能状态机 → charge/bypass/invert_state_process
 *          8. 输出监督 → 通道失效立即停 / 旁路或逆变空载 50min 自动停 (只关输出)
 *          9. 功率分级 → >500W 声光报警 / >700W 持续 30s 停输出 / >1000W 持续 3s 立即停
 *         10. 判满自停 → A: SOC=100% 且(FQ+TTF 或 持续5min)
 *                         B: 单体≥3600mV 且 |avgI|≤500mA 持续1h (SOC 到不了 100% 时) → 断充 + 10min 冷却
 */
static void work_status_process(void)
{
    /* 故障锁死 / 已进入关机流程: 不再合成任何通路 */
    if (s_state.flags.fault_active || s_state.mode == WORK_MODE_SHUTDOWN)
        return;

    ac_input_state_t ac = mod_metering_get_ac_input().state;

    /* 电池 NTC 过温降载: >=60℃ 停充 + 停交流输出 (可恢复, 不锁死) */
    const TEMP_INFO_T *t = mod_temp_get();
    battery_over_temp_guard(t);

    /* 保护检查 */
    bool charge = path_active_charge();
    bool invert = path_active_invert();
    if (thermal_protect(t, charge, invert) || battery_protect(charge, invert))
        return;

    /* 旧兜底判满已废弃 (SOC=100% 判满), 保留空调用以维持结构不变 */
    charge_full_fallback_update();

    /* 充电策略: 220V 稳定 2s 且 SOC<100% → need_charge */
    charge_strategy_update(ac);

    /* 模式决策: 市电在位可信 → CHARGE_BYPASS; 掉电/不可信 → INVERT。 */
    mode_decide(ac);

    /* 220V 会话: 插入自动开旁路输出; 拔出清请求 (逆变输出改为按键触发) */
    ac_output_session_update(ac);

    /* 充电状态机: IDLE → STARTING → RUNNING → STOPPING */
    charge_state_process(ac);
    /* 旁路状态机: IDLE → RUNNING → IDLE */
    bypass_state_process(ac);
    /* 逆变状态机: IDLE → PRECHARGE → DC_CONNECT → OUTPUT → RUNNING → STOPPING */
    invert_state_process(ac);

    /* 稳态监护 */
    METER_INFO_T *m = metering_get_info();
    /* 输出监督: 通道失效 → 立即停止; 空载 50min → 自动停止 */
    output_supervise(m);
    /* 功率分级: >500W 声光报警; >700W 30s 停输出; >1000W 3s 立即停 */
    output_power_supervise(m);
    /* 充满自停: MAX17263 FQ 上升沿检测 → 断充 */
    full_charge_auto_stop();
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
    if (s_state.mode == WORK_MODE_SHUTDOWN)
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

/*====================================================================
 * 报警灯 + 电池告警同步
 *====================================================================*/

/* 显示用电池告警位: 通讯硬故障 (ERR01)。
 * 温度类 OTP 由 thermal_protect / battery_over_temp_guard 直接合并, 不经此处;
 * OVP/UVP/OCP 由 battery_protect 跳闸时合并。
 * 注: 低电量 (ERR13) 已按 2026-11-15 需求停用, 不再进显示位图。 */
#define BATTERY_ALARM_DISP_MASK (SYS_ALARM_BAT_COMM)

/**
 * @brief 同步电池快照告警位到统一告警位图 (故障时显示, 恢复自动清码)。
 * @note  20ms 周期调用。fold_alarm 结果仅存于快照 alarm_bits, 原先无人合并
 *        导致 ERR01 从不上屏 — 此处补齐链路。快照无效/陈旧 (正是通讯断的
 *        典型场景) 时出参副本仍携带 alarm_bits, 故不判定
 *        battery_get_snapshot 返回值。只操作显示位掩码, 不触碰 enter_fault
 *        锁死的保护位 (BAT_INVALID/OVP/UVP/OTP)。
 */
static void battery_alarm_sync(void)
{
    BATT_INFO_T bat;
    battery_get_snapshot(&bat);
    uint32_t bits = bat.alarm_bits & BATTERY_ALARM_DISP_MASK;
    app_alarm_unmerge(BATTERY_ALARM_DISP_MASK);
    app_alarm_merge(bits);
}

/**
 * @brief 市电异常告警同步 (ERR40): 有电压但不在可用窗口内 (欠压/过压) 才报。
 * @note  真正没接市电 (电压≈0) 不报 —— 便携机拔掉市电跑电池是正常工况。
 *        需持续 AC_ABNORMAL_HOLD_MS 才置位, 避免拔插瞬间的电压衰减误报。
 *        20ms 周期调用。
 */
#define AC_ABNORMAL_MIN_V    30.0f    /* 高于此值仍判为"没市电"= 视为异常输入 */
#define AC_ABNORMAL_HOLD_MS  3000u    /* 异常持续多久才上报 */
static void ac_input_alarm_sync(void)
{
    static uint32_t s_since;   /* 异常起算时间戳, 0=当前未异常 */

    ac_input_snapshot_t ac = mod_metering_get_ac_input();
    bool abnormal = (ac.state == AC_INPUT_ABSENT) && (ac.voltage >= AC_ABNORMAL_MIN_V);

    if (!abnormal)
    {
        s_since = 0;
        app_alarm_unmerge(SYS_ALARM_AC_INPUT);
        return;
    }
    if (s_since == 0)
    {
        s_since = HAL_GetTick();
        return;
    }
    if ((uint32_t)(HAL_GetTick() - s_since) >= AC_ABNORMAL_HOLD_MS)
    {
        app_alarm_merge(SYS_ALARM_AC_INPUT);
    }
}

/**
 * @brief 声光报警输出: 报警灯 + 过载蜂鸣节奏 (20ms 周期调用)。
 * @note  优先级: 故障锁死(常亮) > 过载告警(2.0s 节奏) > 故障码(500ms 闪) > 熄灭。
 *        过载相位: 前 1.0s 报警灯亮并在上升沿短响一声, 后 1.0s 静默;
 *        即"2.0s 间隔短响滴一声", 与屏端 OVERLOAD 文本闪动同相 (app_ui_overload_phase)。
 */
static void alarm_output_process(void)
{
    if (s_state.flags.fault_active)
    {
        drv_led_on(LED_ALM);
        return;
    }

    if (s_overload_alarm)
    {
        bool phase = app_ui_overload_phase();
        if (phase && !s_overload_phase_prev)
        {
            drv_buzzer_short_sound(1);  /* 每个 2.0s 周期短响一次 */
        }
        s_overload_phase_prev = phase;

        if (phase)
        {
            drv_led_on(LED_ALM);
        }
        else
        {
            drv_led_off(LED_ALM);
        }
        return;
    }
    s_overload_phase_prev = false;

    if (app_alarm_err_code() != 0u)
    {
        if ((HAL_GetTick() % 1000u) < 500u)
        {
            drv_led_on(LED_ALM);
        }
        else
        {
            drv_led_off(LED_ALM);
        }
        return;
    }
    drv_led_off(LED_ALM);
}

/**
 * @brief 当前是否在对外"带载供电" (旁路或逆变, 且输出有负载)。
 * @return true: 通路 RUNNING 且输出计量有效、功率 > 1W。
 * @note  用功率结果判定而非"输出请求/继电器命令": 输出开着但没接负载不算供电。
 */
static bool output_delivering_power(void)
{
    METER_INFO_T *m = metering_get_info();
    return output_running() && (m != NULL) && (m->out_ok != 0u) &&
           (m->vout_p_w > AC_IDLE_PWR_W);
}

/**
 * @brief 低功耗自动关机: 1h 内既没有"带载供电"、也没有"给整机充电" → 关闭整机。
 * @note  20ms 周期调用。判据是**结果**而不是请求:
 *          - 对外供电: 旁路/逆变 RUNNING 且输出有载 (>1W, 输出计量有效);
 *          - 整机充电: 充电状态机 STARTING/RUNNING (CHG_IN 已吸合)。
 *        任一成立即复位计时, 两者皆无持续 IDLE_SHUTDOWN_MS(1h) → system_shutdown。
 *        与旧实现的差别: 不再用"输出请求/子状态机是否 IDLE"判定, 因此
 *        "输出开着但一直没接负载"也计入空闲 —— 插着 220V 空载时, 旁路会在
 *        1h 被空载自停关掉, 随后本函数触发关机 (不再长期待命)。
 *        故障锁死态不参与计时 (保留故障提示, 由用户处理);
 *        关机动作与 ONOFF 长按共用 system_shutdown (唯一断电出口)。
 */
static void idle_shutdown_check(void)
{
    static uint32_t s_idle_since; /* 空闲起算时间戳, 0=非空闲 */

    /* 故障锁死态: 不自动关机, 保留故障提示 */
    if (s_state.flags.fault_active)
    {
        s_idle_since = 0;
        return;
    }

    /* 有效工作 = 带载对外供电 或 给整机充电; 两者皆无才算空闲 */
    bool busy = output_delivering_power() || app_ui_charging();

    if (busy)
    {
        s_idle_since = 0;
        return;
    }
    if (s_idle_since == 0)
    {
        s_idle_since = HAL_GetTick();
        return;
    }
    if ((uint32_t)(HAL_GetTick() - s_idle_since) < IDLE_SHUTDOWN_MS)
    {
        return;
    }

    s_idle_since = 0;
    system_shutdown("idle 1h: no load supply & no charge");
}

/**
 * @brief 功率显示同步: 通路关闭期间立即清零对应通道功率显示.
 * @note  主循环 20ms 周期调用, 始终执行 (故障/关机态也清零, 不受
 *        work_status_process 早退影响):
 *          - 输出: 旁路/逆变子状态机均非 RUNNING → 清输出功率
 *            (按键关断/顺序断电起始即清, 不等计量芯片断电残留)
 *          - 输入: 市电不在位 → 清输入功率
 *        通路重新开启后首个有效样本经中值滤波直通, 立即同步.
 */
static void power_display_sync(void)
{
    bool out_running = (s_state.bypass_state == BYPASS_STATE_RUNNING ||
                        s_state.invert_state == INVERT_STATE_RUNNING);
    if (!out_running)
    {
        mod_metering_zero_output();
    }
    if (mod_metering_get_ac_input().state != AC_INPUT_PRESENT)
    {
        mod_metering_zero_input();
    }
}

/*====================================================================
 * UART1 命令解析 (调试串口接收)
 *====================================================================*/

#define UART1_CMD_MAX_LEN  64

/**
 * @brief  UART1 命令解析 (AT 式: 收到 \r\n 或 \n 触发处理)
 * @note   支持命令:
 *           "HELP\r\n"  → 打印帮助
 *           "STATUS\r\n" → 打印当前状态
 *           "LOG LEVEL <0-5>\r\n" → 设置日志级别
 */
static void uart1_cmd_process(void)
{
    static uint8_t s_cmd_buf[UART1_CMD_MAX_LEN];
    static uint16_t s_cmd_len = 0;
    uint8_t ch;

    /* 逐字节读取, 按行处理 */
    while (logger_rx_read(&ch, 1) == 1)
    {
        if (ch == '\r')
        {
            continue;  /* 忽略 \r */
        }
        if (ch == '\n' || s_cmd_len >= UART1_CMD_MAX_LEN - 1)
        {
            /* 行结束, 处理命令 */
            s_cmd_buf[s_cmd_len] = '\0';

            if (s_cmd_len > 0)
            {
                /* 命令分发 */
                if (strcmp((char *)s_cmd_buf, "HELP") == 0)
                {
                    log_print("=== UART1 Commands ===");
                    log_print("HELP         - Show this help");
                    log_print("STATUS       - Show work status");
                    log_print("LOG LEVEL <n> - Set log level (0-5)");
                }
                else if (strcmp((char *)s_cmd_buf, "STATUS") == 0)
                {
                    BATT_INFO_T bat_snap;
                    BATT_INFO_T *bat = battery_get_snapshot(&bat_snap) ? &bat_snap : NULL;

                    log_print("=== System Status ===");
                    log_print("Mode: %u, Charge: %u, Bypass: %u, Invert: %u",
                              s_state.mode, s_state.charge_state, s_state.bypass_state, s_state.invert_state);

                    if (bat && bat->valid)
                    {
                        log_print("[BAT] soc: %u%%, soh: %u%%, vol: %lumV, cur: %ldmA",
                                  bat->soc, bat->soh, (unsigned long)bat->voltage_mv, (long)bat->current_ma);
                        log_print("[BAT] ttf: %u min, tte: %u min, cycles: %u",
                                  bat->ttf_valid ? bat->time_to_full_min : 0,
                                  bat->tte_valid ? bat->time_to_empty_min : 0,
                                  bat->cycles_raw);
                        log_print("[BAT] repcap: %lumAh, fullcap: %lumAh, temp: %d.%dC",
                                  (unsigned long)bat->repcap_mah, (unsigned long)bat->fullcap_mah,
                                  bat->temp_deg / 10, bat->temp_deg % 10);
                    }
                    else
                    {
                        log_print("[BAT] offline or not ready");
                    }
                }
                else if (strncmp((char *)s_cmd_buf, "LOG LEVEL ", 10) == 0)
                {
                    int level = atoi((char *)&s_cmd_buf[10]);
                    if (level >= 0 && level <= 5)
                    {
                        log_print("Log level set to %d", level);
                        /* TODO: 调用 logger_set_level(level) */
                    }
                    else
                    {
                        log_print("Invalid level: %d (0-5)", level);
                    }
                }
                else
                {
                    log_print("Unknown command: %s", s_cmd_buf);
                }
            }
            s_cmd_len = 0;
        }
        else
        {
            /* 追加到命令缓冲 */
            s_cmd_buf[s_cmd_len++] = ch;
        }
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
        /* 电池告警同步: 故障时显示 ERR 码, 恢复自动清码 (锁死期间仍刷新) */
        battery_alarm_sync();
        /* 市电异常同步: 输入有电压但不在可用窗口 -> ERR40 */
        ac_input_alarm_sync();
        /* 工作状态机: 保护 + 充电策略 + 通路切换 + 稳态管理 */
        work_status_process();
        /* 功率显示同步: 通路关闭期间立即清零对应通道功率显示 */
        power_display_sync();
        /* 按键: 轮询扫描, 事件经回调分发 (仅 ONOFF 长按 / ACOUT 按下) */
        mod_key_poll_handler();
        /* UART1 命令解析: 读取调试串口接收数据, 按行处理 */
        uart1_cmd_process();
        /* 指示灯: 非阻塞刷新工作灯 (绿灯 1s 亮灭 / 蓝灯 5s 快闪) */
        work_led_process();
        /* 声光报警: 报警灯三态 + 过载蜂鸣节奏 (2.0s 间隔短响一声) */
        alarm_output_process();
        /* 低功耗自动关机: 1h 无输出且未充电 → 关机待机 */
        idle_shutdown_check();
        /* 显示: 通路/状态有变化时向显示任务发送图标消息 */
        display_publish();
        /* 看门狗: 主任务心跳喂狗 */
        bsp_wdg_task_heartbeat(WDG_TASK_MAIN);
        /* 20ms 任务节拍 */
        osDelay(20);
    }
}
