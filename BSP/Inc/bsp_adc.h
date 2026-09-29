/**
 * @file    bsp_adc.h
 * @brief   ADC 通用外设接口 (BSP 层) — 纯 HAL_ADC 封装
 *
 * 设计目标:
 *   - 单板单 ADC1, 句柄由 BSP 内部持有, 上层只需通道枚举
 *   - 只提供基础函数: init/deinit/get_raw/get_mv/get_temp
 *   - 方便更换芯片或重新映射 ADC
 *
 * 数据流:
 *   ADC1 硬件扫描 (5 通道) → DMA2_Stream0 循环 → s_adc1_dma_buf[] (5通道×32块交织)
 *   上层任意时刻读取即得到最新值, 无阻塞;
 *   bsp_adc_get_raw_average 对整窗 32 样本软件平均 (参考 19-3 例程)
 *
 * 通道序列 (Board.h "7. ADC 通道映射"):
 *   Rank 0: PC1  ADC1_IN11 充电模拟备份
 *   Rank 1: PA3  ADC1_IN3   充电电压
 *   Rank 2: PA4  ADC1_IN4   充电器温度 NTC
 *   Rank 3: PA5  ADC1_IN5   电池温度 NTC
 *   Rank 4: PA6  ADC1_IN6   逆变器温度 NTC
 *
 * @note 引脚分配见 Board.h "3. GPIO 引脚分配总表 / 7. ADC 通道映射"
 *       NTC B 值换算由本层提供 (纯板级换算, 原 drv_adc_get_temp 移入).
 */
#ifndef __BSP_ADC_H__
#define __BSP_ADC_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ADC 通道枚举 (rank 即数组下标, 对应 Board.h "7. ADC 通道映射") */
typedef enum {
    BSP_ADC_CH_CHG_ANA    = 0,  /* PC1  充电模拟备份 */
    BSP_ADC_CH_CHG_VOL    = 1,  /* PA3  充电电压 */
    BSP_ADC_CH_CHG_TMP    = 2,  /* PA4  充电器温度 NTC */
    BSP_ADC_CH_BAT_TMP    = 3,  /* PA5  电池温度 NTC */
    BSP_ADC_CH_INV_TMP    = 4,  /* PA6  逆变器温度 NTC */
    BSP_ADC_CH_CNT
} bsp_adc_ch_enum;


/* 12bit 最大值 (Board.h, 用于电压/NTC 换算) */
#define BSP_ADC_RESOLUTION      BOARD_ADC_RESOLUTION
#define BSP_ADC_VREF_MV         BOARD_ADC_VREF_MV



extern bool bsp_adc_init(void);
extern bool bsp_adc_deinit(void);
extern uint16_t bsp_adc_get_raw(bsp_adc_ch_enum ch);
extern uint16_t bsp_adc_get_raw_average(bsp_adc_ch_enum ch);
extern uint16_t bsp_adc_get_mv(bsp_adc_ch_enum ch);
extern uint16_t bsp_adc_get_mv_average(bsp_adc_ch_enum ch);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_ADC_H__ */
