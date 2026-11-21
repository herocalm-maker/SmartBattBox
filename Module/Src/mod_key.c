/**
 * @file    mod_key.c
 * @brief   按键管理模块 (Module 层)
 *
 * 按下确认制状态机 (20ms 轮询):
 *   IDLE → CONFIRM → PRESSED → LONG
 *   - CONFIRM: 连续 KEY_DEBOUNCE_N 次采样一致 (~40ms) 立即上报短按, 不等释放
 *   - PRESSED: 按住达到长按阈值 (ONOFF 500ms，其他键800ms) 进入 LONG 态, 不立即上报
 *   - LONG:    达标后等待松开, 松手时才上报一次长按 (不连发)
 *   - 松开判定与按下对称: 需连续 KEY_DEBOUNCE_N 次采到松开才算,
 *     防止继电器/逆变器 EMI 单帧毛刺把"按住"误判成"松开"而误触发长按
 *
 * 组合键: 按下键数变化时重新去抖, 同一手势内已上报过的按键不重复上报,
 *         避免组合键松开一半时误触发短按
 *
 * 上电按住保护: 初始化时已处于按下态的按键, 其整段手势视为无效
 *               (不触发短按/长按), 必须先松开一次后才恢复正常判定,
 *               防止上电时按住关机键在松手瞬间直接触发关机流程
 *
 * 调用方式: 20ms 轮询调用 mod_key_poll_handler()
 * 时间基准: KEY_POLL_PERIOD_MS = 20ms
 */

#include "mod_key.h"
#include "bsp_io.h"
#include "Board.h"
#include "comm_logger.h"
#include <string.h>

/*--------------------------------------------------------------------
 * 常量定义 (轮询周期 20ms)
 *--------------------------------------------------------------------*/
#define KEY_POLL_PERIOD_MS   20u       /* 轮询周期 */
#define KEY_DEBOUNCE_N        2u       /* 短按去抖: 2×20ms = 40ms */
#define KEY_LONG_MS          800u
#define KEY_ONOFF_LONG_MS    500u   /* ONOFF 关机键: 按住达 500ms 后, 松开才执行关机 */
#define KEY_NONE              0x00u

/* 按键状态 */
#define KEY_STATE_IDLE        0        /* 空闲 */
#define KEY_STATE_CONFIRM     1        /* 按下确认中 (去抖) */
#define KEY_STATE_PRESSED     2        /* 已上报短按, 按住计时 */
#define KEY_STATE_LONG        3        /* 已上报长按, 等待释放 */

/*--------------------------------------------------------------------
 * 引脚映射表
 *--------------------------------------------------------------------*/
static const struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       key_value;          /* 按键值 (位掩码) */
} s_pin_map[KEY_IDX_CNT] = {
    { BOARD_ONOFF_KEY_PORT, BOARD_ONOFF_KEY_PIN, KEY_VALUE_ONOFF },
    { BOARD_ACIN_KEY_PORT,  BOARD_ACIN_KEY_PIN,  KEY_VALUE_ACIN  },
    { BOARD_ACOUT_KEY_PORT, BOARD_ACOUT_KEY_PIN, KEY_VALUE_ACOUT },
    { BOARD_K1_PORT,        BOARD_K1_PIN,        KEY_VALUE_K1    },
};

/*--------------------------------------------------------------------
 * 内部状态
 *--------------------------------------------------------------------*/
typedef struct {
    uint8_t  key_state;       /* 按键状态 (KEY_STATE_*) */
    uint8_t  sample_mask;     /* 去抖中的采样掩码 */
    uint8_t  reported_mask;   /* 本手势内已上报过的按键 (去重) */
    uint32_t pressed_since_ms; /* HAL tick: continuous hold, wrap-safe. */
    uint16_t hold_cnt;        /* 当前掩码持续计数 */
} key_state_t;

static volatile key_state_t s_key = {0};
static mod_key_callback_t s_key_callback = NULL;
static uint8_t s_boot_hold_mask = KEY_NONE;  /* 上电时已按下的按键 (须先松开才判定) */

/*--------------------------------------------------------------------
 * 内部: 读取所有按键原始状态 (位掩码)
 *--------------------------------------------------------------------*/
/*********************************************************************
 * @fn      mod_key_read_all_keys
 * @brief   扫描引脚映射表, 返回当前所有按下按键的位掩码.
 * @param   无
 * @return  按键位掩码 (KEY_VALUE_* 按位或), 无键按下为 KEY_NONE.
 * @note    引脚低电平有效 (上拉输入, 按下接地), 读到 0 视为按下.
********************************************************************/
static uint8_t mod_key_read_all_keys(void)
{
    uint8_t mask = 0;
    for (int i = 0; i < KEY_IDX_CNT; i++) {
        if (bsp_gpio_read(s_pin_map[i].port, s_pin_map[i].pin) == 0) {  /* 低电平 = 按下 */
            mask |= s_pin_map[i].key_value;
        }
    }
    return mask;
}

/*====================================================================
 * API
 *====================================================================*/

/*********************************************************************
 * @fn      mod_key_init
 * @brief   初始化按键 GPIO 与内部状态机.
 * @param   无
 * @return  true = 初始化成功.
 * @note    纯输入轮询, 不使用 EXTI; 回调需另行通过 mod_key_register_callback 注册.
********************************************************************/
bool mod_key_init(void)
{
    /* GPIO 初始化 (纯输入, EXTI 未使用) — 由引脚映射表循环收敛 */
    for (int i = 0; i < KEY_IDX_CNT; i++)
    {
        bsp_gpio_init(s_pin_map[i].port, s_pin_map[i].pin,
                      GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    }

    /* 清零状态 */
    memset((void *)&s_key, 0, sizeof(s_key));
    s_key_callback = NULL;

    /* 记录初始化时刻已处于按下态的按键: 这些按键的当次手势视为无效,
     * 须先松开一次后才恢复判定 (防上电按住关机键松手即关机) */
    s_boot_hold_mask = mod_key_read_all_keys();
    if (s_boot_hold_mask != KEY_NONE)
    {
        logger_out(LOG_INFO, "mod_key: boot hold mask=0x%02X, wait release", s_boot_hold_mask);
    }

    logger_out(LOG_INFO, "mod_key: init OK");
    return true;
}

/*********************************************************************
 * @fn      mod_key_register_callback
 * @brief   注册按键事件回调 (短按/长按统一入口).
 * @param   cb  回调函数指针, 传入 KEY_VALUE_* 或 KEY_LONG_MARK|KEY_VALUE_*; NULL 取消注册
 * @return  none
 * @note    回调在轮询任务 (AppMainTask) 上下文同步执行, 不得阻塞.
********************************************************************/
void mod_key_register_callback(mod_key_callback_t cb)
{
    s_key_callback = cb;
}

/*--------------------------------------------------------------------
 * 轮询 (20ms) - 按下确认制状态机
 *--------------------------------------------------------------------*/
/*********************************************************************
 * @fn      mod_key_poll_handler
 * @brief   按键状态机轮询入口 (20ms 周期调用), 完成去抖/短按/长按判定.
 * @param   无
 * @return  none
 * @note    四态: IDLE→CONFIRM(去抖, 连续 KEY_DEBOUNCE_N 次一致即上报短按)
 *          →PRESSED(按住计时到阈值进入 LONG, 不立即上报)
 *          →LONG(达标后等松开, 松手才上报一次长按, 不连发).
 *          按下键集合变化即重新去抖, 同一手势内已上报的键不重复上报.
********************************************************************/
void mod_key_poll_handler(void)
{
    uint8_t return_key = KEY_NONE;
    uint8_t cur_key_val = mod_key_read_all_keys();

    /* 上电按住保护: 初始化时已按下的按键在松开前不参与任何判定,
     * 整段手势 (含长按松手触发) 全部丢弃; 全部松开后恢复正常处理 */
    if (s_boot_hold_mask != KEY_NONE)
    {
        if (cur_key_val & s_boot_hold_mask)
        {
            s_key.key_state = KEY_STATE_IDLE;
            s_key.reported_mask = KEY_NONE;
            return;
        }
        s_boot_hold_mask = KEY_NONE;
    }

    switch (s_key.key_state)
    {
      case KEY_STATE_IDLE:
        if (cur_key_val != KEY_NONE) 
        {
            /* 有键按下, 进入去抖确认 */
            s_key.sample_mask = cur_key_val;
            s_key.pressed_since_ms = HAL_GetTick();
            s_key.hold_cnt = 0;
            s_key.key_state = KEY_STATE_CONFIRM;
        }
        break;

      case KEY_STATE_CONFIRM:
        if (cur_key_val == KEY_NONE) 
        {
            /* 抖动或过短按压, 放弃确认 */
            s_key.key_state = KEY_STATE_IDLE;
        } 
        else if (cur_key_val != s_key.sample_mask) 
        {
            /* 按下键数变化 (含组合键), 重新去抖 */
            s_key.sample_mask = cur_key_val;
            s_key.pressed_since_ms = HAL_GetTick();
            s_key.hold_cnt = 0;
        } 
        else if (++s_key.hold_cnt >= KEY_DEBOUNCE_N) 
        {
            /* 按下确认: 立即上报短按, 不等释放 */
            s_key.hold_cnt = 0;
            s_key.key_state = KEY_STATE_PRESSED;
            if (!(s_key.reported_mask & s_key.sample_mask)) 
            {
                return_key = s_key.sample_mask;
                s_key.reported_mask |= s_key.sample_mask;
            }
        }
        break;

      case KEY_STATE_PRESSED:
        if (cur_key_val == KEY_NONE)
        {
            /* 松开确认: 连续 KEY_DEBOUNCE_N 次采到松开才回 IDLE,
             * 单帧毛刺不清手势/不清去重掩码 (防 EMI 误断手势导致重复上报) */
            if (++s_key.hold_cnt >= KEY_DEBOUNCE_N)
            {
                s_key.key_state = KEY_STATE_IDLE;
                s_key.reported_mask = KEY_NONE;
                s_key.hold_cnt = 0;
            }
        }
        else if (cur_key_val != s_key.sample_mask)
        {
            /* 按下键数变化, 重新去抖 (不重复上报已报过的键) */
            s_key.sample_mask = cur_key_val;
            s_key.pressed_since_ms = HAL_GetTick();
            s_key.hold_cnt = 0;
            s_key.key_state = KEY_STATE_CONFIRM;
        }
        else if ((uint32_t)(HAL_GetTick() - s_key.pressed_since_ms) >=  /* 无符号差值, 兼容 tick 回绕 */
                 (s_key.sample_mask == KEY_VALUE_ONOFF ? KEY_ONOFF_LONG_MS : KEY_LONG_MS))
        {
            /* 长按达标: 进入 LONG 态但不立即上报, 等松开后才触发 (关机等操作在松手时执行) */
            s_key.hold_cnt = 0;
            s_key.key_state = KEY_STATE_LONG;
        }
        else
        {
            /* 仍按住原键: 清松开确认计数 */
            s_key.hold_cnt = 0;
        }
        break;

      case KEY_STATE_LONG:
        if (cur_key_val == KEY_NONE)
        {
            /* 长按达标且已松开: 连续 KEY_DEBOUNCE_N 次确认松开后上报一次长按, 不连发;
             * 单帧毛刺不清计数, 防止按住期间被 EMI 误判松手而误触发关机 */
            if (++s_key.hold_cnt >= KEY_DEBOUNCE_N)
            {
                return_key = KEY_LONG_MARK | s_key.sample_mask;
                s_key.key_state = KEY_STATE_IDLE;
                s_key.reported_mask = KEY_NONE;
                s_key.hold_cnt = 0;
            }
        }
        else if (cur_key_val != s_key.sample_mask)
        {
            /* 按下键数变化, 重新去抖 (本次长按不再上报) */
            s_key.sample_mask = cur_key_val;
            s_key.pressed_since_ms = HAL_GetTick();
            s_key.hold_cnt = 0;
            s_key.key_state = KEY_STATE_CONFIRM;
        }
        else
        {
            /* 仍按住原键: 毛刺回弹, 清松开确认计数 */
            s_key.hold_cnt = 0;
        }
        break;

     default:
        s_key.key_state = KEY_STATE_IDLE;
        break;
    }

    /* 回调通知 */
    if (return_key != KEY_NONE && s_key_callback) {
        s_key_callback(return_key);
    }
}
