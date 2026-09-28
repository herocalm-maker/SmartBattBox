/**
 * @file    mod_key.c
 * @brief   按键管理模块 (Module 层)
 *
 * 按下确认制状态机 (20ms 轮询):
 *   IDLE → CONFIRM → PRESSED → LONG
 *   - CONFIRM: 连续 KEY_DEBOUNCE_N 次采样一致 (~40ms) 立即上报短按, 不等释放
 *   - PRESSED: 按住达到 长按阈值 (ONOFF 3000ms，其他键800ms) 上报一次长按
 *   - LONG:    只等待释放, 不连发
 *
 * 组合键: 按下键数变化时重新去抖, 同一手势内已上报过的按键不重复上报,
 *         避免组合键松开一半时误触发短按
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
#define KEY_ONOFF_LONG_MS    3000u
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

/*--------------------------------------------------------------------
 * 内部: 读取所有按键原始状态 (位掩码)
 *--------------------------------------------------------------------*/
static uint8_t _read_all_keys(void)
{
    uint8_t mask = 0;
    for (int i = 0; i < KEY_IDX_CNT; i++) {
        if (bsp_gpio_read(s_pin_map[i].port, s_pin_map[i].pin) == 0) {
            mask |= s_pin_map[i].key_value;
        }
    }
    return mask;
}

/*====================================================================
 * API
 *====================================================================*/

bool mod_key_init(void)
{
    /* GPIO 初始化 (纯输入, EXTI 未使用) */
    bsp_gpio_init(BOARD_ONOFF_KEY_PORT, BOARD_ONOFF_KEY_PIN,
                  GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_ACIN_KEY_PORT, BOARD_ACIN_KEY_PIN,
                  GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_ACOUT_KEY_PORT, BOARD_ACOUT_KEY_PIN,
                  GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_K1_PORT, BOARD_K1_PIN,
                  GPIO_MODE_INPUT, GPIO_PULLUP, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);

    /* 清零状态 */
    memset((void *)&s_key, 0, sizeof(s_key));
    s_key_callback = NULL;

    logger_out(LOG_INFO, "mod_key: init OK");
    return true;
}

void mod_key_register_callback(mod_key_callback_t cb)
{
    s_key_callback = cb;
}

/*--------------------------------------------------------------------
 * 轮询 (20ms) - 按下确认制状态机
 *--------------------------------------------------------------------*/
void mod_key_poll_handler(void)
{
    uint8_t return_key = KEY_NONE;
    uint8_t cur_key_val = _read_all_keys();

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
            /* 释放 */
            s_key.key_state = KEY_STATE_IDLE;
            s_key.reported_mask = KEY_NONE;
        } 
        else if (cur_key_val != s_key.sample_mask) 
        {
            /* 按下键数变化, 重新去抖 (不重复上报已报过的键) */
            s_key.sample_mask = cur_key_val;
            s_key.pressed_since_ms = HAL_GetTick();
            s_key.hold_cnt = 0;
            s_key.key_state = KEY_STATE_CONFIRM;
        } 
        else if ((uint32_t)(HAL_GetTick() - s_key.pressed_since_ms) >=
                 (s_key.sample_mask == KEY_VALUE_ONOFF ? KEY_ONOFF_LONG_MS : KEY_LONG_MS)) 
        {
            /* 长按确认: 只上报一次, 不连发 */
            s_key.hold_cnt = 0;
            s_key.key_state = KEY_STATE_LONG;
            return_key = KEY_LONG_MARK | s_key.sample_mask;
        }
        break;

      case KEY_STATE_LONG:
        if (cur_key_val == KEY_NONE) 
        {
            /* 释放 */
            s_key.key_state = KEY_STATE_IDLE;
            s_key.reported_mask = KEY_NONE;
        } 
        else if (cur_key_val != s_key.sample_mask) 
        {
            /* 按下键数变化, 重新去抖 */
            s_key.sample_mask = cur_key_val;
            s_key.pressed_since_ms = HAL_GetTick();
            s_key.hold_cnt = 0;
            s_key.key_state = KEY_STATE_CONFIRM;
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
