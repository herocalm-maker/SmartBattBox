/**
 * @file    app_display_task.h
 * @brief   显示任务 — 串口屏 (DWIN) 驱动
 *
 * DisplayTask 职责:
 *   - UART4 初始化 (DWIN 串口屏, 115200bps)
 *   - 推送电池/计量/告警数据到屏幕 (单页 0.bmp)
 *
 * VP 组态基线: 2026-11-15 屏端变量地址表 (DGUS "查看地址设置")
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
 * VP 地址常量 (0.bmp 页面, 与屏端 2026-11-15 组态对齐)
 *====================================================================*/

/* ---- 电量: ArtisticVarBattPer (艺术字) ----
 *   数值: VP 0x5100 (每 500ms 写)
 *   颜色: SP+4 高字节 = ICL 库号, 低字节 = 显示模式 (0=透明) (仅跨档时写) */
#define VP_BATT_SOC        0x5100u  /* ArtisticVarBattPer 电量数值 */
#define VP_BATT_SOC_SP     0x5188u  /* ArtisticVarBattPer 描述指针 */
#define VP_BATT_SOC_LIB    (VP_BATT_SOC_SP + 4u)   /* 0x518C 库号(高字节)+模式(低字节) */

#define LIB_BATT_EMPTY     0x28u    /* 40_num_72_170_empty.icl  空白 (无有效电量时) */
#define LIB_BATT_WHITE     0x35u    /* 53_num_72_170.icl        白 */
#define LIB_BATT_ORANGE    0x36u    /* 54_num_orange_72_170.icl 橙 */
#define LIB_BATT_RED       0x37u    /* 55_num_red_72_170.icl    红 */

#define VP_DATA_INPUT_PWR  0x5104u  /* ArtisticVarInput    输入功率 W */
#define VP_DATA_OUTPUT_PWR 0x510Au  /* ArtisticVarOutput   输出功率 W */

/* 电量颜色档位 (供屏端 OS 核方案/调试): 0=白 1=橙 2=红 */
#define VP_SOC_COLOR_LEVEL 0x202Au

/* 电量条: 底层静态图标 (变量图标) + 上层动画图标 */
#define VP_BATT_BAR        0x2070u  /* VarIconBattPer    0~10 格 (soc/10) */
#define VP_ANIM_BATT       0x2040u  /* AnimationiconBatt 1=播放循环 / 0=停止 */

/*====================================================================
 * 时间显示 (2026-11-15 新组态)
 *   VarIconTime     0x2102  时间图标: 0=不显示 1=显示
 *   VARIconUnit     0x2120  单位: 0=不显示 1=MIN (<1h) 2=H (>1h)
 *                           图标库 45_icon_48_timeunit (3 张, 均 96x48)
 *   ArtisticVarTime 0x2106  数值: 充电距充满 / 无 220V 时逆变剩余可用时间
 *                           整数小时 (屏端控件无小数位), 三件套仅在取到
 *                           有效时间时同时显示, 否则同时隐藏
 *====================================================================*/
#define VP_ICON_TIME       0x2102u
#define VP_ICON_TIME_UNIT  0x2120u
#define VP_ART_TIME        0x2106u
#define VP_ART_TIME_SP     0x5900u                      /* 描述指针 (屏端已配置) */
#define VP_ART_TIME_LIB    (VP_ART_TIME_SP + 4u)        /* 0x5904 库号(高字节)+模式(低字节) */

/* ArtisticVarTime 的两个字库 (ICL 序号 = 十六进制低字节) */
#define LIB_TIME_NORMAL    0x3Bu   /* 59_num_18_36.icl       正常数字 */
#define LIB_TIME_BLANK     0x3Cu   /* 60_num_18_36_empty.icl 空白 (隐藏) */

#define ICON_TIME_ON       1
#define ICON_UNIT_H        2   /* VARIconUnit (45_icon_48_timeunit): 2=H   (>1h) */
#define ICON_UNIT_MIN      1   /* VARIconUnit: 1=MIN (<1h) */
#define ICON_TEMP_UNIT_ON  1   /* VARIconTempUnit: 屏端默认已显示, 开机写一次 1 */
#define ICON_UNIT_OFF      0

/*====================================================================
 * 温度显示 (2026-11-15 新组态)
 *   ArtisticVarTemp 0x2130  SP 0x5800  电池温度整数 ℃; 超阈值转红靠切 ICL 库
 *   VARIconTempUnit 0x2140  温度单位: 上电常显 (写 2)
 *   AnimiconTermp   0x2150  超阈值播放动画: 1=播放 / 0=失能不显示
 *====================================================================*/
#define VP_ART_TEMP        0x2130u
#define VP_ART_TEMP_SP     0x5800u
#define VP_ART_TEMP_LIB    (VP_ART_TEMP_SP + 4u)   /* 0x5804 库号(高字节)+模式(低字节) */
#define VP_ICON_TEMP_UNIT  0x2140u
#define VP_ANIM_TEMP       0x2150u

/* 温度艺术字库号 (当前未启用: 无红色字库, 见 app_display_task.c 的
 * TEMP_VALUE_COLOR_ENABLE 开关)。启用后若温度艺术字另配字库, 改此两处。 */
#define LIB_TEMP_WHITE     0x31u
#define LIB_TEMP_RED       0x34u

/*====================================================================
 * 频率显示: ArtisticVarHZ(0x2100) 数值 + VarIconHZ(0x2006) 图标
 *   SP 0x5600 → 库号在 0x5604。不输出时切 60_num_18_36_empty 隐藏。
 *   正常库直接写死 (屏端不支持回读), 与时间同一套 18x36 字库。
 *====================================================================*/
#define VP_ART_HZ          0x2100u
#define VP_ART_HZ_SP       0x5600u
#define VP_ART_HZ_LIB      (VP_ART_HZ_SP + 4u)   /* 0x5604 库号(高字节)+模式(低字节) */
#define LIB_HZ_NORMAL      0x3Bu                 /* 59_num_18_36.icl 正常数字 */
#define LIB_HZ_BLANK       0x3Cu                 /* 60_num_18_36_empty.icl 空白 */
#define VP_ICON_HZ         0x2006u

/*====================================================================
 * 文本控件 (Text)
 *====================================================================*/
#define VP_TEXT_ERR   0x2050u  /* TextError  "ERRxx" (正常写空) */
#define VP_TEXT_ALARM 0x2060u  /* TextAlarm  "OVERLOAD" (正常写空) */

#define VP_TEXT_ERR_SP   0x5780u /* TextError 描述指针 */
#define VP_TEXT_ALARM_SP 0x5790u /* TextAlarm 描述指针 */

/* 描述指针 + 3 = 文字前景色 (RGB565 大端, 已实测确认) */
#define TEXT_SP_COLOR_OFS  3u
#define TEXT_COLOR_RED     0xF800u  /* RGB565 纯红: R=31 G=0 B=0 */
#define TEXT_COLOR_WHITE   0xFFFFu  /* RGB565 白 (屏端组态默认色) */
#define VP_TEXT_ERR_COLOR   (VP_TEXT_ERR_SP + TEXT_SP_COLOR_OFS)    /* 0x5783 */
#define VP_TEXT_ALARM_COLOR (VP_TEXT_ALARM_SP + TEXT_SP_COLOR_OFS)  /* 0x5793 */

#define VP_TEXT_ERR_LEN   6u   /* TextError 显示长度 6 字符 (屏端控件"文本长度"=6, 偶数免补齐) */
#define VP_TEXT_ALARM_LEN 8u   /* TextAlarm 显示长度 8 字符 */

/*====================================================================
 * 图标变量 (VAR Icon)
 *====================================================================*/
#define VP_ICON_CHG  0x2002u  /* VarIconChg        充电图标 */
#define VP_ICON_OUT  0x2004u  /* VarIconOutput     逆变输出图标 (11) */
#define VP_ICON_SIDE 0x2008u  /* VarIconSideOutput 旁路 (市电直通) 图标 (6) */
#define VP_ICON_BLE  0x200Au  /* VarIconBle        蓝牙图标 (12, 连接时显示) */

/* 动画图标: 0=失能不显示 / 1=使能显示 (控件内部自动播放, 固件不管播放相位) */
#define VP_ANIM_FAN   0x510Eu  /* AnimationFan 风扇动画 */

/* 图标状态值 (屏端 ICL 索引) */
#define ICON_OFF      0
#define ICON_ON       1
#define ICON_CHG_ON   1   /* 充电图标显示值 */
#define ICON_OUT_ON   11  /* 逆变输出图标显示值 */
#define ICON_HZ_ON    5   /* 输出频率图标显示值 */
#define ICON_SIDE_ON  6   /* VarIconSideOutput 旁路输出图标显示值 */
#define ICON_BLE_ON   12  /* VarIconBle 蓝牙图标显示值 */
#define ANIM_FAN_OFF  0   /* 风扇动画失能 (无风扇运行, 不显示) */
#define ANIM_FAN_ON   1   /* 风扇动画使能显示 (控件自动播放) */
#define ANIM_TEMP_OFF 0   /* 温度动画失能 */
#define ANIM_TEMP_ON  1   /* 温度动画使能 (超阈值) */


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
