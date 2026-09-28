/**
 * @file    app_display_task.h
 * @brief   显示任务 — 串口屏 (DWIN) 驱动
 *
 * DisplayTask 职责:
 *   - UART4 初始化 (DWIN 串口屏, 115200bps)
 *   - 推送电池/计量/告警数据到屏幕 (单页 0.bmp)
 */
#ifndef __APP_DISPLAY_TASK_H__
#define __APP_DISPLAY_TASK_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>
#include "cmsis_os2.h"
#include "main.h"

/*====================================================================
 * VP 地址常量 (0.bmp 页面, 完整地址, 与屏端工程对齐)
 *====================================================================*/

/* 数值变量 */
#define VP_BATTERY 0x2002u      /* battery        Data variables */
#define VP_INPUT_POWER 0x2022u  /* inputpower     Data variables */
#define VP_OUTPUT_POWER 0x2026u /* outputpower    Data variables */

/* 文本变量 */
#define VP_TEXT_ERR 0x2030u /* Texterror      Text */

/* 图标变量 (VAR Icon) */
#define VP_ICON1 0x2004u
#define VP_ICON2 0x2006u
#define VP_ICON3 0x2008u
#define VP_ICON4 0x200Au
#define VP_ICON5 0x200Du
#define VP_ICON6 0x200Fu
#define VP_ICON7 0x2011u
#define VP_ICON8 0x2013u
#define VP_ICON9 0x2015u
#define VP_ICON_PWR 0x2017u

/* 图标状态值 */
#define ICON_OFF 0
#define ICON_ON 1

/**
 * @brief 向显示任务队列发送消息，不等待队列空间。
 * @param msg 待发送消息，调用方需提供有效指针。
 * @return true：消息入队；false：队列满。
 */
extern bool Send_Msg_AppDisplayTask(ucMESSAGE_t *msg);
/**
 * @brief 显示任务入口：响应图标消息，并按循环计数刷新数值。
 * @param arg 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
extern void DisplayTask(void *arg);

#ifdef __cplusplus
}
#endif
#endif /* __APP_DISPLAY_TASK_H__ */
