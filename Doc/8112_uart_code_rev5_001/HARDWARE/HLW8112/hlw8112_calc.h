/**
 * @file    hlw8112_calc.h
 * @brief   HLW8112/HLW8110 物理量换算接口（全部参数化）
 * @details 所有 Calc 函数均为纯数学换算，零全局依赖。
 *          输入：原始寄存器字节数组 + 校准系数
 *          输出：通过指针返回（u32 寄存器值 + float 物理量值）
 *          可脱离硬件做单元测试。
 *
 * 使用示例:
 *   u8 rx_buf[3] = {0x12, 0x34, 0x56};
 *   float volt;
 *   u32   volt_reg;
 *   HLW811X_Calc_Voltage(rx_buf, U16_RMSUC_RegData,  // 校准系数来自全局变量
 *                        &volt_reg, &volt);
 *
 * @version V1.0
 * @date    2026-06-05 重构实现
 * @note    校准系数通过参数传入（不从全局变量隐式读取），
 *          调用者传入 U16_RMSIAC_RegData 等全局变量。
 */

#ifndef __HLW8112_CALC_H
#define __HLW8112_CALC_H

/* #include "hlw8112_data.h"       /* HLW811X_MeasureResult_t, HLW811X_CalCoef_t */

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================
 * 物理量换算函数 — 全部参数化，零全局变量访问
 *============================================================*/

/**
 * @brief  计算电压有效值
 * @param  rx_buf[3]  原始寄存器数据（3字节，RMSU 寄存器）
 * @param  coeff_u    电压转换系数（来自芯片 REG_RMS_UC_ADDR 或默认值）
 * @param  out_reg    输出：寄存器原始值 (u32)
 * @param  out_v      输出：电压有效值 (V)
 */
void HLW811X_Calc_Voltage(const u8 rx_buf[3], u16 coeff_u,
                          u32 *out_reg, float *out_v);

#if HLW811X_CHANNEL_A_ENABLE
/**
 * @brief  计算A通道电流有效值
 * @param  rx_buf[3]  原始寄存器数据（3字节，RMSIA 寄存器）
 * @param  coeff_ia   A通道电流转换系数
 * @param  out_reg    输出：寄存器原始值
 * @param  out_ia     输出：电流有效值 (A)
 */
void HLW811X_Calc_IA(const u8 rx_buf[3], u16 coeff_ia,
                     u32 *out_reg, float *out_ia);

/**
 * @brief  计算A通道有功功率
 * @param  rx_buf[4]  原始寄存器数据（4字节，POWERPA 寄存器）
 * @param  coeff_pa   A通道功率转换系数
 * @param  out_reg    输出：寄存器原始值
 * @param  out_pa     输出：有功功率 (W)
 */
void HLW811X_Calc_PA(const u8 rx_buf[4], u16 coeff_pa,
                     u32 *out_reg, float *out_pa);

/**
 * @brief  计算A通道有功电能
 * @param  rx_buf[3]  原始寄存器数据（3字节，ENERGY_PA 寄存器）
 * @param  coeff_ea   A通道电能转换系数
 * @param  out_reg    输出：寄存器原始值
 * @param  out_ea     输出：有功电能 (KWH)
 */
void HLW811X_Calc_EA(const u8 rx_buf[3], u16 coeff_ea,
                     u32 *out_reg, float *out_ea);
/* #endif /* HLW811X_CHANNEL_A_ENABLE */

#if HLW811X_CHANNEL_B_ENABLE
/**
 * @brief  计算B通道电流有效值
 */
void HLW811X_Calc_IB(const u8 rx_buf[3], u16 coeff_ib,
                     u32 *out_reg, float *out_ib);

/**
 * @brief  计算B通道有功功率
 */
void HLW811X_Calc_PB(const u8 rx_buf[4], u16 coeff_pb,
                     u32 *out_reg, float *out_pb);

/**
 * @brief  计算B通道有功电能
 */
void HLW811X_Calc_EB(const u8 rx_buf[3], u16 coeff_eb,
                     u32 *out_reg, float *out_eb);
/* #endif /* HLW811X_CHANNEL_B_ENABLE */

#if HLW811X_APPARENT_POWER_ENABLE
/**
 * @brief  计算视在功率
 */
void HLW811X_Calc_PSC(const u8 rx_buf[4], u16 coeff_ps,
                      u32 *out_reg, float *out_ps);
/* #endif /* HLW811X_APPARENT_POWER_ENABLE */

#if HLW811X_FREQ_ANGLE_ENABLE
/**
 * @brief  计算市电频率
 * @param  rx_buf[2]  原始寄存器数据（2字节，UFREQ 寄存器）
 * @param  out_freq   输出：市电频率 (Hz)
 */
void HLW811X_Calc_Freq(const u8 rx_buf[2], float *out_freq);

/**
 * @brief  计算相角
 * @param  rx_buf[2]  原始寄存器数据（2字节，ANGLE 寄存器）
 * @param  freq       当前市电频率（用于确定系数，来自最近一次 Freq 计算）
 * @param  power_a    当前A通道功率（用于小功率归零判断）
 * @param  out_angle  输出：相角 (度)
 */
void HLW811X_Calc_Angle(const u8 rx_buf[2], float freq, float power_a,
                        float *out_angle);
/* #endif /* HLW811X_FREQ_ANGLE_ENABLE */

#if HLW811X_POWER_FACTOR_ENABLE
/**
 * @brief  计算功率因数
 * @param  rx_buf[3]  原始寄存器数据（3字节，PF 寄存器）
 * @param  power_a    当前A通道功率（用于小功率归零判断）
 * @param  out_pf     输出：功率因数
 */
void HLW811X_Calc_PF(const u8 rx_buf[3], float power_a, float *out_pf);
/* #endif /* HLW811X_POWER_FACTOR_ENABLE */

#ifdef __cplusplus
}
#endif

/* #endif /* __HLW8112_CALC_H */
