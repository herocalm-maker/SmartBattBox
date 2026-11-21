/**
 * @file app_alarm.c
 * @brief 统一告警 + 故障码派生实现 (App 层)
 *
 * 实现要点:
 *   - s_alarm 是系统唯一告警位图 (merge/unmerge/get, 临界区保护)
 *   - 一张按优先级排列的码表完成"位图 → ERR 码"映射, 表序即优先级
 *   - 温度高 = BAT_OTP / INV_OTP / CHG_OTP 任一置位 (显示任务点亮温度图标)
 *
 * 码段 (2026-11 重新定义, 详见 app_alarm.h):
 *   01~09 通讯/数据/存储 | 10~19 电池保护 | 20~29 温度保护 | 40~49 输入 | 88 保留
 */
#include "app_alarm.h"
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"

static volatile uint32_t s_alarm;

/*--------------------------------------------------------------------
 * ERR 码映射表 (表序即优先级: 越靠前越先显示)
 *   位                      码                          说明
 *   BAT_COMM         -> 01  电池电量计通讯失败        (锁死)
 *   METER_COMM       -> 02  计量芯片通讯失败          (锁死/降级)
 *   BAT_INVALID      -> 03  电池数据无效              (锁死)
 *   BAT_OCP          -> 12  电池过流                  (锁死)
 *   BAT_UVP          -> 11  电池欠压                  (停放电)
 *   BAT_OVP          -> 10  电池过压                  (停充)
 *   BAT_OTP          -> 20  电池过温                  (可恢复降载)
 *   CHG_OTP          -> 21  充电过温                  (锁死)
 *   INV_OTP          -> 22  逆变过温                  (锁死)
 *   STORAGE          -> 04  存储/参数异常             (降级)
 *   AC_INPUT         -> 40  市电异常                  (转逆变)
 *--------------------------------------------------------------------*/
static const struct { uint32_t bit; uint8_t code; } s_code_tab[] = {
    { SYS_ALARM_BAT_COMM,    APP_ERR_BAT_COMM    },
    { SYS_ALARM_METER_COMM,  APP_ERR_METER_COMM  },
    { SYS_ALARM_BAT_INVALID, APP_ERR_BAT_INVALID },
    { SYS_ALARM_BAT_OCP,     APP_ERR_BAT_OCP     },
    { SYS_ALARM_BAT_UVP,     APP_ERR_BAT_UVP     },
    { SYS_ALARM_BAT_OVP,     APP_ERR_BAT_OVP     },
    { SYS_ALARM_BAT_OTP,     APP_ERR_BAT_OTP     },
    { SYS_ALARM_CHG_OTP,     APP_ERR_CHG_OTP     },
    { SYS_ALARM_INV_OTP,     APP_ERR_INV_OTP     },
    { SYS_ALARM_STORAGE,     APP_ERR_STORAGE     },
    { SYS_ALARM_AC_INPUT,    APP_ERR_AC_INPUT    },
};
#define CODE_TAB_N (sizeof(s_code_tab) / sizeof(s_code_tab[0]))

/**
 * @brief  合并 (上报告警): 按位 OR 写入位图.
 * @param  bits  待置位掩码
 * @note   位图为读-改-写, 主任务 (battery_alarm_sync) 与计量任务
 *         (METER_COMM) 并发操作, 临界区防止互相丢位.
 */
void app_alarm_merge(uint32_t bits)
{
    taskENTER_CRITICAL();
    s_alarm |= bits;
    taskEXIT_CRITICAL();
}

/**
 * @brief  清除告警: 按位 AND ~bits.
 * @param  bits  待清除掩码
 */
void app_alarm_unmerge(uint32_t bits)
{
    taskENTER_CRITICAL();
    s_alarm &= ~bits;
    taskEXIT_CRITICAL();
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
 * @brief  温度高判定: 任一过温告警 (电池/逆变/充电) 置位.
 * @return true = 任一 OTP 置位 (显示任务据此将温度图标置 8).
 */
bool app_alarm_temp_high(void)
{
    uint32_t a = app_alarm_get();
    return (a & (SYS_ALARM_BAT_OTP | SYS_ALARM_INV_OTP | SYS_ALARM_CHG_OTP)) != 0u;
}

/**
 * @brief  读取当前最高优先级统一 ERR 码.
 * @note   按码表顺序取首个命中项; 表序即优先级 (通讯/数据 > 电池 > 温度 > 存储 > 低电量 > 市电).
 * @return 码值; 0 = 当前无任何异常.
 */
uint8_t app_alarm_err_code(void)
{
    uint32_t a = app_alarm_get();

    for (size_t i = 0; i < CODE_TAB_N; i++)
    {
        if (a & s_code_tab[i].bit)
        {
            return s_code_tab[i].code;
        }
    }
    return 0;
}

/**
 * @brief  判断 ERR 码是否属于"停机锁死"类 (只能断电重启恢复).
 * @param  code ERR 码
 * @return true = 锁死类; false = 可恢复/提示类或无效码.
 */
bool app_alarm_code_is_latched(uint8_t code)
{
    switch (code)
    {
        case APP_ERR_BAT_COMM:
        case APP_ERR_METER_COMM:
        case APP_ERR_BAT_INVALID:
        case APP_ERR_BAT_OCP:
        case APP_ERR_CHG_OTP:
        case APP_ERR_INV_OTP:
            return true;
        default:
            return false;
    }
}
