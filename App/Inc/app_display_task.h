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
 * VP 地址常量 (0.bmp 页面新界面, 与屏端工程 2026-10-11 组态对齐)
 *====================================================================*/

/* 数值变量 (Data variables) */
#define VP_BATT_SOC        0x2020u  /* DataVarBattPer  电池电量 0-100 */
#define VP_DATA_INPUT_PWR  0x2022u  /* DataVarInput    输入功率 W */
#define VP_DATA_OUTPUT_PWR 0x2024u  /* DataVarOutput   输出功率 W */

/* 电量条: 底层静态图标 (变量图标) + 上层动画图标 */
#define VP_BATT_BAR        0x2070u  /* VarIconBattPer    底层电量条 0~10 格 (soc/10) */
#define VP_ANIM_BATT       0x2040u  /* AnimationiconBatt 上层动画使能 (1=播放/0=停止) */

/* 电量动画参数 (起始图标索引/总帧数) 可写 VP: 屏端组态未提供, 0=未分配
 * 若屏端支持运行时切换 充电流水(ICL 11-20)/放电闪烁(21-22), 在此填入参数地址 */
#define VP_ANIM_BATT_CFG 0x0000u

/* 文本变量 (Text), 显示长度 = 字节数 (每字 2 字符) */
#define VP_TEXT_ERR   0x2050u  /* TextError  异常提示 "ERRxx" (正常写空) */
#define VP_TEXT_ALARM 0x2060u  /* TextAlarm  过载警报 "OVERLOAD" (正常写空) */
#define VP_TEXT_TIME  0x2036u  /* TextTime   充电剩余充满/放电可用时间 "%uMIN" */
#define VP_TEXT_HZ    0x2030u  /* TextHZ     逆变输出频率 (取整, 不输出写空) */

#define VP_TEXT_ERR_LEN   6u   /* TextError  显示长度 6 字符 */
#define VP_TEXT_ALARM_LEN 8u   /* TextAlarm  显示长度 8 字符 */
#define VP_TEXT_TIME_LEN  9u   /* TextTime   显示长度 9 字符 (如 1234MIN) */
#define VP_TEXT_HZ_LEN    2u   /* TextHZ     显示长度 2 字符 */

/* 图标变量 (VAR Icon) */
#define VP_ICON_CHG  0x2002u  /* VarIconChg   充电图标 */
#define VP_ICON_OUT  0x2004u  /* VarIconOutput 逆变输出图标 */
#define VP_ICON_HZ   0x2006u  /* VarIconHZ    输出频率图标 */
#define VP_ICON_UPS  0x2008u  /* VarIconUPS   旁路输出 (市电直通) 图标 */
#define VP_ICON_BLE  0x200Au  /* VarIconBle   蓝牙图标 (暂不显示) */
#define VP_ICON_TIME 0x200Cu  /* VarIconTime  时间图标 (固定 13) */
#define VP_ICON_TEMP 0x200Eu  /* VarIconTemp  温度异常图标 (过高/过低常亮) */

/* 图标状态值 (屏端 ICL 索引) */
#define ICON_OFF      0
#define ICON_ON       1
#define ICON_CHG_ON   1   /* 充电图标显示值 */
#define ICON_OUT_ON   11  /* 逆变输出图标显示值 */
#define ICON_HZ_ON    5   /* 输出频率图标显示值 */
#define ICON_TEMP_ON  8   /* 温度异常图标显示值 */
#define ICON_TIME_ON  13  /* 时间图标显示值 (固定) */
#define ICON_UPS_ON   6   /* 旁路输出图标显示值 */

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
