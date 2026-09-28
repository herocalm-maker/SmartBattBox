/**
 * @file    drv_hlw8110.h
 * @brief   HLW8110 电能计量驱动
 */
#ifndef __DRV_HLW8110_H__
#define __DRV_HLW8110_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include "bsp_uart.h"
#include "comm_ringbuf.h"



/****************************************************************************
 *  HLW8110 寄存器定义（参考REV1.19手册 + 原厂示例HLW8110.h）
 ***************************************************************************/
/*---------------- 系统/功能配置寄存器 ----------------*/
#define HLW8110_REG_SYSCON        0x00U   //系统控制寄存器；PGA增益、ADC通道开关配置
#define HLW8110_REG_EMUCON        0x01U   //计量控制寄存器；电能累加模式、高通滤波器、PBRUN/PARUN等
#define HLW8110_REG_HFCONST       0x02U   //脉冲频率寄存器；电表常数相关
#define HLW8110_REG_PSTARTPA      0x03U   //A通道无负载启动/潜动阈值
#define HLW8110_REG_PSTARTPB      0x04U   //B通道无负载启动/潜动阈值
#define HLW8110_REG_PAGAIN        0x05U   //A通道有功功率增益校准
#define HLW8110_REG_PBGAIN        0x06U   //B通道有功功率增益校准
#define HLW8110_REG_PHASEA        0x07U   //A通道相位校正寄存器
#define HLW8110_REG_PHASEB        0x08U   //B通道相位校正寄存器
#define HLW8110_REG_PAOS          0x0AU   //A通道有功Offset偏移校准
#define HLW8110_REG_PBOS          0x0BU   //B通道有功Offset偏移校准
#define HLW8110_REG_RMSIAOS       0x0EU   //A电流有效值Offset偏移
#define HLW8110_REG_RMSIBOS       0x0FU   //B电流有效值Offset偏移
#define HLW8110_REG_IBGAIN        0x10U   //B通道电流增益，A/B通道一致性校准
#define HLW8110_REG_PSGAIN        0x11U   //视在功率增益校准
#define HLW8110_REG_PSOS          0x12U   //视在功率Offset偏移校准
#define HLW8110_REG_EMUCON2       0x13U   //计量控制寄存器2；更新速率、波形使能、过零、峰值、SAG、OV使能
#define HLW8110_REG_DCIA          0x14U   //IA通道直流偏置校正
#define HLW8110_REG_DCIB          0x15U   //IB通道直流偏置校正
#define HLW8110_REG_DCIU          0x16U   //U电压通道直流偏置校正
#define HLW8110_REG_SAGCYC        0x17U   //电压骤降周期设置
#define HLW8110_REG_SAGLVL        0x18U   //电压骤降阈值
#define HLW8110_REG_OVLVL         0x19U   //电压过压阈值
#define HLW8110_REG_OIALVL        0x1AU   //A通道过流阈值
#define HLW8110_REG_OIBLVL        0x1BU   //B通道过流阈值
#define HLW8110_REG_OPLVL         0x1CU   //有功功率过载阈值
#define HLW8110_REG_INT_CFG       0x1DU   //INT1/INT2引脚输出功能选择

/*---------------- 测量数据寄存器（只读） ----------------*/
#define HLW8110_REG_ANGLE         0x22U   //相角寄存器；电压与选中电流通道相角，50Hz:0.0805°/LSB
#define HLW8110_REG_UFREQ         0x23U   //电压频率寄存器；f = clk_sys/(8*Ufreq)
#define HLW8110_REG_RMSIA         0x24U   //A通道电流有效值，24bit
#define HLW8110_REG_RMSIB         0x25U   //B通道电流有效值，24bit（可做测温输出）
#define HLW8110_REG_RMSU          0x26U   //电压有效值，24bit
#define HLW8110_REG_PF            0x27U   //功率因数，24bit；0x7FFFFF=1.0，0x800000=-1.0
#define HLW8110_REG_ENERGY_PA     0x28U   //A通道有功电能，24bit，默认读后清零，可配置不清零
#define HLW8110_REG_ENERGY_PB     0x29U   //B通道有功电能，24bit
#define HLW8110_REG_POWER_PA      0x2CU   //A通道平均有功功率，32bit有符号
#define HLW8110_REG_POWER_PB      0x2DU   //B通道平均有功功率，32bit有符号
#define HLW8110_REG_POWER_S       0x2EU   //视在功率，32bit
#define HLW8110_REG_EMU_STATUS    0x2FU   //计量状态寄存器；通道选择、潜动、反向功率、校验和

/*---------------- OTP免校准系数寄存器（只读）原厂示例0x70‑0x77 ----------------*/
#define HLW8110_REG_CHECKSUM      0x6FU   //OTP校表参数校验和
#define HLW8110_REG_RMS_IAC       0x70U   //A电流有效值转换系数
#define HLW8110_REG_RMS_IBC       0x71U   //B电流有效值转换系数
#define HLW8110_REG_RMS_UC        0x72U   //电压有效值转换系数
#define HLW8110_REG_POWER_PAC     0x73U   //A通道有功功率转换系数
#define HLW8110_REG_POWER_PBC     0x74U   //B通道有功功率转换系数
#define HLW8110_REG_POWER_SC      0x75U   //视在功率转换系数
#define HLW8110_REG_ENERGY_AC     0x76U   //A通道电能转换系数
#define HLW8110_REG_ENERGY_BC     0x77U   //B通道电能转换系数

#define HLW8110_REG_DEVICEID      0x7FU   //芯片ID寄存器，3字节固定值 0x77 0x61 0x10

/*---------------- 特殊命令（0xEA命令码） ----------------*/
#define HLW8110_CMD_SPECIAL       0xEAU   //特殊命令前缀码
#define HLW8110_CMD_WR_EN         0xE5U   //写使能：打开写保护寄存器写入权限
#define HLW8110_CMD_WR_DIS        0xDCU   //写关闭：关闭写保护寄存器写入权限
#define HLW8110_CMD_SEL_CHA       0x5AU   //选择A通道做相角/视在/功率因数计算
#define HLW8110_CMD_SEL_CHB       0xA5U   //选择B通道做相角/视在/功率因数计算
#define HLW8110_CMD_RESET         0x96U   //软件复位芯片

#define HLW8110_PREAMBLE          0xA5U    /* 帧头 */
#define HLW8110_REG_W_BIT         0x80U    /* 写命令bit7置1 */

/* 缓冲区尺寸定义 */
#define HLW8110_RX_BUF_SIZE       32U
#define HLW8110_TEMP_BUF_SIZE     32U
#define HLW8110_RING_BUF_SIZE     64U

/* 单寄存器读响应超时 (ms)，9600 波特率下最长响应约 5ms，50ms 留足裕量 */
#define HLW8110_RESP_TIMEOUT_MS   300


/*====================================================================
 * 板级采样硬件配置（对应《移动电池逆变器电路原理图_20260706》第3页）
 *--------------------------------------------------------------------
 * 电流通道: ZEMCT131(2000:1) + 100Ω 负载(R35), 5A→250mV
 *   ⇒ 电流通道 PGA 必须 = 1, 否则 100Ω 负载信号在 5A 即饱和(>35mV@PGA16)
 * 电压通道: ZMPT107-1(2mA) 次级 100Ω 负载, 220V→~200mV ⇒ PGA=1
 *====================================================================*/

/* 免校表归一化系数 K1/K2 (依据芯片手册, 相对出厂参考标定条件)：
 *   出厂参考: 电流 5mV@PGA16→5A; 电压 200mV@PGA1→200V
 *   K1 = (5A/2000×100Ω×1)/(5mV×16)  = 3.125
 *   K2 = (200V/110K×100Ω×1)/200mV    ≈ 0.909
 * 注: 电阻/互感器存在容差, 定型建议用标准源校准后微调. */
#define HLW8110_K1                    3.125f
#define HLW8110_K2                    0.909f

/* 寄存器初始化值 (本板)：
 * SYSCON  = 0x0E00: 三通道全开, PGA_A=1, PGA_U=1, PGA_B=1
 *           (官方参考默认 0x0A04=PGA16 是针对 1Ω 锰铜, 本板 100Ω 负载必须 PGA_A=1)
 * EMUCON  = 0x0001: PFA_RUN=1 使能 A 通道有功电能累加
 * EMUCON2 = 0x0041: PF 计算使能 + VREFSEL */
#define HLW8110_SYSCON_VALUE          0x0E00u
#define HLW8110_EMUCON_VALUE          0x0001u
#define HLW8110_EMUCON2_VALUE         0x0041u

/* OTP 免校表系数后备默认值 (读芯片 OTP 失败时使用, 取原厂出厂参考值) */
#define HLW8110_DEF_RMS_IAC           0xcad1u
#define HLW8110_DEF_RMS_IBC           0xcacfu
#define HLW8110_DEF_RMS_UC            0xa4c3u
#define HLW8110_DEF_POWER_PAC         0xab47u
#define HLW8110_DEF_POWER_PBC         0xab49u
#define HLW8110_DEF_POWER_SC          0xab41u
#define HLW8110_DEF_ENERGY_AC         0xe9ccu
#define HLW8110_DEF_ENERGY_BC         0xe9ceu

/**
 * @brief 板级硬件采样配置 (K1/K2 归一化系数 + 寄存器初始化值)
 * @note  默认使用上表板级值; 如需按批次/校准覆盖, 用 drv_hlw8110_set_hwcfg() 注入
 */
typedef struct
{
    float    k1;         /* 电流归一化系数 (本板 3.125) */
    float    k2;         /* 电压归一化系数 (本板 0.909) */
    uint16_t syscon;     /* SYSCON 寄存器初始化值 (0x0E00) */
    uint16_t emucon;     /* EMUCON 寄存器初始化值 (0x0001) */
    uint16_t emucon2;    /* EMUCON2 寄存器初始化值 (0x0041) */
} hlw8110_hwcfg_t;



/**
 * @brief OTP出厂转换系数，对应原厂示例全局变量
 */
typedef struct
{
    uint16_t rms_iac;
    uint16_t rms_ibc;
    uint16_t rms_uc;
    uint16_t power_pac;
    uint16_t power_pbc;
    uint16_t power_sc;
    uint16_t energy_ac;
    uint16_t energy_bc;
    uint16_t checksum_reg;
} hlw8110_otp_t;

/**
 * @brief 原始寄存器读数结构体，存放芯片原始raw数据
 */
typedef struct {
    uint32_t rms_u;         /* 电压有效值 24bit */
    uint32_t rms_ia;        /* A通道电流有效值24bit */
    uint32_t power_pa;      /* A通道有功功率32bit */
    uint32_t energy_pa;     /* A通道有功电能24bit */
    uint32_t power_factor;  /* 功率因数24bit */
    uint16_t u_freq;        /* 电压频率16bit */
    uint16_t hf_const;      /* 脉冲常数HFCONST */
} hlw8110_data_t;

/**
 * @brief 换算后物理测量值（完全对齐原厂示例换算公式）
 */
typedef struct {
    float voltage;          /* 电压，单位V */
    float current;          /* 电流，单位A */
    float power;            /* 有功功率，单位W，支持反向负功率 */
    float power_factor;     /* 功率因数，范围 [-1.0 ~ +1.0] */
    float energy_kwh;       /* 累计电能，单位kWh */
    float freq_hz;          /* 电网频率，单位Hz */
} hlw8110_measure_t;

/**
 * @brief HLW8110设备实例
 * @note 每个电量计对应1个实例；输入电量计、输出电量计各实例一份
 */
typedef struct {
    uint8_t             id;
    bsp_uart_port_t     port;
    hlw8110_otp_t       otp;        /* 芯片OTP出厂系数，每颗芯片不同 */
    hlw8110_hwcfg_t     hw;         /* 板级硬件采样配置 (K1/K2 + 寄存器值) */
    uint8_t             hw_set;     /* 1=已通过 set_hwcfg 显式配置 */

    uint8_t             tempbuff[HLW8110_TEMP_BUF_SIZE];
    comm_ringbuf_t      rx_ring;
    uint8_t             ring_storage[HLW8110_RING_BUF_SIZE];
    uint8_t             uart_rx_buf[HLW8110_RX_BUF_SIZE];
} hlw8110_t;




/* ========= 对外API接口 ========= */
extern bool drv_hlw8110_init(hlw8110_t *inst, bsp_uart_port_t port);
extern void drv_hlw8110_deinit(hlw8110_t *inst);
/* 握手：读 DeviceID (0x7F) 验证通信，接线/波特率/校验位排查的第一手段 */
extern bool drv_hlw8110_handshake(hlw8110_t *inst);
extern bool drv_hlw8110_load_otp(hlw8110_t *inst);
/* 批量读取原始寄存器数据 */
extern bool drv_hlw8110_read_data(hlw8110_t *inst, hlw8110_data_t *out);
/* 读取并且换算得到物理量，使用OTP出厂系数，参考原厂示例计算公式 */
extern bool drv_hlw8110_measure(hlw8110_t *inst, hlw8110_measure_t *out);

/* 注入板级硬件采样配置 (K1/K2 + 寄存器初始化值)，须在 drv_hlw8110_init 之前调用 */
extern bool drv_hlw8110_set_hwcfg(hlw8110_t *inst, const hlw8110_hwcfg_t *cfg);

/* 读取单个寄存器 (len=2/3/4)，大端组装为 uint32；供上层校验寄存器配置 */
extern bool drv_hlw8110_read_reg(hlw8110_t *inst, uint8_t addr, uint8_t len, uint32_t *val);



#ifdef __cplusplus
}
#endif
#endif /* __DRV_HLW8110_H__ */
