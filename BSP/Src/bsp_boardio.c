/**
 * @file    bsp_boardio.c
 * @brief   板载 GPIO 器件统一初始化入口 (BSP 层)
 *
 * 薄壳实现 — 仅聚合各 drv_*_init() 调用.
 * 所有器件逻辑已拆分至独立驱动:
 *   - drv_relaysw: 继电器/光耦
 *   - drv_fan:     风扇
 *   - drv_led:     LED
 *   - drv_buzzer:  蜂鸣器
 */
#include "bsp_boardio.h"
#include "comm_logger.h"

/*********************************************************************
 * @fn      bsp_boardio_init
 * @brief   板载 GPIO 器件统一初始化
 * @note    顺序: 继电器/光耦 (先建立安全断电态) → 风扇 → LED → 蜂鸣器
 * @return  none
********************************************************************/
void bsp_boardio_init(void)
{
    drv_relay_init();    /* 含 all_off 安全时序 */
    drv_fan_init();
    drv_led_init();

    logger_print(LOG_INFO, "bsp_boardio: all drivers init");
}
