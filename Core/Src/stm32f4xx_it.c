/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Board.h"
/* USER CODE END Includes */

extern void app_exti_callback(uint16_t GPIO_Pin);

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
/* htim1 由 stm32f4xx_hal_timebase_tim.c 定义 (HAL 1ms 时基, TIM1_UP_TIM10_IRQHandler 使用) */
extern TIM_HandleTypeDef htim1;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles TIM1 update interrupt and TIM10 global interrupt.
  */
void TIM1_UP_TIM10_IRQHandler(void)
{
   /* USER CODE BEGIN TIM1_UP_TIM10_IRQn 0 */

   /* USER CODE END TIM1_UP_TIM10_IRQn 0 */
   HAL_TIM_IRQHandler(&htim1);
   /* USER CODE BEGIN TIM1_UP_TIM10_IRQn 1 */

   /* USER CODE END TIM1_UP_TIM10_IRQn 1 */
}


/**
  * @brief EXTI0 中断 - PB0 ACIN_KEY（下降沿）
  */
void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BOARD_ACIN_KEY_PIN);
}

/**
  * @brief EXTI1 中断 - PB1 ACOUT_KEY（下降沿）
  */
void EXTI1_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BOARD_ACOUT_KEY_PIN);
}

/**
  * @brief EXTI3 中断 - PB3 ONOFF_KEY（下降沿）
  */
void EXTI3_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BOARD_ONOFF_KEY_PIN);
}

/**
  * @brief EXTI9_5 中断 - PB5 CHG_WKUP(上升沿) / PB8 GUAGE_ALRT(下降沿)
  *        需在 HAL_GPIO_EXTI_Callback 内根据引脚区分。
  */
void EXTI9_5_IRQHandler(void)
{
    if (__HAL_GPIO_EXTI_GET_IT(BOARD_CHG_WKUP_PIN) != RESET) {
        HAL_GPIO_EXTI_IRQHandler(BOARD_CHG_WKUP_PIN);
    }
    if (__HAL_GPIO_EXTI_GET_IT(BOARD_GUAGE_ALRT_PIN) != RESET) {
        HAL_GPIO_EXTI_IRQHandler(BOARD_GUAGE_ALRT_PIN);
    }
}

/**
  * @brief GPIO EXTI 中断回调(覆盖 HAL weak 版)
  *        直接分发: CHG_WKUP → AC 事件, GUAGE_ALRT → 电量计告警
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    app_exti_callback(GPIO_Pin);
}

/* USARTx_IRQHandler / DMAx_StreamY_IRQHandler 已迁移到 bsp_uart.c:
 *   BSP 层统一接管 UART/DMA 中断, IDLE/DMA 接收完成事件经
 *   HAL_UARTEx_RxEventCallback 派发到 bsp_uart_register_rx_cb 注册的回调.
 * TIM4_IRQHandler / DMA2_Stream0_IRQHandler 已迁移到 bsp_tim.c / bsp_adc.c:
*   各 BSP 模块统一接管自身外设中断, HAL 句柄 static 不外泄. */

/* USER CODE END 1 */
