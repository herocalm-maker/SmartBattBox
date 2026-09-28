/**
 * @file    hlw8112_regs.h
 * @brief   HLW8110/HLW8112 芯片寄存器映射与位定义
 * @details 本文件来自芯片数据手册，包含：
 *          1. UART 通信协议命令字节
 *          2. 全部寄存器地址
 *          3. SYSCON / EMUCON / EMUCON2 寄存器位定义
 *          4. IE / INT 寄存器定义
 * @note    用户无需修改此文件。寄存器配置值请在 hlw8112_config.h 中修改。
 * @version V1.0
 * @date    2026-06-03 从 hlw8112.h 中拆分出来
 */

#ifndef __HLW8112_REGS_H
#define __HLW8112_REGS_H

/*============================================================
 * HLW8112 UART 通信协议命令字节
 * 命令格式: [0xA5] [操作码/地址] [子命令/数据...] [校验和]
 *============================================================*/
/* #define HLW811X_CMD_PREAMBLE        0xA5    /* 命令起始字节 (所有命令都以 0xA5 开头) */
/* #define HLW811X_CMD_OP_WRITE_EN     0xEA    /* 写入寄存器操作码 (配合子命令使用) */
/* #define HLW811X_CMD_SUB_ENABLE      0xE5    /* 写使能子命令 — 允许写入寄存器 */
/* #define HLW811X_CMD_SUB_DISABLE     0xDC    /* 写禁止子命令 — 禁止写入寄存器 */
/* #define HLW811X_CMD_SUB_CH_A        0x5A    /* 选择A通道 (视在功率/功率因数/相角用) */
/* #define HLW811X_CMD_SUB_CH_B        0xA5    /* 选择B通道 */
/* #define HLW811X_CMD_SUB_RESET       0x96    /* 芯片复位命令 */
/* #define HLW811X_CMD_READ_MASK       0x80    /* 写寄存器时地址最高位置1 */

/*============================================================
 * HLW8110/HLW8112 寄存器地址表
 *============================================================*/
/* #define REG_SYSCON_ADDR         0x00    /* 系统控制寄存器 */
/* #define REG_EMUCON_ADDR         0x01    /* 计量控制寄存器 */
/* #define REG_HFCONST_ADDR        0x02    /* 脉冲频率寄存器 */
/* #define REG_PA_START_ADDR       0x03    /* 通道A有功起动功率设置 */
/* #define REG_PB_START_ADDR       0x04    /* 通道B有功起动功率设置 */
/* #define REG_PA_GAIN_ADDR        0x05    /* 通道A功率增益校准寄存器 */
/* #define REG_PB_GAIN_ADDR        0x06    /* 通道B率增益校准寄存器 */
/* #define REG_A_PHASE_ADDR        0x07    /* 通道A相位校准寄存器 */
/* #define REG_B_PHASE_ADDR        0x08    /* 通道B相位校准寄存器 */
/* #define REG_RmsPAOS_ADDR        0x0A    /* 通道A有功功率Offset校准 */
/* #define REG_RmsPBOS_ADDR        0x0B    /* 通道B有功功率Offset校准 */

/* #define REG_RmsIAOS_ADDR        0x0E    /* 电流通道A有效值Offset补偿 */
/* #define REG_RmsIBOS_ADDR        0x0F    /* 电流通道B有效值Offset补偿 */
/* #define REG_IB_GAIN_ADDR        0x10    /* 电流通道B增益设置 */
/* #define REG_PS_GAIN_ADDR        0x11    /* 视在功率增益校准 */
/* #define REG_PS_OS_ADDR          0x12    /* 视在功率Offset补偿 */
/* #define REG_EMUCON2_ADDR        0x13    /* 计量控制寄存器2 */
/* #define REG_DCIA_ADDR           0x14    /* IA通道直流offset校正寄存器 */
/* #define REG_DCIB_ADDR           0x15    /* IB通道直流offset校正寄存器 */
/* #define REG_DCIC_U_ADDR         0x16    /* U通道直流offset校正寄存器 */
/* #define REG_SAGCYC_ADDR         0x17    /* 电压骤降周期设置 */
/* #define REG_SAGLVL_ADDR         0x18    /* 电压骤降阈值设置 */
/* #define REG_OVLVL_ADDR          0x19    /* 电压过压阈值设置 */
/* #define REG_OIALVL_ADDR         0x1A    /* 电流通道A过流阈值设置 */
/* #define REG_OIBLVL_ADDR         0x1B    /* 电流通道B过流阈值设置 */
/* #define REG_OPLVL_ADDR          0x1C    /* 有功功率过载阈值设置 */
/* #define REG_INT_ADDR            0x1D    /* INT1默认是输出PFA,NT2默认是输出PFB */

/* #define REG_PFCNT_PA_ADDR       0x20    /* 通道A快速组合有功脉冲计数 */
/* #define REG_PFCNT_PB_ADDR       0x21    /* 通道B快速组合有功脉冲计数 */
/* #define REG_ANGLE_ADDR          0x22    /* 相角寄存器,电流与电压之间的相角 */
/* #define REG_UFREQ_ADDR          0x23    /* 电压频率,市电线性频率 */
/* #define REG_RMSIA_ADDR          0x24    /* 通道A电流的有效值 */
/* #define REG_RMSIB_ADDR          0x25    /* 通道B电流的有效值 */
/* #define REG_RMSU_ADDR           0x26    /* 电压有效值 */
/* #define REG_PF_ADDR             0x27    /* 功率因数寄存器 */
/* #define REG_ENERGY_PA_ADDR      0x28    /* 通道A有功电能 */
/* #define REG_ENERGY_PB_ADDR      0x29    /* 通道B有功电能 */
/* #define REG_POWER_PA_ADDR       0x2C    /* 通道A有功功率 */
/* #define REG_POWER_PB_ADDR       0x2D    /* 通道B有功功率 */
/* #define REG_POWER_PS_ADDR       0x2E    /* 视在功率 */
/* #define REG_EMUSTATUS_ADDR      0x2F    /* 计量状态及校验和寄存器 */
/* #define REG_PEAKIA_ADDR         0x30    /* 电流通道A的峰值 */
/* #define REG_PEAKIB_ADDR         0x31    /* 电流通道B的峰值 */
/* #define REG_PEAKU_ADDR          0x32    /* 电压通道U的峰值 */
/* #define REG_IA_INSTAN_ADDR      0x33    /* 电流通道A瞬时值 */
/* #define REG_IB_INSTAN_ADDR      0x34    /* 电流通道B瞬时值 */
/* #define REG_U_INSTAN_ADDR       0x35    /* 电压通道瞬时值 */
/* #define REG_WAVEIA_ADDR         0x36    /* 电流通道A波形 */
/* #define REG_WAVEIB_ADDR         0x37    /* 电流通道B波形 */
/* #define REG_WAVEU_ADDR          0x38    /* 电压通道波形 */

/* #define REG_P_INSTAN_ADDR       0x3C    /* 有功功率瞬时值 */
/* #define REG_PS_INSTAN_ADDR      0x3D    /* 视在功率瞬时值 */

/* #define REG_IE_ADDR             0x40    /* 中断允许寄存器 */
/* #define REG_IF_ADDR             0x41    /* 中断标志寄存器 （不可写） */
/* #define REG_RIF_ADDR            0x42    /* 复位中断状态寄存器 */
/* #define REG_SYSSTATUS_ADDR      0x43    /* 系统状态寄存器 */
/* #define REG_RDATA_ADDR          0x44    /* 上一次SPI读出的数据 */
/* #define REG_WDATA_ADDR          0x45    /* 上一次SPI写入的数据 */

#define REG_SPAREC9_ADDR        0x6E

/* #define REG_CHECKSUM_ADDR       0x6F    /* 校表系数校验和 */
/* #define REG_RMS_IAC_ADDR        0x70    /* 电流通道A有效值转换系数 */
/* #define REG_RMS_IBC_ADDR        0x71    /* 电流通道B有效值转换系数 */
/* #define REG_RMS_UC_ADDR         0x72    /* 电压通道U有效值转换系数 */
/* #define REG_POWER_PAC_ADDR      0x73    /* 电流通道A有功功率转换系数 */
/* #define REG_POWER_PBC_ADDR      0x74    /* 电流通道B有功功率转换系数 */
/* #define REG_POWER_SC_ADDR       0x75    /* 视在功率转换系数 */
/* #define REG_ENERGY_AC_ADDR      0x76    /* A通道能量转换系数 */
/* #define REG_ENERGY_BC_ADDR      0x77    /* B通道能量转换系数 */

/* #define REG_TRIM_RC_ADDR        0x7C    /* 内置时钟 Trim寄存器 */
/* #define REG_TRIM_VREF_ADDR      0x7D    /* 内置VrefTrim寄存器 */

/*============================================================
 * SYSCON 寄存器位定义 (ADDR:0x00, default:0x0A04)
 *============================================================*/
/* 通道使能 */
/* #define U_ON    0x0800  /* 开启电压通道 */
/* #define U_OFF   0x0000  /* 关闭电压通道 */
/* #define IB_ON   0x0400  /* 开启电流通道B */
/* #define IB_OFF  0x0000  /* 关闭电流通道B */
/* #define IA_ON   0x0200  /* 开启电流通道A */
/* #define IA_OFF  0x0000  /* 关闭电流通道A */

/* 电流A通道 PGA */
/* #define PGAIA_16    0x0004  /* ×16 */
/* #define PGAIA_08    0x0003  /* ×8  */
/* #define PGAIA_04    0x0002  /* ×4  */
/* #define PGAIA_02    0x0001  /* ×2  */
/* #define PGAIA_01    0x0000  /* ×1  */

/* 电流B通道 PGA */
/* #define PGAIB_16    0x0100  /* ×16 */
/* #define PGAIB_08    0x00C0  /* ×8  */
/* #define PGAIB_04    0x0080  /* ×4  */
/* #define PGAIB_02    0x0040  /* ×2  */
/* #define PGAIB_01    0x0000  /* ×1  */

/* 电压通道 PGA */
/* #define PGAU_16     0x0020  /* ×16 */
/* #define PGAU_08     0x0018  /* ×8  */
/* #define PGAU_04     0x0010  /* ×4  */
/* #define PGAU_02     0x0008  /* ×2  */
/* #define PGAU_01     0x0000  /* ×1  */

/*============================================================
 * EMUCON 寄存器位定义 (ADDR:0x01, default:0x0000)
 *============================================================*/
/* 温度测量步骤 (bit15-14) */
/* #define TEMPERATUR_STEP1    0x0000  /* STEP-1, default */
/* #define TEMPERATUR_STEP2    0x4000  /* STEP-2 */
/* #define TEMPERATUR_STEP3    0x8000  /* STEP-3 */
/* #define TEMPERATUR_STEP4    0xC000  /* STEP-4 */

/* B通道温度测量 (bit13) */
/* #define TEMPERATUR_OFF      0x0000  /* 关闭, default */
/* #define TEMPERATUR_ON       0x2000  /* 开启 */

/* B通道比较器 (bit12) */
/* #define COMP_OFF            0x0000  /* 关闭, default */
/* #define COMP_ON             0x1000  /* 开启 */

/* 有功电能计算方式 (bit11-10) */
/* #define PMODE_00    0x0000  /* 正反向代数和, default */
/* #define PMODE_01    0x0400  /* 只累加正向 */
/* #define PMODE_10    0x0800  /* 正反向绝对值 */
/* #define PMODE_11    0x0C00  /* 保留 */

/* 有效值工作模式 (bit9) */
/* #define DCMODE_OFF  0x0000  /* 正常模式, default */
/* #define DCMODE_ON   0x0200  /* 直通模式 */

/* 过零模式 (bit8-7) */
/* #define ZERO_UP     0x0000  /* 正向过零, default */
/* #define ZERO_DOWN   0x0080  /* 负向过零 */
/* #define ZERO_ALL    0x0100  /* 双向过零 */

/* 高通滤波器 (bit6-4) */
/* #define IBHPF_ON    0x0000  /* B通道HPF开启(交流), default */
/* #define IBHPF_OFF   0x0040  /* B通道HPF关闭(直流) */
/* #define IAHPF_ON    0x0000  /* A通道HPF开启(交流), default */
/* #define IAHPF_OFF   0x0020  /* A通道HPF关闭(直流) */
/* #define UHPF_ON     0x0000  /* 电压HPF开启(交流), default */
/* #define UHPF_OFF    0x0010  /* 电压HPF关闭(直流) */

/* 脉冲/电能使能 (bit1-0) */
/* #define PFB_OFF     0x0000  /* B通道脉冲关闭, default */
/* #define PFB_ON      0x0002  /* B通道脉冲使能 */
/* #define PFA_OFF     0x0000  /* A通道脉冲关闭, default */
/* #define PFA_ON      0x0001  /* A通道脉冲使能 */

/*============================================================
 * EMUCON2 寄存器位定义 (ADDR:0x13, default:0x0001)
 *============================================================*/
/* bit15 */
/* #define DOTPSEL0    0x0000  /* default */
#define DOTPSEL1    0x8000

/* SDO输出模式 (bit12) */
/* #define CMOS_OUTPUT         0x0000  /* CMOS输出, default */
/* #define OPEN_DRAIN_OUTPUT   0x1000  /* 开漏输出 */

/* 电能寄存器读后行为 (bit11-10) */
/* #define ENERGY_PB_READ_CLEAR    0x0000  /* B通道读后清零, default */
/* #define ENERGY_PB_READ          0x0800  /* B通道读后不清零 */
/* #define ENERGY_PA_READ_CLEAR    0x0000  /* A通道读后清零, default */
/* #define ENERGY_PA_READ          0x0400  /* A通道读后不清零 */

/* 数据更新速率 (bit9-8) */
/* #define DataUpdate_3HZ   0x0000  /* 3.4Hz, default */
/* #define DataUpdate_6HZ   0x0100  /* 6.8Hz */
/* #define DataUpdate_13HZ  0x0200  /* 13.65Hz */
/* #define DataUpdate_27HZ  0x0300  /* 27.3Hz */

/* B通道功能选择 (bit7) */
/* #define IB_TEMPSENER_ON     0x0000  /* 温度测量, default */
/* #define IB_CURRENT_ON       0x0080  /* 电流测量 */

/* 功率因数/波形/检测使能 (bit6-0) */
/* #define PF_OFF      0x0000  /* 关闭功率因数 */
/* #define PF_ON       0x0040  /* 开启功率因数 */
/* #define WAVE_OFF    0x0000  /* 关闭波形数据 */
/* #define WAVE_ON     0x0020  /* 开启波形数据 */
/* #define SAGEN_OFF   0x0000  /* 关闭电压跌落 */
/* #define SAGEN_ON    0x0010  /* 开启电压跌落 */
/* #define OVEREN_OFF  0x0000  /* 关闭过压/过流检测 */
/* #define OVEREN_ON   0x0008  /* 开启过压/过流检测 */
/* #define Zx_OFF      0x0000  /* 关闭过零检测 */
/* #define Zx_ON       0x0004  /* 开启过零检测 */
/* #define PEAK_OFF    0x0000  /* 关闭峰值检测 */
/* #define PEAK_ON     0x0002  /* 开启峰值检测 */
/* #define VREFSEL_OFF 0x0000  /* 关闭内置基准 */
/* #define VREFSEL_ON  0x0001  /* 开启内置基准, default */

/*============================================================
 * IE 中断使能寄存器 (ADDR:0x40, default:0x0000)
 *============================================================*/
/* #define IE_B_Leakgae    0x8000  /* B通道漏电中断使能 */
/* #define IE_V_ZERO       0x4000  /* 电压过零中断使能 */
/* #define IE_IA_ZERO      0x2000  /* 电流A过零中断使能 */
/* #define IE_IB_ZERO      0x1000  /* 电流B过零中断使能 */
/* #define IE_SAG_V        0x0800  /* 电压欠压中断使能 */
/* #define IE_OVER_P       0x0400  /* 功率过载中断使能 */
/* #define IE_OVER_V       0x0200  /* 电压过压中断使能 */
/* #define IE_OVER_IB      0x0100  /* 电流B过流中断使能 */
/* #define IE_OVER_IA      0x0080  /* 电流A过流中断使能 */
/* #define IE_INSTAN       0x0040  /* 瞬时值更新中断使能 */
/* #define IE_PEB_OIE      0x0010  /* B通道电能溢出中断使能 */
/* #define IE_PEA_OIE      0x0008  /* A通道电能溢出中断使能 */
/* #define IE_PFB          0x0004  /* PFB脉冲输出中断使能 */
/* #define IE_PFA          0x0002  /* PFA脉冲输出中断使能 */
/* #define IE_DUPDIE       0x0001  /* 均值数据更新中断使能 */

/*============================================================
 * INT 中断输出配置 (ADDR:0x1D, default:0x3210)
 *============================================================*/
/* INT2 输出功能选择 (bit7-4) */
#define INT2_PFA        0x00
#define INT2_PFB        0x10
#define INT2_B_Leakgae  0x20
#define INT2_IRQ        0x30
#define INT2_OVER_P     0x40
#define INT2_A_REVP     0x50
#define INT2_B_REVP     0x60
#define INT2_INSTAN     0x70
#define INT2_RMSDATA    0x80
#define INT2_V_ZERO     0x90
#define INT2_IA_ZERO    0xa0
#define INT2_IB_ZERO    0xb0
#define INT2_OVER_V     0xc0
#define INT2_SAG_V      0xd0
#define INT2_OVER_IA    0xe0
#define INT2_OVER_IB    0xf0

/* INT1 输出功能选择 (bit3-0) */
#define INT1_PFA        0x00
#define INT1_PFB        0x01
#define INT1_B_Leakgae  0x02
#define INT1_IRQ        0x03
#define INT1_OVER_P     0x04
#define INT1_A_REVP     0x05
#define INT1_B_REVP     0x06
#define INT1_INSTAN     0x07
#define INT1_RMSDATA    0x08
#define INT1_V_ZERO     0x09
#define INT1_IA_ZERO    0x0a
#define INT1_IB_ZERO    0x0b
#define INT1_OVER_V     0x0c
#define INT1_SAG_V      0x0d
#define INT1_OVER_IA    0x0e
#define INT1_OVER_IB    0x0f

/* #endif /* __HLW8112_REGS_H */
