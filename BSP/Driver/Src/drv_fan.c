/**
 * @file    drv_fan.c
 * @brief   散热风扇驱动实现 (Driver 层)
 *
 * 2 路风扇, 仅通断控制 (无 PWM).
 */
#include "drv_fan.h"
#include "bsp_io.h"
#include "Board.h"
#include "comm_logger.h"

/*********************************************************************
 * @fn      drv_fan_init
 * @brief   初始化风扇控制引脚 (推挽输出, 默认关闭)
 * @return  none
********************************************************************/
void drv_fan_init(void)
{
    bsp_gpio_init(BOARD_FAN1_CTRL_PORT, BOARD_FAN1_CTRL_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_FAN2_CTRL_PORT, BOARD_FAN2_CTRL_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);

    drv_fan_all_off();
    logger_out(LOG_INFO, "drv_fan: init");
}

/*********************************************************************
 * @fn      drv_fan_on
 * @brief   打开指定风扇 (全速)
 * @param   id  风扇 ID (FAN_MAIN / FAN_AUX)
 * @return  none
********************************************************************/
void drv_fan_on(fan_id_enum id)
{
    switch (id)
    {
        case FAN_MAIN:
            bsp_gpio_write(BOARD_FAN1_CTRL_PORT, BOARD_FAN1_CTRL_PIN, GPIO_PIN_SET);
            break;
        case FAN_AUX:
            bsp_gpio_write(BOARD_FAN2_CTRL_PORT, BOARD_FAN2_CTRL_PIN, GPIO_PIN_SET);
            break;
        default:
            break;
    }
}

/*********************************************************************
 * @fn      drv_fan_off
 * @brief   关闭指定风扇
 * @param   id  风扇 ID
 * @return  none
********************************************************************/
void drv_fan_off(fan_id_enum id)
{
    switch (id)
    {
        case FAN_MAIN:
            bsp_gpio_write(BOARD_FAN1_CTRL_PORT, BOARD_FAN1_CTRL_PIN, GPIO_PIN_RESET);
            break;
        case FAN_AUX:
            bsp_gpio_write(BOARD_FAN2_CTRL_PORT, BOARD_FAN2_CTRL_PIN, GPIO_PIN_RESET);
            break;
        default:
            break;
    }
}

/*********************************************************************
 * @fn      drv_fan_all_off
 * @brief   关闭全部风扇
 * @return  none
********************************************************************/
void drv_fan_all_off(void)
{
    bsp_gpio_write(BOARD_FAN1_CTRL_PORT, BOARD_FAN1_CTRL_PIN, GPIO_PIN_RESET);
    bsp_gpio_write(BOARD_FAN2_CTRL_PORT, BOARD_FAN2_CTRL_PIN, GPIO_PIN_RESET);
}



