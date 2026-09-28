/**
 * @file    mod_temp.c
 * @brief   温度采集模块 (Module 层)
 *
 * 实现:
 *   - 三路 NTC 原始值由 bsp_adc_get_raw_average() 读取 (BSP 层仅提供 ADC 采集)
 *   - NTC B 值方程在本模块完成: R→T 换算, 输出 0.1°C 分辨率
 *   - 每次 update 同时计算最高温度及其来源, 供上层保护/显示使用
 *   - 线程安全: 仅 SensorTask 写, 其他任务读, ARM Cortex-M4 字对齐读天然原子
 *
 * NTC 参数 (上拉阻值/R0/T0/B值) 来自 Board.h BOARD_NTC_*.
 */

#include "mod_temp.h"
#include "bsp_adc.h"
#include "Board.h"
#include <math.h>

/*--------------------------------------------------------------------
 * 静态变量
 *--------------------------------------------------------------------*/
static TEMP_INFO_T s_temp = {0};

/*--------------------------------------------------------------------
 * 内部: raw ADC → 温度 (0.1°C)
 *
 * NTC 分压: Vout = Vcc × R_ntc / (R_pullup + R_ntc)
 *           raw = Vout / Vcc × resolution
 *           → R_ntc = R_pullup × raw / (resolution - raw)
 *
 * B 值方程: T = 1 / (1/T0 + ln(R/R0) / B) - 273.15
 *--------------------------------------------------------------------*/
static int16_t _raw_to_temp(uint16_t raw)
{
    if (raw == 0 || raw >= BSP_ADC_RESOLUTION) {
        return -1000;   /* 异常: 开路或短路 */
    }

    float r_ntc = (float)BOARD_NTC_PULLUP_OHM * (float)raw /
                  (float)(BSP_ADC_RESOLUTION - raw);

    float t_k = 1.0f / (1.0f / BOARD_NTC_T0_K +
                        logf(r_ntc / (float)BOARD_NTC_R0_OHM) / BOARD_NTC_BETA);
    float t_c = t_k - 273.15f;

    return (int16_t)(t_c * 10.0f);
}

/*--------------------------------------------------------------------
 * 内部: 更新 max_temp / max_src
 *--------------------------------------------------------------------*/
static void _update_max(void)
{
    int16_t max = s_temp.chg_temp;
    temp_src_enum src = TEMP_SRC_CHG;

    if (s_temp.bat_temp > max) { max = s_temp.bat_temp; src = TEMP_SRC_BAT; }
    if (s_temp.inv_temp > max) { max = s_temp.inv_temp; src = TEMP_SRC_INV; }

    s_temp.max_temp = max;
    s_temp.max_src  = src;
}

/*====================================================================
 * API
 *====================================================================*/

bool mod_temp_init(void)
{
    s_temp.chg_temp = 0;
    s_temp.bat_temp = 0;
    s_temp.inv_temp = 0;
    s_temp.max_temp = 0;
    s_temp.max_src  = TEMP_SRC_CHG;

    /* 启动首次 ADC DMA 传输 (DMA_NORMAL 模式, 需手动启动) */
    bsp_adc_dma_enable(BOARD_ADC_CHANNEL_CNT);
    return true;
}

void mod_temp_update(void)
{
    s_temp.chg_temp = _raw_to_temp(bsp_adc_get_raw_average(BSP_ADC_CH_CHG_TMP));
    s_temp.bat_temp = _raw_to_temp(bsp_adc_get_raw_average(BSP_ADC_CH_BAT_TMP));
    s_temp.inv_temp = _raw_to_temp(bsp_adc_get_raw_average(BSP_ADC_CH_INV_TMP));
    _update_max();

    /* 重启下一轮 DMA 传输 (本轮读旧值, 下轮读新值) */
    bsp_adc_dma_enable(BOARD_ADC_CHANNEL_CNT);
}

const TEMP_INFO_T* mod_temp_get(void)
{
    return &s_temp;
}
