/**
 * @file    app_metering_task.h
 * @brief   电能计量任务 (HLW8110 双路采集)
 */
#ifndef __APP_METERING_TASK_H__
#define __APP_METERING_TASK_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "main.h"

/**
 * @brief 向计量任务队列发送消息，最多等待3000个RTOS tick。
 * @param msg 待发送消息，调用方需提供有效指针。
 * @return true：消息入队；false：队列满。
 */
extern bool Send_Msg_AppMeteringTask(ucMESSAGE_t *msg);
/**
 * @brief 计量任务入口：初始化采样配置并周期调用双路采集。
 * @param arg 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
extern void MeteringTask(void *arg);

#ifdef __cplusplus
}
#endif
#endif /* __APP_METERING_TASK_H__ */
