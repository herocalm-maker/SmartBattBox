/**
 * @file    hlw8112_calc.c
 * @brief   HLW8112/HLW8110 物理量换算实现（纯数学，零全局依赖）
 * @details 所有 Calc 函数为纯数学换算函数，输入通过参数传递，
 *          输出通过指针返回。不访问任何全局变量或外部数据。
 *          可脱离硬件做独立单元测试。
 *
 * @note    换算公式核心逻辑保留自 hlw8112.c 原始代码（禁止修改）
 *          Keil ARMCC v5 不支持 0x 十六进制浮点常量，已全部转换为十进制。
 * @version V1.0
 * @date    2026-06-05 重构实现
 */

#include "hlw8112_calc.h"
/* #include "hlw8112_calib.h"          /* D_CAL_*, D_K1, D_K2 — 编译时常量 */

/*============================================================
 * 电压换算
 *  公式: reg * coeff / 4194304 (=0x400000) / K2 / 100 * CAL_U
 *  溢出保护: bit23=1 → 输出0
 *  禁止改动
 *============================================================*/
void HLW811X_Calc_Voltage(const u8 rx_buf[3], u16 coeff_u,
                          u32 *out_reg, float *out_v)
{
    float a;
    u32 reg = ((u32)rx_buf[0] << 16) | ((u32)rx_buf[1] << 8) | (u32)rx_buf[2];

    *out_reg = reg;

    if (reg & 0x800000) {
        *out_v = 0.0f;
    } else {
        a = (float)reg;
        a = a * (float)coeff_u;
/*         a = a / 4194304.0f;         /* 0x400000 */
        a = a / D_K2;
        a = a / 100.0f;
        a = a * D_CAL_U;
        *out_v = a;
    }
}

/*============================================================
 * A通道电流换算
 *  公式: reg * coeff / 8388608 (=0x800000) / K1 / 1000 * CAL_A_I
 *  溢出保护: bit23=1 → 输出0
 *  禁止改动
 *============================================================*/
#if HLW811X_CHANNEL_A_ENABLE
void HLW811X_Calc_IA(const u8 rx_buf[3], u16 coeff_ia,
                     u32 *out_reg, float *out_ia)
{
    float a;
    u32 reg = ((u32)rx_buf[0] << 16) | ((u32)rx_buf[1] << 8) | (u32)rx_buf[2];

    *out_reg = reg;

    if (reg & 0x800000) {
        *out_ia = 0.0f;
    } else {
        a = (float)reg;
        a = a * (float)coeff_ia;
/*         a = a / 8388608.0f;         /* 0x800000 */
        a = a / D_K1;
        a = a / 1000.0f;
        a = a * D_CAL_A_I;
        *out_ia = a;
    }
}

/**
 * A通道功率换算
 *  公式: reg * coeff / 2147483648 (=0x80000000) / K1 / K2 * CAL_A_P
 *  符号处理: reg > 0x80000000 → 按位取反
 *  禁止改动
 */
void HLW811X_Calc_PA(const u8 rx_buf[4], u16 coeff_pa,
                     u32 *out_reg, float *out_pa)
{
    float a, b;
    u32 reg = ((u32)rx_buf[0] << 24) | ((u32)rx_buf[1] << 16) |
              ((u32)rx_buf[2] << 8)  | (u32)rx_buf[3];

    *out_reg = reg;

    if (reg > 0x80000000UL) {
        b = (float)(~reg);
    } else {
        b = (float)reg;
    }
    a = b;
    a = a * (float)coeff_pa;
/*     a = a / 2147483648.0f;         /* 0x80000000 */
    a = a / D_K1;
    a = a / D_K2;
    a = a * D_CAL_A_P;
    *out_pa = a;
}

/**
 * A通道电能换算
 *  公式: reg * coeff / 536870912 (=0x20000000) / K1 / K2 * CAL_A_E
 *  禁止改动
 */
void HLW811X_Calc_EA(const u8 rx_buf[3], u16 coeff_ea,
                     u32 *out_reg, float *out_ea)
{
    float a;
    u32 reg = ((u32)rx_buf[0] << 16) | ((u32)rx_buf[1] << 8) | (u32)rx_buf[2];

    *out_reg = reg;

    a = (float)reg;
    a = a * (float)coeff_ea;
/*     a = a / 536870912.0f;          /* 0x20000000 */
    a = a / D_K1;
    a = a / D_K2;
    a = a * D_CAL_A_E;
    *out_ea = a;
}
/* #endif /* HLW811X_CHANNEL_A_ENABLE */

/*============================================================
 * B通道电流/功率/电能换算
 *  公式同A通道，使用 B 通道校正系数
 *  禁止改动
 *============================================================*/
#if HLW811X_CHANNEL_B_ENABLE
void HLW811X_Calc_IB(const u8 rx_buf[3], u16 coeff_ib,
                     u32 *out_reg, float *out_ib)
{
    float a;
    u32 reg = ((u32)rx_buf[0] << 16) | ((u32)rx_buf[1] << 8) | (u32)rx_buf[2];

    *out_reg = reg;

    if (reg & 0x800000) {
        *out_ib = 0.0f;
    } else {
        a = (float)reg;
        a = a * (float)coeff_ib;
/*         a = a / 8388608.0f;         /* 0x800000 */
        a = a / D_K1;
        a = a / 1000.0f;
        a = a * D_CAL_B_I;
        *out_ib = a;
    }
}

void HLW811X_Calc_PB(const u8 rx_buf[4], u16 coeff_pb,
                     u32 *out_reg, float *out_pb)
{
    float a, b;
    u32 reg = ((u32)rx_buf[0] << 24) | ((u32)rx_buf[1] << 16) |
              ((u32)rx_buf[2] << 8)  | (u32)rx_buf[3];

    *out_reg = reg;

    if (reg > 0x80000000UL) {
        b = (float)(~reg);
    } else {
        b = (float)reg;
    }
    a = b;
    a = a * (float)coeff_pb;
/*     a = a / 2147483648.0f;         /* 0x80000000 */
    a = a / D_K1;
    a = a / D_K2;
    a = a * D_CAL_B_P;
    *out_pb = a;
}

void HLW811X_Calc_EB(const u8 rx_buf[3], u16 coeff_eb,
                     u32 *out_reg, float *out_eb)
{
    float a;
    u32 reg = ((u32)rx_buf[0] << 16) | ((u32)rx_buf[1] << 8) | (u32)rx_buf[2];

    *out_reg = reg;

    a = (float)reg;
    a = a * (float)coeff_eb;
/*     a = a / 536870912.0f;          /* 0x20000000 */
    a = a / D_K1;
    a = a / D_K2;
    a = a * D_CAL_B_E;
    *out_eb = a;
}
/* #endif /* HLW811X_CHANNEL_B_ENABLE */

/*============================================================
 * 视在功率换算
 *  公式: reg * coeff / 2147483648 (=0x80000000) / K1 / K2 * CAL_A_P
 *  符号处理: reg > 0x80000000 → 按位取反
 *  禁止改动
 *============================================================*/
#if HLW811X_APPARENT_POWER_ENABLE
void HLW811X_Calc_PSC(const u8 rx_buf[4], u16 coeff_ps,
                      u32 *out_reg, float *out_ps)
{
    float a, b;
    u32 reg = ((u32)rx_buf[0] << 24) | ((u32)rx_buf[1] << 16) |
              ((u32)rx_buf[2] << 8)  | (u32)rx_buf[3];

    *out_reg = reg;

    if (reg > 0x80000000UL) {
        b = (float)(~reg);
    } else {
        b = (float)reg;
    }
    a = b;
    a = a * (float)coeff_ps;
/*     a = a / 2147483648.0f;         /* 0x80000000 */
    a = a / D_K1;
    a = a / D_K2;
    a = a * D_CAL_A_P;
    *out_ps = a;
}
/* #endif /* HLW811X_APPARENT_POWER_ENABLE */

/*============================================================
 * 频率换算
 *  公式: 3579545 / (8 * reg)
 *  禁止改动
 *============================================================*/
#if HLW811X_FREQ_ANGLE_ENABLE
void HLW811X_Calc_Freq(const u8 rx_buf[2], float *out_freq)
{
    u32 b = ((u32)rx_buf[0] << 8) | (u32)rx_buf[1];
    *out_freq = 3579545.0f / (8.0f * (float)b);
}

/**
 * 相角换算
 *  公式: reg * 0.0805 (freq<55) 或 reg * 0.0965 (freq>=55)
 *  功率小于0.5W时相角强制归零
 *  禁止改动
 */
void HLW811X_Calc_Angle(const u8 rx_buf[2], float freq, float power_a,
                        float *out_angle)
{
    u32 b = ((u32)rx_buf[0] << 8) | (u32)rx_buf[1];
    float a;

    if (freq < 55.0f) {
        a = (float)b * 0.0805f;
    } else {
        a = (float)b * 0.0965f;
    }
    if (power_a < 0.5f) {
        a = 0.0f;
    }
    *out_angle = a;
}
/* #endif /* HLW811X_FREQ_ANGLE_ENABLE */

/*============================================================
 * 功率因数换算
 *  公式: reg / 0x7fffff (正) 或 (0xffffff - reg + 1) / 0x7fffff (负)
 *  功率小于0.3W时PF强制归零
 *  禁止改动
 *============================================================*/
#if HLW811X_POWER_FACTOR_ENABLE
void HLW811X_Calc_PF(const u8 rx_buf[3], float power_a, float *out_pf)
{
    u32 b = ((u32)rx_buf[0] << 16) | ((u32)rx_buf[1] << 8) | (u32)rx_buf[2];
    float a;

    if (b > 0x800000UL) {
        a = (float)(0xffffffUL - b + 1) / 0x7fffff;
    } else {
        a = (float)b / 0x7fffff;
    }
    if (power_a < 0.3f) {
        a = 0.0f;
    }
    *out_pf = a;
}
/* #endif /* HLW811X_POWER_FACTOR_ENABLE */
