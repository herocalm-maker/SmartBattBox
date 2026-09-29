/**
 * @file    mod_metering.c
 * @brief   计量模块 (Module 层)
 *
 * 框架:
 *   - mod_metering_init()      — 初始化双路 HLW8110
 *   - mod_metering_handler()   — 周期调用，读取数据 + 异常检测
 *   - metering_get_info()      — 获取数据指针
 */
#include "mod_metering.h"
#include "drv_hlw8110.h"
#include "Board.h"
#include "comm_logger.h"
#include <string.h>
#include "bsp_tim.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>

/*====================================================================
 * 常量
 *====================================================================*/

#define METERING_SELF_CHECK_INTERVAL  50u  /* 每 50 次更新 (~10s) 自检一次 */
#define METERING_FAIL_THRESHOLD       10u  /* 连续失败 10 次 → 离线 (与清零阈值统一) */
#define METERING_RECOVER_THRESHOLD    3u   /* 连续成功 3 次 → 在线 */
#define METERING_NO_DATA_TIMEOUT      10u  /* 无数据 10 次 → 对外 V/I/P/freq 清零 */

/*====================================================================
 * 通道索引 (输入/输出双路)
 *====================================================================*/

typedef enum {
    METERING_IDX_INPUT = 0,    /* 输入侧 (USART2) */
    METERING_IDX_OUTPUT,       /* 输出侧 (USART3) */
    METERING_IDX_CNT
} metering_idx_enum;

/*====================================================================
 * 内部状态
 *====================================================================*/

static hlw8110_t s_hlw_input = {0};    /* 输入侧 (USART2) */
static hlw8110_t s_hlw_output = {0};   /* 输出侧 (USART3) */

static METER_INFO_T s_info = {0};
static bool s_initialized = false;
static bool s_data_valid = false;
static uint16_t vin_no_data_count = 0;
static uint16_t vout_no_data_count = 0;

/*--------------------------------------------------------------------
 * 通道描述表 (输入/输出共用一套处理逻辑)
 *--------------------------------------------------------------------*/
typedef struct {
    const char      *name;           /* 日志名 */
    hlw8110_t       *hlw;            /* 通道句柄 */
    bsp_uart_port_t  uart;           /* 对应串口 */
    uint16_t        *no_data_cnt;    /* 无数据计数 */
    float           *v;              /* 电压 V */
    float           *i;              /* 电流 A */
    float           *p;              /* 功率 W */
    float           *freq;           /* Frequency Hz */
    float           *energy;         /* 累计电能 Wh */
} metering_ch_t;

static metering_ch_t s_ch[METERING_IDX_CNT] = {
    [METERING_IDX_INPUT] = {
        .name = "in",  .hlw = &s_hlw_input,  .uart = BSP_UART_2,
        .no_data_cnt = &vin_no_data_count,
        .v = &s_info.vin_v, .i = &s_info.vin_i, .p = &s_info.vin_p_w,
        .freq = &s_info.vin_freq_hz,
        .energy = &s_info.vin_energy_wh,
    },
    [METERING_IDX_OUTPUT] = {
        .name = "out", .hlw = &s_hlw_output, .uart = BSP_UART_3,
        .no_data_cnt = &vout_no_data_count,
        .v = &s_info.vout_v, .i = &s_info.vout_i, .p = &s_info.vout_p_w,
        .freq = &s_info.vout_freq_hz,
        .energy = &s_info.vout_energy_wh,
    },
};

/* 会话电能累加状态 (首读建立基线, 之后按差值累加) */
static bool  s_first_energy[METERING_IDX_CNT];
static float s_last_energy_wh[METERING_IDX_CNT];

/* 周期自检计数 */
static uint16_t s_selfcheck_cnt = 0;

static ac_input_snapshot_t s_ac_input;
static uint8_t s_ac_good_samples;

/* Publish immediately after INPUT processing, before any OUTPUT UART wait. */
static void ac_input_update(bool success, float voltage)
{
    uint32_t now = bsp_get_tick_ms();
    taskENTER_CRITICAL();
    if (!success || !isfinite(voltage) || voltage < 0.0f)
    {
        s_ac_input.state = AC_INPUT_UNKNOWN;
        s_ac_good_samples = 0;
    }
    else
    {
        if ((uint32_t)(now - s_ac_input.sample_ms) > METERING_AC_STALE_MS)
            s_ac_good_samples = 0;
        s_ac_input.voltage = voltage;
        s_ac_input.sample_ms = now;
        if (voltage >= METERING_AC_MIN_V && voltage <= METERING_AC_MAX_V)
        {
            if (s_ac_good_samples < METERING_AC_CONFIRM_SAMPLES)
            	++s_ac_good_samples;
            s_ac_input.state = s_ac_good_samples >= METERING_AC_CONFIRM_SAMPLES ?
                               AC_INPUT_PRESENT : AC_INPUT_UNKNOWN;
        }
        else
        {
            s_ac_good_samples = 0;
            s_ac_input.state = AC_INPUT_ABSENT; /* Revoke bypass on first bad sample. */
        }
    }
    s_info.ac_in_present = s_ac_input.state == AC_INPUT_PRESENT;
    taskEXIT_CRITICAL();
}

ac_input_snapshot_t mod_metering_get_ac_input(void)
{
    ac_input_snapshot_t result;
    taskENTER_CRITICAL();
    result = s_ac_input;
    taskEXIT_CRITICAL();
    if ((uint32_t)(bsp_get_tick_ms() - result.sample_ms) > METERING_AC_STALE_MS)
        result.state = AC_INPUT_UNKNOWN;
    return result;
}

/*====================================================================
 * 内部函数
 *====================================================================*/

/**
 * @brief  累加电能 (Wh): 计算两次读数差值, 有效增量 (0~10Wh) 累加到 session
 * @param  idx        通道索引 (INPUT=0, OUTPUT=1)
 * @param  energy_wh  当前累计电能 (Wh)
 */
static void _accumulate_energy(uint32_t idx, float energy_wh)
{
    if (idx >= METERING_IDX_CNT)
      return;
    
    if (s_first_energy[idx]) 
    {
        s_last_energy_wh[idx] = energy_wh;
        s_first_energy[idx] = false;
        return;
    }

    float delta = energy_wh - s_last_energy_wh[idx];
    if (delta > 0.0f && delta < 10.0f) 
    {
        if (idx == METERING_IDX_INPUT) 
            s_info.session_in_wh += delta;
        else 
            s_info.session_out_wh += delta;
    }

    s_last_energy_wh[idx] = energy_wh;
}

/**
 * @brief  单通道处理: 读取数据, 失败超过 50 次则重初始化通道
 * @param  idx  通道索引 (INPUT / OUTPUT)
 * @return true=读取成功
 */
static bool _channel_process(uint32_t idx)
{
    metering_ch_t *ch = &s_ch[idx];
    hlw8110_measure_t meas = {0};

    if (drv_hlw8110_measure(ch->hlw, &meas))
    {
        *ch->no_data_cnt = 0;
        *ch->v      = meas.voltage;
        *ch->i      = meas.current;
        *ch->p      = meas.power;
        *ch->freq   = meas.freq_hz;
        *ch->energy = meas.energy_kwh * 1000.0f;
        _accumulate_energy(idx, *ch->energy);
        return true;
    }

    if (++(*ch->no_data_cnt) == METERING_NO_DATA_TIMEOUT)
    {
        /* 持续 10 次读失败 → 对外 V/I/P/freq 清零 (避免陈旧值误导; 电能/累计不清)
         * 输入安全快照在首次失败即为 UNKNOWN，不依赖此处清零 */
        log_warn("Metering: ch[%s] 10x no data, zero output", ch->name);
        *ch->v    = 0.0f;
        *ch->i    = 0.0f;
        *ch->p    = 0.0f;
        *ch->freq = 0.0f;
    }
    if (*ch->no_data_cnt > 50)
    {
        *ch->no_data_cnt = 0;
        log_warn("Metering: ch[%s] no data, reinit", ch->name);
        drv_hlw8110_deinit(ch->hlw);
        drv_hlw8110_init(ch->hlw, ch->uart);
    }
    return false;
}

/*====================================================================
 * API
 *====================================================================*/

/** @brief  初始化计量模块: 双路 HLW8110 init (USART2=输入, USART3=输出) */
void mod_metering_init(void)
{
    memset(&s_info, 0, sizeof(s_info));
    taskENTER_CRITICAL();
    memset(&s_ac_input, 0, sizeof(s_ac_input));
    s_ac_good_samples = 0;
    taskEXIT_CRITICAL();

    bool in_ok  = drv_hlw8110_init(&s_hlw_input, BSP_UART_2);
    bool out_ok = drv_hlw8110_init(&s_hlw_output, BSP_UART_3);

    s_initialized = true;
    s_data_valid  = false;
    s_selfcheck_cnt = 0;
    vin_no_data_count  = 0;
    vout_no_data_count = 0;
    for (uint32_t i = 0; i < METERING_IDX_CNT; i++)
    {
        s_first_energy[i]   = true;   /* 首次读数建立基线 */
        s_last_energy_wh[i] = 0.0f;
    }
   log_print("Metering: init (in=%s out=%s)", in_ok ? "OK" : "FAIL", out_ok ? "OK" : "FAIL");
}

/**
 * @brief  周期处理 (SensorTask 200ms 调用): 读双路数据 → 异常判定 → 周期自检
 * @note   连续 10 次双路超时 → 触发 HLW8110 自检恢复; 每 50 次强制自检一次
 */
void mod_metering_handler(void)
{
    static uint32_t waittime = 0;

    if(bsp_get_tick_ms() - waittime < 1000)
      return;
    
    waittime = bsp_get_tick_ms();
    bool any_success = false;

    /* 双通道读取 (输入/输出共用逻辑) */
    for (uint32_t idx = 0; idx < METERING_IDX_CNT; idx++)
    {
        bool success = _channel_process(idx);
        if (idx == METERING_IDX_INPUT)
        	ac_input_update(success, s_info.vin_v);

        if (idx == METERING_IDX_OUTPUT)
            s_info.ac_out_present = success && s_info.vout_v >= METERING_AC_MIN_V &&
                                    s_info.vout_v <= METERING_AC_MAX_V;

        if (success)
        	any_success = true;
    }

    /* 在线/离线判定 (迟滞: 连续失败/成功计数) */
    if (any_success)
    {
        s_info.ok_count++;
        s_info.fail_count = 0;
        if (s_info.online == 0u && s_info.ok_count >= METERING_RECOVER_THRESHOLD)
        {
            s_info.online = 1u;
            log_print("Metering: IC online");
        }
    }
    else
    {
        s_info.fail_count++;
        s_info.ok_count = 0;
        if (s_info.online != 0u && s_info.fail_count >= METERING_FAIL_THRESHOLD)
        {
            s_info.online = 0u;
            log_print("Metering: IC offline");
        }
    }
    s_data_valid = (s_info.online != 0u);

    /* 周期自检 (~10s): OTP 可读性探测, 失败则重初始化对应通道 */
    if (++s_selfcheck_cnt >= METERING_SELF_CHECK_INTERVAL)
    {
        s_selfcheck_cnt = 0;
        for (uint32_t idx = 0; idx < METERING_IDX_CNT; idx++)
        {
            metering_ch_t *ch = &s_ch[idx];
            if (!drv_hlw8110_load_otp(ch->hlw))
            {
                log_warn("Metering: ch[%s] self-check fail, reinit", ch->name);
                if (idx == METERING_IDX_INPUT)
                	ac_input_update(false, 0.0f);
                drv_hlw8110_deinit(ch->hlw);
                drv_hlw8110_init(ch->hlw, ch->uart);
            }
        }
    }
}

/** @brief  获取计量信息指针 (仅 online 时有效) */
METER_INFO_T* metering_get_info(void)
{
    return &s_info;
}

/** @brief  获取指定通道 HLW8110 驱动实例 (0=输入, 1=输出) */
hlw8110_t *mod_metering_get_hlw(uint8_t idx)
{
    if (idx >= METERING_IDX_CNT)
        return NULL;
    return s_ch[idx].hlw;
}

/*====================================================================
 * 板级采样配置 (原 App 层注入逻辑下沉, 收敛驱动直连)
 *====================================================================*/
static const hlw8110_hwcfg_t s_metering_hwcfg = {
    .k1      = HLW8110_K1,          /* 电流通道校准系数 */
    .k2      = HLW8110_K2,          /* 电压通道校准系数 */
    .syscon  = HLW8110_SYSCON_VALUE,/* 采样通道配置 (PGA_A=1) */
    .emucon  = HLW8110_EMUCON_VALUE,
    .emucon2 = HLW8110_EMUCON2_VALUE,
};

/** @brief  注入板级采样配置到双路 HLW8110 (需在 init 完成后调用) */
void mod_metering_apply_board_config(void)
{
    for (uint8_t i = 0; i < METERING_IDX_CNT; i++)
    {
        hlw8110_t *hlw = mod_metering_get_hlw(i);
        if (hlw)
            drv_hlw8110_set_hwcfg(hlw, &s_metering_hwcfg);
    }
}

/** @brief  回读校验 SYSCON (PGA_A=1), 打印 K1/K2 与校验结果 */
void mod_metering_verify_syscon(void)
{
    static const char *s_ch_name[METERING_IDX_CNT] = { "in", "out" };

    for (uint8_t i = 0; i < METERING_IDX_CNT; i++)
    {
        hlw8110_t *hlw = mod_metering_get_hlw(i);
        if (hlw == NULL)
            continue;

        uint32_t syscon = 0;
        if (drv_hlw8110_read_reg(hlw, HLW8110_REG_SYSCON, 2, &syscon))
        {
            int pga_ok   = ((syscon & 0x07u) == 0u);          /* PGAIA[2:0]==0 → PGA_A=1 */
            int k1_i     = (int)(s_metering_hwcfg.k1);
            int k1_d     = (int)(s_metering_hwcfg.k1 * 1000.0f) % 1000;
            int k2_i     = (int)(s_metering_hwcfg.k2);
            int k2_d     = (int)(s_metering_hwcfg.k2 * 1000.0f) % 1000;
            log_print("Metering: ch[%s] SYSCON=0x%04X PGA_A=1 %s (K1=%d.%03d K2=%d.%03d)",
                      s_ch_name[i], (unsigned)syscon, pga_ok ? "OK" : "MISMATCH",
                      k1_i, k1_d, k2_i, k2_d);
        }
        else
        {
            log_print("Metering: ch[%s] SYSCON read fail", s_ch_name[i]);
        }
    }
}

