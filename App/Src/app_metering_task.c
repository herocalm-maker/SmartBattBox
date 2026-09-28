/**
 * @file    app_metering_task.c
 * @brief   电能计量任务 — HLW8110 双路采集 (输入/输出)
 *
 * MeteringTask 职责:
 *   - 初始化双路 HLW8110 (USART2=输入, USART3=输出)
 *   - 周期调用 mod_metering_handler() 读取电压/电流/功率/电能
 *   - 异常检测 + 自动恢复
 */
#include "main.h"
#include "app_metering_task.h"
#include "mod_metering.h"
#include "drv_hlw8110.h"
#include "bsp_wdg.h"
#include "comm_logger.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"

/*====================================================================
 * 板级硬件采样配置 (对应《移动电池逆变器电路原理图_20260706》)
 *--------------------------------------------------------------------
 * 电流通道: ZEMCT131(2000:1) + 100Ω 负载 → 5A 时 ADC 引脚 250mV, 电流 PGA 必须=1
 * 电压通道: ZMPT107-1(2mA) 次级 100Ω 负载 → 220V 时 ~200mV, PGA=1
 * K1/K2 为相对芯片出厂标定条件的归一化系数 (见 drv_hlw8110.h)。
 * 若后期换板/改采样电阻, 只需改这里, 并同步 hlw8110_config 侧。
 *====================================================================*/
static const hlw8110_hwcfg_t s_metering_hwcfg = {
    .k1 = HLW8110_K1,               /* 电流归一化系数 = 3.125 */
    .k2 = HLW8110_K2,               /* 电压归一化系数 = 0.909 */
    .syscon = HLW8110_SYSCON_VALUE, /* 0x0E00: PGA_A=1 (关键, 防饱和) */
    .emucon = HLW8110_EMUCON_VALUE, /* A 通道电能累加 */
    .emucon2 = HLW8110_EMUCON2_VALUE,
};

/**
 * @brief 在计量初始化前向双路HLW8110注入板级采样配置。
 */
static void metering_inject_board_config(void)
{
    for (uint8_t i = 0; i < 2; i++)
    {
        hlw8110_t *hlw = mod_metering_get_hlw(i);
        if (hlw != NULL)
        {
            drv_hlw8110_set_hwcfg(hlw, &s_metering_hwcfg);
        }
    }
}

/**
 * @brief 回读双路SYSCON寄存器并打印电流通道增益校验结果。
 */
static void metering_verify_syscon(void)
{
    static const char *ch_name[2] = {"in", "out"};
    for (uint8_t i = 0; i < 2; i++)
    {
        hlw8110_t *hlw = mod_metering_get_hlw(i);
        if (hlw == NULL)
        {
            continue;
        }

        uint32_t syscon = 0;
        if (drv_hlw8110_read_reg(hlw, HLW8110_REG_SYSCON, 2, &syscon))
        {
            bool pga_ok = ((syscon & 0x07u) == 0u); /* PGAIA[2:0] = 000 → PGA=1 */
            /* 整数拆分打印, 避免 nano.specs 无 %f 支持 */
            int k1_i = (int)(s_metering_hwcfg.k1);
            int k1_d = (int)(s_metering_hwcfg.k1 * 1000.0f) % 1000;
            int k2_i = (int)(s_metering_hwcfg.k2);
            int k2_d = (int)(s_metering_hwcfg.k2 * 1000.0f) % 1000;
            log_print("Metering: ch[%s] SYSCON=0x%04X PGA_A=1 %s (K1=%d.%03d K2=%d.%03d)",
                      ch_name[i], (unsigned)syscon, pga_ok ? "OK" : "MISMATCH", k1_i, k1_d, k2_i,
                      k2_d);
        }
        else
        {
            log_print("Metering: ch[%s] SYSCON read fail", ch_name[i]);
        }
    }
}

/*====================================================================
 * 消息发送接口
 *====================================================================*/
/**
 * @brief 向计量任务队列发送消息，最多等待3000个RTOS tick。
 * @param msg 待发送消息，调用方需提供有效指针。
 * @return true：消息入队；false：队列满。
 */
bool Send_Msg_AppMeteringTask(ucMESSAGE_t *msg)
{
    BaseType_t err;
    err = xQueueSend(xMeteringQueueHandle, msg, 3000);
    if (err == errQUEUE_FULL)
    {
        return false;
    }
    return true;
}

#define METERING_POLL_MS 100 /* 轮询周期 (mod_metering_handler 内部有 1s 节流) */
#define METERING_LOG_TICKS (10000 / METERING_POLL_MS) /* 10s 打印一次 */

/**
 * @brief 按调用次数打印双路计量数据，使用整数拆分显示小数。
 */
static void metering_tim_process(void)
{
    static uint32_t log_cnt = 0;
    /* 10s 周期打印计量数据 (整数拆分避免 nano.specs 无 %f 支持) */
    if (++log_cnt >= METERING_LOG_TICKS)
    {
        log_cnt = 0;
        METER_INFO_T *m = metering_get_info();
        if (m)
        {
            int32_t vin_v_i = (int32_t)(m->vin_v);
            int32_t vin_v_d = (int32_t)(m->vin_v * 10 + 0.5f) % 10;
            int32_t vin_i_i = (int32_t)(m->vin_i);
            int32_t vin_i_d = (int32_t)(m->vin_i * 100 + 0.5f) % 100;
            int32_t vin_p_i = (int32_t)(m->vin_p_w);
            int32_t vin_p_d = (int32_t)(m->vin_p_w * 10 + 0.5f) % 10;
            int32_t vout_v_i = (int32_t)(m->vout_v);
            int32_t vout_v_d = (int32_t)(m->vout_v * 10 + 0.5f) % 10;
            int32_t vout_i_i = (int32_t)(m->vout_i);
            int32_t vout_i_d = (int32_t)(m->vout_i * 100 + 0.5f) % 100;
            int32_t vout_p_i = (int32_t)(m->vout_p_w);
            int32_t vout_p_d = (int32_t)(m->vout_p_w * 10 + 0.5f) % 10;
            int32_t vin_f_i = (int32_t)(m->vin_freq_hz);
            int32_t vin_f_d = (int32_t)(m->vin_freq_hz * 10 + 0.5f) % 10;
            int32_t vout_f_i = (int32_t)(m->vout_freq_hz);
            int32_t vout_f_d = (int32_t)(m->vout_freq_hz * 10 + 0.5f) % 10;

            logger_print(
                LOG_INFO,
                "[METER] VIN:%d.%dV %d.%02dA %d.%dW %d.%dHz | VOUT:%d.%dV %d.%02dA %d.%dW %d.%dHz | AC:%d",
                vin_v_i, vin_v_d < 0 ? -vin_v_d : vin_v_d, vin_i_i,
                vin_i_d < 0 ? -vin_i_d : vin_i_d, vin_p_i, vin_p_d < 0 ? -vin_p_d : vin_p_d,
                vin_f_i, vin_f_d < 0 ? -vin_f_d : vin_f_d, vout_v_i,
                vout_v_d < 0 ? -vout_v_d : vout_v_d, vout_i_i, vout_i_d < 0 ? -vout_i_d : vout_i_d,
                vout_p_i, vout_p_d < 0 ? -vout_p_d : vout_p_d, vout_f_i,
                vout_f_d < 0 ? -vout_f_d : vout_f_d, m->ac_in_present);
        }
    }
}

/*====================================================================
 * 任务入口
 *====================================================================*/

/**
 * @brief 计量任务入口：初始化采样配置并周期调用双路采集。
 * @param arg 任务创建参数，当前未使用。
 * @note 由RTOS调用，任务循环不返回。
 */
void MeteringTask(void *arg)
{
    uint32_t uwRet = 0;
    ucMESSAGE_t Msg = {0};

    /* 注入板级硬件配置 (K1/K2 + PGA_A=1), 必须在 init 之前 */
    metering_inject_board_config();

    /* 初始化计量模块 (双路 HLW8110) */
    mod_metering_init();

    /* init 完成后回读校验关键寄存器 (PGA_A=1) */
    metering_verify_syscon();

    for (;;)
    {
        uwRet = xQueueReceive(xMeteringQueueHandle, &Msg, pdMS_TO_TICKS(METERING_POLL_MS));
        if (uwRet == pdPASS)
        {
            switch (Msg.MsgID)
            {
                case 0:
                    break;

                default:
                    break;
            }
        }

        /* 周期采集: 读双路 HLW8110 数据 + 异常检测 */
        mod_metering_handler();

        /* 日志周期按任务循环次数累计。 */
        metering_tim_process();

        /* 心跳 — 监控定时器检测任务存活 */
        bsp_wdg_task_heartbeat(WDG_TASK_METERING);
    }
}
