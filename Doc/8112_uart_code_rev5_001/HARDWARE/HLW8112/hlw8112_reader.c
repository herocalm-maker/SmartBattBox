/**
 * @file    hlw8112_reader.c
 * @brief   HLW8112/HLW8110 非阻塞数据读取状态机实现（全局变量方案）
 * @details 所有状态变量为全局变量，函数无 ctx 参数。
 *          初始化状态机精简为 6 态（原 10 态）。
 *
 * @version V2.1 全局变量方案（C51 兼容）
 * @date    2026-06-05
 */

#include "hlw8112_reader.h"
/* #include "hlw8112_protocol.h"   /* HLW811X_Proto_* */
/* #include "hlw8112_calc.h"       /* HLW811X_Calc_* */
#include "hlw8112_log.h"
/* #include "hlw8112_hw.h"         /* HLW811X_HW_Print, _TxTimeoutIRQ */

/*============================================================
 * 全局变量定义
 *============================================================*/

/* 主读取状态机 */
volatile u8  g_hlw811x_reader_state   = HLW811X_READER_STATE_IDLE;
volatile u8  g_hlw811x_reader_item    = HLW811X_READER_ITEM_U;
volatile u8  g_hlw811x_reader_ready   = 0;
volatile u8  g_hlw811x_reader_start   = 0;
volatile u8  g_hlw811x_reader_timeout = 0;
volatile u32 g_hlw811x_reader_errors  = 0;
volatile u32 g_hlw811x_reader_loops   = 0;
volatile u8  g_hlw811x_reader_err_dly = 0;
volatile u8  g_hlw811x_reader_rst_det = 0;
volatile u8  g_hlw811x_reader_sysstat = 0;
volatile u8  g_hlw811x_reader_read_error = 0;

/* 初始化状态机 */
volatile u8  g_hlw811x_reader_init_st    = HLW811X_READER_INIT_IDLE;
volatile u8  g_hlw811x_reader_init_done  = 0;
volatile u8  g_hlw811x_reader_init_step  = 0;
volatile u8  g_hlw811x_reader_init_retry = 0;

/* 芯片状态 */
volatile u8  g_hlw811x_chip_state  = HLW811X_CHIP_STATE_INIT;

/* 通道选择 */
u8  g_hlw811x_channel = 0;

/* 测量结果 — 沿用旧命名，与 extern 一致 */
float  F_AC_V;
float  F_AC_I_A, F_AC_P_A, F_AC_E_A;
float  F_AC_I_B, F_AC_P_B, F_AC_E_B;
float  F_AC_P_SC;
float  F_AC_PF;
float  F_AC_LINE_Freq, F_Angle;
u32    U32_RMSU_RegData, U32_RMSIA_RegData, U32_POWERPA_RegData, U32_ENERGY_PA_RegData;
u32    U32_RMSIB_RegData, U32_POWERPB_RegData, U32_ENERGY_PB_RegData;
u32    U32_POWERPS_RegData;

/* 校准系数 — 沿用旧命名 */
u16 U16_RMSIAC_RegData;
u16 U16_RMSIBC_RegData;
u16 U16_RMSUC_RegData;
u16 U16_PowerPAC_RegData;
u16 U16_PowerPBC_RegData;
u16 U16_PowerSC_RegData;
u16 U16_EnergyAC_RegData;
u16 U16_EnergyBC_RegData;
u16 U16_CheckSUM_RegData;
u16 U16_CheckSUM_Data;

/*============================================================
 * 测量项查找表
 *============================================================*/
typedef struct {
    u8  reg_addr;
    u8  rx_len;
} HLW811X_MeasureTableEntry_t;

static const HLW811X_MeasureTableEntry_t s_measure_table[HLW811X_READER_ITEM_MAX] = {
/*     { REG_RMSU_ADDR,       3 },  /* U    */
/*     { REG_RMSIA_ADDR,      3 },  /* IA   */
/*     { REG_POWER_PA_ADDR,   4 },  /* PA   */
/*     { REG_ENERGY_PA_ADDR,  3 },  /* EA   */
/*     { REG_RMSIB_ADDR,      3 },  /* IB   */
/*     { REG_POWER_PB_ADDR,   4 },  /* PB   */
/*     { REG_ENERGY_PB_ADDR,  3 },  /* EB   */
/*     { REG_POWER_PS_ADDR,   4 },  /* PSC  */
/*     { REG_UFREQ_ADDR,      2 },  /* FREQ */
    { REG_ANGLE_ADDR,      2 },  /* ANGLE*/
/*     { REG_PF_ADDR,         3 },  /* PF   */
/*     { REG_SYSSTATUS_ADDR,  1 },  /* SYSSTATUS */
};

/*============================================================
 * 内部函数声明
 *============================================================*/
static void Send_Measure_Command(u8 item);
static void Process_Received_Data(u8 item);
static void InitPhase_RegWrite(void);
static void InitPhase_RegVerify(void);
static void InitPhase_CalRead(void);
static void InitPhase_CalChecksum(void);

/* 校准系数读取地址表 */
static const u8 s_cal_regs[9] = {
    REG_RMS_IAC_ADDR, REG_RMS_IBC_ADDR, REG_RMS_UC_ADDR,
    REG_POWER_PAC_ADDR, REG_POWER_PBC_ADDR, REG_POWER_SC_ADDR,
    REG_ENERGY_AC_ADDR, REG_ENERGY_BC_ADDR, REG_CHECKSUM_ADDR
};

/* 默认校准系数 */
static const u16 s_default_cal[10] = {
    0xcad1, 0xcacf, 0xa4c3, 0xab47, 0xab49,
    0xab41, 0xe9cc, 0xe9ce, 0, 0
};

/*============================================================
 * API 函数实现
 *============================================================*/

void HLW811X_Reader_Reset(void)
{
    g_hlw811x_reader_state   = HLW811X_READER_STATE_IDLE;
    g_hlw811x_reader_item    = HLW811X_READER_ITEM_U;
    g_hlw811x_reader_ready   = 0;
    g_hlw811x_reader_start   = 0;
    g_hlw811x_reader_timeout = 0;
    g_hlw811x_reader_errors  = 0;
    g_hlw811x_reader_loops   = 0;
    g_hlw811x_reader_err_dly = 0;
    g_hlw811x_reader_rst_det = 0;
    g_hlw811x_reader_sysstat = 0;
    g_hlw811x_reader_read_error = 0;
    g_hlw811x_reader_init_st   = HLW811X_READER_INIT_IDLE;
    g_hlw811x_reader_init_done = 0;
    g_hlw811x_reader_init_step = 0;
    g_hlw811x_reader_init_retry= 0;
}

void HLW811X_Reader_Start(void)
{
    if (g_hlw811x_reader_state == HLW811X_READER_STATE_IDLE) {
        g_hlw811x_reader_start = 1;
    }
}

u8 HLW811X_Reader_IsReady(void)
{
    return g_hlw811x_reader_ready;
}

void HLW811X_Reader_Run(void)
{
    switch (g_hlw811x_reader_state) {
        case HLW811X_READER_STATE_IDLE:
            if (g_hlw811x_reader_start) {
                g_hlw811x_reader_start = 0;
                g_hlw811x_reader_item  = HLW811X_READER_ITEM_U;
                g_hlw811x_reader_ready = 0;
                g_hlw811x_reader_state = HLW811X_READER_STATE_SEND_CMD;
            }
            break;

        case HLW811X_READER_STATE_SEND_CMD:
            Send_Measure_Command(g_hlw811x_reader_item);
#if HLW811X_UART_BAUD == 9600
            g_hlw811x_reader_timeout = 5;
#elif HLW811X_UART_BAUD == 19200
            g_hlw811x_reader_timeout = 3;
#elif HLW811X_UART_BAUD == 38400
            g_hlw811x_reader_timeout = 2;
#else
            g_hlw811x_reader_timeout = 5;
#endif
            g_hlw811x_reader_state = HLW811X_READER_STATE_WAIT_RX;
            break;

        case HLW811X_READER_STATE_WAIT_RX:
            if (HLW811X_Proto_IsRxDone()) {
                g_hlw811x_reader_state = HLW811X_READER_STATE_PROCESS_DATA;
            } else if (g_hlw811x_reader_timeout == 0) {
                HLW811X_Proto_ClearRxBuf();
                g_hlw811x_reader_errors++;
                g_hlw811x_reader_err_dly = 10;
                g_hlw811x_reader_state = HLW811X_READER_STATE_ERROR_DELAY;
            }
            break;

        case HLW811X_READER_STATE_PROCESS_DATA:
            Process_Received_Data(g_hlw811x_reader_item);
            if (g_hlw811x_reader_read_error) {
                g_hlw811x_reader_read_error = 0;
                g_hlw811x_reader_err_dly = 15;
                g_hlw811x_reader_state = HLW811X_READER_STATE_ERROR_DELAY;
            } else {
                g_hlw811x_reader_state = HLW811X_READER_STATE_NEXT_ITEM;
            }
            break;

        case HLW811X_READER_STATE_ERROR_DELAY:
            if (g_hlw811x_reader_err_dly == 0) {
                g_hlw811x_reader_state = HLW811X_READER_STATE_NEXT_ITEM;
            }
            break;

        case HLW811X_READER_STATE_NEXT_ITEM:
            g_hlw811x_reader_item++;
            if (g_hlw811x_reader_item < HLW811X_READER_ITEM_MAX) {
                g_hlw811x_reader_state = HLW811X_READER_STATE_SEND_CMD;
            } else {
                g_hlw811x_reader_loops++;
                g_hlw811x_reader_ready = 1;
                g_hlw811x_reader_state = HLW811X_READER_STATE_COMPLETE;
            }
            break;

        case HLW811X_READER_STATE_COMPLETE:
            g_hlw811x_reader_ready = 0;
            g_hlw811x_reader_state = HLW811X_READER_STATE_IDLE;
            break;

        default:
            g_hlw811x_reader_state = HLW811X_READER_STATE_IDLE;
            break;
    }
}

void HLW811X_Reader_TimerIRQ(void)
{
    if (g_hlw811x_reader_timeout > 0) g_hlw811x_reader_timeout--;
    if (g_hlw811x_reader_err_dly > 0) g_hlw811x_reader_err_dly--;
    HLW811X_HW_TxTimeoutIRQ();
}

/*============================================================
 * 内部函数：发送测量命令
 *============================================================*/
static void Send_Measure_Command(u8 item)
{
    if (item >= HLW811X_READER_ITEM_MAX) return;
    HLW811X_Proto_ReadReg(s_measure_table[item].reg_addr,
                          s_measure_table[item].rx_len);
}

/*============================================================
 * 内部函数：处理接收数据 — 调用参数化 Calc 函数
 *============================================================*/
static void Process_Received_Data(u8 item)
{
    u8 rx_buf[10];
    u8 reg_len;

    if (item >= HLW811X_READER_ITEM_MAX) {
        HLW811X_Proto_ClearRxBuf();
        return;
    }

    reg_len = s_measure_table[item].rx_len;
    if (!HLW811X_Proto_GetRxData(rx_buf, reg_len)) {
        g_hlw811x_reader_read_error = 1;
        g_hlw811x_reader_errors++;
        return;
    }

    switch (item) {
        case HLW811X_READER_ITEM_U:
            HLW811X_Calc_Voltage(rx_buf, U16_RMSUC_RegData,
                &U32_RMSU_RegData, &F_AC_V);
            break;
        case HLW811X_READER_ITEM_IA:
            HLW811X_Calc_IA(rx_buf, U16_RMSIAC_RegData,
                &U32_RMSIA_RegData, &F_AC_I_A);
            break;
        case HLW811X_READER_ITEM_PA:
            HLW811X_Calc_PA(rx_buf, U16_PowerPAC_RegData,
                &U32_POWERPA_RegData, &F_AC_P_A);
            break;
        case HLW811X_READER_ITEM_EA:
            HLW811X_Calc_EA(rx_buf, U16_EnergyAC_RegData,
                &U32_ENERGY_PA_RegData, &F_AC_E_A);
            break;
        case HLW811X_READER_ITEM_IB:
            HLW811X_Calc_IB(rx_buf, U16_RMSIBC_RegData,
                &U32_RMSIB_RegData, &F_AC_I_B);
            break;
        case HLW811X_READER_ITEM_PB:
            HLW811X_Calc_PB(rx_buf, U16_PowerPBC_RegData,
                &U32_POWERPB_RegData, &F_AC_P_B);
            break;
        case HLW811X_READER_ITEM_EB:
            HLW811X_Calc_EB(rx_buf, U16_EnergyBC_RegData,
                &U32_ENERGY_PB_RegData, &F_AC_E_B);
            break;
        case HLW811X_READER_ITEM_PSC:
            HLW811X_Calc_PSC(rx_buf, U16_PowerSC_RegData,
                &U32_POWERPS_RegData, &F_AC_P_SC);
            break;
        case HLW811X_READER_ITEM_FREQ:
            HLW811X_Calc_Freq(rx_buf, &F_AC_LINE_Freq);
            break;
        case HLW811X_READER_ITEM_ANGLE:
            HLW811X_Calc_Angle(rx_buf, F_AC_LINE_Freq, F_AC_P_A, &F_Angle);
            break;
        case HLW811X_READER_ITEM_PF:
            HLW811X_Calc_PF(rx_buf, F_AC_P_A, &F_AC_PF);
            break;
        case HLW811X_READER_ITEM_SYSSTATUS:
            if (reg_len >= 1) {
                g_hlw811x_reader_sysstat = rx_buf[0];
                if (rx_buf[0] & 0x01) g_hlw811x_reader_rst_det = 1;
            }
            break;
        default:
            break;
    }
}

/*============================================================
 * GetErrors / PrintErrors
 *============================================================*/
u32 HLW811X_Reader_GetErrors(void)
{
    return g_hlw811x_reader_errors;
}

void HLW811X_Reader_PrintErrors(void)
{
#if HLW811X_LOG_ENABLE
    HLW811X_HW_Print("[HLW811X] ErrCnt: %lu | LoopCnt: %lu\r\n",
                     g_hlw811x_reader_errors, g_hlw811x_reader_loops);
#endif
}

/*============================================================
 * 非阻塞初始化状态机（精简版 6 态）
 *============================================================*/
void HLW811X_Reader_InitChip(void)
{
    switch (g_hlw811x_reader_init_st)
    {
        case HLW811X_READER_INIT_IDLE:
            if (g_hlw811x_chip_state == HLW811X_CHIP_STATE_INIT) {
                g_hlw811x_reader_init_step  = 0;
                g_hlw811x_reader_init_retry = 0;
                g_hlw811x_reader_init_st    = HLW811X_READER_INIT_REG_WRITE;
            }
            break;
        case HLW811X_READER_INIT_REG_WRITE:   InitPhase_RegWrite();    break;
        case HLW811X_READER_INIT_REG_VERIFY:  InitPhase_RegVerify();   break;
        case HLW811X_READER_INIT_CAL_READ:    InitPhase_CalRead();     break;
        case HLW811X_READER_INIT_CAL_CHKSUM:  InitPhase_CalChecksum(); break;
        case HLW811X_READER_INIT_DONE:
            g_hlw811x_reader_init_done = 1;
            break;
        default:
            g_hlw811x_reader_init_st = HLW811X_READER_INIT_IDLE;
            break;
    }
}

u8 HLW811X_Reader_IsInitDone(void)
{
    return g_hlw811x_reader_init_done;
}

/*============================================================
 * 初始化子阶段1: 寄存器写入 (step 0~4)
 *============================================================*/
static void InitPhase_RegWrite(void)
{
    if (g_hlw811x_reader_init_step >= 5) {
        g_hlw811x_reader_init_step = 0;
        g_hlw811x_reader_read_error = 0;
        g_hlw811x_reader_init_st = HLW811X_READER_INIT_REG_VERIFY;
        return;
    }
    if (g_hlw811x_reader_timeout == 0) {
        if (!HLW811X_Proto_IsTxBusy()) {
            switch (g_hlw811x_reader_init_step) {
                case 0: HLW811X_Proto_WriteRegEN();                       break;
                case 1: HLW811X_Proto_WriteReg(REG_SYSCON_ADDR, 2, D_REG_SYSCON_DATA);   break;
                case 2: HLW811X_Proto_WriteReg(REG_EMUCON_ADDR, 2, D_REG_EMUCON_DATA);   break;
                case 3: HLW811X_Proto_WriteReg(REG_EMUCON2_ADDR, 2, D_REG_EMUCON2_DATA); break;
                case 4: HLW811X_Proto_WriteRegDIS();                      break;
            }
            g_hlw811x_reader_timeout = 5;
        }
        return;
    }
    if (HLW811X_Proto_IsTxBusy()) {
        if (g_hlw811x_reader_timeout == 0) {
            g_hlw811x_reader_errors++;
            g_hlw811x_reader_init_step++;
        }
        return;
    }
    g_hlw811x_reader_init_step++;
    g_hlw811x_reader_timeout = 0;
}

/*============================================================
 * 初始化子阶段2: 寄存器回读验证
 *============================================================*/
static void InitPhase_RegVerify(void)
{
    static const u8 verify_regs[3] = {
        REG_SYSCON_ADDR, REG_EMUCON_ADDR, REG_EMUCON2_ADDR
    };
    u8 rx_buf[4];

    if (g_hlw811x_reader_init_step >= 3) {
        goto verify_done;
    }
    if (g_hlw811x_reader_timeout == 0 && !HLW811X_Proto_IsRxDone()) {
        HLW811X_Proto_ReadReg(verify_regs[g_hlw811x_reader_init_step], 2);
        g_hlw811x_reader_timeout = 5;
        return;
    }
    if (!HLW811X_Proto_IsRxDone()) {
        if (g_hlw811x_reader_timeout > 0) return;
        g_hlw811x_reader_errors++;
        g_hlw811x_reader_init_step++;
        g_hlw811x_reader_timeout = 0;
        return;
    }
    if (!HLW811X_Proto_GetRxData(rx_buf, 2)) {
        g_hlw811x_reader_read_error = 1;
        g_hlw811x_reader_errors++;
    }
    g_hlw811x_reader_init_step++;
    g_hlw811x_reader_timeout = 0;
    return;

verify_done:
    if (g_hlw811x_reader_read_error == 0) {
        g_hlw811x_reader_init_step = 0;
        g_hlw811x_reader_init_st = HLW811X_READER_INIT_CAL_READ;
    } else {
        if (g_hlw811x_reader_init_retry < 2) {
            g_hlw811x_reader_init_retry++;
            g_hlw811x_reader_err_dly = 100;
            g_hlw811x_reader_init_step = 0;
            g_hlw811x_reader_init_st = HLW811X_READER_INIT_REG_WRITE;
        } else {
            g_hlw811x_reader_init_step = 0;
            g_hlw811x_reader_init_st = HLW811X_READER_INIT_CAL_READ;
        }
    }
}

/*============================================================
 * 初始化子阶段3: 校准系数读取
 *============================================================*/
static void InitPhase_CalRead(void)
{
    u8 rx_buf[4];
    u16 cal_val;

    if (g_hlw811x_reader_init_step >= 9) {
        g_hlw811x_reader_init_st = HLW811X_READER_INIT_CAL_CHKSUM;
        return;
    }
    if (g_hlw811x_reader_timeout == 0 && !HLW811X_Proto_IsRxDone()) {
        HLW811X_Proto_ReadReg(s_cal_regs[g_hlw811x_reader_init_step], 2);
        g_hlw811x_reader_timeout = 5;
        return;
    }
    if (!HLW811X_Proto_IsRxDone()) {
        if (g_hlw811x_reader_timeout > 0) return;
        g_hlw811x_reader_errors++;
        g_hlw811x_reader_init_step++;
        g_hlw811x_reader_timeout = 0;
        return;
    }
    if (!HLW811X_Proto_GetRxData(rx_buf, 2)) {
        g_hlw811x_reader_errors++;
    } else {
        cal_val = (u16)(rx_buf[0] << 8) | rx_buf[1];
/*         /* 存储到对应的校准系数全局变量 */
        switch (g_hlw811x_reader_init_step) {
            case 0: U16_RMSIAC_RegData   = cal_val; break;
            case 1: U16_RMSIBC_RegData   = cal_val; break;
            case 2: U16_RMSUC_RegData    = cal_val; break;
            case 3: U16_PowerPAC_RegData = cal_val; break;
            case 4: U16_PowerPBC_RegData = cal_val; break;
            case 5: U16_PowerSC_RegData  = cal_val; break;
            case 6: U16_EnergyAC_RegData = cal_val; break;
            case 7: U16_EnergyBC_RegData = cal_val; break;
            case 8: U16_CheckSUM_RegData = cal_val; break;
        }
    }
    g_hlw811x_reader_init_step++;
    g_hlw811x_reader_timeout = 0;
}

/*============================================================
 * 初始化子阶段4: 校验和验证
 *============================================================*/
static void InitPhase_CalChecksum(void)
{
    u32 a;
    u8  d;

    a = ~(0xffffUL + U16_RMSIAC_RegData + U16_RMSIBC_RegData +
          U16_RMSUC_RegData + U16_PowerPAC_RegData + U16_PowerPBC_RegData +
          U16_PowerSC_RegData + U16_EnergyAC_RegData + U16_EnergyBC_RegData);
    U16_CheckSUM_Data = (u16)(a & 0xffff);

    d = (U16_CheckSUM_Data == U16_CheckSUM_RegData) ? 1 : 0;

    if (d == 0) {
        U16_RMSIAC_RegData   = s_default_cal[0];
        U16_RMSIBC_RegData   = s_default_cal[1];
        U16_RMSUC_RegData    = s_default_cal[2];
        U16_PowerPAC_RegData = s_default_cal[3];
        U16_PowerPBC_RegData = s_default_cal[4];
        U16_PowerSC_RegData  = s_default_cal[5];
        U16_EnergyAC_RegData = s_default_cal[6];
        U16_EnergyBC_RegData = s_default_cal[7];
    }

    g_hlw811x_reader_init_st = HLW811X_READER_INIT_DONE;
}
