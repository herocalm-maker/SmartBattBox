/**
 * @file    drv_relaysw.c
 * @brief   继电器/光耦驱动实现 (Driver 层)
 *
 * 6 路开关控制 (5 继电器 + 1 光耦), 含安全断电时序.
 *
 * 器件一览 (引脚见 Board.h):
 *   继电器: CHG_IN / CHG_OUT / AC_CTRL / INV_OUT / INV_PRO (位图维护状态)
 *   光耦:   INV_IN (逆变器输入光耦)
 */
#include "drv_relaysw.h"
#include "bsp_io.h"
#include "Board.h"
#include "comm_logger.h"

/* =====================================================================
 *  内部状态
 * ===================================================================== */
static uint8_t s_relay_state;   /* 位图: bit i = relay i 是否吸合 */

/*********************************************************************
 * @fn      drv_relay_init
 * @brief   初始化继电器/光耦控制引脚 (推挽输出, 默认全部断开)
 * @return  none
********************************************************************/
void drv_relay_init(void)
{
    bsp_gpio_init(BOARD_INV_OUT_PORT, BOARD_INV_OUT_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,  GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_AC_CTRL_PORT, BOARD_AC_CTRL_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,  GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_CHG_IN_PORT, BOARD_CHG_IN_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,   GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_CHG_OUT_PORT, BOARD_CHG_OUT_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,  GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_INV_PRO_PORT, BOARD_INV_PRO_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,  GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_INV_IN_PORT, BOARD_INV_IN_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,    GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);

    s_relay_state = 0;
    drv_relay_all_off();
    logger_out(LOG_INFO, "drv_relaysw: init (6ch: 5 relay + 1 opto)");
}

/*********************************************************************
 * @fn      drv_relay_on
 * @brief   吸合指定继电器
 * @param   id  继电器 ID
 * @return  bool true=成功 false=ID 无效
********************************************************************/
bool drv_relay_on(relay_id_enum id)
{
    if ((unsigned)id >= RELAY_CNT)
    {
        return false;
    }
    /* Only AppMainTask sequences these relays; command bits are not feedback. */
    if ((id == RELAY_AC_CTRL || id == RELAY_CHG_IN || id == RELAY_CHG_OUT) &&
        (drv_relay_is_on(RELAY_INV_OUT) || drv_relay_is_on(RELAY_INV_PRO) ||
         drv_relay_is_on(RELAY_INV_IN)))
    {
        return false;
    }
    if ((id == RELAY_INV_OUT || id == RELAY_INV_PRO || id == RELAY_INV_IN) &&
        (drv_relay_is_on(RELAY_AC_CTRL) || drv_relay_is_on(RELAY_CHG_IN) ||
         drv_relay_is_on(RELAY_CHG_OUT)))
    {
        return false;
    }
    /* DC supply is commanded PD4 then PD7; both remain on during inversion.
     * AppMainTask owns the stabilization delay before PD0. */
    if (id == RELAY_INV_IN && !drv_relay_is_on(RELAY_INV_PRO))
    {
        return false;
    }
    if (id == RELAY_INV_OUT && (!drv_relay_is_on(RELAY_INV_IN) ||
                               !drv_relay_is_on(RELAY_INV_PRO)))
    {
        return false;
    }
    if (id == RELAY_CHG_OUT && !drv_relay_is_on(RELAY_CHG_IN))
    {
        return false;
    }
    switch (id)
    {
        case RELAY_CHG_IN:
            bsp_gpio_write(BOARD_CHG_IN_PORT, BOARD_CHG_IN_PIN, GPIO_PIN_SET);
            break;
        case RELAY_CHG_OUT:
            bsp_gpio_write(BOARD_CHG_OUT_PORT, BOARD_CHG_OUT_PIN, GPIO_PIN_SET);
            break;
        case RELAY_AC_CTRL:
            bsp_gpio_write(BOARD_AC_CTRL_PORT, BOARD_AC_CTRL_PIN, GPIO_PIN_SET);
            break;
        case RELAY_INV_OUT:
            bsp_gpio_write(BOARD_INV_OUT_PORT, BOARD_INV_OUT_PIN, GPIO_PIN_SET);
            break;
        case RELAY_INV_PRO:
            bsp_gpio_write(BOARD_INV_PRO_PORT, BOARD_INV_PRO_PIN, GPIO_PIN_SET);
            break;
        case RELAY_INV_IN:
            bsp_gpio_write(BOARD_INV_IN_PORT, BOARD_INV_IN_PIN, GPIO_PIN_SET);
            break;
        default:
            return false;
    }
    s_relay_state |= (1u << id);
    return true;
}

/*********************************************************************
 * @fn      drv_relay_off
 * @brief   断开指定继电器
 * @param   id  继电器 ID
 * @return  bool true=成功 false=ID 无效
********************************************************************/
bool drv_relay_off(relay_id_enum id)
{
    if ((unsigned)id >= RELAY_CNT)
    {
        return false;
    }
    switch (id)
    {
        case RELAY_CHG_IN:
            bsp_gpio_write(BOARD_CHG_IN_PORT, BOARD_CHG_IN_PIN, GPIO_PIN_RESET);
            break;
        case RELAY_CHG_OUT:
            bsp_gpio_write(BOARD_CHG_OUT_PORT, BOARD_CHG_OUT_PIN, GPIO_PIN_RESET);
            break;
        case RELAY_AC_CTRL:
            bsp_gpio_write(BOARD_AC_CTRL_PORT, BOARD_AC_CTRL_PIN, GPIO_PIN_RESET);
            break;
        case RELAY_INV_OUT:
            bsp_gpio_write(BOARD_INV_OUT_PORT, BOARD_INV_OUT_PIN, GPIO_PIN_RESET);
            break;
        case RELAY_INV_PRO:
            bsp_gpio_write(BOARD_INV_PRO_PORT, BOARD_INV_PRO_PIN, GPIO_PIN_RESET);
            break;
        case RELAY_INV_IN:
            bsp_gpio_write(BOARD_INV_IN_PORT, BOARD_INV_IN_PIN, GPIO_PIN_RESET);
            break;
        default:
            return false;
    }
    s_relay_state &= ~(1u << id);
    return true;
}

/*********************************************************************
 * @fn      drv_relay_is_on
 * @brief   查询指定继电器当前状态
 * @param   id  继电器 ID
 * @return  bool true=吸合 false=断开; ID 无效返回 false
********************************************************************/
bool drv_relay_is_on(relay_id_enum id)
{
    if ((unsigned)id >= RELAY_CNT)
    {
        return false;
    }
    return (s_relay_state & (1u << id)) != 0;
}

/* Emergency off: remove AC output commands first. Normal release delays
 * are managed by AppMainTask; GPIO state is not contact feedback. */
void drv_relay_all_off(void)
{
    bsp_gpio_write(BOARD_INV_OUT_PORT, BOARD_INV_OUT_PIN, GPIO_PIN_RESET);
    bsp_gpio_write(BOARD_AC_CTRL_PORT, BOARD_AC_CTRL_PIN, GPIO_PIN_RESET);
    bsp_gpio_write(BOARD_CHG_OUT_PORT, BOARD_CHG_OUT_PIN, GPIO_PIN_RESET);
    bsp_gpio_write(BOARD_INV_PRO_PORT, BOARD_INV_PRO_PIN, GPIO_PIN_RESET);
    bsp_gpio_write(BOARD_INV_IN_PORT, BOARD_INV_IN_PIN, GPIO_PIN_RESET);
    bsp_gpio_write(BOARD_CHG_IN_PORT, BOARD_CHG_IN_PIN, GPIO_PIN_RESET);
    s_relay_state = 0;
}
