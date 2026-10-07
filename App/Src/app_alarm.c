/**
 * @file    app_alarm.c
 * @brief   统一告警 + 故障码派生实现 (App 层)
 *
 * 实现:
 *   - s_alarm 为系统唯一告警位图持有者 (app_alarm_merge/unmerge/get)
 *   - 按独立映射表取最高优先级 ERROR 码 / ALARM 码 (显示任务拼接文本)
 *   - 温度高 = 任一过温告警 (BAT_OTP / INV_OTP / CHG_OTP)
 */
#include "app_alarm.h"
#include <stddef.h>

static volatile uint32_t s_alarm;

/*--------------------------------------------------------------------
 * ERROR 码映射表 (按优先级从高到低排列, 表序即优先级)
 *   ERROR 01 BAT_OVP 电池过压 | 02 BAT_UVP 电池欠压
 *   ERROR 03 BAT_OTP 电池过温 | 04 BAT_OCP 电池过流
 *   ERROR 05 INV_OTP 逆变过温 | 06 CHG_OTP 充电过温
 *   ERROR 07 RELAY 继电器故障
 *--------------------------------------------------------------------*/
static const struct { uint32_t bit; uint8_t code; } s_err_tab[] = {
    { SYS_ALARM_BAT_OTP,   3 },
    { SYS_ALARM_INV_OTP,   5 },
    { SYS_ALARM_CHG_OTP,   6 },
    { SYS_ALARM_BAT_OVP,   1 },
    { SYS_ALARM_BAT_OCP,   4 },
    { SYS_ALARM_BAT_UVP,   2 },
    { SYS_ALARM_RELAY,     7 },
};
#define ERR_TAB_N (sizeof(s_err_tab) / sizeof(s_err_tab[0]))

/*--------------------------------------------------------------------
 * ALARM 码映射表 (按优先级从高到低排列)
 *   ALARM 01 AC_INPUT | 02 BAT_INVALID | 03 STORAGE | 04 LOW_SOC
 *--------------------------------------------------------------------*/
static const struct { uint32_t bit; uint8_t code; } s_alm_tab[] = {
    { SYS_ALARM_AC_INPUT,    1 },
    { SYS_ALARM_BAT_INVALID, 2 },
    { SYS_ALARM_STORAGE,     3 },
    { SYS_ALARM_LOW_SOC,     4 },
};
#define ALM_TAB_N (sizeof(s_alm_tab) / sizeof(s_alm_tab[0]))

/**
 * @brief  合并 (上报告警): 按位 OR 写入位图.
 * @param  bits  待置位掩码
 */
void app_alarm_merge(uint32_t bits)
{
    s_alarm |= bits;
}

/**
 * @brief  清除告警: 按位 AND ~bits.
 * @param  bits  待清除掩码
 */
void app_alarm_unmerge(uint32_t bits)
{
    s_alarm &= ~bits;
}

/**
 * @brief  读取当前合并后告警位图.
 * @return 位图值
 */
uint32_t app_alarm_get(void)
{
    return s_alarm;
}

/**
 * @brief  读取当前最高优先级 ERROR 故障码.
 * @note   从统一告警位图按 ERROR 映射表取首个命中项, 表序即优先级.
 * @return 故障码 (1..7); 0 = 当前无 ERROR 置位.
 */
uint8_t app_alarm_error(void)
{
    uint32_t a = app_alarm_get() & APP_ALARM_ERR_MASK;
    for (size_t i = 0; i < ERR_TAB_N; i++)
    {
        if (a & s_err_tab[i].bit)
        {
            return s_err_tab[i].code;
        }
    }
    return 0;
}

/**
 * @brief  读取当前最高优先级 ALARM 报警码.
 * @note   从统一告警位图按 ALARM 映射表取首个命中项, 表序即优先级.
 * @return 报警码 (1..4); 0 = 当前无 ALARM 置位.
 */
uint8_t app_alarm_alarm(void)
{
    uint32_t a = app_alarm_get() & APP_ALARM_ALM_MASK;
    for (size_t i = 0; i < ALM_TAB_N; i++)
    {
        if (a & s_alm_tab[i].bit)
        {
            return s_alm_tab[i].code;
        }
    }
    return 0;
}

/**
 * @brief  温度高判定: 任一过温告警 (电池/逆变/充电) 置位.
 * @return true = 任一 OTP 置位 (显示任务据此将 0x200E 温度图标置 8).
 */
bool app_alarm_temp_high(void)
{
    uint32_t a = app_alarm_get();
    return (a & (SYS_ALARM_BAT_OTP | SYS_ALARM_INV_OTP | SYS_ALARM_CHG_OTP)) != 0u;
}

/**
 * @brief  读取当前最高优先级统一 ERR 码 (新界面 TextError "ERRxx" 显示用).
 * @note   ERROR 类保持 01-07; 无 ERROR 时 ALARM 类偏移 +7 -> 08-11
 *         (AC_INPUT=08 / BAT_INVALID=09 / STORAGE=10 / LOW_SOC=11).
 * @return 统一异常码 (1..11); 0 = 当前无任何异常.
 */
uint8_t app_alarm_err_code(void)
{
    uint8_t e = app_alarm_error();
    if (e != 0u)
    {
        return e;
    }
    uint8_t a = app_alarm_alarm();
    return (a != 0u) ? (uint8_t)(a + 7) : 0u;
}
