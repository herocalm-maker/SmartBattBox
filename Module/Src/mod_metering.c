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

/* 功率显示滤波: 3 样本中值 (仅显示用, 保护用原始值; 1s 采样 → 3s 窗口) */
#define METERING_MED_WIN       3u     /* 中值窗口 (1s 采样 → 3s, 兼顾平滑与跟随) */

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
    float           *p_f;            /* 功率滤波值 W (仅显示用) */
    float           *freq;           /* Frequency Hz */
    float           *energy;         /* 累计电能 Wh */
} metering_ch_t;

/* 中值滤波器状态 (10 样本环形数组) */
typedef struct {
    float   buf[METERING_MED_WIN];   /* 样本环形数组 */
    uint8_t idx;                     /* 下一个写入位置 */
    uint8_t count;                   /* 已填充样本数 (爬窗期) */
} metering_med_t;

/**
 * @brief 计量模块状态结构体: 统一管理 HLW8110 双通道、计量信息、状态标志
 */
typedef struct
{
    /* HLW8110 双通道 */
    hlw8110_t hlw_input;   /* USART2 输入侧 */
    hlw8110_t hlw_output;  /* USART3 输出侧 */

    /* 计量信息 */
    METER_INFO_T info;

    /* 状态标志 */
    struct {
        uint8_t initialized : 1;
        uint8_t data_valid : 1;
    } flags;

    /* 无数据计数 */
    struct {
        uint16_t vin;
        uint16_t vout;
    } no_data_count;

    /* 通道描述表 (输入/输出共用一套处理逻辑) */
    metering_ch_t ch[METERING_IDX_CNT];

    /* 功率显示滤波器 (输入/输出各一个) */
    metering_med_t p_med[METERING_IDX_CNT];

    /* 会话电能累加状态 (首读建立基线, 之后按差值累加) */
    bool first_energy[METERING_IDX_CNT];
    float last_energy_wh[METERING_IDX_CNT];

    /* 周期自检计数 */
    uint16_t selfcheck_cnt;

    /* AC 输入检测 */
    struct {
        ac_input_snapshot_t snapshot;
        uint8_t good_samples;
    } ac_input;
} metering_module_state_t;

static metering_module_state_t s_meter = {0};

/* 通道描述表初始化 (运行时初始化, 因为需要引用 s_meter 内部地址) */
/*********************************************************************
 * @fn      metering_channel_init
 * @brief   填写输入/输出两路的通道描述表 (句柄、串口、数据指针绑定).
 * @param   无
 * @return  none
 * @note    必须在 mod_metering_init 中最先调用; 描述表引用 s_meter 内部
 *          字段地址, 故不能静态初始化, 也不能在 init 之后重复执行.
********************************************************************/
static void metering_channel_init(void)
{
    s_meter.ch[METERING_IDX_INPUT] = (metering_ch_t){
        .name = "in",  .hlw = &s_meter.hlw_input,  .uart = BSP_UART_2,
        .no_data_cnt = &s_meter.no_data_count.vin,
        .v = &s_meter.info.vin_v, .i = &s_meter.info.vin_i, .p = &s_meter.info.vin_p_w,
        .p_f = &s_meter.info.vin_p_w_f,
        .freq = &s_meter.info.vin_freq_hz,
        .energy = &s_meter.info.vin_energy_wh,
    };
    s_meter.ch[METERING_IDX_OUTPUT] = (metering_ch_t){
        .name = "out", .hlw = &s_meter.hlw_output, .uart = BSP_UART_3,
        .no_data_cnt = &s_meter.no_data_count.vout,
        .v = &s_meter.info.vout_v, .i = &s_meter.info.vout_i, .p = &s_meter.info.vout_p_w,
        .p_f = &s_meter.info.vout_p_w_f,
        .freq = &s_meter.info.vout_freq_hz,
        .energy = &s_meter.info.vout_energy_wh,
    };
}

/*--------------------------------------------------------------------
 * 功率显示滤波: 10 样本中值
 *--------------------------------------------------------------------*/
/*********************************************************************
 * @fn      metering_med_update
 * @brief   送入一个新样本, 返回当前窗口中值.
 * @param   f  滤波器状态
 * @param   x  原始样本
 * @return  中值 (偶数个样本取中间两数平均).
 * @note    填窗期按实际样本数取中值: 首样本即原值, 通路开启立即同步;
 *          窗满后新样本替换最老样本. 前置条件: 通路开启时滤波器已被
 *          清零 (metering_med_reset), 不混入关闭期的旧样本.
********************************************************************/
static float metering_med_update(metering_med_t *f, float x)
{
    float tmp[METERING_MED_WIN];
    uint8_t i, j;

    f->buf[f->idx] = x;
    f->idx = (uint8_t)((f->idx + 1u) % METERING_MED_WIN);
    if (f->count < METERING_MED_WIN)
        f->count++;

    /* 复制有效样本 (填窗期 buf[0..count-1] 连续有效) 后插入排序 */
    memcpy(tmp, f->buf, (uint32_t)f->count * sizeof(float));
    for (i = 1; i < f->count; i++)
    {
        float key = tmp[i];
        for (j = i; j > 0 && tmp[j - 1] > key; j--)
            tmp[j] = tmp[j - 1];
        tmp[j] = key;
    }
    if ((f->count & 1u) != 0u)
    {
        return tmp[f->count / 2u];
    }
    return (tmp[f->count / 2u - 1u] + tmp[f->count / 2u]) * 0.5f;
}

/*********************************************************************
 * @fn      metering_med_reset
 * @brief   清零中值滤波器状态 (通路关闭/通道失效时调用, 防残留旧值).
********************************************************************/
static void metering_med_reset(metering_med_t *f)
{
    memset(f, 0, sizeof(*f));
}

/*====================================================================
 * 通路关闭立即清零 (App 层周期调用)
 *====================================================================*/
/*********************************************************************
 * @fn      metering_zero_channel
 * @brief   清零指定通道显示值 (V/I/P/滤波值/频率) 并复位中值滤波器.
 * @param   idx  通道索引 (METERING_IDX_INPUT / METERING_IDX_OUTPUT)
 * @note    计量芯片在市电/输出断开后会断电, 读数残留 ~10s 才被通道失效
 *          兜底清零; 通路关闭期间由 App 层周期调用强制显示 0, 通路重新
 *          开启后首个有效样本即经中值直通同步. 未初始化时直接跳过.
********************************************************************/
static void metering_zero_channel(uint32_t idx)
{
    metering_ch_t *ch;
    if (!s_meter.flags.initialized)
    {
        return;
    }
    ch = &s_meter.ch[idx];
    *ch->v    = 0.0f;
    *ch->i    = 0.0f;
    *ch->p    = 0.0f;
    *ch->p_f  = 0.0f;
    *ch->freq = 0.0f;
    metering_med_reset(&s_meter.p_med[idx]);
}

/**
 * @brief  输入通路关闭期间立即清零输入侧功率显示.
 */
void mod_metering_zero_input(void)
{
    metering_zero_channel(METERING_IDX_INPUT);
}

/**
 * @brief  输出通路关闭期间立即清零输出侧功率显示.
 */
void mod_metering_zero_output(void)
{
    metering_zero_channel(METERING_IDX_OUTPUT);
}

/* Publish immediately after INPUT processing, before any OUTPUT UART wait. */
/*********************************************************************
 * @fn      ac_input_update
 * @brief   根据输入通道最新采样更新 AC 输入在位状态快照 (带确认与撤销).
 * @param   success  本次输入通道读取是否成功
 * @param   voltage  输入侧电压 (V)
 * @return  none
 * @note    电压落入 [METERING_AC_MIN_V, METERING_AC_MAX_V] 连续
 *          METERING_AC_CONFIRM_SAMPLES 次才置 AC_INPUT_PRESENT;
 *          任一次越窗立即撤销为 ABSENT (旁路判定宁可不旁路);
 *          读失败/非法值置 UNKNOWN 并清确认计数.
********************************************************************/
static void ac_input_update(bool success, float voltage)
{
    uint32_t now = bsp_get_tick_ms();
    taskENTER_CRITICAL();
    if (!success || !isfinite(voltage) || voltage < 0.0f)
    {
        s_meter.ac_input.snapshot.state = AC_INPUT_UNKNOWN;
        s_meter.ac_input.good_samples = 0;
    }
    else
    {
        if ((uint32_t)(now - s_meter.ac_input.snapshot.sample_ms) > METERING_AC_STALE_MS)
            s_meter.ac_input.good_samples = 0;
        s_meter.ac_input.snapshot.voltage = voltage;
        s_meter.ac_input.snapshot.sample_ms = now;
        if (voltage >= METERING_AC_MIN_V && voltage <= METERING_AC_MAX_V)
        {
            if (s_meter.ac_input.good_samples < METERING_AC_CONFIRM_SAMPLES)
            	++s_meter.ac_input.good_samples;
            s_meter.ac_input.snapshot.state = s_meter.ac_input.good_samples >= METERING_AC_CONFIRM_SAMPLES ?
                               AC_INPUT_PRESENT : AC_INPUT_UNKNOWN;
        }
        else
        {
            s_meter.ac_input.good_samples = 0;
            s_meter.ac_input.snapshot.state = AC_INPUT_ABSENT; /* Revoke bypass on first bad sample. */
        }
    }
    s_meter.info.ac_in_present = s_meter.ac_input.snapshot.state == AC_INPUT_PRESENT;
    taskEXIT_CRITICAL();
}

/*********************************************************************
 * @fn      mod_metering_get_ac_input
 * @brief   获取 AC 输入快照副本 (线程安全), 并叠加时效判断.
 * @param   无
 * @return  快照副本; 采样时刻距今超过 METERING_AC_STALE_MS 时 state 降级为 UNKNOWN.
 * @note    临界区仅覆盖结构体复制; 可跨任务调用.
********************************************************************/
ac_input_snapshot_t mod_metering_get_ac_input(void)
{
    ac_input_snapshot_t result;
    taskENTER_CRITICAL();
    result = s_meter.ac_input.snapshot;
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
static void mod_metering_accumulate_energy(uint32_t idx, float energy_wh)
{
    if (idx >= METERING_IDX_CNT)
      return;
    
    if (s_meter.first_energy[idx]) 
    {
        s_meter.last_energy_wh[idx] = energy_wh;
        s_meter.first_energy[idx] = false;
        return;
    }

    float delta = energy_wh - s_meter.last_energy_wh[idx];
    /* 10Wh 上限过滤掉重初始化/掉线后的异常跳变; 负差值为寄存器回绕或复位, 不累计 */
    if (delta > 0.0f && delta < 10.0f)
    {
        if (idx == METERING_IDX_INPUT) 
            s_meter.info.session_in_wh += delta;
        else 
            s_meter.info.session_out_wh += delta;
    }

    s_meter.last_energy_wh[idx] = energy_wh;
}

/**
 * @brief  单通道处理: 读取数据, 失败超过 50 次则重初始化通道
 * @param  idx  通道索引 (INPUT / OUTPUT)
 * @return true=读取成功
 */
static bool mod_metering_channel_process(uint32_t idx)
{
    metering_ch_t *ch = &s_meter.ch[idx];
    hlw8110_measure_t meas = {0};

    if (drv_hlw8110_measure(ch->hlw, &meas))
    {
        *ch->no_data_cnt = 0;
        *ch->v      = meas.voltage;
        *ch->i      = meas.current;
        *ch->p      = meas.power;
        *ch->p_f    = metering_med_update(&s_meter.p_med[idx], meas.power);
        *ch->freq   = meas.freq_hz;
        *ch->energy = meas.energy_kwh * 1000.0f;  /* 驱动输出 kWh, 对外统一换算为 Wh */
        mod_metering_accumulate_energy(idx, *ch->energy);
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
        *ch->p_f  = 0.0f;
        *ch->freq = 0.0f;
        metering_med_reset(&s_meter.p_med[idx]);
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
    /* 运行时初始化通道描述表 (需要引用 s_meter 内部地址) */
    metering_channel_init();

    memset(&s_meter.info, 0, sizeof(s_meter.info));
    for (uint32_t i = 0; i < METERING_IDX_CNT; i++)
        metering_med_reset(&s_meter.p_med[i]);
    taskENTER_CRITICAL();
    memset(&s_meter.ac_input.snapshot, 0, sizeof(s_meter.ac_input.snapshot));
    s_meter.ac_input.good_samples = 0;
    taskEXIT_CRITICAL();

    bool in_ok  = drv_hlw8110_init(&s_meter.hlw_input, BSP_UART_2);
    bool out_ok = drv_hlw8110_init(&s_meter.hlw_output, BSP_UART_3);

    s_meter.flags.initialized = true;
    s_meter.flags.data_valid  = false;
    s_meter.selfcheck_cnt = 0;
    s_meter.no_data_count.vin  = 0;
    s_meter.no_data_count.vout = 0;
    for (uint32_t i = 0; i < METERING_IDX_CNT; i++)
    {
        s_meter.first_energy[i]   = true;   /* 首次读数建立基线 */
        s_meter.last_energy_wh[i] = 0.0f;
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

    /* 调用方为 SensorTask 200ms 周期, 此处二次门控到实际 1s 采样周期
     * (无符号差值比较, 兼容 tick 回绕) */
    if(bsp_get_tick_ms() - waittime < 1000)
      return;
    
    waittime = bsp_get_tick_ms();
    bool any_success = false;

    /* 双通道读取 (输入/输出共用逻辑) */
    for (uint32_t idx = 0; idx < METERING_IDX_CNT; idx++)
    {
        bool success = mod_metering_channel_process(idx);
        if (idx == METERING_IDX_INPUT)
        	ac_input_update(success, s_meter.info.vin_v);

        if (idx == METERING_IDX_OUTPUT)
            s_meter.info.ac_out_present = success && s_meter.info.vout_v >= METERING_AC_MIN_V &&
                                    s_meter.info.vout_v <= METERING_AC_MAX_V;

        if (success)
        	any_success = true;
    }

    /* 分通道有效性: 连续 METERING_NO_DATA_TIMEOUT 次无数据 → 通道失效
     * (输入失效影响充电/AC 判定; 输出失效影响逆变/旁路, 但不关闭充电) */
    s_meter.info.in_ok  = (s_meter.no_data_count.vin  < METERING_NO_DATA_TIMEOUT) ? 1u : 0u;
    s_meter.info.out_ok = (s_meter.no_data_count.vout < METERING_NO_DATA_TIMEOUT) ? 1u : 0u;

    /* 在线/离线判定 (迟滞: 连续失败/成功计数) */
    if (any_success)
    {
        s_meter.info.ok_count++;
        s_meter.info.fail_count = 0;
        if (s_meter.info.online == 0u && s_meter.info.ok_count >= METERING_RECOVER_THRESHOLD)
        {
            s_meter.info.online = 1u;
            log_print("Metering: IC online");
        }
    }
    else
    {
        s_meter.info.fail_count++;
        s_meter.info.ok_count = 0;
        if (s_meter.info.online != 0u && s_meter.info.fail_count >= METERING_FAIL_THRESHOLD)
        {
            s_meter.info.online = 0u;
            log_print("Metering: IC offline");
        }
    }
    s_meter.flags.data_valid = (s_meter.info.online != 0u);

    /* 周期自检 (~10s): OTP 可读性探测, 失败则重初始化对应通道 */
    if (++s_meter.selfcheck_cnt >= METERING_SELF_CHECK_INTERVAL)
    {
        s_meter.selfcheck_cnt = 0;
        for (uint32_t idx = 0; idx < METERING_IDX_CNT; idx++)
        {
            metering_ch_t *ch = &s_meter.ch[idx];
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

/**
 * @brief  复位输出通道"无数据"计数。
 * @note   进入逆变/旁路输出通路时由 App 层调用: 输出通道 HLW8110 若由
 *         输出端供电, 空闲期会持续读失败而累积计数, 导致 out_ok 假失效;
 *         启动时清零, 让有效性从输出建立时刻重新判定。
 */
void mod_metering_reset_out_no_data(void)
{
    s_meter.no_data_count.vout = 0;
}

/** @brief  获取计量信息指针 (仅 online 时有效) */
METER_INFO_T* metering_get_info(void)
{
    return &s_meter.info;
}

/** @brief  获取指定通道 HLW8110 驱动实例 (0=输入, 1=输出) */
hlw8110_t *mod_metering_get_hlw(uint8_t idx)
{
    if (idx >= METERING_IDX_CNT)
        return NULL;
    return s_meter.ch[idx].hlw;
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
    static const char *ch_name[METERING_IDX_CNT] = { "in", "out" };

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
                      ch_name[i], (unsigned)syscon, pga_ok ? "OK" : "MISMATCH",
                      k1_i, k1_d, k2_i, k2_d);
        }
        else
        {
            log_print("Metering: ch[%s] SYSCON read fail", ch_name[i]);
        }
    }
}

