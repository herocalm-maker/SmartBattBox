/**
 * @file    mod_temp.h
 * @brief   温度采集模块 (Module 层)
 *
 * 功能:
 *   - 三路 NTC 周期采样 (充电器 / 电池 / 逆变器)
 *   - 提供统一查询接口 (当前值 + 最大值 + 来源)
 *   - 数据单位: 0.1 °C (与 Board.h 阈值一致)
 *
 * 调用方:
 *   - SensorTask 周期调用 mod_temp_update()
 *   - 任意任务通过 mod_temp_get() 查询
 */
#ifndef __MOD_TEMP_H__
#define __MOD_TEMP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/*--------------------------------------------------------------------
 * 温度源枚举
 *--------------------------------------------------------------------*/
typedef enum {
    TEMP_SRC_CHG = 0,    /* 充电器 NTC (PA4) */
    TEMP_SRC_BAT,        /* 电池   NTC (PA5) */
    TEMP_SRC_INV,        /* 逆变器 NTC (PA6) */
    TEMP_SRC_CNT
} temp_src_enum;

/*--------------------------------------------------------------------
 * 温度信息结构
 *--------------------------------------------------------------------*/
typedef struct {
    int16_t         chg_temp;    /* 充电器温度 (0.1°C) */
    int16_t         bat_temp;    /* 电池   温度 (0.1°C) */
    int16_t         inv_temp;    /* 逆变器 温度 (0.1°C) */
    int16_t         max_temp;    /* 当前最高温度 */
    temp_src_enum   max_src;     /* 最高温度来源 */
} TEMP_INFO_T;

/*--------------------------------------------------------------------
 * API
 *--------------------------------------------------------------------*/

/* 初始化温度模块 */
extern bool mod_temp_init(void);

/* 周期更新: 读取三路 ADC, 计算最大值 (SensorTask 调用) */
extern void mod_temp_update(void);

/* 获取完整温度信息 (只读指针) */
extern const TEMP_INFO_T* mod_temp_get(void);

#ifdef __cplusplus
}
#endif
#endif /* __MOD_TEMP_H__ */
