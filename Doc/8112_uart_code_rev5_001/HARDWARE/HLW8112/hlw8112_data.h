/**
 * @file    hlw8112_data.h
 * @brief   HLW8112/HLW8110 全驱动数据模型定义
 * @details 本文件定义全部数据类型（枚举/结构体）并声明所有 extern 全局变量。
 *          全局变量沿用旧命名（F_AC_V、U16_RMSIAC_RegData 等），
 *          在 hlw8112_reader.c 中定义，各模块通过 extern 直接访问。
 *          结构体类型保留在此作为文档参考，实际使用全局变量。
 *
 * @version V1.2 全局变量方案（C51 兼容）
 * @date    2026-06-05
 */

#ifndef __HLW8112_DATA_H
#define __HLW8112_DATA_H

/* #include "hlw8112_port.h"       /* u8/u16/u32 类型定义 */
/* #include "hlw8112_config.h"    /* 功能开关宏 */

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================
 * 结构体类型定义（仅供文档参考与组内封装，实际访问用全局变量）
 *============================================================*/

/* 测量结果结构体 */
typedef struct {
    float   voltage;
    float   current_a;
    float   power_a;
    float   energy_a;
    float   current_b;
    float   power_b;
    float   energy_b;
    float   apparent_power;
    float   power_factor;
    float   freq;
    float   angle;
    u32     voltage_reg;
    u32     current_a_reg;
    u32     power_a_reg;
    u32     energy_a_reg;
    u32     current_b_reg;
    u32     power_b_reg;
    u32     energy_b_reg;
    u32     apparent_power_reg;
} HLW811X_MeasureResult_t;

/* 校准系数结构体 */
typedef struct {
    u16     rmsiac;
    u16     rmsibc;
    u16     rmsuc;
    u16     powerpac;
    u16     powerpbc;
    u16     powersc;
    u16     energyac;
    u16     energybc;
    u16     checksum_reg;
    u16     checksum_calc;
} HLW811X_CalCoef_t;

/* 芯片状态枚举 */
typedef enum {
    HLW811X_CHIP_STATE_INIT = 0,
    HLW811X_CHIP_STATE_WORK,
    HLW811X_CHIP_STATE_RESET
} HLW811X_ChipState_t;

/* 初始化状态机子状态（精简为 6 态） */
typedef enum {
    HLW811X_READER_INIT_IDLE = 0,
    HLW811X_READER_INIT_REG_WRITE,
    HLW811X_READER_INIT_REG_VERIFY,
    HLW811X_READER_INIT_CAL_READ,
    HLW811X_READER_INIT_CAL_CHKSUM,
    HLW811X_READER_INIT_DONE
} HLW811X_Reader_InitState_t;

/* 主读取状态机状态 */
typedef enum {
    HLW811X_READER_STATE_IDLE = 0,
    HLW811X_READER_STATE_SEND_CMD,
    HLW811X_READER_STATE_WAIT_RX,
    HLW811X_READER_STATE_PROCESS_DATA,
    HLW811X_READER_STATE_ERROR_DELAY,
    HLW811X_READER_STATE_NEXT_ITEM,
    HLW811X_READER_STATE_COMPLETE
} HLW811X_Reader_State_t;

/* 测量项枚举 */
typedef enum {
    HLW811X_READER_ITEM_U = 0,
    HLW811X_READER_ITEM_IA,
    HLW811X_READER_ITEM_PA,
    HLW811X_READER_ITEM_EA,
    HLW811X_READER_ITEM_IB,
    HLW811X_READER_ITEM_PB,
    HLW811X_READER_ITEM_EB,
    HLW811X_READER_ITEM_PSC,
    HLW811X_READER_ITEM_FREQ,
    HLW811X_READER_ITEM_ANGLE,
    HLW811X_READER_ITEM_PF,
    HLW811X_READER_ITEM_SYSSTATUS,
    HLW811X_READER_ITEM_MAX
} HLW811X_Reader_Item_t;

/*============================================================
 * 全局变量 extern 声明
 *============================================================*/

/* --- 状态机状态变量（hlw8112_reader.c 定义） --- */
/* extern volatile u8  g_hlw811x_reader_state;        /* 主状态机状态 */
/* extern volatile u8  g_hlw811x_reader_item;          /* 当前测量项 */
/* extern volatile u8  g_hlw811x_reader_ready;         /* 数据就绪标志 */
/* extern volatile u8  g_hlw811x_reader_start;         /* 启动测量标志 */
/* extern volatile u8  g_hlw811x_reader_timeout;       /* 超时计数器 (2ms/tick) */
/* extern volatile u32 g_hlw811x_reader_errors;        /* 错误计数 */
/* extern volatile u32 g_hlw811x_reader_loops;         /* 循环计数 */
/* extern volatile u8  g_hlw811x_reader_err_dly;       /* 出错延时计数器 */
/* extern volatile u8  g_hlw811x_reader_rst_det;       /* 芯片复位检测标志 */
/* extern volatile u8  g_hlw811x_reader_sysstat;       /* SYSSTATUS寄存器原始值 */
/* extern volatile u8  g_hlw811x_reader_read_error;    /* 读取校验错误标志 */

/* --- 初始化状态机变量（hlw8112_reader.c 定义） --- */
/* extern volatile u8  g_hlw811x_reader_init_st;       /* 初始化状态 */
/* extern volatile u8  g_hlw811x_reader_init_done;     /* 初始化完成标志 */
/* extern volatile u8  g_hlw811x_reader_init_step;     /* 当前子步骤 */
/* extern volatile u8  g_hlw811x_reader_init_retry;    /* 重试计数 */

/* --- 芯片生命周期状态 --- */
/* extern volatile u8  g_hlw811x_chip_state;           /* 芯片状态 */

/* --- 通道选择 --- */
extern u8  g_hlw811x_channel;

/* --- 测量结果 --- */
/* extern float  F_AC_V;               /* 电压有效值 (V) */
/* extern float  F_AC_I_A;             /* A通道电流 (A) */
/* extern float  F_AC_P_A;             /* A通道有功功率 (W) */
/* extern float  F_AC_E_A;             /* A通道有功电能 (KWH) */
/* extern float  F_AC_I_B;             /* B通道电流 (A) */
/* extern float  F_AC_P_B;             /* B通道有功功率 (W) */
/* extern float  F_AC_E_B;             /* B通道有功电能 (KWH) */
/* extern float  F_AC_P_SC;            /* 视在功率 (W) */
/* extern float  F_AC_PF;              /* 功率因数 */
/* extern float  F_AC_LINE_Freq;       /* 市电频率 (Hz) */
/* extern float  F_Angle;              /* 相角 (度) */

/* --- 寄存器原始值 --- */
extern u32 U32_RMSU_RegData;
extern u32 U32_RMSIA_RegData;
extern u32 U32_POWERPA_RegData;
extern u32 U32_ENERGY_PA_RegData;
extern u32 U32_RMSIB_RegData;
extern u32 U32_POWERPB_RegData;
extern u32 U32_ENERGY_PB_RegData;
extern u32 U32_POWERPS_RegData;

/* --- 校准系数 --- */
extern u16 U16_RMSIAC_RegData;
extern u16 U16_RMSIBC_RegData;
extern u16 U16_RMSUC_RegData;
extern u16 U16_PowerPAC_RegData;
extern u16 U16_PowerPBC_RegData;
extern u16 U16_PowerSC_RegData;
extern u16 U16_EnergyAC_RegData;
extern u16 U16_EnergyBC_RegData;
extern u16 U16_CheckSUM_RegData;
extern u16 U16_CheckSUM_Data;

#ifdef __cplusplus
}
#endif

/* #endif /* __HLW8112_DATA_H */
