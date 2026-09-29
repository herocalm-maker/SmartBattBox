/**
 * @file    mod_acswitch.c
 * @brief   能量通路对象实现 (Module 层)
 *
 * drv_relay_init / drv_fan_init 已由 bsp_boardio_init 统一调用, 本模块不重复初始化.
 */
#include "mod_acswitch.h"
#include "drv_relaysw.h"
#include "drv_fan.h"

bool mod_acswitch_relay_on(relay_id_enum id)
{
    return drv_relay_on(id);
}

bool mod_acswitch_relay_off(relay_id_enum id)
{
    return drv_relay_off(id);
}

bool mod_acswitch_relay_is_on(relay_id_enum id)
{
    return drv_relay_is_on(id);
}

void mod_acswitch_relay_all_off(void)
{
    drv_relay_all_off();
}

void mod_acswitch_set_fan(fan_id_enum id, bool on)
{
    if (on)
    {
        drv_fan_on(id);
    }
    else
    {
        drv_fan_off(id);
    }
}

bool mod_acswitch_path_charges(energy_path_t path)
{
    return path == PATH_CHARGE || path == PATH_CHARGE_BYPASS;
}

bool mod_acswitch_path_needs_ac(energy_path_t path)
{
    return path == PATH_BYPASS || mod_acswitch_path_charges(path);
}
