/**
 * @file    mod_battery.c
 * @brief   电池采集、故障管理、学习日志及STM32任务适配 (Module 层)
 *
 * 文件分为：平台无关状态机、双扇区学习日志、SensorTask适配三个部分。
 * 核心接口见app_battery.h，跨任务接口见mod_battery.h。
 * @note    此模块输出状态与故障；通路动作由AppMainTask决定，不能替代独立BMS。
 */
#include "app_battery.h"
#include "sys_alarm.h"
#include <string.h>

/* 双扇区学习日志扫描结果 (仅本文件内部使用, 不进公共头文件) */
typedef struct
{
    bool found;        /* 是否找到完整匹配记录 */
    uint32_t offset;   /* 命中记录在存储内的字节偏移 */
    uint32_t seq;      /* 记录序号 (半范围比较处理回绕) */
    uint8_t record[64];/* 完整64字节记录 */
} scan_t;

/*********************************************************************
 * @fn      now
 * @brief   取得应用实例的毫秒时钟。
 * @param   a  应用实例
 * @return  当前32位毫秒计数。
 * @note    允许计数回绕，调度使用无符号差值。
********************************************************************/
static uint32_t now(battery_app_t *a)
{
    return a->gauge.bus.now_ms(a->gauge.bus.ctx);
}

/*********************************************************************
 * @fn      event
 * @brief   锁存当前故障并通知上层更新快照。
 * @param   a  应用实例
 * @return  none
 * @note    回调在调用任务中同步执行，不得递归操作电量计。
********************************************************************/
static void event(battery_app_t *a)
{
    a->latched_faults |= a->active_faults;
    if (a->on_update)
    {
        a->on_update(a->user, a);
    }
}

/*********************************************************************
 * @fn      setup
 * @brief   加载可用学习记录并完成芯片、告警和LED初始化。
 * @param   a  应用实例
 * @return  M_OK成功；其他值为初始化错误。
 * @note    无历史记录允许新电池初始化；存储故障保留告警但不直接阻断采样。
********************************************************************/
static int setup(battery_app_t *a)
{
    max17263_learned_t learned;
    const max17263_learned_t *p = NULL;
    int r;
    /* 先查匹配记录；真正是否恢复由驱动根据POR决定，MCU复位不覆盖芯片当前学习值。 */
    if (a->store.read)
    {
        r = battery_store_load(&a->store, &a->gauge, &learned);
        if (r == M_OK)
        {
            p = &learned;
            a->last_saved_cycles = p->cycles;
            a->have_saved = true;
        }
        else if (r != M_ENOTREADY)
        {
            a->active_faults |= BAT_FAULT_NVM;
        }
    }
    r = drv_max17263_initialize(&a->gauge, p);
    if (r)
    {
        return r;
    }
    r = drv_max17263_alerts(&a->gauge, a->policy.min_cell_mv, a->policy.max_cell_mv, a->policy.min_temp_c,
                            a->policy.max_temp_c, a->policy.low_soc, -a->policy.max_discharge_ma,
                            a->policy.max_charge_ma);
    if (r)
    {
        return r;
    }
    return drv_max17263_led(&a->gauge, a->policy.led_bars, a->policy.led_mode, a->policy.led_brightness,
                            a->policy.led_mv, a->policy.led_while_charging);
}

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
int battery_app_start(battery_app_t *a, const max17263_bus_t *b, const max17263_config_t *c,
                      const battery_policy_t *p, const battery_store_t *store)
{
    int r;
    if (!a || !p || !c || p->period_ms < 100 || p->period_ms > 60000 || p->stale_ms < p->period_ms ||
        p->stale_ms > 600000 || p->min_cell_mv >= p->max_cell_mv || p->max_charge_ma <= 0 ||
        p->max_discharge_ma <= 0 || p->max_cell_mv > 5100 || p->min_cell_mv % 20 || p->max_cell_mv % 20 ||
        p->min_temp_c >= p->max_temp_c || p->low_soc > 100 || p->led_bars < 3 || p->led_bars > 12 ||
        p->led_mode > 3 || p->led_brightness > 31 || p->led_mv > 2520 || p->led_mv % 40)
    {
        return M_EARG;
    }
    /* 先检查硬件告警分辨率，拒绝静默取整导致的阈值偏移。 */
    if ((int64_t)p->max_charge_ma * c->rsense_uohm % 400000 ||
        (int64_t)p->max_discharge_ma * c->rsense_uohm % 400000 ||
        (int64_t)p->max_charge_ma * c->rsense_uohm / 400000 > 127 ||
        (int64_t)p->max_discharge_ma * c->rsense_uohm / 400000 > 128)
    {
        return M_ERANGE;
    }
    if (store && (!store->read || !store->program || !store->erase || store->sector_size < 64 ||
                  store->sector_size > 0x1000000u || store->sector_size % 64))
    {
        return M_EARG;
    }
    /* 只绑定软件状态，留出安装回调的机会，首次process才访问硬件。 */
    memset(a, 0, sizeof(*a));
    a->policy = *p;
    if (store)
    {
        a->store = *store;
    }
    r = drv_max17263_bind(&a->gauge, b, c);
    a->last_error = r;
    if (r)
    {
        return r;
    }
    a->started = true;
    a->last_attempt = now(a) - 5000u;
    a->last_save = now(a);
    if (!c->model)
    {
        a->active_faults |= BAT_WARN_UNCHARACTERIZED;
    }
    a->active_faults |= BAT_FAULT_STALE;
    /* Initialization is performed on the next process call; callbacks can be installed now. */
    return M_OK;
}

/*********************************************************************
 * @fn      limit
 * @brief   根据触发和恢复条件更新一个带滞回的故障位。
 * @param   a  应用实例
 * @param   bit  待更新的故障掩码
 * @param   trip  true置位
 * @param   clear  true允许恢复清零
 * @return  none
 * @note    触发优先；两个条件均不满足时保持原状态。
********************************************************************/
static void limit(battery_app_t *a, uint32_t bit, bool trip, bool clear)
{
    if (trip)
    {
        a->active_faults |= bit;
    }
    else if (clear)
    {
        a->active_faults &= ~bit;
    }
}

/*********************************************************************
 * @fn      battery_app_checkpoint
 * @brief   读取学习参数并同步追加一条持久化记录。
 * @param   a  应用实例
 * @return  M_OK成功；M_ENOTREADY状态不允许；其他值为采集或存储错误。
 * @note    调用方保证任务串行和供电稳定；函数自身不检查最小保存间隔。
********************************************************************/
int battery_app_checkpoint(battery_app_t *a)
{
    max17263_learned_t p;
    int r;
    if (!a || !a->ready || !a->sample.valid || !a->store.read)
    {
        return M_ENOTREADY;
    }
    /* 读取原始学习参数，不保存显示SOC；CRC日志保证只接收完整提交。 */
    r = drv_max17263_get_learned(&a->gauge, &p);
    if (!r)
    {
        r = battery_store_save(&a->store, &a->gauge, &p);
    }
    if (!r)
    {
        a->have_saved = true;
        a->last_saved_cycles = p.cycles;
        a->active_faults &= ~BAT_FAULT_NVM;
    }
    else
    {
        a->active_faults |= BAT_FAULT_NVM;
    }
    a->last_save = now(a); /* Rate-limit failures as well. */
    return r;
}

/*********************************************************************
 * @fn      battery_app_process
 * @brief   执行初始化重试、周期采样、故障检测和学习参数保存。
 * @param   a  已经start的应用实例
 * @return  none
 * @note    仅单任务调用；初始化及存储可能阻塞，不得在ISR执行。
********************************************************************/
void battery_app_process(battery_app_t *a)
{
    uint32_t t;
    int r;
    max17263_sample_t *s;
    if (!a || !a->started)
    {
        return;
    }
    t = now(a);
    /* A manual LED command may observe POR before the periodic sample does. */
    if (a->ready && !a->gauge.initialized)
    {
        a->ready = false;
        a->sample.valid = false;
        a->active_faults |= BAT_FAULT_RESET;
    }
    /* 尚无成功采样或已过有效期时立即失效，不能把旧数值继续当成在线数据。 */
    if (!a->have_good || (uint32_t)(t - a->last_good) > a->policy.stale_ms)
    {
        a->active_faults |= BAT_FAULT_STALE;
        a->sample.valid = false;
    }
    /* 正常状态按period_ms采样，初始化失败按5秒退避；差值写法兼容时钟回绕。 */
    if ((uint32_t)(t - a->last_attempt) < (a->ready ? a->policy.period_ms : 5000u))
    {
        return;
    }
    a->last_attempt = t;
    if (!a->ready)
    {
        r = setup(a);
        if (r)
        {
            goto error;
        }
        a->ready = true;
    }
    r = drv_max17263_sample(&a->gauge, &a->sample);
    if (r)
    {
        goto error;
    }
    a->last_good = now(a);
    a->have_good = true;
    a->consecutive_errors = 0;
    a->last_error = M_OK;
    a->active_faults &=
        ~(BAT_FAULT_IO | BAT_FAULT_SENSOR | BAT_FAULT_RESET | BAT_FAULT_STALE | BAT_FAULT_PROFILE);
    /* 芯片告警可能是两次采样之间的短时事件，先锁存历史再清硬件告警。 */
    s = &a->sample;
    a->observed_alerts |= (uint16_t)(s->status & M_ALERT_FLAGS);
    if (s->status & 0x0100u)
    {
        a->latched_faults |= BAT_FAULT_UV;
    }
    if (s->status & 0x1000u)
    {
        a->latched_faults |= BAT_FAULT_OV;
    }
    if (s->status & 0x0004u)
    {
        a->latched_faults |= BAT_FAULT_OC_DIS;
    }
    if (s->status & 0x0040u)
    {
        a->latched_faults |= BAT_FAULT_OC_CHG;
    }
    /* 恢复滞回：电压80mV、温度2℃、电流回到阈值90%、SOC回升2个百分点。 */
    limit(a, BAT_FAULT_UV, s->cell_mv<a->policy.min_cell_mv, s->cell_mv> a->policy.min_cell_mv + 80);
    limit(a, BAT_FAULT_OV, s->cell_mv > a->policy.max_cell_mv, s->cell_mv < a->policy.max_cell_mv - 80);
    limit(a, BAT_FAULT_COLD, s->temp_c<a->policy.min_temp_c, s->temp_c> a->policy.min_temp_c + 2);
    limit(a, BAT_FAULT_HOT, s->temp_c > a->policy.max_temp_c, s->temp_c < a->policy.max_temp_c - 2);
    limit(a, BAT_FAULT_OC_DIS,
          s->current_ma<-a->policy.max_discharge_ma, s->current_ma> - 0.9f * a->policy.max_discharge_ma);
    limit(a, BAT_FAULT_OC_CHG, s->current_ma > a->policy.max_charge_ma,
          s->current_ma < 0.9f * a->policy.max_charge_ma);
    limit(a, BAT_FAULT_LOW_SOC, s->soc_pct<a->policy.low_soc, s->soc_pct> a->policy.low_soc + 2);
    limit(a, BAT_WARN_CYCLE_RANGE, s->cycles_raw >= 65000, s->cycles_raw < 65000);
    /* Keep observed_alerts for short threshold events even if the live value recovered. */
    r = drv_max17263_ack_alerts(&a->gauge, (uint16_t)(s->status & M_ALERT_FLAGS));
    if (r)
    {
        goto error;
    }
    /* 首次建立记录或跨过64个原始循环计数区间后保存，最小间隔60秒以限制磨损。 */
    if (a->store.read && (uint32_t)(now(a) - a->last_save) >= 60000u &&
        (!a->have_saved || s->cycles_raw / 64u != a->last_saved_cycles / 64u))
    {
        r = battery_app_checkpoint(a);
        if (r)
        {
            a->last_error = r;
        }
    }
    event(a);
    return;
error:
    /* 任何必要步骤失败都不发布为有效数据；恢复总线后仍要通过下一轮真实读取。 */
    a->last_error = r;
    a->sample.valid = false;
    if (a->consecutive_errors < 1000000u)
    {
        a->consecutive_errors++;
    }
    if (r == M_EPOR)
    {
        a->ready = false;
        a->active_faults |= BAT_FAULT_RESET;
    }
    else if (r == M_EPROFILE || r == M_EARG)
    {
        a->ready = false;
        a->active_faults |= BAT_FAULT_PROFILE;
    }
    else if (r == M_ERANGE)
    {
        a->active_faults |= BAT_FAULT_SENSOR;
    }
    else
    {
        a->active_faults |= BAT_FAULT_IO;
    }
    if (r == M_EIO && a->consecutive_errors % 3u == 0 && a->recover_bus)
    {
        (void)a->recover_bus(a->user);
    }
    event(a);
}

/*********************************************************************
 * @fn      battery_app_clear_history
 * @brief   清理历史故障，保留仍然有效的当前故障。
 * @param   a  应用实例，NULL时不操作
 * @return  none
 * @note    不会清除芯片故障条件，也不会解除当前应用故障。
********************************************************************/
void battery_app_clear_history(battery_app_t *a)
{
    if (a)
    {
        a->latched_faults = a->active_faults;
        a->observed_alerts = 0;
    }
}

/*********************************************************************
 * @fn      battery_app_led_mask
 * @brief   在电池任务中切换十二灯自定义图案。
 * @param   a  应用实例
 * @param   mask  低12位灯掩码，1亮0灭
 * @return  M_OK成功；未就绪或驱动错误返回负值。
 * @note    此核心接口非线程安全；跨任务请调用mod_battery_led_mask。
********************************************************************/
int battery_app_led_mask(battery_app_t *a, uint16_t mask)
{
    if (!a || !a->ready)
    {
        return M_ENOTREADY;
    }
    return drv_max17263_led_mask(&a->gauge, mask);
}

/*********************************************************************
 * @fn      battery_app_led_soc
 * @brief   恢复策略中规定的自动SOC灯条显示。
 * @param   a  应用实例
 * @return  M_OK成功；未就绪或驱动错误返回负值。
 * @note    跨任务请调用mod_battery_led_soc，避免并发操作I2C。
********************************************************************/
int battery_app_led_soc(battery_app_t *a)
{
    if (!a || !a->ready || !a->gauge.initialized)
    {
        return M_ENOTREADY;
    }
    return drv_max17263_led(&a->gauge, a->policy.led_bars, a->policy.led_mode, a->policy.led_brightness,
                            a->policy.led_mv, a->policy.led_while_charging);
}

/* ---------- 可选双扇区学习参数日志：CRC、配置/电池匹配、最后提交 ---------- */
#define MAGIC 0x3632374Du
#define COMMIT 0xA55AC33Cu
/*********************************************************************
 * @fn      get32
 * @brief   从记录中解码小端32位整数。
 * @param   b  至少包含4个字节的缓冲区
 * @return  解码后的无符号整数。
 * @note    按字节读取，不依赖地址对齐。
********************************************************************/
static uint32_t get32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

/*********************************************************************
 * @fn      put32
 * @brief   将32位整数按小端格式写入记录缓冲区。
 * @param   b  至少4字节的目标缓冲区
 * @param   v  待写入值
 * @return  none
 * @note    只修改内存，不触发Flash操作。
********************************************************************/
static void put32(uint8_t *b, uint32_t v)
{
    unsigned i;
    for (i = 0; i < 4; i++)
    {
        b[i] = (uint8_t)(v >> (8 * i));
    }
}

/*********************************************************************
 * @fn      crc32
 * @brief   计算日志完整性校验CRC32。
 * @param   b  输入字节数组
 * @param   n  字节数
 * @return  CRC32校验值。
 * @note    反射多项式0xEDB88320，初值及末尾异或均为0xFFFFFFFF。
********************************************************************/
static uint32_t crc32(const uint8_t *b, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    unsigned k;
    while (n--)
    {
        c ^= *b++;
        for (k = 0; k < 8; k++)
        {
            c = (c >> 1) ^ ((c & 1) ? 0xEDB88320u : 0);
        }
    }
    return ~c;
}

/*********************************************************************
 * @fn      hash_word
 * @brief   将一个32位值并入配置摘要。
 * @param   h  上一轮摘要
 * @param   v  待合入的配置值
 * @return  更新后的摘要。
 * @note    逐字节FNV风格散列，只用于配置匹配，不用于安全认证。
********************************************************************/
static uint32_t hash_word(uint32_t h, uint32_t v)
{
    unsigned i;
    for (i = 0; i < 4; i++)
    {
        h ^= (uint8_t)(v >> (8 * i));
        h *= 16777619u;
    }
    return h;
}

/*********************************************************************
 * @fn      battery_profile_hash
 * @brief   计算电池配置和模型的匹配摘要。
 * @param   c  有效配置指针
 * @return  配置摘要。
 * @note    电池ID在记录中单独匹配；不将指针地址当成配置内容。
********************************************************************/
uint32_t battery_profile_hash(const max17263_config_t *c)
{
    uint32_t h = 2166136261u;
    size_t i;
#define H(x) h = hash_word(h, (uint32_t)(x))
    H(c->profile_id);
    H(c->rsense_uohm);
    H(c->design_mah);
    H(c->ichgterm_ma);
    H(c->empty_mv);
    H(c->recovery_mv);
    H(c->series_cells);
    H(c->external_ntc);
    H(c->ntc_100k);
    H(c->model != NULL);
    if (c->model)
    {
        for (i = 0; i < 32; i++)
        {
            H(c->model->table[i]);
        }
        H(c->model->rcompseg);
        H(c->model->rcomp0);
        H(c->model->tempco);
        for (i = 0; i < 4; i++)
        {
            H(c->model->qr[i]);
        }
        H(c->model->optional_count);
        for (i = 0; i < c->model->optional_count; i++)
        {
            H(c->model->optional[i].reg);
            H(c->model->optional[i].value);
        }
    }
#undef H
    return h;
}
/*********************************************************************
 * @fn      backend_valid
 * @brief   检查双扇区NOR存储后端是否完整有效。
 * @param   s  存储接口
 * @return  true可使用；false配置无效。
 * @note    两个擦除单元大小相同，且每个单元能容纳整数条64字节记录。
********************************************************************/
static bool backend_valid(const battery_store_t *s)
{
    return s && s->read && s->erase && s->program && s->sector_size >= 64 && s->sector_size <= 0x1000000u &&
           s->sector_size % 64 == 0;
}

/*********************************************************************
 * @fn      valid
 * @brief   验证学习记录格式、提交标记和CRC。
 * @param   r  完整64字节记录
 * @return  true记录完整；false无效。
 * @note    本函数不检查电池ID及配置摘要，由扫描器另行匹配。
********************************************************************/
static bool valid(const uint8_t *r)
{
    return get32(r) == MAGIC && get32(r + 4) == 1 && get32(r + 60) == COMMIT && get32(r + 56) == crc32(r, 56);
}

/*********************************************************************
 * @fn      scan
 * @brief   扫描两个扇区，选择最新的完整学习记录。
 * @param   s  存储后端
 * @param   hash  期望配置摘要
 * @param   id  期望电池ID
 * @param   match  true筛选配置和电池，false扫描全部完整记录
 * @param   out  扫描结果，found表示是否找到
 * @return  M_OK扫描完成，包括空存储；M_ENVM读取失败。
 * @note    序号使用半范围比较处理回绕；损坏或未提交记录被跳过。
********************************************************************/
static int scan(const battery_store_t *s, uint32_t hash, uint32_t id, bool match, scan_t *out)
{
    uint32_t off;
    uint8_t r[64];
    memset(out, 0, sizeof(*out));
    /* 全扫描允许跳过半写入记录；不能在第一条空记录处提前结束。 */
    for (off = 0; off < 2 * s->sector_size; off += 64)
    {
        if (s->read(s->ctx, off, r, 64))
        {
            return M_ENVM;
        }
        if (!valid(r) || (match && (get32(r + 12) != hash || get32(r + 16) != id)))
        {
            continue;
        }
        if (!out->found || ((get32(r + 8) - out->seq) != 0 && (get32(r + 8) - out->seq) < 0x80000000u))
        {
            out->found = true;
            out->offset = off;
            out->seq = get32(r + 8);
            memcpy(out->record, r, 64);
        }
    }
    return M_OK;
}

/*********************************************************************
 * @fn      battery_store_load
 * @brief   读取与当前电池和配置匹配的最新学习记录。
 * @param   s  存储后端
 * @param   d  驱动实例，用于取得配置
 * @param   p  输出学习参数
 * @return  M_OK成功；M_ENOTREADY无匹配记录；M_ENVM存储失败；M_EPROFILE容量不合理。
 * @note    返回成功前还会检查学习容量范围；失败时不得使用输出值。
********************************************************************/
int battery_store_load(const battery_store_t *s, const max17263_t *d, max17263_learned_t *p)
{
    scan_t x;
    int rc;
    if (!backend_valid(s) || !d || !p)
    {
        return M_EARG;
    }
    rc = scan(s, battery_profile_hash(&d->cfg), d->cfg.battery_id, true, &x);
    if (rc)
    {
        return rc;
    }
    if (!x.found)
    {
        return M_ENOTREADY;
    }
    p->rcomp0 = (uint16_t)get32(x.record + 20);
    p->tempco = (uint16_t)get32(x.record + 24);
    p->fullcaprep = (uint16_t)get32(x.record + 28);
    p->fullcapnom = (uint16_t)get32(x.record + 32);
    p->cycles = (uint16_t)get32(x.record + 36);
    return drv_max17263_learned_valid(d, p) ? M_OK : M_EPROFILE;
}

/*********************************************************************
 * @fn      erased
 * @brief   检查一个64字节记录位置是否全部为擦除态。
 * @param   b  待检查的64字节缓冲区
 * @return  true全为0xFF；false有内容。
 * @note    并不执行扇区擦除。
********************************************************************/
static bool erased(const uint8_t *b)
{
    unsigned i;
    for (i = 0; i < 64; i++)
    {
        if (b[i] != 0xFF)
        {
            return false;
        }
    }
    return true;
}

/*********************************************************************
 * @fn      battery_store_save
 * @brief   追加学习记录，必要时进行双扇区轮换。
 * @param   s  存储后端
 * @param   d  驱动实例，用于配置和容量校验
 * @param   p  待保存的五项原始学习参数
 * @return  M_OK写入并回读成功；M_EARG参数错误；M_ENVM存储失败。
 * @note    先写正文和CRC并回读，再最后提交；轮换时保留最新有效记录所在扇区。
********************************************************************/
int battery_store_save(const battery_store_t *s, const max17263_t *d, const max17263_learned_t *p)
{
    scan_t x;
    int rc;
    uint32_t base;
    uint32_t off;
    uint8_t r[64];
    uint8_t check[64];
    if (!backend_valid(s) || !d || !drv_max17263_learned_valid(d, p))
    {
        return M_EARG;
    }
    rc = scan(s, 0, 0, false, &x);
    if (rc)
    {
        return rc;
    }
    base = x.found ? (x.offset / s->sector_size) * s->sector_size : 0;
    for (off = base; off < base + s->sector_size; off += 64)
    {
        if (s->read(s->ctx, off, check, 64))
        {
            return M_ENVM;
        }
        if (erased(check))
        {
            break;
        }
    }
    /* 当前扇区满时切换另一扇区，最新完整记录在擦除期间仍然可恢复。 */
    if (off == base + s->sector_size)
    {
        base = base ? 0 : s->sector_size;
        off = base;
        /* The newest committed record remains intact in the other sector. */
        if (s->erase(s->ctx, (unsigned)(base / s->sector_size)))
        {
            return M_ENVM;
        }
    }
    /* 固定64字节格式：magic/version/seq/hash/id，五项学习值，CRC@56，commit@60。 */
    memset(r, 0, 64);
    put32(r, MAGIC);
    put32(r + 4, 1);
    put32(r + 8, x.found ? x.seq + 1 : 1);
    put32(r + 12, battery_profile_hash(&d->cfg));
    put32(r + 16, d->cfg.battery_id);
    put32(r + 20, p->rcomp0);
    put32(r + 24, p->tempco);
    put32(r + 28, p->fullcaprep);
    put32(r + 32, p->fullcapnom);
    put32(r + 36, p->cycles);
    put32(r + 56, crc32(r, 56));
    put32(r + 60, COMMIT);
    /* 先写正文并校验，再写最后4字节提交标记，避免半条记录被误认为有效。 */
    if (s->program(s->ctx, off, r, 60))
    {
        return M_ENVM;
    }
    if (s->read(s->ctx, off, check, 60) || memcmp(r, check, 60))
    {
        return M_ENVM;
    }
    if (s->program(s->ctx, off + 60, r + 60, 4))
    {
        return M_ENVM; /* commit LAST */
    }
    if (s->read(s->ctx, off, check, 64) || memcmp(r, check, 64) || !valid(check))
    {
        return M_ENVM;
    }
    return M_OK;
}

#ifndef BATTERY_CORE_TEST
/* STM32/RTOS integration. Single owner: SensorTask. */
#include "mod_battery.h"
#include "battery_config.h"
#include "mod_storage.h"
#include "bsp_i2c_hw.h"
#include "FreeRTOS.h"
#include "task.h"
#include "comm_logger.h"
static battery_app_t gauge_app;
static BATT_INFO_T published;
static bool module_started;
static uint32_t save_request;
static uint32_t save_done;
static int save_result;
static bool led_pending;
static int32_t led_request;
/*********************************************************************
 * @fn      bus_read
 * @brief   将核心7位地址转换为HAL约定地址并读取寄存器。
 * @param   c  未使用的上下文
 * @param   a  未左移的7位地址
 * @param   r  寄存器地址
 * @param   b  接收缓冲区
 * @param   n  字节数，最大255
 * @return  0成功；-1参数或通信错误。
 * @note    仅SensorTask调用；地址在此左移一次。
********************************************************************/
static int bus_read(void *c, uint8_t a, uint8_t r, uint8_t *b, size_t n)
{
    (void)c;
    if (n > 255)
    {
        return -1;
    }
    return bsp_i2c_hw_readData((uint8_t)(a << 1), r, b, (uint8_t)n) == BSP_I2C_OK ? 0 : -1;
}

/*********************************************************************
 * @fn      bus_write
 * @brief   将核心写请求适配到项目硬件I2C接口。
 * @param   c  未使用的上下文
 * @param   a  未左移的7位地址
 * @param   r  寄存器地址
 * @param   b  发送缓冲区
 * @param   n  字节数，最大255
 * @return  0成功；-1参数或通信错误。
 * @note    底层接口不修改发送数据；类型转换用于兼容现有BSP声明。
********************************************************************/
static int bus_write(void *c, uint8_t a, uint8_t r, const uint8_t *b, size_t n)
{
    (void)c;
    if (n > 255)
    {
        return -1;
    }
    return bsp_i2c_hw_writeData((uint8_t)(a << 1), r, (uint8_t *)b, (uint8_t)n) == BSP_I2C_OK ? 0 : -1;
}

/*********************************************************************
 * @fn      bus_delay
 * @brief   以RTOS延时实现驱动等待。
 * @param   c  未使用的上下文
 * @param   ms  等待毫秒数
 * @return  none
 * @note    至少让出一个tick，防止短延时被换算为零。
********************************************************************/
static void bus_delay(void *c, uint32_t ms)
{
    (void)c;
    vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1);
}

/*********************************************************************
 * @fn      bus_time
 * @brief   读取HAL毫秒计数。
 * @param   c  未使用的上下文
 * @return  毫秒计数。
 * @note    与应用有效期判断使用同一时基。
********************************************************************/
static uint32_t bus_time(void *c)
{
    (void)c;
    return HAL_GetTick();
}

/*********************************************************************
 * @fn      recover_i2c
 * @brief   请求重新初始化I2C控制器及引脚。
 * @param   c  未使用的上下文
 * @return  当前适配返回0，实际恢复情况由后续通信确认。
 * @note    此处没有SCL脉冲解锁；不能据此认定外部总线已恢复。
********************************************************************/
static int recover_i2c(void *c)
{
    (void)c;
    bsp_i2c_hw_init();
    return 0;
}

/*********************************************************************
 * @fn      rounded
 * @brief   将范围内的浮点值按符号四舍五入为整数。
 * @param   f  待转换数值
 * @return  四舍五入后的int32值。
 * @note    调用方保证数值有限且在int32范围内。
********************************************************************/
static int32_t rounded(float f)
{
    return (int32_t)(f + (f >= 0 ? 0.5f : -0.5f));
}

/*********************************************************************
 * @fn      publish
 * @brief   将完整采样结果换算为应用快照并发布。
 * @param   c  未使用的上下文
 * @param   a  电池应用实例
 * @return  none
 * @note    临界区仅覆盖结构体复制；日志及诊断读取均在临界区外。
********************************************************************/
static uint32_t fold_alarm(uint32_t faults)
{
    uint32_t out = 0;
    if (faults & BAT_FAULT_OV)                                out |= SYS_ALARM_BAT_OVP;
    if (faults & BAT_FAULT_UV)                                out |= SYS_ALARM_BAT_UVP;
    if (faults & BAT_FAULT_HOT)                               out |= SYS_ALARM_BAT_OTP;
    if (faults & (BAT_FAULT_OC_DIS | BAT_FAULT_OC_CHG))       out |= SYS_ALARM_BAT_OCP;
    if (faults & BAT_FAULT_LOW_SOC)                           out |= SYS_ALARM_LOW_SOC;
    if (faults & (BAT_FAULT_NVM | BAT_FAULT_PROFILE | BAT_FAULT_RESET | BAT_FAULT_STALE)) out |= SYS_ALARM_STORAGE;
    if (faults & (BAT_FAULT_IO | BAT_FAULT_SENSOR))           out |= SYS_ALARM_BAT_INVALID;
    return out;
}
static void publish(void *c, const battery_app_t *a)
{
    BATT_INFO_T x = {0};
    (void)c;
    x.valid = a->sample.valid;
    x.online = x.valid;
    x.timestamp_ms = a->sample.timestamp_ms;
    x.active_faults = a->active_faults;
    x.alarm_bits = fold_alarm(a->active_faults);
    x.latched_faults = a->latched_faults;
    x.last_error = a->last_error;
    x.alert_flags = a->observed_alerts;
    /* 只有完整有效的测量才参与换算；无效快照保留故障信息，数值不作正常显示。 */
    if (x.valid)
    {
        const max17263_sample_t *s = &a->sample;
        x.voltage_mv = (uint32_t)rounded(s->pack_mv);
        x.cell_voltage_mv = (uint32_t)rounded(s->cell_mv);
        x.current_ma = rounded(s->current_ma);
        x.avg_current_ma = rounded(s->avg_current_ma);
        x.temp_deg = (int16_t)rounded(s->temp_c * 10);
        x.soc = (uint8_t)(s->soc_pct >= 100 ? 100 : rounded(s->soc_pct));
        x.soh = (uint8_t)(s->soh_capacity_pct >= 100 ? 100 : rounded(s->soh_capacity_pct));
        x.soh_valid = true;
        x.repcap_mah = (uint32_t)rounded(s->remaining_mah);
        x.fullcap_mah = (uint32_t)rounded(s->full_mah);
        x.cycles_raw = s->cycles_raw;
        x.tte_valid = s->tte_valid;
        x.ttf_valid = s->ttf_valid;
        x.time_to_empty_min = s->tte_valid ? (uint16_t)rounded(s->tte_s / 60) : 0;
        x.time_to_full_min = s->ttf_valid ? (uint16_t)rounded(s->ttf_s / 60) : 0;
    }
    /* 复制期间短暂禁止抢占，读者不会拿到一半新、一半旧的字段。 */
    taskENTER_CRITICAL();
    published = x;
    taskEXIT_CRITICAL();
#if BATTERY_CSV_ENABLE
    logger_out(LOG_INFO, "BATCSV,%lu,%u,%lu,%ld,%ld,%d,%u,%lu,%lu,%u,%lu,%d", (unsigned long)x.timestamp_ms,
                 x.valid, (unsigned long)x.voltage_mv, (long)x.current_ma, (long)x.avg_current_ma, x.temp_deg,
                 x.soc, (unsigned long)x.repcap_mah, (unsigned long)x.fullcap_mah, x.cycles_raw,
                 (unsigned long)x.active_faults, x.last_error);
    if (x.valid)
    {
        uint16_t fs = 0;
        uint16_t mix = 0;
        uint16_t vf = 0;
        uint16_t rc = 0;
        uint16_t tc = 0;
        int dr = drv_max17263_read(&gauge_app.gauge, M_FSTAT, &fs);
        if (!dr)
        {
            dr = drv_max17263_read(&gauge_app.gauge, 0x0D, &mix);
        }
        if (!dr)
        {
            dr = drv_max17263_read(&gauge_app.gauge, M_VFSOC, &vf);
        }
        if (!dr)
        {
            dr = drv_max17263_read(&gauge_app.gauge, M_RCOMP0, &rc);
        }
        if (!dr)
        {
            dr = drv_max17263_read(&gauge_app.gauge, M_TEMPCO, &tc);
        }
        logger_out(LOG_INFO, "BATDIAG,%lu,%d,%u,%u,%u,%u,%u,%u,%lu", (unsigned long)x.timestamp_ms, dr,
                     a->sample.status, fs, mix, vf, rc, tc,
                     (unsigned long)rounded(a->sample.nominal_full_mah));
    }
#endif
}

/*********************************************************************
 * @fn      mod_battery_init
 * @brief   建立STM32电池模块和存储后端绑定。
 * @return  true绑定成功；false配置失败。
 * @note    仅SensorTask调用；成功不代表芯片已初始化，后续handler完成硬件配置。
********************************************************************/
bool mod_battery_init(void)
{
    if (module_started)
    {
        return true;
    }
    bsp_i2c_hw_init();
    max17263_bus_t bus = {NULL, bus_read, bus_write, bus_delay, bus_time};
    battery_store_t store = {NULL, 4096, mod_storage_battery_read, mod_storage_battery_erase,
                             mod_storage_battery_program};
    int r = battery_app_start(&gauge_app, &bus, &battery_cfg, &battery_policy, &store);
    if (r)
    {
        return false;
    }
    gauge_app.on_update = publish;
    gauge_app.recover_bus = recover_i2c;
    module_started = true;
    logger_out(LOG_WARN, "BAT: 8S 1mOhm internal DIE temp; 20Ah/1A parameters require pack confirmation");
    return true;
}
/* Read-only fault diagnostics: SensorTask owns bus; never reset/reconfigure to hide a fault.
 * Copy the driver object so diagnostic failures do not overwrite original failure context. */
/*********************************************************************
 * @fn      battery_diagnose
 * @brief   限频打印无效数据原因及关键寄存器。
 * @return  none
 * @note    仅只读诊断，不通过重置或改参数掩盖错误；使用驱动副本保留原始错误上下文。
********************************************************************/
static void battery_diagnose(void)
{
    static bool reported;
    static bool was_valid;
    static uint32_t last_log;
    static int last_error;
    uint32_t t = HAL_GetTick();
    if (gauge_app.sample.valid)
    {
        if (!was_valid)
        {
            logger_out(LOG_INFO, "BAT: gauge data valid, initialized=%u", gauge_app.ready);
        }
        was_valid = true;
        return;
    }
    /* 相同错误最多每10秒输出一次；首次错误或状态变化立即输出。 */
    if (reported && !was_valid && last_error == gauge_app.last_error && (uint32_t)(t - last_log) < 10000u)
    {
        return;
    }
    reported = true;
    was_valid = false;
    last_log = t;
    last_error = gauge_app.last_error;
    logger_out(LOG_WARN, "BAT FAIL: err=%d(%s) init=%u io_fail_reg=0x%02X faults=0x%08lX count=%u",
                 gauge_app.last_error, drv_max17263_error_string(gauge_app.last_error), gauge_app.ready,
                 gauge_app.gauge.failed_reg, (unsigned long)gauge_app.active_faults,
                 gauge_app.consecutive_errors);
    /* 诊断读失败只能影响副本，不能覆盖导致本轮采样失败的原始寄存器地址。 */
    max17263_t probe = gauge_app.gauge;
    const uint8_t regs[] = {M_DEVNAME,  M_STATUS,     M_FSTAT,      M_MODELCFG, M_CONFIG,  M_DESIGNCAP,
                            M_ICHGTERM, M_VEMPTY,     M_HIBCFG,     M_VCELL,    M_CURRENT, M_TEMP,
                            M_REPSOC,   M_FULLCAPREP, M_FULLCAPNOM, M_LEDCFG1,  M_LEDCFG2, M_LEDCFG3};
    uint16_t v[sizeof(regs)] = {0};
    for (unsigned i = 0; i < sizeof(regs); i++)
    {
        int r = drv_max17263_read(&probe, regs[i], &v[i]);
        if (r)
        {
            logger_out(LOG_WARN,
                         "BAT REG: read reg=0x%02X failed=%d; check I2C1 PB6/PB7, 0x36 and gauge supply",
                         regs[i], r);
            return;
        }
    }
    logger_out(LOG_WARN, "BAT REG: Dev=%04X Status=%04X FStat=%04X Model=%04X Config=%04X", v[0], v[1],
                 v[2], v[3], v[4]);
    logger_out(LOG_WARN,
                 "BAT CFG: Design=%04X/%04X Term=%04X/%04X Empty=%04X/%04X Hib=%04X (actual/expected)", v[5],
                 probe.design_raw, v[6], probe.term_raw, v[7], probe.empty_raw, v[8]);
    logger_out(LOG_WARN, "BAT RAW: VCell=%04X Current=%04X Temp=%04X SOC=%04X FullRep=%04X FullNom=%04X",
                 v[9], v[10], v[11], v[12], v[13], v[14]);
    logger_out(LOG_WARN, "BAT LED: Cfg1=%04X Cfg2=%04X Cfg3=%04X", v[15], v[16], v[17]);
    if (gauge_app.last_error == M_EPROFILE && !(v[1] & M_POR))
    {
        logger_out(LOG_WARN, "BAT: warm-start profile mismatch; fully power-cycle MAX17263 after firmware "
                               "update (MCU reset is insufficient)");
    }
}

/*********************************************************************
 * @fn      mod_battery_handler
 * @brief   由SensorTask执行电池状态机和跨任务请求。
 * @return  none
 * @note    I2C、学习存储及LED操作均集中在此任务；建议约20ms调用。
********************************************************************/
void mod_battery_handler(void)
{
    uint32_t req;
    bool led;
    int32_t mask;
    if (!module_started && !mod_battery_init())
    {
        return;
    }
    battery_app_process(&gauge_app);
    battery_diagnose();
    /* 只在临界区取出邮箱内容，所有慢速I2C/Flash操作都在临界区外执行。 */
    taskENTER_CRITICAL();
    req = save_request;
    led = led_pending;
    mask = led_request;
    led_pending = false;
    taskEXIT_CRITICAL();
    if (led)
    {
        int r = mask < 0 ? battery_app_led_soc(&gauge_app) : battery_app_led_mask(&gauge_app, (uint16_t)mask);
        gauge_app.last_error = r;
        publish(NULL, &gauge_app);
    }
    /* 请求序号用于配对完成结果；60秒内已有记录时合并重复关机保存请求。 */
    if (req != save_done)
    {
        int r = (uint32_t)(HAL_GetTick() - gauge_app.last_save) < 60000u && gauge_app.have_saved
                    ? M_OK
                    : battery_app_checkpoint(&gauge_app);
        taskENTER_CRITICAL();
        save_result = r;
        save_done = req;
        taskEXIT_CRITICAL();
        publish(NULL, &gauge_app);
    }
}

/*********************************************************************
 * @fn      battery_get_snapshot
 * @brief   以短临界区复制电池快照，并判断有效期。
 * @param   out  输出快照；不能为NULL
 * @return  true数据有效且未过期；false无效、过期或指针为空。
 * @note    非NULL时即使返回false也会给出状态；调用方不得把其中旧值作为有效测量。
********************************************************************/
bool battery_get_snapshot(BATT_INFO_T *out)
{
    if (!out)
    {
        return false;
    }
    /* 返回独立副本，避免其他任务持有会被采样任务继续修改的内部结构指针。 */
    taskENTER_CRITICAL();
    *out = published;
    taskEXIT_CRITICAL();
    if (!out->valid || (uint32_t)(HAL_GetTick() - out->timestamp_ms) > BATTERY_STALE_MS)
    {
        out->valid = false;
        out->online = 0;
        out->active_faults |= BAT_FAULT_STALE;
        return false;
    }
    return true;
}

/*********************************************************************
 * @fn      mod_battery_led_mask
 * @brief   向SensorTask提交十二灯自定义显示请求。
 * @param   m  低12位灯掩码，高4位必须为零
 * @return  true请求已接受；false掩码非法。
 * @note    线程安全单槽邮箱，新请求覆盖旧请求；返回true不代表已经完成硬件写入。
********************************************************************/
bool mod_battery_led_mask(uint16_t m)
{
    if (m & 0xF000u)
    {
        return false;
    }
    taskENTER_CRITICAL();
    led_request = m;
    led_pending = true;
    taskEXIT_CRITICAL();
    return true;
}

/*********************************************************************
 * @fn      mod_battery_led_soc
 * @brief   向SensorTask提交恢复自动SOC显示请求。
 * @return  none
 * @note    只提交邮箱，不直接访问芯片；后续handler串行执行。
********************************************************************/
void mod_battery_led_soc(void)
{
    taskENTER_CRITICAL();
    led_request = -1;
    led_pending = true;
    taskEXIT_CRITICAL();
}

/*********************************************************************
 * @fn      mod_battery_checkpoint
 * @brief   请求SensorTask保存学习参数，并有限等待完成。
 * @param   timeout_ms  最长等待时间，毫秒
 * @return  true保存成功或近期保存被合并；false忙、超时或保存失败。
 * @note    从其他任务调用，禁止在SensorTask或ISR调用；超时不取消已提交请求。
********************************************************************/
bool mod_battery_checkpoint(uint32_t timeout_ms)
{
    uint32_t id, start = HAL_GetTick();
    taskENTER_CRITICAL();
    if (save_request != save_done)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    /* 先发布请求再让出CPU，由SensorTask完成写入；调用任务只等待对应序号。 */
    id = ++save_request;
    taskEXIT_CRITICAL();
    do
    {
        bool done;
        int r;
        taskENTER_CRITICAL();
        done = save_done == id;
        r = save_result;
        taskEXIT_CRITICAL();
        if (done)
        {
            return r == M_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
    } while ((uint32_t)(HAL_GetTick() - start) < timeout_ms);
    return false;
}

#endif /* BATTERY_CORE_TEST */
