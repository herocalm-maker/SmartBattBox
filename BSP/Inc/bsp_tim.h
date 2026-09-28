/**
 * @file    bsp_tim.h
 * @brief   TIM 通用外设接口 (BSP 层) — 纯 HAL_TIM 封装
 *
 * 设计目标:
 *   - 只提供基础函数: init/deinit/start/stop
 *   - 通过 BSP_TIM_NUM 枚举索引, 内部维护句柄查找表
 *   - 方便更换芯片或重新映射 TIM
 *
 * 注意:
 *   - HAL_TIM_Base_MspInit/MspDeInit 由 HAL 内部调用, 配置时钟/NVIC
 *   - TIM1 已被 HAL 系统时基占用, 不在本模块管理范围
 *   - bsp_busy_delay_ms 使用 DWT_CYCCNT, 不依赖任何 TIM
 */
#ifndef __BSP_TIM_H__
#define __BSP_TIM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>


typedef enum
{
  BSP_TIM_3 = 0,    /* TIM3 -> 通用定时器 */
  BSP_TIM_4,         /* TIM4 -> 100ms 周期中断 */
  BSP_TIM_MAX,
}BSP_TIM_NUM;


/**
 * @brief  TIM 周期完成回调函数类型
 * @param  htim  TIM 句柄指针
 */
typedef void (*bsp_tim_period_cb_t)(void);

/**
 * @brief  TIM 配置参数
 */
typedef struct {
    uint16_t             Prescaler;   /**< 时钟预分频数 */
    uint16_t             Period;      /**< 自动重装值 */
    bsp_tim_period_cb_t  tim_cb;      /**< 周期回调 (可为 NULL) */
} bsp_tim_config_t;


extern bool bsp_tim_init(BSP_TIM_NUM index, const bsp_tim_config_t *cfg);
extern bool bsp_tim_deinit(BSP_TIM_NUM index);
extern bool bsp_tim_start(BSP_TIM_NUM index);
extern bool bsp_tim_stop(BSP_TIM_NUM index);

extern uint32_t bsp_get_tick_ms(void);
extern void bsp_delay_ms(uint32_t ms);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_TIM_H__ */
