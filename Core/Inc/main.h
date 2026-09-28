/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "queue.h"

/* Mutually exclusive power routes; CHARGE_BYPASS keeps charging enabled. */
typedef enum {
    PATH_NONE,
    PATH_BYPASS,
    PATH_CHARGE,
    PATH_INVERT,
    PATH_CHARGE_BYPASS
} energy_path_t;

typedef enum
{
  CHANNEL_TYPE_NONE    = 0,
	CHANNEL_TYPE_UART    = 1,
	CHANNEL_TYPE_BLE     = 2,
	CHANNEL_TYPE_MAX
}CHANNEL_TYPE;



typedef struct
{
  uint8_t    Channel;              //通道 
  uint8_t    Type;								 //事件类型
  uint8_t	   Result;						   //结果 
  uint8_t    Value[8]; 						 //事件值 
}MSG_EVENT_PARAS;

//消息结构体
typedef struct
{
  uint16_t    MsgID;               //消息ID
  MSG_EVENT_PARAS Paras;           //参数
  uint16_t   Length;               //数据长度
  uint8_t    *pData;               //数据指针
}ucMESSAGE_t;


#define DEV_POWER_ON()           HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET)
#define DEV_POWER_OFF()          HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET)


extern QueueHandle_t xMainQueueHandle;
extern QueueHandle_t xDisplayQueueHandle;
extern QueueHandle_t xSensorQueueHandle;
extern QueueHandle_t xBleQueueHandle;
extern QueueHandle_t xMeteringQueueHandle;



extern void Error_Handler(void);


#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
