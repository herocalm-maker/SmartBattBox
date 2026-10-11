/**
 * @file    mod_metering.h
 * @brief   计量模块 (Module 层)
 *
 * 框架:
 *   - mod_metering_init()      — 初始化双路 HLW8110
 *   - mod_metering_handler()   — 周期调用，读取数据 + 异常检测
 *   - metering_get_info()      — 获取数据指针
 */
#ifndef __MOD_METERING_H__
#define __MOD_METERING_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "drv_hlw8110.h"   /* 提供 hlw8110_t, 供 mod_metering_get_hlw() */

/*====================================================================
 * 数据结构
 *====================================================================*/

typedef struct {
    /* 输入侧 */
    float vin_v;              /* 输入电压 V */
    float vin_i;              /* 输入电流 A */
    float vin_p_w;            /* 输入功率 W */
    float vin_energy_wh;      /* 输入电能 Wh */

    /* 输出侧 */
    float vout_v;             /* 输出电压 V */
    float vout_i;             /* 输出电流 A */
    float vout_p_w;           /* 输出功率 W */
    float vout_energy_wh;     /* 输出电能 Wh */

    /* 频率 (Hz) */
    float vin_freq_hz;        /* 输入电压频率 (如 50.0/60.0; 无 AC 时为 0) */
    float vout_freq_hz;       /* 输出电压频率 */

    /* AC 状态 */
    bool ac_in_present;
    bool ac_out_present;

    /* 会话电能 */
    float session_in_wh;
    float session_out_wh;

    /* 状态 */
    uint8_t online;           /* 1=在线 0=离线 */
    uint16_t fail_count;
    uint16_t ok_count;
    uint8_t in_ok;            /* 输入通道有效 (1=连续读取正常, 0=持续无数据) */
    uint8_t out_ok;           /* 输出通道有效 (1=连续读取正常, 0=持续无数据) */
} METER_INFO_T;

/*====================================================================
 * API
 *====================================================================*/

void mod_metering_init(void);

/**
 * @brief  周期处理 (建议 200ms 调用一次)
 */
void mod_metering_handler(void);

/**
 * @brief  获取计量信息指针
 * @return NULL=离线
 */
METER_INFO_T* metering_get_info(void);

/* Input channel safety status is independent of output-channel online state. */
#define METERING_AC_MIN_V          90.0f
#define METERING_AC_MAX_V          300.0f
#define METERING_AC_CONFIRM_SAMPLES 2u
#define METERING_AC_STALE_MS       2500u

typedef enum {
    AC_INPUT_UNKNOWN = 0,
    AC_INPUT_ABSENT,
    AC_INPUT_PRESENT
} ac_input_state_t;

typedef struct {
    ac_input_state_t state;
    float voltage;
    uint32_t sample_ms; /* Last successful input measurement. */
} ac_input_snapshot_t;

/* Atomic snapshot with freshness validation; UNKNOWN is never ABSENT. */
ac_input_snapshot_t mod_metering_get_ac_input(void);

/**
 * @brief  获取指定通道的 HLW8110 驱动实例 (供上层注入板级硬件配置/校验寄存器)
 * @param  idx  0=输入侧(USART2), 1=输出侧(USART3)
 * @return 实例指针; idx 越界返回 NULL
 */
hlw8110_t *mod_metering_get_hlw(uint8_t idx);

/* 板级采样配置注入 + 关键寄存器校验 (收敛 App 对 drv_hlw8110 的直连) */
void mod_metering_apply_board_config(void);
void mod_metering_verify_syscon(void);

#ifdef __cplusplus
}
#endif
#endif /* __MOD_METERING_H__ */
