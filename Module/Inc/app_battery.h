/**
 * @file    app_battery.h
 * @brief   平台无关的电池状态机和学习日志接口，实现在mod_battery.c。
 * 包含周期采样、故障/滞回、学习参数CRC日志、LED模式切换。
 * battery_app_start之后安装回调，再重复调用battery_app_process。
 * optional_store传NULL可不用Flash；保留原battery_app_* API以兼容现有调用。
 * 电池容量、终止电流和NTC曲线须按实物配置，不能由原理图推断。
 */
#ifndef APP_BATTERY_H
#define APP_BATTERY_H
#include "drv_max17263.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* 可选的非易失存储后端；日志实现位于mod_battery.c中。 */
#define BATTERY_RECORD_SIZE 64u
/* Two independent erase units. Offsets are relative to the reserved NVM region.
 * program() must implement NOR 1->0 writes; return 0 only after readback success.
 * Save is synchronous, never call from IRQ. */
typedef struct
{
    void *ctx;
    uint32_t sector_size;
    int (*read)(void *ctx, uint32_t offset, uint8_t *data, size_t length);
    int (*erase)(void *ctx, unsigned sector);
    int (*program)(void *ctx, uint32_t offset, const uint8_t *data, size_t length);
} battery_store_t;
/*********************************************************************
 * @fn      battery_profile_hash
 * @brief   计算电池配置和模型的匹配摘要。
 * @param   c  有效配置指针
 * @return  配置摘要。
 * @note    电池ID在记录中单独匹配；不将指针地址当成配置内容。
********************************************************************/
extern uint32_t battery_profile_hash(const max17263_config_t *c);
/* M_ENOTREADY means no matching, valid record; M_ENVM means backend failed. */
/*********************************************************************
 * @fn      battery_store_load
 * @brief   读取与当前电池和配置匹配的最新学习记录。
 * @param   s  存储后端
 * @param   d  驱动实例，用于取得配置
 * @param   p  输出学习参数
 * @return  M_OK成功；M_ENOTREADY无匹配记录；M_ENVM存储失败；M_EPROFILE容量不合理。
 * @note    返回成功前还会检查学习容量范围；失败时不得使用输出值。
********************************************************************/
extern int battery_store_load(const battery_store_t *s, const max17263_t *d, max17263_learned_t *p);
/*********************************************************************
 * @fn      battery_store_save
 * @brief   追加学习记录，必要时进行双扇区轮换。
 * @param   s  存储后端
 * @param   d  驱动实例，用于配置和容量校验
 * @param   p  待保存的五项原始学习参数
 * @return  M_OK写入并回读成功；M_EARG参数错误；M_ENVM存储失败。
 * @note    先写正文和CRC并回读，再最后提交；轮换时保留最新有效记录所在扇区。
********************************************************************/
extern int battery_store_save(const battery_store_t *s, const max17263_t *d, const max17263_learned_t *p);
enum
{
    BAT_FAULT_IO = 1u << 0,
    BAT_FAULT_SENSOR = 1u << 1,
    BAT_FAULT_UV = 1u << 2,
    BAT_FAULT_OV = 1u << 3,
    BAT_FAULT_COLD = 1u << 4,
    BAT_FAULT_HOT = 1u << 5,
    BAT_FAULT_OC_DIS = 1u << 6,
    BAT_FAULT_OC_CHG = 1u << 7,
    BAT_FAULT_LOW_SOC = 1u << 8,
    BAT_FAULT_NVM = 1u << 9,
    BAT_FAULT_PROFILE = 1u << 10,
    BAT_FAULT_RESET = 1u << 11,
    BAT_FAULT_STALE = 1u << 12,
    BAT_WARN_CYCLE_RANGE = 1u << 13,
    BAT_WARN_UNCHARACTERIZED = 1u << 14
};
typedef struct
{
    uint16_t min_cell_mv;
    uint16_t max_cell_mv;
    int8_t min_temp_c;
    int8_t max_temp_c;
    uint8_t low_soc;
    int32_t max_discharge_ma;
    int32_t max_charge_ma;
    uint32_t period_ms;
    uint32_t stale_ms;
    uint8_t led_bars;
    uint8_t led_mode;
    uint8_t led_brightness;
    uint16_t led_mv;
    bool led_while_charging;
} battery_policy_t;
typedef struct battery_app battery_app_t;
struct battery_app
{
    max17263_t gauge;
    battery_store_t store;
    battery_policy_t policy;
    max17263_sample_t sample;
    uint32_t active_faults;
    uint32_t latched_faults;
    uint32_t last_attempt;
    uint32_t last_good;
    uint32_t last_save;
    uint16_t last_saved_cycles;
    uint16_t observed_alerts;
    unsigned consecutive_errors;
    int last_error;
    bool ready;
    bool started;
    bool have_saved;
    bool have_good;
    void *user;
    /* Events are advisory; BMS protection must work independently of this code. */
    void (*on_update)(void *, const battery_app_t *);
    int (*recover_bus)(void *);
};
/*********************************************************************
 * @fn      battery_app_start
 * @brief   校验配置并建立电池应用实例。
 * @param   a  待初始化实例
 * @param   b  总线回调
 * @param   c  电芯和硬件配置
 * @param   p  采样、告警和LED策略
 * @param   store  可选存储后端，NULL表示不持久化
 * @return  M_OK成功；参数、范围或驱动绑定错误返回负值。
 * @note    本函数不访问芯片；返回后安装回调，由process执行首次初始化。
********************************************************************/
extern int battery_app_start(battery_app_t *a, const max17263_bus_t *b, const max17263_config_t *c,
                             const battery_policy_t *p, const battery_store_t *store);
/* Run from a single main-loop/RTOS task. May block during POR and Flash erase. */
/*********************************************************************
 * @fn      battery_app_process
 * @brief   执行初始化重试、周期采样、故障检测和学习参数保存。
 * @param   a  已经start的应用实例
 * @return  none
 * @note    仅单任务调用；初始化及存储可能阻塞，不得在ISR执行。
********************************************************************/
extern void battery_app_process(battery_app_t *a);
/*********************************************************************
 * @fn      battery_app_clear_history
 * @brief   清理历史故障，保留仍然有效的当前故障。
 * @param   a  应用实例，NULL时不操作
 * @return  none
 * @note    不会清除芯片故障条件，也不会解除当前应用故障。
********************************************************************/
extern void battery_app_clear_history(battery_app_t *a); /* Retains currently active faults. */
/*********************************************************************
 * @fn      battery_app_checkpoint
 * @brief   读取学习参数并同步追加一条持久化记录。
 * @param   a  应用实例
 * @return  M_OK成功；M_ENOTREADY状态不允许；其他值为采集或存储错误。
 * @note    调用方保证任务串行和供电稳定；函数自身不检查最小保存间隔。
********************************************************************/
extern int battery_app_checkpoint(battery_app_t *a); /* Before planned poweroff, if power is stable. */
/* 任务内切换12灯图案或恢复policy规定的自动SOC显示；POR恢复后回到SOC。 */
/*********************************************************************
 * @fn      battery_app_led_mask
 * @brief   在电池任务中切换十二灯自定义图案。
 * @param   a  应用实例
 * @param   mask  低12位灯掩码，1亮0灭
 * @return  M_OK成功；未就绪或驱动错误返回负值。
 * @note    此核心接口非线程安全；跨任务请调用mod_battery_led_mask。
********************************************************************/
extern int battery_app_led_mask(battery_app_t *a, uint16_t mask);
/*********************************************************************
 * @fn      battery_app_led_soc
 * @brief   恢复策略中规定的自动SOC灯条显示。
 * @param   a  应用实例
 * @return  M_OK成功；未就绪或驱动错误返回负值。
 * @note    跨任务请调用mod_battery_led_soc，避免并发操作I2C。
********************************************************************/
extern int battery_app_led_soc(battery_app_t *a);
#ifdef __cplusplus
}
#endif

#endif
