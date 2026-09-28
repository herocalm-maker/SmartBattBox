/**
 * @file    bsp_io.h
 * @brief   GPIO 通用外设接口 (BSP 层) — 纯 HAL_GPIO 封装
 *
 * 设计目标:
 *   - 只提供 4 个基础函数: init/deinit/write/read
 *   - 所有引脚定义在 Board.h (BOARD_*_PORT / BOARD_*_PIN)
 *   - 上层直接传 GPIO_TypeDef* + pin, 无查找表
 *   - 方便更换芯片或重新映射引脚
 */
#ifndef __BSP_IO_H__
#define __BSP_IO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  配置 GPIO
 * @param  port   GPIO 端口 (GPIOA/GPIOB/...)
 * @param  pin    引脚号 (GPIO_PIN_0 ~ GPIO_PIN_15)
 * @param  mode   GPIO_MODE_OUTPUT_PP / GPIO_MODE_INPUT / GPIO_MODE_IT_RISING 等
 * @param  pull   GPIO_NOPULL / GPIO_PULLUP / GPIO_PULLDOWN
 * @param  init_level  初始电平 (输出模式用) GPIO_PIN_SET / GPIO_PIN_RESET
 * @param  speed  GPIO_SPEED_FREQ_LOW/MEDIUM/HIGH/VERY_HIGH
 */
void bsp_gpio_init(GPIO_TypeDef *port, uint16_t pin, uint32_t mode, uint32_t pull,
                   GPIO_PinState init_level, uint32_t speed);

/**
 * @brief  反初始化 GPIO (恢复默认状态)
 * @param  port  GPIO 端口
 * @param  pin   引脚号
 */
extern void bsp_gpio_deinit(GPIO_TypeDef *port, uint16_t pin);

/**
 * @brief  写 GPIO
 * @param  port   GPIO 端口
 * @param  pin    引脚号
 * @param  level  GPIO_PIN_SET / GPIO_PIN_RESET
 */
extern void bsp_gpio_write(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level);

/**
 * @brief  读 GPIO
 * @param  port  GPIO 端口
 * @param  pin   引脚号
 * @return GPIO_PinState (GPIO_PIN_SET / GPIO_PIN_RESET)
 */
extern GPIO_PinState bsp_gpio_read(GPIO_TypeDef *port, uint16_t pin);

/**
 * @brief  翻转 GPIO
 * @param  port  GPIO 端口
 * @param  pin   引脚号
 */
extern void bsp_gpio_toggle(GPIO_TypeDef *port, uint16_t pin);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_IO_H__ */
