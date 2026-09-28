/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If there is no LICENSE file with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"
#include "bsp_wdg.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp_io.h"

/* Driver layer: 器件协议驱动 (薄 GPIO 器件已合并入 bsp_boardio) */
#include "drv_hlw8110.h"
#include "drv_max17263.h"
#include "drv_w25q128.h"
#include "drv_pb03f.h"
#include "bsp_boardio.h"
#include "drv_dwin.h"

/* Module layer */
#include "mod_battery.h"
#include "mod_metering.h"
#include "mod_key.h"
#include "mod_storage.h"
#include "mod_temp.h"

/* App layer */
#include "app_main_task.h"
#include "app_sensor_task.h"
#include "app_ble.h"
#include "app_display_task.h"
#include "app_metering_task.h"

/* Comm */
#include "comm_logger.h"
/* USER CODE END Includes */




//主任务
static osThreadId_t s_task_main;
QueueHandle_t xMainQueueHandle = {0};
#define MAIN_MSG_QUEUE_MAX_NUM       10
static uint8_t ucMainMsgQueueBuff[MAIN_MSG_QUEUE_MAX_NUM * sizeof(ucMESSAGE_t)] = {0};
static StaticQueue_t xMainMsgQueue;

//ble
static osThreadId_t s_task_ble;
QueueHandle_t xBleQueueHandle = {0};
#define BLE_MSG_QUEUE_MAX_NUM        6
static uint8_t ucBleMsgQueueBuff[BLE_MSG_QUEUE_MAX_NUM * sizeof(ucMESSAGE_t)] = {0};
static StaticQueue_t xBleMsgQueue;

//传感器采集
static osThreadId_t s_task_sensor;
QueueHandle_t xSensorQueueHandle = {0};
#define SENSOR_MSG_QUEUE_MAX_NUM        6
static uint8_t ucSensorMsgQueueBuff[SENSOR_MSG_QUEUE_MAX_NUM * sizeof(ucMESSAGE_t)] = {0};
static StaticQueue_t xSensorMsgQueue;

//显示任务
static osThreadId_t s_task_display;
QueueHandle_t xDisplayQueueHandle = {0};
#define DISPLAY_MSG_QUEUE_MAX_NUM       6
static uint8_t ucDisplayMsgQueueBuff[DISPLAY_MSG_QUEUE_MAX_NUM * sizeof(ucMESSAGE_t)] = {0};
static StaticQueue_t xDisplayMsgQueue;

//计量任务 (HLW8110)
static osThreadId_t s_task_metering;
QueueHandle_t xMeteringQueueHandle = {0};
#define METERING_MSG_QUEUE_MAX_NUM      6
static uint8_t ucMeteringMsgQueueBuff[METERING_MSG_QUEUE_MAX_NUM * sizeof(ucMESSAGE_t)] = {0};
static StaticQueue_t xMeteringMsgQueue;




/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) 
{
  /* === BSP 板载器件层 === */
  bsp_boardio_init();
  /* === Module 层 === */
  mod_storage_init();
  //显示屏
  if (!drv_dwin_init(BSP_UART_4))
  {
    logger_out(LOG_ERROR, "dwin: init failed");
  }
  logger_out(LOG_INFO, "display_task init");
  //创建消息队列
  xMainQueueHandle   = xQueueCreateStatic(MAIN_MSG_QUEUE_MAX_NUM, sizeof(ucMESSAGE_t), ucMainMsgQueueBuff, &xMainMsgQueue);
  xBleQueueHandle    = xQueueCreateStatic(BLE_MSG_QUEUE_MAX_NUM, sizeof(ucMESSAGE_t), ucBleMsgQueueBuff, &xBleMsgQueue);
  xSensorQueueHandle = xQueueCreateStatic(SENSOR_MSG_QUEUE_MAX_NUM, sizeof(ucMESSAGE_t), ucSensorMsgQueueBuff, &xSensorMsgQueue);
  xDisplayQueueHandle = xQueueCreateStatic(DISPLAY_MSG_QUEUE_MAX_NUM, sizeof(ucMESSAGE_t), ucDisplayMsgQueueBuff, &xDisplayMsgQueue);
  xMeteringQueueHandle = xQueueCreateStatic(METERING_MSG_QUEUE_MAX_NUM, sizeof(ucMESSAGE_t), ucMeteringMsgQueueBuff, &xMeteringMsgQueue);

  /* === 启动任务 === */
  /* 优先级: SensorTask(6) > BleTask(5) = DisplayTask(5) = MeteringTask(5) > AppMainTask(4) */
  xTaskCreate((TaskFunction_t )SensorTask,
              (const char*    )"AppSensorTask",
              (uint16_t       )1024,
              (void*          )NULL,
              (UBaseType_t    )6,        /* 最高: 实时采集 + 按键响应 */
              (TaskHandle_t*  )&s_task_sensor);

  xTaskCreate((TaskFunction_t )BleTask,
              (const char*    )"appBleTask",
              (uint16_t       )512,
              (void*          )NULL,
              (UBaseType_t    )5,        /* 中等: 蓝牙通讯 */
              (TaskHandle_t*  )&s_task_ble);

  xTaskCreate((TaskFunction_t )MeteringTask,
              (const char*    )"appMeteringTask",
              (uint16_t       )512,
              (void*          )NULL,
              (UBaseType_t    )6,        /* 中等: 电能计量 */
              (TaskHandle_t*  )&s_task_metering);

  xTaskCreate((TaskFunction_t )DisplayTask,
              (const char*    )"appDisplayTask",
              (uint16_t       )640,
              (void*          )NULL,
              (UBaseType_t    )6,        /* 中等: 显示刷新 */
              (TaskHandle_t*  )&s_task_display);        

  xTaskCreate((TaskFunction_t )AppMainTask,
              (const char*    )"userAppMainTask",
              (uint16_t       )1024,
              (void*          )NULL,
              (UBaseType_t    )6,        /* 最低: 业务逻辑 */
              (TaskHandle_t*  )&s_task_main); 

  log_print("=== System ready, tasks running ===");
}



