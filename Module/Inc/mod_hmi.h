/**
 * @file    mod_hmi.h
 * @brief   提示 / 指示薄对象 (Module 层)
 *
 * 封装 ALM 报警灯 + 蜂鸣器, 供 App 层统一调用, 避免 App 直连 BSP 驱动.
 * 私有: drv_led (ALM) + drv_buzzer.
 * 电池 12 灯由 SensorTask 经 mod_battery 管理, 与本模块无关.
 */
#ifndef MOD_HMI_H
#define MOD_HMI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "drv_led.h"   /* led_id_enum */

void mod_hmi_init(void);
void mod_hmi_alarm(bool on);      /* ALM 报警灯亮/灭 */
void mod_hmi_buzz(uint8_t times); /* 蜂鸣短响 N 次 */
void mod_hmi_buzz_reject(void);   /* 拒绝提示音 */
void mod_hmi_led(led_id_enum id, bool on); /* 通用工作灯控制 */

#ifdef __cplusplus
}
#endif
#endif /* MOD_HMI_H */
