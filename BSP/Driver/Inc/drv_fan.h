/**
 * @file    drv_fan.h
 * @brief   散热风扇驱动接口 (Driver 层)
 *
 * 2 路风扇, 仅通断控制 (无 PWM).
 *   - FAN_MAIN: 主散热 (逆变器/电池仓)
 *   - FAN_AUX:  辅散热 (充电器/电源仓)
 *
 * @note 引脚定义见 Board.h
 */
#ifndef __DRV_FAN_H__
#define __DRV_FAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* 风扇 ID 枚举 */
typedef enum {
    FAN_MAIN = 0,   /* 风扇1 (主散热) */
    FAN_AUX  = 1,   /* 风扇2 (辅散热) */
    FAN_CNT
} fan_id_enum;

extern void drv_fan_init(void);
extern void drv_fan_on(fan_id_enum id);
extern void drv_fan_off(fan_id_enum id);
extern void drv_fan_all_off(void);

#ifdef __cplusplus
}
#endif
#endif /* __DRV_FAN_H__ */
