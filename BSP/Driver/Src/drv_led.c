/**
 * @file    drv_led.c
 * @brief   LED 驱动实现 (Driver 层)
 *
 * PC4/PC5 active-low; PD14 alarm active-high. Push-pull outputs.
 */
#include "drv_led.h"
#include "bsp_io.h"
#include "Board.h"
#include "comm_logger.h"

/*********************************************************************
 * @fn      drv_led_init
 * @brief   初始化 LED 控制引脚 (推挽输出, 默认全灭)
 * @return  none
********************************************************************/
void drv_led_init(void)
{
    bsp_gpio_init(BOARD_LED1_PORT, BOARD_LED1_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,      GPIO_PIN_SET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_LED2_PORT, BOARD_LED2_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,      GPIO_PIN_SET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_ALARM_LED_PORT, BOARD_ALARM_LED_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);

    drv_led_off(LED_SIG);
    drv_led_off(LED_SIG2);
    drv_led_off(LED_ALM);
    logger_out(LOG_INFO, "drv_led: init");
}

/*********************************************************************
 * @fn      drv_led_on
 * @brief   点亮指定 LED
 * @param   id  LED ID
 * @return  none
********************************************************************/
void drv_led_on(led_id_enum id)
{
    switch (id) {
        case LED_SIG:  bsp_gpio_write(BOARD_LED1_PORT, BOARD_LED1_PIN, GPIO_PIN_RESET);        break;
        case LED_SIG2: bsp_gpio_write(BOARD_LED2_PORT, BOARD_LED2_PIN, GPIO_PIN_RESET);        break;
        case LED_ALM:  bsp_gpio_write(BOARD_ALARM_LED_PORT, BOARD_ALARM_LED_PIN, GPIO_PIN_SET);   break;
        default: break;
    }
}

/*********************************************************************
 * @fn      drv_led_off
 * @brief   熄灭指定 LED
 * @param   id  LED ID
 * @return  none
********************************************************************/
void drv_led_off(led_id_enum id)
{
    switch (id) {
        case LED_SIG:  bsp_gpio_write(BOARD_LED1_PORT, BOARD_LED1_PIN, GPIO_PIN_SET);        break;
        case LED_SIG2: bsp_gpio_write(BOARD_LED2_PORT, BOARD_LED2_PIN, GPIO_PIN_SET);        break;
        case LED_ALM:  bsp_gpio_write(BOARD_ALARM_LED_PORT, BOARD_ALARM_LED_PIN, GPIO_PIN_RESET);   break;
        default: break;
    }
}

/*********************************************************************
 * @fn      drv_led_toggle
 * @brief   翻转指定 LED 状态
 * @param   id  LED ID
 * @return  none
********************************************************************/
void drv_led_toggle(led_id_enum id)
{
    switch (id) 
    {
        case LED_SIG:  
            bsp_gpio_toggle(BOARD_LED1_PORT, BOARD_LED1_PIN);        
            break;
        case LED_SIG2: 
            bsp_gpio_toggle(BOARD_LED2_PORT, BOARD_LED2_PIN);        
            break;
        case LED_ALM:  
            bsp_gpio_toggle(BOARD_ALARM_LED_PORT, BOARD_ALARM_LED_PIN);   
            break;
        default: 
            break;
    }
}




