/**
 * @file    bsp_io.c
 * @brief   GPIO 通用外设接口 (BSP 层) — 纯 HAL_GPIO 封装
 *
 * 设计目标:
 *   - 只提供 5 个基础函数: init/deinit/write/read/toggle
 *   - 所有引脚定义在 Board.h (BOARD_*_PORT / BOARD_*_PIN)
 *   - 上层直接传 GPIO_TypeDef* + pin, 无查找表
 *   - 方便更换芯片或重新映射引脚
 */
#include "bsp_io.h"
#include "stm32f4xx_hal.h"

/* 使能指定 GPIO 端口时钟 */
static void bsp_io_enable_port_clk(GPIO_TypeDef *port)
{
    if      (port == GPIOA) __HAL_RCC_GPIOA_CLK_ENABLE();
    else if (port == GPIOB) __HAL_RCC_GPIOB_CLK_ENABLE();
    else if (port == GPIOC) __HAL_RCC_GPIOC_CLK_ENABLE();
    else if (port == GPIOD) __HAL_RCC_GPIOD_CLK_ENABLE();
    else if (port == GPIOE) __HAL_RCC_GPIOE_CLK_ENABLE();
    else if (port == GPIOH) __HAL_RCC_GPIOH_CLK_ENABLE();
}

/* 由引脚号推导 EXTI 中断号 */
static IRQn_Type bsp_io_exti_irqn(uint16_t pin)
{
    switch (pin) {
        case GPIO_PIN_0:  return EXTI0_IRQn;
        case GPIO_PIN_1:  return EXTI1_IRQn;
        case GPIO_PIN_2:  return EXTI2_IRQn;
        case GPIO_PIN_3:  return EXTI3_IRQn;
        case GPIO_PIN_4:  return EXTI4_IRQn;
        case GPIO_PIN_5:
        case GPIO_PIN_6:
        case GPIO_PIN_7:
        case GPIO_PIN_8:
        case GPIO_PIN_9:  return EXTI9_5_IRQn;
        default:          return EXTI15_10_IRQn;
    }
}

/*********************************************************************
 * @fn      bsp_gpio_init
 * @brief   配置 GPIO (输出/输入/中断)
 * @param   port        GPIO 端口 (GPIOA/GPIOB/...)
 * @param   pin         引脚号 (GPIO_PIN_0 ~ GPIO_PIN_15)
 * @param   mode        GPIO_MODE_OUTPUT_PP / GPIO_MODE_INPUT / GPIO_MODE_IT_RISING 等
 * @param   pull        GPIO_NOPULL / GPIO_PULLUP / GPIO_PULLDOWN
 * @param   init_level  初始电平 (输出模式用) GPIO_PIN_SET / GPIO_PIN_RESET
 * @param   speed       GPIO_SPEED_FREQ_LOW / MEDIUM / HIGH / VERY_HIGH
 * @return  none
********************************************************************/
void bsp_gpio_init(GPIO_TypeDef *port, uint16_t pin, uint32_t mode, uint32_t pull,
                   GPIO_PinState init_level, uint32_t speed)
{
    bsp_io_enable_port_clk(port);

    /* 输出模式: 先写初始电平再配模式(防毛刺) */
    if (mode == GPIO_MODE_OUTPUT_PP || mode == GPIO_MODE_OUTPUT_OD) {
        HAL_GPIO_WritePin(port, pin, init_level);
    }

    GPIO_InitTypeDef g = {0};
    g.Pin   = pin;
    g.Mode  = mode;
    g.Pull  = pull;
    g.Speed = speed;
    HAL_GPIO_Init(port, &g);

    /* 中断模式: 按引脚号使能对应 EXTI NVIC */
    if (mode == GPIO_MODE_IT_RISING || mode == GPIO_MODE_IT_FALLING ||
        mode == GPIO_MODE_IT_RISING_FALLING) {
        IRQn_Type irqn = bsp_io_exti_irqn(pin);
        HAL_NVIC_SetPriority(irqn, 5, 0);
        HAL_NVIC_EnableIRQ(irqn);
    }
}

/*********************************************************************
 * @fn      bsp_gpio_deinit
 * @brief   反初始化 GPIO (恢复默认状态)
 * @param   port  GPIO 端口
 * @param   pin   引脚号
 * @return  none
********************************************************************/
void bsp_gpio_deinit(GPIO_TypeDef *port, uint16_t pin)
{
    HAL_GPIO_DeInit(port, pin);
}

/*********************************************************************
 * @fn      bsp_gpio_write
 * @brief   写 GPIO 电平
 * @param   port   GPIO 端口
 * @param   pin    引脚号
 * @param   level  GPIO_PIN_SET / GPIO_PIN_RESET
 * @return  none
********************************************************************/
void bsp_gpio_write(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
    HAL_GPIO_WritePin(port, pin, level);
}

/*********************************************************************
 * @fn      bsp_gpio_read
 * @brief   读 GPIO 电平
 * @param   port  GPIO 端口
 * @param   pin   引脚号
 * @return  GPIO_PinState (GPIO_PIN_SET / GPIO_PIN_RESET)
********************************************************************/
GPIO_PinState bsp_gpio_read(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin);
}

/*********************************************************************
 * @fn      bsp_gpio_toggle
 * @brief   翻转 GPIO 电平
 * @param   port  GPIO 端口
 * @param   pin   引脚号
 * @return  none
********************************************************************/
void bsp_gpio_toggle(GPIO_TypeDef *port, uint16_t pin)
{
    HAL_GPIO_TogglePin(port, pin);
}
