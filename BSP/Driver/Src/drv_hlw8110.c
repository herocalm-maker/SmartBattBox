/**
 * @file    drv_hlw8110.c
 * @brief   HLW8110 电能计量驱动 (Driver 层)
 *
 * 基于 HLW8110/8112 官方 REV41 例程封装, 适配当前框架.
 *
 * 协议格式 (官方 REV41):
 *   读命令: [0xA5] [addr]              — 2 字节, 无校验和
 *   读响应: [data...] [checksum]       — checksum = ~(sum(data) + 0xA5 + addr) & 0xFF
 *   写命令: [0xA5] [addr|0x80] [data...] [checksum]
 *   Write_EN:  [0xA5] [0xEA] [0xE5] [checksum]
 *   Write_DIS: [0xA5] [0xEA] [0xDC] [checksum]
 *   Set_CH_A:  [0xA5] [0xEA] [0x5A] [checksum]
 *
 * 数据流:
 *   TX: bsp_uart_send(inst->port, cmd)  → HLW8110 IC
 *   RX: HLW8110 IC → UART RX ISR → comm_ringbuf_push → readReg pop 读取
 */

#include <string.h>
#include "bsp_tim.h"
#include "FreeRTOS.h"
#include "task.h"
#include "cmsis_os2.h"
#include "comm_logger.h"
#include "drv_hlw8110.h"



static hlw8110_t *s_dev[BSP_UART_MAX] = {NULL};



/**
 * @brief  UART RX 回调 (ISR 上下文)
 * @note   由 bsp_uart IDLE 中断触发, 将接收数据推入对应实例的 ringbuf
 * @param  port  UART 端口号
 * @param  buf   接收数据指针
 * @param  len   接收数据长度
 */
static void hlw8110_rx_cb(bsp_uart_port_t port, uint8_t *buf, uint16_t len)
{
    if (port >= BSP_UART_MAX)
        return;
    hlw8110_t *inst = s_dev[port];
    if (inst != NULL) {
        comm_ringbuf_push(&inst->rx_ring, buf, len);
    }
}

static bool hlw8110_special_cmd(hlw8110_t *inst, uint8_t dat);

/*====================================================================
 * Checksum 原厂算法
 *====================================================================*/
/**
 * @brief  计算帧校验和
 * @note   对 buf[0..len-1] 求和后取反, 与官方 checkSum_Write 一致
 * @param  buf  数据缓冲区
 * @param  len  参与计算的数据长度 (不含校验字节本身)
 * @return 校验和字节
 */
static uint8_t hlw8110_checksum_clac(const uint8_t *buf, uint8_t len)
{
    uint8_t sum = 0;
    uint8_t i = 0;
    for (i = 0; i < len; i++)
        sum += buf[i];
    return (uint8_t)((~sum)&0xFF);
}

/*====================================================================
 * 核心函数: sendAndRecv
 *====================================================================*/
/**
 * @brief  发送原始命令帧 (不带响应等待)
 * @param  inst     实例指针
 * @param  cmd      命令缓冲区
 * @param  cmd_len  命令长度 (字节)
 * @return 发送字节数 (0=参数非法或发送失败)
 */
static uint16_t hlw8110_send(hlw8110_t *inst, const uint8_t *cmd, uint8_t cmd_len)
{
    if (inst == NULL || cmd == NULL || cmd_len == 0)
        return 0;
    return bsp_uart_send(inst->port, cmd, cmd_len, 100);
}

/**
 * @brief  HLW8110 命令‑响应交互核心
 * @note   流程: 清队列 → bsp_uart_send → 轮询 ringbuf 直到收满期望长度或超时.
 *         响应无 0xA5 前导, 直接累计接收, 校验由调用方 (readReg) 完成.
 * @param  inst         实例指针
 * @param  cmd          发送命令 (NULL=仅接收)
 * @param  cmd_len      命令长度
 * @param  respbuff     响应缓冲区
 * @param  resp_len     期望响应长度 (字节, 缓冲区至少这么大)
 * @param  timeout_ms   超时时间 (毫秒)
 * @return 实际接收长度 (>=resp_len=收满, 0=无响应)
 */
static uint16_t hlw8110_sendAndRecv(hlw8110_t *inst, const uint8_t *cmd, uint8_t cmd_len,
                                    uint8_t *respbuff, uint8_t resp_len,
                                    uint16_t timeout_ms)
{
    uint32_t starttime = 0;
    uint16_t recvlen   = 0;
    uint16_t n         = 0;
    if (inst == NULL || respbuff == NULL || resp_len == 0)
        return 0;

    comm_ringbuf_reset(&inst->rx_ring);
    if (cmd != NULL && cmd_len > 0)
    {
        bsp_uart_send(inst->port, cmd, cmd_len, 100);
    }

    starttime = bsp_get_tick_ms();
    recvlen   = 0u;
    do
    {
        n = comm_ringbuf_pop(&inst->rx_ring, &respbuff[recvlen], (uint16_t)(resp_len - recvlen));
        recvlen += n;                       /* 累加已收字节 (原实现误用 = 覆盖, 只统计第一次) */
        if (recvlen >= resp_len)
        {
            recvlen = resp_len;
            break;
        }
        osDelay(50);
    } while (bsp_get_tick_ms() - starttime < timeout_ms);
    return recvlen;
}

/**
 * @brief  读响应校验和验证
 * @note   读命令 [0xA5][addr] 无校验字节, 响应 [data...][checksum],
 *         校验和 = ~(0xA5 + addr + Σdata) & 0xFF (命令字节参与计算)
 * @param  cmd       已发送的读命令 (cmd[0]=0xA5, cmd[1]=addr)
 * @param  rx        响应数据 (前 data_len 字节为数据, 第 data_len 字节为校验和)
 * @param  data_len  数据长度
 * @return true=校验通过
 */
static bool hlw8110_check_read_checksum(const uint8_t *cmd, const uint8_t *rx, uint8_t data_len)
{
    uint16_t sum = cmd[0] + cmd[1];
    for (uint8_t i = 0; i < data_len; i++)
        sum += rx[i];
    return ((uint8_t)(~sum & 0xFFU)) == rx[data_len];
}

/**
 * @brief  读取 HLW8110 寄存器
 * @param  inst      实例指针
 * @param  regaddr   寄存器地址
 * @param  readbuff  读取数据缓冲区 (输出, 大端字节序)
 * @param  readlen   读取数据长度 (2/3/4 字节)
 * @return 读取成功返回 >0, 失败返回 0
 */
static uint16_t hlw8110_readReg(hlw8110_t *inst, uint8_t regaddr, uint8_t *readbuff, uint8_t readlen)
{
    uint8_t cmd[2] = {0};
    uint8_t resp[HLW8110_TEMP_BUF_SIZE] = {0};
    uint16_t recvlen = 0;
    if (inst == NULL || readbuff == NULL || readlen < 2 || readlen > 4)
        return 0;
    cmd[0] = HLW8110_PREAMBLE;
    cmd[1] = regaddr;
    /* 读命令固定 2 字节无校验; 响应 = readlen 字节数据 + 1 字节校验和 */
    recvlen = hlw8110_sendAndRecv(inst, cmd, 2, resp, (uint8_t)(readlen + 1U),HLW8110_RESP_TIMEOUT_MS);
    if (recvlen < (uint16_t)(readlen + 1U))
        return 0;
    /* 校验和验证: ~(0xA5 + addr + Σdata) & 0xFF 应与末字节一致 (原实现被注释, 已恢复) */
    if (!hlw8110_check_read_checksum(cmd, resp, readlen))
        return 0;
    memcpy(readbuff, resp, readlen);
    return readlen;
}

/**
 * @brief  写入 HLW8110 寄存器
 * @note   时序: 写使能(WR_EN) → 发送写帧 → 写关闭(WR_DIS), 与原厂例程一致
 * @param  inst      实例指针
 * @param  regaddr   寄存器地址
 * @param  writebuff 写入数据缓冲区 (大端字节序)
 * @param  writelen  写入数据长度 (2/3/4 字节)
 * @return true=写入成功
 */
static bool hlw8110_writeReg(hlw8110_t *inst, uint8_t regaddr, const uint8_t *writebuff, uint8_t writelen)
{
    uint8_t cmd[8] = {0};
    uint8_t i = 0;
    bool ok = false;
    if (inst == NULL || writebuff == NULL || writelen < 2 || writelen > 4)
        return false;
    cmd[0] = HLW8110_PREAMBLE;
    cmd[1] = regaddr | HLW8110_REG_W_BIT;
    for (i = 0; i < writelen; i++)
        cmd[2 + i] = writebuff[i];
    cmd[2 + writelen] = hlw8110_checksum_clac(cmd, 2 + writelen);

    hlw8110_special_cmd(inst, HLW8110_CMD_WR_EN);
    bsp_delay_ms(2);
    ok = (hlw8110_send(inst, cmd, (2 + writelen + 1U)) > 0);
    bsp_delay_ms(2);
    hlw8110_special_cmd(inst, HLW8110_CMD_WR_DIS);
    return ok;
}

/**
 * @brief  发送 0xEA 特殊命令 (写使能/写关闭/通道选择/软复位)
 * @note   帧格式: [0xA5] [0xEA] [子命令码] [checksum]
 * @param  inst  实例指针
 * @param  dat   子命令码 (HLW8110_CMD_*)
 * @return true=发送成功 false=参数非法或发送失败
 */
static bool hlw8110_special_cmd(hlw8110_t *inst, uint8_t dat)
{
    uint8_t cmd[5U];
    cmd[0] = HLW8110_PREAMBLE;
    cmd[1] = HLW8110_CMD_SPECIAL;
    cmd[2] = dat;
    cmd[3] = hlw8110_checksum_clac(cmd,3U);
    if(hlw8110_send(inst, cmd,4U) > 0U)
        return true;
    return false;
}

/*========================对外接口实现========================*/

/**
 * @brief  握手: 读 DeviceID 寄存器, 验证固定值 0x77 0x61 0x10
 * @note   判断接线/电平/波特率/校验位是否正确的最直接手段
 * @param  inst  实例指针
 * @return true=芯片应答且 ID 正确
 */
bool drv_hlw8110_handshake(hlw8110_t *inst)
{
    uint8_t id_buf[3] = {0};
    if (inst == NULL)
        return false;
    if (hlw8110_readReg(inst, HLW8110_REG_DEVICEID, id_buf, 3) == 0)
        return false;
    return (id_buf[0] == 0x77U && id_buf[1] == 0x61U && id_buf[2] == 0x10U);
}

/**
 * @brief  初始化 HLW8110 实例并绑定 UART 端口
 * @note   清零实例 → ringbuf 初始化 → bsp_uart_init (9600 8E1, RX 回调
 *         推入实例 ringbuf) → 延时等待芯片上电 → DeviceID 握手验证通信
 * @param  inst  实例指针 (由调用方分配)
 * @param  port  UART 端口号
 * @return true=成功 false=参数非法/UART 初始化失败/握手失败
 */
bool drv_hlw8110_init(hlw8110_t *inst, bsp_uart_port_t port)
{
    if(inst == NULL || port >= BSP_UART_MAX)
        return false;

    /* 保存上层注入的板级硬件配置 (memset 会整体清零, 需先暂存再恢复) */
    uint8_t          hw_set_save = inst->hw_set;
    hlw8110_hwcfg_t  hw_save;
    if (hw_set_save) { hw_save = inst->hw; }

    memset(inst,0,sizeof(hlw8110_t));
    inst->port = port;

    /* 恢复/默认硬件配置 */
    if (hw_set_save)
    {
        inst->hw     = hw_save;
        inst->hw_set = 1;
    }
    else
    {
        /* 未显式注入 → 采用板级默认 (PGA_A=1 + K1/K2 本板值) */
        inst->hw.k1     = HLW8110_K1;
        inst->hw.k2     = HLW8110_K2;
        inst->hw.syscon = HLW8110_SYSCON_VALUE;
        inst->hw.emucon = HLW8110_EMUCON_VALUE;
        inst->hw.emucon2= HLW8110_EMUCON2_VALUE;
    }

    comm_ringbuf_init(&inst->rx_ring, inst->ring_storage, HLW8110_RING_BUF_SIZE);

    bsp_uart_config_t cfg = {
        .baudrate    = 9600,
        .data_bits   = BSP_UART_DATA_9B,
        .parity      = BSP_UART_PARITY_EVEN,
        .stop_bits   = BSP_UART_STOPBITS_1,
        .rx_cb       = hlw8110_rx_cb,
        .rx_buf      = inst->uart_rx_buf,
        .rx_buf_size = HLW8110_RX_BUF_SIZE
    };

    if(!bsp_uart_init(port, &cfg))
        return false;

    s_dev[port] = inst;

    bsp_delay_ms(10);

    if(drv_hlw8110_handshake(inst))
    {
       ;
    }

    /* 寄存器配置 (本板: SYSCON=0x0E00 → PGA_A=1 适配 100Ω 电流负载; EMUCON=0x0001 → A 通道电能累加)
     * 注: 原实现 SYSCON=0x0A04(PGA_A=16) 针对 1Ω 锰铜, 本板 100Ω 负载 5A 时信号 250mV 会饱和, 必须 PGA=1. */
    uint8_t syscon_val[2]  = { (uint8_t)(inst->hw.syscon >> 8), (uint8_t)(inst->hw.syscon & 0xFFu) };
    uint8_t emucon_val[2]  = { (uint8_t)(inst->hw.emucon >> 8), (uint8_t)(inst->hw.emucon & 0xFFu) };
    uint8_t emucon2_val[2] = { (uint8_t)(inst->hw.emucon2>> 8), (uint8_t)(inst->hw.emucon2 & 0xFFu) };

    hlw8110_writeReg(inst, HLW8110_REG_SYSCON,  syscon_val,  2U);
    hlw8110_writeReg(inst, HLW8110_REG_EMUCON,  emucon_val,  2U);
    hlw8110_writeReg(inst, HLW8110_REG_EMUCON2, emucon2_val, 2U);

    /* 加载 OTP 免校表系数 (measure 换算依赖); 失败用出厂默认值, 保证首读即有数值
     * (原实现仅在 ~10s 自检时加载, 首读会一直为 0) */
    if (!drv_hlw8110_load_otp(inst))
    {
        inst->otp.rms_iac    = HLW8110_DEF_RMS_IAC;
        inst->otp.rms_ibc    = HLW8110_DEF_RMS_IBC;
        inst->otp.rms_uc     = HLW8110_DEF_RMS_UC;
        inst->otp.power_pac  = HLW8110_DEF_POWER_PAC;
        inst->otp.power_pbc  = HLW8110_DEF_POWER_PBC;
        inst->otp.power_sc   = HLW8110_DEF_POWER_SC;
        inst->otp.energy_ac  = HLW8110_DEF_ENERGY_AC;
        inst->otp.energy_bc  = HLW8110_DEF_ENERGY_BC;
    }

    return true;
}

/**
 * @brief  反初始化: 解除端口绑定 → 释放 UART → 清零实例
 * @param  inst  实例指针
 * @return none
 */
void drv_hlw8110_deinit(hlw8110_t *inst)
{
    if(inst == NULL)
        return;
    bsp_uart_port_t port = inst->port;
    if(port < BSP_UART_MAX)
    {
        s_dev[port] = NULL;
    }
    bsp_uart_deinit(port);

    /* 保留板级硬件配置 (重 init 后仍生效), 仅清运行态 */
    uint8_t          hw_set_save = inst->hw_set;
    hlw8110_hwcfg_t  hw_save;
    if (hw_set_save) { hw_save = inst->hw; }
    memset(inst,0,sizeof(hlw8110_t));
    if (hw_set_save) { inst->hw = hw_save; inst->hw_set = 1; }
}

/**
 * @brief  读取 OTP 出厂转换系数并校验
 * @note   参考 Judge_CheckSum_HLW8110_Calfactor: 依次读 0x70~0x77 共 8 个
 *         系数寄存器 + 0x6F 校验和寄存器, 按原厂算法
 *         checksum = ~(0xFFFF + Σ系数) 重算比对
 * @param  inst  实例指针 (系数写入 inst->otp)
 * @return true=OTP 校验通过 false=任一寄存器读取失败或校验和不符
 */
bool drv_hlw8110_load_otp(hlw8110_t *inst)
{
    if(inst == NULL)
        return false;
    uint8_t buf[3];
    uint16_t calc_checksum;
    uint16_t reg_checksum;

    /* 读取OTP 0x70~0x77 */
    if(hlw8110_readReg(inst,HLW8110_REG_RMS_IAC,buf,2)==0) return false;
    inst->otp.rms_iac = (buf[0]<<8)|buf[1];

    if(hlw8110_readReg(inst,HLW8110_REG_RMS_IBC,buf,2)==0) return false;
    inst->otp.rms_ibc = (buf[0]<<8)|buf[1];

    if(hlw8110_readReg(inst,HLW8110_REG_RMS_UC,buf,2)==0) return false;
    inst->otp.rms_uc = (buf[0]<<8)|buf[1];

    if(hlw8110_readReg(inst,HLW8110_REG_POWER_PAC,buf,2)==0) return false;
    inst->otp.power_pac = (buf[0]<<8)|buf[1];

    if(hlw8110_readReg(inst,HLW8110_REG_POWER_PBC,buf,2)==0) return false;
    inst->otp.power_pbc = (buf[0]<<8)|buf[1];

    if(hlw8110_readReg(inst,HLW8110_REG_POWER_SC,buf,2)==0) return false;
    inst->otp.power_sc = (buf[0]<<8)|buf[1];

    if(hlw8110_readReg(inst,HLW8110_REG_ENERGY_AC,buf,2)==0) return false;
    inst->otp.energy_ac = (buf[0]<<8)|buf[1];

    if(hlw8110_readReg(inst,HLW8110_REG_ENERGY_BC,buf,2)==0) return false;
    inst->otp.energy_bc = (buf[0]<<8)|buf[1];

    if(hlw8110_readReg(inst,HLW8110_REG_CHECKSUM,buf,2)==0) return false;
    reg_checksum = (buf[0]<<8)|buf[1];
    inst->otp.checksum_reg = reg_checksum;

    /* 原厂OTP校验和计算 */
    uint32_t sum = 0xFFFFU;
    sum += inst->otp.rms_iac;
    sum += inst->otp.rms_ibc;
    sum += inst->otp.rms_uc;
    sum += inst->otp.power_pac;
    sum += inst->otp.power_pbc;
    sum += inst->otp.power_sc;
    sum += inst->otp.energy_ac;
    sum += inst->otp.energy_bc;
    calc_checksum = (uint16_t)(~sum);

    if(calc_checksum != reg_checksum)
    {
        return false; /* OTP校验和错误，芯片OTP异常 */
    }
    return true;
}

/**
 * @brief  批量读取测量数据寄存器 (原始值, 不含换算)
 * @note   依次读取: 电压有效值 rms_u(24bit)/电流 rms_ia(24bit)/
 *         有功功率 power_pa(32bit)/电能 energy_pa(24bit)/
 *         功率因数 pf(24bit)/电压频率 ufreq(16bit)/HFCONST(16bit)
 * @param  inst  实例指针
 * @param  out   原始寄存器读数输出 (大端字节序已转 uint)
 * @return true=全部读取成功 false=任一寄存器响应超时
 */
bool drv_hlw8110_read_data(hlw8110_t *inst, hlw8110_data_t *out)
{
    if (inst == NULL || out == NULL) {
        return false;
    }
    uint8_t buf[5U] = {0};
    uint16_t ret = 0;

    ret = hlw8110_readReg(inst, HLW8110_REG_RMSU, buf, 3U);
    if (ret == 0) {
        return false;
    }
    out->rms_u = ((uint32_t)buf[0] << 16U) | ((uint32_t)buf[1] << 8U) | buf[2];

    ret = hlw8110_readReg(inst, HLW8110_REG_RMSIA, buf, 3U);
    if (ret == 0) {
        return false;
    }
    out->rms_ia = ((uint32_t)buf[0] << 16U) | ((uint32_t)buf[1] << 8U) | buf[2];

    ret = hlw8110_readReg(inst, HLW8110_REG_POWER_PA, buf, 4U);
    if (ret == 0) {
        return false;
    }
    out->power_pa = ((uint32_t)buf[0] << 24U) | ((uint32_t)buf[1] << 16U) | ((uint32_t)buf[2] << 8U) | buf[3];

    ret = hlw8110_readReg(inst, HLW8110_REG_ENERGY_PA, buf, 3U);
    if (ret == 0) {
        return false;
    }
    out->energy_pa = ((uint32_t)buf[0] << 16U) | ((uint32_t)buf[1] << 8U) | buf[2];

    ret = hlw8110_readReg(inst, HLW8110_REG_PF, buf, 3U);
    if (ret == 0) {
        return false;
    }
    out->power_factor = ((uint32_t)buf[0] << 16U) | ((uint32_t)buf[1] << 8U) | buf[2];

    ret = hlw8110_readReg(inst, HLW8110_REG_UFREQ, buf, 2U);
    if (ret == 0) {
        return false;
    }
    out->u_freq = ((uint16_t)buf[0] << 8U) | buf[1];

    ret = hlw8110_readReg(inst, HLW8110_REG_HFCONST, buf, 2U);
    if (ret == 0) {
        return false;
    }
    out->hf_const = ((uint16_t)buf[0] << 8U) | buf[1];

    return true;
}

/**
 * @brief  读取并换算物理量 (电压/电流/功率/功率因数/电能/频率)
 * @note   完全移植原厂 HLW8110.c 计算公式, 使用 inst->otp 出厂系数;
 *         含负功率取反、小功率 PF 置 0 等原厂边界处理
 * @param  inst  实例指针
 * @param  out   换算结果输出 (V/A/W/-/kWh/Hz)
 * @return true=成功 false=原始寄存器读取失败
 */
bool drv_hlw8110_measure(hlw8110_t *inst, hlw8110_measure_t *out)
{
    if(inst == NULL || out == NULL)
        return false;
    hlw8110_data_t raw = {0};
    if(!drv_hlw8110_read_data(inst, &raw))
        return false;

    /* ========= 电压换算，原厂公式 + K2 板级归一化 ========= */
    if((raw.rms_u & 0x800000U) != 0U)
    {
        out->voltage = 0.0f;
    }
    else
    {
        float val = (float)raw.rms_u * inst->otp.rms_uc;
        val = val / (0x400000U * inst->hw.k2);   /* ÷(2^22·K2) */
        val = val / 100.0f;
        out->voltage = val;
    }

    /* ========= 电流换算，原厂公式 + K1 板级归一化 ========= */
    if((raw.rms_ia & 0x800000U) !=0U)
    {
        out->current = 0.0f;
    }
    else
    {
        float val = (float)raw.rms_ia * inst->otp.rms_iac;
        val = val / (0x800000U * inst->hw.k1);   /* ÷(2^23·K1) */
        val = val /1000.0f;
        out->current = val;
    }

    /* ========= 有功功率，原厂正负功率处理 + K1·K2 ========= */
    float p_val;
    if(raw.power_pa > 0x80000000U)
    {
        p_val = (float)(~raw.power_pa);
    }
    else
    {
        p_val = (float)raw.power_pa;
    }
    p_val = p_val * inst->otp.power_pac;
    p_val = p_val / (0x80000000U * inst->hw.k1 * inst->hw.k2);   /* ÷(2^31·K1·K2) */
    out->power = p_val;

    /* ========= 功率因数，原厂逻辑：小功率PF置0 ========= */
    uint32_t pf_raw = raw.power_factor;
    float pf;
    if(pf_raw >= 0x800000U)
    {
        pf = -1.0f * ((0xFFFFFFU + 1U) - pf_raw)/(float)0x800000U;
    }
    else
    {
        pf = (float)pf_raw / (float)0x7FFFFFU;
    }
    if(out->power < 0.3f)
    {
        pf = 0.0f;
    }
    out->power_factor = pf;

    /* ========= 电能换算，原厂公式 + K1·K2 =========
     * 注: 除数沿用本工程原厂例程 0x20000000(2^29)。若需严格按手册公式
     *     (÷2^26·4096 且乘 HPConst) 对齐, 可在校表阶段统一换算口径。 */
    float e_val = (float)raw.energy_pa * inst->otp.energy_ac;
    e_val = e_val / (0x20000000U * inst->hw.k1 * inst->hw.k2);
    out->energy_kwh = e_val;

    /* ========= 频率 ========= */
    if(raw.u_freq !=0U)
    {
        out->freq_hz = 3579545.0f / ( 8.0f * (float)raw.u_freq );
    }
    else
    {
        out->freq_hz = 0.0f;
    }

    return true;
}

/*====================================================================
 * 新增公共接口
 *====================================================================*/

/**
 * @brief  注入板级硬件采样配置 (K1/K2 归一化系数 + 寄存器初始化值)
 * @note   须在 drv_hlw8110_init 之前调用; 之后 init 会采用本配置写寄存器并在
 *         measure 换算时使用 K1/K2。若在 init 之后调用, 需重新 init 才生效。
 * @param  inst  实例指针
 * @param  cfg   硬件配置 (NULL 则恢复为板级默认)
 * @return true=成功
 */
bool drv_hlw8110_set_hwcfg(hlw8110_t *inst, const hlw8110_hwcfg_t *cfg)
{
    if (inst == NULL)
        return false;
    if (cfg == NULL)
    {
        inst->hw_set = 0;
    }
    else
    {
        inst->hw     = *cfg;
        inst->hw_set = 1;
    }
    return true;
}

/**
 * @brief  读取单个寄存器 (len=2/3/4)，大端组装为 uint32
 * @param  inst  实例指针
 * @param  addr  寄存器地址
 * @param  len   数据长度 (2/3/4)
 * @param  val   输出值
 * @return true=读取成功 (含校验和通过)
 */
bool drv_hlw8110_read_reg(hlw8110_t *inst, uint8_t addr, uint8_t len, uint32_t *val)
{
    uint8_t buf[4] = {0};
    if (inst == NULL || val == NULL || len < 2 || len > 4)
        return false;
    if (hlw8110_readReg(inst, addr, buf, len) == 0)
        return false;
    uint32_t v = 0;
    for (uint8_t i = 0; i < len; i++)
        v = (v << 8) | buf[i];
    *val = v;
    return true;
}














