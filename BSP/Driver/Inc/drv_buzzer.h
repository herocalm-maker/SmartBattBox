/**
 * @file    drv_buzzer.h
 * @brief   蜂鸣器驱动接口 (Driver 层)
 *
 * 有源蜂鸣器, 支持:
 *   - 短鸣: N 次间歇鸣叫 (可配置次数)
 *   - 长鸣: 持续鸣叫指定毫秒数
 *   - 停止: 立即静音
 *
 * @note 需周期调用 drv_buzzer_tick() (当前 TIM4 = 50ms).
 *       不依赖专用硬件定时器, 纯软件状态机.
 *       引脚定义见 Board.h.
 */
#ifndef __DRV_BUZZER_H__
#define __DRV_BUZZER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>



/* 默认配置 (单位: tick, 1 tick = 50ms) */
#define BEEP_TICK_PERIOD_MS   50    /* TIM4 周期 50ms */
#define BEEP_SHORT_ON_TICK    5     /* 普通短鸣响 250ms */
#define BEEP_SHORT_OFF_TICK   5     /* 普通短鸣间隔 250ms */



/* 蜂鸣器声音类型 */
typedef enum {
    BEEP_SOUND_NONE = 0,
    BEEP_SOUND_SHORT,      /* 短响 */
    BEEP_SOUND_LONG,       /* 长响 */
} BEEP_SOUND_TYPE;

/* 蜂鸣器数据结构 (参考 GPS 项目 BeepData) */
typedef struct {
    BEEP_SOUND_TYPE SoundType;
    bool  ShortState;      /* 短鸣当前状态: true=响, false=间隔 */
    uint8_t  ShortNumber;  /* 短鸣剩余次数 */
    uint32_t LongTimeMs;   /* 长鸣总时长 (ms) */
} BEEP_DATA_t;



extern void drv_buzzer_init(void);
extern bool drv_buzzer_short_sound(uint8_t count);
/* Rejected ACIN command: three quicker beeps, 150ms on / 150ms off. */
extern bool drv_buzzer_reject_sound(void);
extern bool drv_buzzer_long_sound(uint32_t duration_ms);
extern bool drv_buzzer_stop(void);
extern bool drv_buzzer_is_busy(void);
extern void drv_buzzer_tick(void);  /* 周期调用, 当前 TIM4 = 50ms */

#ifdef __cplusplus
}
#endif
#endif /* __DRV_BUZZER_H__ */
