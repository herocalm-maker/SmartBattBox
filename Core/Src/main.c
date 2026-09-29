/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp_uart.h"
#include "bsp_adc.h"
#include "bsp_tim.h"
#include "bsp_spi.h"
#include "bsp_io.h"
#include "bsp_wdg.h"
#include "drv_relaysw.h"
#include "drv_fan.h"
#include "drv_led.h"
#include "drv_buzzer.h"
#include "Board.h"
#include "comm_logger.h"
#include <stdio.h>
extern void MX_FREERTOS_Init(void);
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/



static uint8_t isrtimcnt = 0;



/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  //__disable_irq();
 // while (1)
  //{
  //}
  /* USER CODE END Error_Handler_Debug */
}


void user_tim4_CallBack(void)
{
  isrtimcnt++;
  #if BOARD_WDG_ENABLE
    bsp_wdg_monitor_tick();  /* 喂狗 + 心跳检测 (内部 20 tick=1s, 1200 tick=60s) */
  #endif
  drv_buzzer_tick();       /* 蜂鸣器时序 (TIM4 = 50ms 周期) */
}


void dev_power_key_init(void)
{
  __HAL_RCC_GPIOB_CLK_ENABLE();
  
  GPIO_InitTypeDef gpio_pwr = {0};
  gpio_pwr.Pin   = GPIO_PIN_4;
  gpio_pwr.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio_pwr.Pull  = GPIO_NOPULL;
  gpio_pwr.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &gpio_pwr);
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);  /* PB4 = HIGH (自锁) */
}

void timer_init(void)
{
  bsp_tim_config_t tim_cfg = {0};
  tim_cfg.Period    = BOARD_TIM4_ARR;
  tim_cfg.Prescaler = BOARD_TIM4_PSC;
  tim_cfg.tim_cb = user_tim4_CallBack;
  if (!bsp_tim_init(BSP_TIM_4, &tim_cfg)) 
  { 
    Error_Handler(); 
  }
  bsp_tim_start(BSP_TIM_4);
  log_print("tim4: ok");
}



/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  //HAL初始化
  HAL_Init();
  //系统时钟初始化
  SystemClock_Config();
  //systick初始化
  SysTick_Config(SystemCoreClock / 1000U);
  //系统电源初始化
  dev_power_key_init();
  //日志初始化
  logger_init(115200);
  log_print("=== SmartBattBox boot ===");
  log_print("clk: %lu Hz", (unsigned long)SystemCoreClock);
  //看门狗+守护初始化
  bsp_wdg_monitor_init();
  //定时器初始化
  timer_init();
  //ADC初始化
  bsp_adc_init();

  //板载GPIO器件统一初始化: 继电器/光耦(安全断电态) → 风扇 → LED → 蜂鸣器
  drv_relay_init();    /* 含 all_off 安全时序 */
  drv_fan_init();
  drv_led_init();
  drv_buzzer_init();
  log_print("boardio: all drivers init");

  log_print("bsp init done, starting RTOS...");
  //
  osKernelInitialize();  /* Call init function for freertos objects (in cmsis_os2.c) */
  //
  MX_FREERTOS_Init();

  /* Start scheduler */
  osKernelStart();

   /* We should never get here as control is now taken by the scheduler */

   /* Infinite loop */
   /* USER CODE BEGIN WHILE */
  while (1)
  {
     /* USER CODE END WHILE */

     /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}






#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
