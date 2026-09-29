/**
 * @file    mod_hmi.c
 * @brief   提示 / 指示薄对象实现 (Module 层)
 *
 * drv_led_init / drv_buzzer_init 已在 main.c 统一调用, 本模块不再重复初始化.
 */
#include "mod_hmi.h"
#include "drv_buzzer.h"
#include "drv_led.h"

void mod_hmi_alarm(bool on)
{
    if (on)
    {
        drv_led_on(LED_ALM);
    }
    else
    {
        drv_led_off(LED_ALM);
    }
}

void mod_hmi_buzz(uint8_t times)
{
    if (times > 0)
    {
        drv_buzzer_short_sound(times);
    }
}

void mod_hmi_buzz_reject(void)
{
    drv_buzzer_reject_sound();
}

void mod_hmi_led(led_id_enum id, bool on)
{
    if (on)
    {
        drv_led_on(id);
    }
    else
    {
        drv_led_off(id);
    }
}
