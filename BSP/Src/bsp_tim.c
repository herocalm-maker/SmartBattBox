/**
 * @file    bsp_tim.c
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
 *   - bsp_busy_delay_ms 内部使用 HAL_Delay, 依赖 uwTick
 */
#include "bsp_tim.h"
#include "stm32f4xx_hal.h"

/* =====================================================================
 *  TIM 句柄定义 (全局, 供 IRQ handler 和上层使用)
 * ===================================================================== */
TIM_HandleTypeDef s_htim3;
TIM_HandleTypeDef s_htim4;


static TIM_HandleTypeDef *s_tim_list[BSP_TIM_MAX] = {&s_htim3, &s_htim4};

typedef struct {
    bsp_tim_period_cb_t tim_cb;
} tim_cb_entry_t;

static tim_cb_entry_t s_tim_cb_map[BSP_TIM_MAX] = {0};

/* =====================================================================
 *  HAL MSP 回调 (由 HAL 内部调用)
 * ===================================================================== */
/**
 * @fn      HAL_TIM_Base_MspInit
 * @brief   TIM3/TIM4 底层初始化 (HAL 回调): 使能定时器时钟并配置 NVIC 中断
 * @param   tim_baseHandle  TIM 句柄 (由 HAL_TIM_Base_Init 内部传入, Instance 已设置)
 * @return  无
 * @note    抢占优先级均为 5 (RTOS 允许的中断优先级范围);
 *          TIM4 子优先级更低 (看门狗监控 tick, 让位给 TIM3 的业务周期中断).
 */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *tim_baseHandle)
{
    if (tim_baseHandle->Instance == TIM3)
    {
        /* TIM3 clock enable */
        __HAL_RCC_TIM3_CLK_ENABLE();

        /* TIM3 interrupt Init */
        HAL_NVIC_SetPriority(TIM3_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(TIM3_IRQn);
    }
    else if (tim_baseHandle->Instance == TIM4)
    {
        /* TIM4 clock enable */
        __HAL_RCC_TIM4_CLK_ENABLE();

        /* TIM4 interrupt Init */
        HAL_NVIC_SetPriority(TIM4_IRQn, 5, 1);
        HAL_NVIC_EnableIRQ(TIM4_IRQn);
    }
}

/**
 * @fn      HAL_TIM_Base_MspDeInit
 * @brief   TIM3/TIM4 底层反初始化 (HAL 回调): 关外设时钟并关闭 NVIC 中断
 * @param   tim_baseHandle  TIM 句柄
 * @return  无
 */
void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef *tim_baseHandle)
{
    if (tim_baseHandle->Instance == TIM3)
    {
        /* Peripheral clock disable */
        __HAL_RCC_TIM3_CLK_DISABLE();

         /* TIM3 interrupt Deinit */
        HAL_NVIC_DisableIRQ(TIM3_IRQn);
    }
    else if (tim_baseHandle->Instance == TIM4)
    {
        /* Peripheral clock disable */
        __HAL_RCC_TIM4_CLK_DISABLE();

         /* TIM4 interrupt Deinit */
        HAL_NVIC_DisableIRQ(TIM4_IRQn);
    }
}

/* =====================================================================
 *  TIM4 IRQ Handler (由 NVIC 调用)
 * ===================================================================== */
/**
 * @fn      TIM3_IRQHandler
 * @brief   TIM3 中断入口, 转发 HAL 统一处理 (溢出事件最终进入 HAL_TIM_PeriodElapsedCallback)
 * @param   无
 * @return  无
 */
void TIM3_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&s_htim3);
}

/**
 * @fn      TIM4_IRQHandler
 * @brief   TIM4 中断入口, 转发 HAL 统一处理 (溢出事件最终进入 HAL_TIM_PeriodElapsedCallback)
 * @param   无
 * @return  无
 */
void TIM4_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&s_htim4);
}


/* =====================================================================
 *  HAL 周期完成回调 (由 HAL 内部调用)
 *  注: TIM1 (HAL 系统时基, HAL_IncTick) 由 stm32f4xx_hal_timebase_tim.c 处理
 * ===================================================================== */
/**
 * @fn      HAL_TIM_PeriodElapsedCallback
 * @brief   定时器溢出回调分发: TIM1 为系统时基, TIM3/TIM4 转发用户注册的周期回调
 * @param   htim  发生溢出的 TIM 句柄
 * @return  无
 * @note    本回调在 ISR 上下文执行, 注册的用户回调须短小且不可阻塞.
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1)
    {
       HAL_IncTick();
    }
    else if (htim->Instance == TIM3)
    {
       if(s_tim_cb_map[BSP_TIM_3].tim_cb)
       {
         s_tim_cb_map[BSP_TIM_3].tim_cb();
       }
    }
    else if (htim->Instance == TIM4)
    {
       if(s_tim_cb_map[BSP_TIM_4].tim_cb)
       {
         s_tim_cb_map[BSP_TIM_4].tim_cb();
       }
    }
}




/*********************************************************************
 * @fn      bsp_tim_init
 * @brief   初始化 TIM 外设 (不启动计数, 需调用 bsp_tim_start 启动)
 * @param   index  TIM 端口枚举 (BSP_TIM_3 / BSP_TIM_4)
 * @param   cfg    配置参数 (Prescaler + Period + 回调)
 * @return  bool true=成功 false=失败
 * @note    溢出时间: Tout = ((arr+1) * (psc+1)) / Ft
 *          Ft = 定时器时钟频率, APB1 域 TIM3/TIM4 = 84MHz
********************************************************************/
bool bsp_tim_init(BSP_TIM_NUM index, const bsp_tim_config_t *cfg)
{
    if((index >= BSP_TIM_MAX)||
       (cfg == NULL))
    {
      return false;
    }

    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};
    TIM_HandleTypeDef *htim = NULL;

    htim = s_tim_list[index];
    s_tim_cb_map[index].tim_cb = cfg->tim_cb;

    /* 设置定时器实例 (HAL 需要知道操作哪个定时器) */
    if (index == BSP_TIM_3) {
        htim->Instance = TIM3;
    } else if (index == BSP_TIM_4) {
        htim->Instance = TIM4;
    }

    htim->Init.Prescaler     = cfg->Prescaler;
    htim->Init.CounterMode   = TIM_COUNTERMODE_UP;
    htim->Init.Period        = cfg->Period;
    htim->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;

    if (HAL_TIM_Base_Init(htim) != HAL_OK) {   /* 内部回调 HAL_TIM_Base_MspInit */
        return false;
    }
    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    if (HAL_TIM_ConfigClockSource(htim, &sClockSourceConfig) != HAL_OK) {
        return false;
    }
    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if (HAL_TIMEx_MasterConfigSynchronization(htim, &sMasterConfig) != HAL_OK) {
        return false;
    }

    /* 不在此处启动, 由调用方显式调用 bsp_tim_start() */

    return true;
}

/*********************************************************************
 * @fn      bsp_tim_deinit
 * @brief   反初始化 TIM 外设 (停止计数 + 释放资源)
 * @param   htim   TIM 句柄指针
 * @return  bool true=成功 false=失败
********************************************************************/
bool bsp_tim_deinit(BSP_TIM_NUM index)
{
    if(index >= BSP_TIM_MAX) 
    {
      return false;
    }

    TIM_HandleTypeDef *htim = NULL;

    htim = s_tim_list[index];

    /* 停止定时器 (如果正在运行) */
    HAL_TIM_Base_Stop_IT(htim);

    /* 反初始化 */
    if (HAL_TIM_Base_DeInit(htim) != HAL_OK) {
        return false;
    }

    s_tim_cb_map[index].tim_cb = NULL;

    return true;
}

/*********************************************************************
 * @fn      bsp_tim_start
 * @brief   启动定时器 (中断模式, 周期溢出触发回调)
 * @param   htim   TIM 句柄指针
 * @return  bool true=成功 false=失败
********************************************************************/
bool bsp_tim_start(BSP_TIM_NUM index)
{
    if(index >= BSP_TIM_MAX) 
    {
      return false;
    }

    TIM_HandleTypeDef *htim = NULL;

    htim = s_tim_list[index];

    return HAL_TIM_Base_Start_IT(htim) == HAL_OK;
}

/*********************************************************************
 * @fn      bsp_tim_stop
 * @brief   停止定时器
 * @param   htim   TIM 句柄指针
 * @return  bool true=成功 false=失败
********************************************************************/
bool bsp_tim_stop(BSP_TIM_NUM index)
{
    if(index >= BSP_TIM_MAX) 
    {
      return false;
    }

    TIM_HandleTypeDef *htim = NULL;

    htim = s_tim_list[index];
    return HAL_TIM_Base_Stop_IT(htim) == HAL_OK;
}


/* =====================================================================
 *  系统滴答 (包装 HAL_GetTick, 上层经此获取, 不直调 HAL)
 * ===================================================================== */
/**
 * @fn      bsp_get_tick_ms
 * @brief   获取系统毫秒时基 (uwTick, 由 TIM1 时基中断维护)
 * @param   无
 * @return  uint32_t 上电/复位以来的毫秒数 (32 位会回绕, 跨度比较须用减法)
 */
uint32_t bsp_get_tick_ms(void)
{
    return HAL_GetTick();
}

/*********************************************************************
 * @fn      bsp_delay_ms
 * @brief   毫秒延时 (内部使用 HAL_Delay)
 * @param   ms  延时毫秒数
 * @note    需要在 RTOS 启动后使用 (依赖 uwTick)
********************************************************************/
void bsp_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}
