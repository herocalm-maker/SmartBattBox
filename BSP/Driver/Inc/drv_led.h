/**
 * @file    drv_led.h
 * @brief   LED 驱动接口 (Driver 层)
 *
 * 3 路直连 LED:
 *   - LED_SIG:  信号指示 (绿色)
 *   - LED_SIG2: 信号指示 (蓝色)
 *   - LED_ALM:  报警指示 (红色)
 *
 * @note 引脚定义见 Board.h
 */
#ifndef __DRV_LED_H__
#define __DRV_LED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* LED ID 枚举 */
typedef enum {
   LED_SIG  = 0,   /* LED1 信号指示（绿色） */
   LED_SIG2 = 1,   /* LED2 信号指示（蓝色） */
   LED_ALM  = 2,   /* 报警指示灯（红色） */
   LED_CNT
} led_id_enum;

extern void drv_led_init(void);
extern void drv_led_on(led_id_enum id);
extern void drv_led_off(led_id_enum id);
extern void drv_led_toggle(led_id_enum id);

#ifdef __cplusplus
}
#endif
#endif /* __DRV_LED_H__ */
