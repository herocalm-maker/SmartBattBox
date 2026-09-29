/**
 * @file    app_faultcode.c
 * @brief   故障码 / 报警码生成 (App 层)
 *
 * 实现:
 *   - 从 app_protect_alarm_get() 读取统一告警位图
 *   - 按独立映射表取最高优先级 ERROR 码 / ALARM 码
 *   - 温度高 = 任一过温告警 (BAT_OTP / INV_OTP / CHG_OTP)
 */
#include "app_faultcode.h"
#include "app_protect.h"
#include <stddef.h>

/*--------------------------------------------------------------------
 * ERROR 码映射表 (按优先级从高到低排列, 表序即优先级)
 *   ERROR 01 BAT_OVP 电池过压     |   ERROR 02 BAT_UVP 电池欠压
 *   ERROR 03 BAT_OTP 电池过温     |   ERROR 04 BAT_OCP 电池过流
 *   ERROR 05 INV_OTP 逆变过温     |   ERROR 06 CHG_OTP 充电过温
 *   ERROR 07 RELAY 继电器故障
 *--------------------------------------------------------------------*/
static const struct { uint32_t bit; uint8_t code; } s_err_tab[] = {
    { SYS_ALARM_BAT_OTP,   3 },  /* 优先级1: 过温(危险) */
    { SYS_ALARM_INV_OTP,   5 },
    { SYS_ALARM_CHG_OTP,   6 },
    { SYS_ALARM_BAT_OVP,   1 },  /* 优先级2: 过压/过流 */
    { SYS_ALARM_BAT_OCP,   4 },
    { SYS_ALARM_BAT_UVP,   2 },  /* 优先级3: 欠压/继电器 */
    { SYS_ALARM_RELAY,     7 },
};
#define ERR_TAB_N (sizeof(s_err_tab) / sizeof(s_err_tab[0]))

/*--------------------------------------------------------------------
 * ALARM 码映射表 (按优先级从高到低排列)
 *   ALARM 01 AC_INPUT      AC 输入异常
 *   ALARM 02 BAT_INVALID   电池数据无效
 *   ALARM 03 STORAGE       存储故障
 *   ALARM 04 LOW_SOC       低电量
 *--------------------------------------------------------------------*/
static const struct { uint32_t bit; uint8_t code; } s_alm_tab[] = {
    { SYS_ALARM_AC_INPUT,    1 },
    { SYS_ALARM_BAT_INVALID, 2 },
    { SYS_ALARM_STORAGE,     3 },
    { SYS_ALARM_LOW_SOC,     4 },
};
#define ALM_TAB_N (sizeof(s_alm_tab) / sizeof(s_alm_tab[0]))

/**
 * @brief  读取当前最高优先级 ERROR 故障码.
 * @note   从统一告警位图按 ERROR 映射表 (s_err_tab) 取首个命中项, 表序即优先级;
 *         与 ALARM 独立编号, 供显示任务拼接 "ERROR: %02u" 文本.
 * @return 故障码 (1..7); 0 = 当前无 ERROR 置位.
 */
uint8_t app_faultcode_get_error(void)
{
    uint32_t a = app_protect_alarm_get() & FAULT_ERR_MASK;
    for (size_t i = 0; i < ERR_TAB_N; i++) {
        if (a & s_err_tab[i].bit) {
            return s_err_tab[i].code;
        }
    }
    return 0;
}

/**
 * @brief  读取当前最高优先级 ALARM 报警码.
 * @note   从统一告警位图按 ALARM 映射表 (s_alm_tab) 取首个命中项, 表序即优先级;
 *         提示性告警, 不触发保护停机, 供显示任务拼接 "ALARM: %02u" 文本.
 * @return 报警码 (1..4); 0 = 当前无 ALARM 置位.
 */
uint8_t app_faultcode_get_alarm(void)
{
    uint32_t a = app_protect_alarm_get() & FAULT_ALM_MASK;
    for (size_t i = 0; i < ALM_TAB_N; i++) {
        if (a & s_alm_tab[i].bit) {
            return s_alm_tab[i].code;
        }
    }
    return 0;
}

/**
 * @brief  温度高判定.
 * @note   任一过温告警 (电池 BAT_OTP / 逆变 INV_OTP / 充电 CHG_OTP) 即触发;
 *         显示任务据此将 0x2017 (VP_ICON_PWR) 图标置为 8.
 * @return true = 任一 OTP 告警置位; false = 无过温.
 */
bool app_faultcode_temp_high(void)
{
    uint32_t a = app_protect_alarm_get();
    return (a & (SYS_ALARM_BAT_OTP | SYS_ALARM_INV_OTP | SYS_ALARM_CHG_OTP)) != 0u;
}
