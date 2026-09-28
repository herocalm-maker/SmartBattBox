/**
 * @file    mod_battery.h
 * @brief   电池快照与跨任务请求接口 (Module 层)
 * @note    I2C和Flash操作归SensorTask所有；其他任务只能读取副本或提交请求。
 */
#ifndef MOD_BATTERY_H
#define MOD_BATTERY_H
#include "app_battery.h"

#ifdef __cplusplus
extern "C"
{
#endif
/* Snapshot values remain inspectable when invalid; never use without valid. */
typedef struct
{
    uint32_t voltage_mv; /* 整包电压，mV */
    uint32_t cell_voltage_mv; /* 等效平均单体电压，mV，不是逐节测量 */
    int32_t current_ma; /* 瞬时电流，mA；充电为正，放电为负 */
    int32_t avg_current_ma; /* 平均电流，mA */
    int16_t temp_deg; /* internal die temperature, 0.1C; not cell temperature */
    uint8_t soc; /* 报告SOC，0～100% */
    uint8_t soh; /* 容量型SOH，显示限制到0～100%，须检查soh_valid */
    uint8_t online; /* 与发布时的数据有效性同步，读取后仍须检查valid */
    uint32_t repcap_mah; /* 报告剩余容量，mAh */
    uint32_t fullcap_mah; /* 报告满容量，mAh */
    uint16_t cycles_raw; /* 原始等效循环计数，0.01次/LSB */
    uint16_t time_to_empty_min; /* 预计放空分钟数，仅tte_valid时有效 */
    uint16_t time_to_full_min; /* 预计充满分钟数，仅ttf_valid时有效 */
    uint16_t alert_flags; /* 已观测到的芯片告警位 */
    uint32_t timestamp_ms; /* 最后有效采样的毫秒时间戳，可回绕 */
    uint32_t active_faults; /* 当前应用故障位图 */
    uint32_t latched_faults; /* 锁存的历史故障位图 */
    int last_error; /* 最近一次应用操作错误码 */
    bool valid; /* 快照是否完整有效且未过期 */
    bool tte_valid; /* 放空时间是否有效 */
    bool ttf_valid; /* 充满时间是否有效 */
    bool soh_valid; /* SOH是否来自有效采样 */
} BATT_INFO_T;
/*********************************************************************
 * @fn      mod_battery_init
 * @brief   建立STM32电池模块和存储后端绑定。
 * @return  true绑定成功；false配置失败。
 * @note    仅SensorTask调用；成功不代表芯片已初始化，后续handler完成硬件配置。
********************************************************************/
extern bool mod_battery_init(void);
/*********************************************************************
 * @fn      mod_battery_handler
 * @brief   由SensorTask执行电池状态机和跨任务请求。
 * @return  none
 * @note    I2C、学习存储及LED操作均集中在此任务；建议约20ms调用。
********************************************************************/
extern void mod_battery_handler(void); /* only SensorTask owns I2C, NVM and LED operations */
/*********************************************************************
 * @fn      battery_get_snapshot
 * @brief   以短临界区复制电池快照，并判断有效期。
 * @param   out  输出快照；不能为NULL
 * @return  true数据有效且未过期；false无效、过期或指针为空。
 * @note    非NULL时即使返回false也会给出状态；调用方不得把其中旧值作为有效测量。
********************************************************************/
extern bool battery_get_snapshot(BATT_INFO_T *out); /* always copies, false if invalid/stale */
/* Thread-safe mailbox. LED request returns accepted, completion in last_error. */
/*********************************************************************
 * @fn      mod_battery_led_mask
 * @brief   向SensorTask提交十二灯自定义显示请求。
 * @param   m  低12位灯掩码，高4位必须为零
 * @return  true请求已接受；false掩码非法。
 * @note    线程安全单槽邮箱，新请求覆盖旧请求；返回true不代表已经完成硬件写入。
********************************************************************/
extern bool mod_battery_led_mask(uint16_t m);
/*********************************************************************
 * @fn      mod_battery_led_soc
 * @brief   向SensorTask提交恢复自动SOC显示请求。
 * @return  none
 * @note    只提交邮箱，不直接访问芯片；后续handler串行执行。
********************************************************************/
extern void mod_battery_led_soc(void);
/* Blocking task API, max bounded wait; SensorTask performs the save. */
/*********************************************************************
 * @fn      mod_battery_checkpoint
 * @brief   请求SensorTask保存学习参数，并有限等待完成。
 * @param   timeout_ms  最长等待时间，毫秒
 * @return  true保存成功或近期保存被合并；false忙、超时或保存失败。
 * @note    从其他任务调用，禁止在SensorTask或ISR调用；超时不取消已提交请求。
********************************************************************/
extern bool mod_battery_checkpoint(uint32_t timeout_ms);
#ifdef __cplusplus
}
#endif

#endif
