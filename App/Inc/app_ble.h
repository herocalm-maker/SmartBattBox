/**
 * @file    app_ble.h
 * @brief   BLE 通信模块
 */
#ifndef __APP_BLE_H__
#define __APP_BLE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "main.h"

/**
 * @brief 向BLE任务队列发送消息，最多等待3000个RTOS tick。
 * @param msg 待发送消息，调用方需提供有效指针。
 * @return true：消息入队；false：队列满。
 */
extern bool Send_Msg_AppBleTask(ucMESSAGE_t *msg);
/* JSON 状态上报 */
/**
 * @brief 读取电池和计量状态，组装JSON并通过BLE串口发送。
 * @note 使用静态发送缓冲区，调用方应避免并发调用。
 */
extern void app_ble_report_status(void);
/* BLE 任务入口 */
/**
 * @brief BLE任务入口：拉高唤醒引脚，接收预留消息并上报任务心跳。
 * @param arg 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
extern void BleTask(void *arg);

#ifdef __cplusplus
}
#endif
#endif /* __APP_BLE_H__ */
