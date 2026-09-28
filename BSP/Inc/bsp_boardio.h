/**
 * @file    bsp_boardio.h
 * @brief   板载 GPIO 器件统一接口 (BSP 层)
 *
 * 薄壳头文件 — 聚合所有板载 GPIO 器件驱动:
 *   - drv_relaysw: 6 路继电器/光耦 (含安全断电时序)
 *   - drv_fan:     2 路散热风扇 (仅通断)
 *   - drv_led:     3 路直连 LED
 *   - drv_buzzer:  蜂鸣器 (50ms tick 时序)
 *
 * @note 初始化统一入口 bsp_boardio_init().
 */
#ifndef __BSP_BOARDIO_H__
#define __BSP_BOARDIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "drv_relaysw.h"   /* 继电器/光耦 */
#include "drv_fan.h"       /* 风扇 */
#include "drv_led.h"       /* LED */
#include "drv_buzzer.h"    /* 蜂鸣器 */

/*********************************************************************
 * @fn      bsp_boardio_init
 * @brief   板载 GPIO 器件统一初始化
 * @note    顺序: 继电器/光耦 (安全断电态) → 风扇 → LED → 蜂鸣器
 * @return  none
********************************************************************/
extern void bsp_boardio_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_BOARDIO_H__ */
