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
/*********************************************************************
 * @fn      mod_temp_raw_to_temp
 * @brief   将单路 NTC 的 ADC 原始均值换算为温度 (0.1°C 分辨率).
 * @param   raw  ADC 采样均值 (0..BSP_ADC_RESOLUTION-1)
 * @return  温度值, 单位 0.1°C (有符号); 异常时返回 -1000.
 * @note    分压公式反解 R_ntc 后代入 B 值方程; raw=0 (对地短路) 或
 *          raw≥满量程 (开路/上拉失效) 无物理意义, 直接报异常哨兵值.
********************************************************************/
static int16_t mod_temp_raw_to_temp(uint16_t raw)
{
    if (raw == 0 || raw >= BSP_ADC_RESOLUTION)
    {
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
/*********************************************************************
 * @fn      mod_temp_update_max
 * @brief   从三路温度中选出最高值及其来源通道.
 * @param   无
 * @return  none
 * @note    并列时优先保留先比较到的通道 (CHG > BAT > INV 优先级),
 *          供上层按来源做针对性降功率/保护.
********************************************************************/
static void mod_temp_update_max(void)
{
    int16_t max = s_temp.chg_temp;
    temp_src_enum src = TEMP_SRC_CHG;

    if (s_temp.bat_temp > max)
    {
        max = s_temp.bat_temp;
        src = TEMP_SRC_BAT;
    }
    if (s_temp.inv_temp > max)
    {
        max = s_temp.inv_temp;
        src = TEMP_SRC_INV;
    }

    s_temp.max_temp = max;
    s_temp.max_src  = src;
}

/*====================================================================
 * API
 *====================================================================*/

/*********************************************************************
 * @fn      mod_temp_init
 * @brief   清零温度模块内部状态.
 * @param   无
 * @return  true = 初始化成功.
 * @note    仅复位软件状态, ADC 由 BSP 层自行初始化.
********************************************************************/
bool mod_temp_init(void)
{
    s_temp.chg_temp = 0;
    s_temp.bat_temp = 0;
    s_temp.inv_temp = 0;
    s_temp.max_temp = 0;
    s_temp.max_src  = TEMP_SRC_CHG;
    s_temp.valid    = false;   /* 首次采样成功后才为 true */

    return true;
}

/*********************************************************************
 * @fn      mod_temp_update
 * @brief   采样三路 NTC 并刷新温度与最高温统计.
 * @param   无
 * @return  none
 * @note   仅 SensorTask 调用 (写侧唯一); 各字段为字对齐的 int16/枚举,
 *         其他任务经 mod_temp_get 读取时天然原子, 无需加锁.
********************************************************************/
void mod_temp_update(void)
{
    s_temp.chg_temp = mod_temp_raw_to_temp(bsp_adc_get_raw_average(BSP_ADC_CH_CHG_TMP));
    s_temp.bat_temp = mod_temp_raw_to_temp(bsp_adc_get_raw_average(BSP_ADC_CH_BAT_TMP));
    s_temp.inv_temp = mod_temp_raw_to_temp(bsp_adc_get_raw_average(BSP_ADC_CH_INV_TMP));
    mod_temp_update_max();
    s_temp.valid = true;   /* 完成一次采样后才允许上层使用 */
}

/*********************************************************************
 * @fn      mod_temp_get
 * @brief   获取温度信息只读指针.
 * @param   无
 * @return  指向内部静态状态 (三路温度 + 最高温来源).
 * @note   返回内部指针而非副本: 各字段读取原子, 但调用方不得长期缓存该指针,
 *         内容会随 mod_temp_update 持续刷新.
********************************************************************/
const TEMP_INFO_T* mod_temp_get(void)
{
    return &s_temp;
}
