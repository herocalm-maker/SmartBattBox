/**
 * @file    mod_acswitch.h
 * @brief   能量通路对象 (Module 层)
 *
 * 封装继电器 + 散热风扇访问, 供 App 层统一调用, 避免 App 直连 BSP 驱动.
 * 私有: drv_relaysw + drv_fan.
 *
 * 说明: energy_path_t 通路类型保留在 Core/Inc/main.h (全局类型头), 本模块
 *       include main.h 获得该类型, 并迁入 path_charges/path_needs_ac 判定辅助.
 *       通路切换的互锁时序仍由 AppMainTask 编排 (保留已验证逻辑).
 */
#ifndef MOD_ACSWITCH_H
#define MOD_ACSWITCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "drv_relaysw.h"
#include "drv_fan.h"
#include "main.h"   /* energy_path_t */

bool mod_acswitch_init(void);

/* ---- 继电器 ---- */
bool mod_acswitch_relay_on(relay_id_enum id);
bool mod_acswitch_relay_off(relay_id_enum id);
bool mod_acswitch_relay_is_on(relay_id_enum id);
void mod_acswitch_relay_all_off(void);

/* ---- 风扇 ---- */
void mod_acswitch_set_fan(fan_id_enum id, bool on);

/* ---- 通路判定辅助 (自 app_main_task 迁入) ---- */
bool mod_acswitch_path_charges(energy_path_t path);
bool mod_acswitch_path_needs_ac(energy_path_t path);

#ifdef __cplusplus
}
#endif
#endif /* MOD_ACSWITCH_H */
