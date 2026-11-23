/**
 * @file    drv_max17263.c
 * @brief   MAX17263 电池计量驱动实现 (Driver 层)
 *
 * 适用硬件：STM32F407、8串磷酸铁锂、R114=1mΩ、12颗LED。
 * 当前温度源为芯片内部温度；具体参数由上层配置传入。
 * I2C使用7位地址0x36，16位寄存器低字节在前；HAL/RTOS适配位于mod_battery.c。
 * @note    所有接口由同一任务串行调用，不可在中断中执行初始化或模型加载。
 */
#include "drv_max17263.h"
#include <string.h>
/* 配置序列错误传播；参数表达式只求值一次。 */
#define MAX17263_TRY(expression)                                                                             \
    do                                                                                                       \
    {                                                                                                        \
        int try_result = (expression);                                                                       \
        if (try_result != M_OK)                                                                              \
        {                                                                                                    \
            return try_result;                                                                               \
        }                                                                                                    \
    } while (0)

/*********************************************************************
 * @fn      drv_max17263_record_error
 * @brief   记录寄存器访问错误，供上层诊断使用。
 * @param   dev  驱动实例，必须有效
 * @param   reg  发生错误的寄存器地址
 * @param   error  待记录的错误码
 * @return  原样返回error。
 * @note    该函数只更新诊断字段，不访问总线。
********************************************************************/
static int drv_max17263_record_error(max17263_t *dev, uint8_t reg, int error)
{
    dev->failed_reg = reg;
    dev->last_error = error;
    return error;
}

/*********************************************************************
 * @fn      drv_max17263_decode_signed16
 * @brief   将16位二进制补码转换为有符号32位值。
 * @param   raw  寄存器原始值
 * @return  -32768～32767范围内的有符号数。
 * @note    显式换算避免依赖无符号转有符号的实现行为。
********************************************************************/
static int32_t drv_max17263_decode_signed16(uint16_t raw)
{
    return raw < 32768u ? (int32_t)raw : (int32_t)raw - 65536;
}

/*********************************************************************
 * @fn      drv_max17263_read
 * @brief   读取一个16位寄存器，按低字节在前组装结果。
 * @param   dev  已绑定总线的驱动实例
 * @param   reg  8位寄存器地址
 * @param   value  输出原始值；失败时不更新
 * @return  M_OK成功；M_EARG参数无效；M_EIO通信失败。
 * @note    读失败最多尝试3次，每次失败后延时2ms。
********************************************************************/
int drv_max17263_read(max17263_t *dev, uint8_t reg, uint16_t *value)
{
    uint8_t data[2];
    unsigned attempt;
    if (!dev || !value || !dev->bus.read)
    {
        return M_EARG;
    }
    for (attempt = 0; attempt < 3; attempt++)
    {
        if (dev->bus.read(dev->bus.ctx, MAX17263_ADDR, reg, data, 2) == 0)
        {
            *value = (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
            return M_OK;
        }
        dev->bus.delay_ms(dev->bus.ctx, 2);
    }
    return drv_max17263_record_error(dev, reg, M_EIO);
}

/*********************************************************************
 * @fn      drv_max17263_write
 * @brief   写入一个16位寄存器，先发送低字节。
 * @param   dev  已绑定总线的驱动实例
 * @param   reg  8位寄存器地址
 * @param   value  待写入的16位原始值
 * @return  M_OK成功；M_EARG参数无效；M_EIO通信失败。
 * @note    写失败不盲目重试，避免已经生效的命令被重复执行。
********************************************************************/
int drv_max17263_write(max17263_t *dev, uint8_t reg, uint16_t value)
{
    uint8_t data[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    if (!dev || !dev->bus.write)
    {
        return M_EARG;
    }
    /* No blind retry: a write may have reached the device despite a bus error. */
    if (dev->bus.write(dev->bus.ctx, MAX17263_ADDR, reg, data, 2) != 0)
    {
        return drv_max17263_record_error(dev, reg, M_EIO);
    }
    return M_OK;
}

/*********************************************************************
 * @fn      drv_max17263_write_verify
 * @brief   写寄存器并回读验证。
 * @param   dev  驱动实例
 * @param   reg  寄存器地址
 * @param   expected  期望写入值
 * @return  M_OK成功；否则返回总线错误或M_EVERIFY。
 * @note    仅用于可稳定回读的寄存器；不用于自清零命令位。
********************************************************************/
static int drv_max17263_write_verify(max17263_t *dev, uint8_t reg, uint16_t expected)
{
    uint16_t actual;
    unsigned attempt;
    for (attempt = 0; attempt < 3; attempt++)
    {
        MAX17263_TRY(drv_max17263_write(dev, reg, expected));
        dev->bus.delay_ms(dev->bus.ctx, 1);
        MAX17263_TRY(drv_max17263_read(dev, reg, &actual));
        if (actual == expected)
        {
            return M_OK;
        }
    }
    return drv_max17263_record_error(dev, reg, M_EVERIFY);
}

/* 前向声明: 寄存器位等待清零实现在下方。 */
static int drv_max17263_wait_clear(max17263_t *dev, uint8_t reg, uint16_t mask);

/*********************************************************************
 * @fn      drv_max17263_quick_start
 * @brief   触发 Quick Start, 让芯片基于当前OCV重新估计初始SOC。
 * @param   dev  已绑定且已初始化的驱动实例
 * @return  M_OK成功；M_EARG未初始化；M_ETIMEOUT等待清零超时；其他为通信错误。
 * @note    RepSOC(06h)/RepCap(05h) 为只读结果寄存器, 写入会被忽略且回读失败;
 *          换电池/深放后需要重估SOC时使用本函数。MiscCfg bit10 自清零,
 *          完成一次 OCV 重学习期间 DNR 会置位, 本函数只等待命令位清零。
********************************************************************/
int drv_max17263_quick_start(max17263_t *dev)
{
    uint16_t misc;
    if (!dev || !dev->initialized)
    {
        return M_EARG;
    }
    MAX17263_TRY(drv_max17263_read(dev, M_MISCCFG, &misc));
    MAX17263_TRY(drv_max17263_write(dev, M_MISCCFG, (uint16_t)(misc | M_MISC_QUICK_START)));
    return drv_max17263_wait_clear(dev, M_MISCCFG, M_MISC_QUICK_START);
}

/*********************************************************************
 * @fn      drv_max17263_update_bits
 * @brief   读改写指定字段，保留掩码外的原始位。
 * @param   dev  驱动实例
 * @param   reg  寄存器地址
 * @param   mask  允许修改的位掩码
 * @param   value  待更新字段值，仅mask范围内有效
 * @return  M_OK成功；否则返回读写或回读校验错误。
 * @note    整个读改写序列必须由同一任务串行执行。
********************************************************************/
static int drv_max17263_update_bits(max17263_t *dev, uint8_t reg, uint16_t mask, uint16_t value)
{
    uint16_t current;
    MAX17263_TRY(drv_max17263_read(dev, reg, &current));
    return drv_max17263_write_verify(dev, reg, (uint16_t)((current & (uint16_t)~mask) | (value & mask)));
}

/*********************************************************************
 * @fn      drv_max17263_wait_clear
 * @brief   有限时间轮询，等待指定寄存器位全部清零。
 * @param   dev  驱动实例
 * @param   reg  寄存器地址
 * @param   mask  需要等待清零的位掩码
 * @return  M_OK就绪；M_ETIMEOUT超时；其他值为通信错误。
 * @note    每10ms轮询；同时使用累计延时和可回绕时钟约束等待时间。
********************************************************************/
static int drv_max17263_wait_clear(max17263_t *dev, uint8_t reg, uint16_t mask)
{
    uint16_t value;
    uint32_t elapsed_ms = 0, start_ms = dev->bus.now_ms(dev->bus.ctx);
    do
    {
        MAX17263_TRY(drv_max17263_read(dev, reg, &value));
        if (!(value & mask))
        {
            return M_OK;
        }
        dev->bus.delay_ms(dev->bus.ctx, 10);
        elapsed_ms += 10;
    } while (elapsed_ms < dev->cfg.poll_timeout_ms &&
             (uint32_t)(dev->bus.now_ms(dev->bus.ctx) - start_ms) < dev->cfg.poll_timeout_ms);
    return drv_max17263_record_error(dev, reg, M_ETIMEOUT);
}

/*********************************************************************
 * @fn      drv_max17263_optional_allowed
 * @brief   检查自定义模型附加配置寄存器是否在白名单内。
 * @param   reg  待检查的寄存器地址
 * @return  true允许；false拒绝。
 * @note    防止模型配置覆盖命令、LED等非模型寄存器。
********************************************************************/
static bool drv_max17263_optional_allowed(uint8_t reg)
{
    switch (reg)
    {
        case M_LEARNCFG:
        case M_RELAXCFG:
        case M_CONFIG:
        case M_CONFIG2:
        case M_FULLSOCTHR:
        case M_TGAIN:
        case M_TOFF:
        case M_CURVE:
        case M_MISCCFG:
        case M_SCOCVLIM:
            return true;
        default:
            return false;
    }
}

/*********************************************************************
 * @fn      drv_max17263_bind
 * @brief   绑定总线和电池配置，校验范围并计算原始配置值。
 * @param   dev  待初始化的驱动实例
 * @param   bus  总线回调，地址使用未左移的7位值
 * @param   config  电池配置，容量mAh、采样电阻µΩ、终止电流mA
 * @return  M_OK成功；M_EARG参数缺失；M_ERANGE超范围；M_EPROFILE模型配置不允许。
 * @note    不访问芯片。配置按值复制，但model及其optional数组必须在驱动生命周期内保持有效。
********************************************************************/
int drv_max17263_bind(max17263_t *dev, const max17263_bus_t *bus, const max17263_config_t *config)
{
    uint64_t capacity_raw;
    uint64_t term_raw;
    size_t index;
    if (!dev || !bus || !config || !bus->read || !bus->write || !bus->delay_ms || !bus->now_ms)
    {
        return M_EARG;
    }
    memset(dev, 0, sizeof(*dev));
    dev->bus = *bus;
    dev->cfg = *config;
    if (!config->rsense_uohm || !config->design_mah || !config->ichgterm_ma || !config->series_cells ||
        config->poll_timeout_ms < 100 || config->poll_timeout_ms > 10000 || !config->profile_id ||
        !config->battery_id)
    {
        return M_EARG;
    }
    /* 容量LSB为5µVh/Rsense；使用64位中间值防止配置乘法溢出。 */
    capacity_raw = ((uint64_t)config->design_mah * config->rsense_uohm + 2500) / 5000;
    term_raw = ((uint64_t)config->ichgterm_ma * config->rsense_uohm * 16 + 12500) / 25000;
    if (capacity_raw < 2 || capacity_raw > 65535 || term_raw < 1 || term_raw > 32767 ||
        config->empty_mv < 2000 || config->empty_mv > 4000 ||
        /* recovery_mv 是相对 empty_mv 的"恢复增量"(芯片 VEmpty.VR)，不是绝对电压；
         * 清除 EDet 的条件是 VCell > empty_mv + recovery_mv。 */
        config->recovery_mv == 0 || config->recovery_mv > 5080 ||
        config->empty_mv % 10 || config->recovery_mv % 40 ||
        (config->full_soc_thr_pct != 0 &&
         (config->full_soc_thr_pct < 50 || config->full_soc_thr_pct > 100)))
    {
        return M_ERANGE;
    }
    if (!config->model && !config->allow_uncharacterized)
    {
        return M_EPROFILE;
    }
    if (config->model)
    {
        if (config->model->optional_count > 32 || (config->model->optional_count && !config->model->optional))
        {
            return M_EARG;
        }
        for (index = 0; index < config->model->optional_count; index++)
        {
            if (!drv_max17263_optional_allowed(config->model->optional[index].reg))
            {
                return M_EPROFILE;
            }
        }
    }
    dev->design_raw = (uint16_t)capacity_raw;
    dev->term_raw = (uint16_t)term_raw;
    /* VEmpty的VE占D15..D7，VR占D6..D0，不能按高低两个字节拼接。 */
    dev->empty_raw = (uint16_t)(((config->empty_mv / 10) << 7) | (config->recovery_mv / 40));
    /* FullSOCThr(13h): D15..D8=VFSOC判满阈值, D7..D0=低位保持5。
     * 依据 UG6597 p30: EZ(未特征化)推荐 80% (0x5005), 特征化模型用 95% (0x5F05)。
     * FQ 判满 = VFSOC>FullSOCThr 且 Current/AvgCurrent 同时落在 [0.125, 1.25]×IChgTerm。 */
    if (!config->model)
    {
        uint8_t thr = config->full_soc_thr_pct ? config->full_soc_thr_pct : 80u;
        dev->fullsocthr_raw = (uint16_t)(((uint16_t)thr << 8) | 0x0005u);
    }
    else if (config->full_soc_thr_pct)
    {
        dev->fullsocthr_raw = (uint16_t)(((uint16_t)config->full_soc_thr_pct << 8) | 0x0005u);
    }
    else
    {
        dev->fullsocthr_raw = 0;
    }
    return M_OK;
}

/*********************************************************************
 * @fn      drv_max17263_learned_valid
 * @brief   校验学习容量与设计容量的基本合理性。
 * @param   dev  已绑定配置的驱动实例
 * @param   learned  待检查的学习参数，使用寄存器原始值
 * @return  true基本合理；false无效。
 * @note    这是损坏筛查，不是SOH或电池健康验收标准。
********************************************************************/
bool drv_max17263_learned_valid(const max17263_t *dev, const max17263_learned_t *learned)
{
    if (!dev || !learned || !dev->design_raw)
    {
        return false;
    }
    /* Corruption/plausibility screen, not an SOH acceptance criterion. */
    return learned->fullcaprep >= dev->design_raw / 10u && learned->fullcapnom >= dev->design_raw / 10u &&
           learned->fullcaprep <= (uint32_t)dev->design_raw * 2u &&
           learned->fullcapnom <= (uint32_t)dev->design_raw * 2u;
}

/*********************************************************************
 * @fn      drv_max17263_lock_model
 * @brief   重新锁定模型区并检查保护是否生效。
 * @param   dev  驱动实例
 * @return  M_OK锁定成功；否则返回通信或校验错误。
 * @note    两个锁寄存器均尝试清零；成功后解除model_lock_pending标记。
********************************************************************/
static int drv_max17263_lock_model(max17263_t *dev)
{
    int lock1_result = drv_max17263_write(dev, M_LOCK1, 0),
        lock2_result = drv_max17263_write(dev, M_LOCK2, 0);
    uint16_t value;
    unsigned index;
    if (lock1_result != M_OK)
    {
        return lock1_result;
    }
    if (lock2_result != M_OK)
    {
        return lock2_result;
    }
    for (index = 0; index < 32; index++)
    {
        MAX17263_TRY(drv_max17263_read(dev, (uint8_t)(0x80 + index), &value));
        if (value)
        {
            return drv_max17263_record_error(dev, (uint8_t)(0x80 + index), M_EVERIFY);
        }
    }
    dev->model_lock_pending = false;
    return M_OK;
}

/*********************************************************************
 * @fn      drv_max17263_load_capacity
 * @brief   按容量恢复顺序写入学习容量及补偿参数。
 * @param   dev  驱动实例
 * @param   learned  已经校验的学习参数
 * @return  M_OK成功；M_ERANGE容量计算溢出；其他值为访问错误。
 * @note    包含10ms和200ms等待；不能在中断中调用。
********************************************************************/
static int drv_max17263_load_capacity(max17263_t *dev, const max17263_learned_t *learned)
{
    uint16_t soc_raw;
    uint32_t capacity_raw;
    MAX17263_TRY(drv_max17263_write_verify(dev, M_FULLCAPREP, learned->fullcaprep));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_DQACC, (uint16_t)(learned->fullcapnom / 2)));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_DPACC, 0x0C80));
    dev->bus.delay_ms(dev->bus.ctx, 10);
    MAX17263_TRY(drv_max17263_write_verify(dev, M_FULLCAPNOM, learned->fullcapnom));
    MAX17263_TRY(drv_max17263_read(dev, M_VFSOC, &soc_raw));
    capacity_raw = (uint32_t)soc_raw * learned->fullcapnom / 25600u;
    if (capacity_raw > 65535u)
    {
        return M_ERANGE;
    }
    MAX17263_TRY(drv_max17263_write(dev, M_MIXCAP, (uint16_t)capacity_raw));
    MAX17263_TRY(drv_max17263_write(dev, M_AVCAP, (uint16_t)capacity_raw));
    dev->bus.delay_ms(dev->bus.ctx, 200);
    MAX17263_TRY(drv_max17263_write_verify(dev, M_RCOMP0, learned->rcomp0));
    return drv_max17263_write_verify(dev, M_TEMPCO, learned->tempco);
}

/*********************************************************************
 * @fn      drv_max17263_load_full_model
 * @brief   加载原厂定制电芯模型并执行LdMdl流程。
 * @param   dev  驱动实例，其cfg.model必须有效
 * @param   saved  可选学习记录；NULL使用模型初始值
 * @param   mode  已保留VSEL/CSEL且未置Refresh的ModelCfg值
 * @return  M_OK成功；否则返回通信、校验、范围或超时错误。
 * @note    模型表必须来自实际电芯特征化；失败清理由上层初始化流程完成。
********************************************************************/
static int drv_max17263_load_full_model(max17263_t *dev, const max17263_learned_t *saved, uint16_t mode)
{
    const max17263_model_t *model = dev->cfg.model;
    max17263_learned_t learned = {model->rcomp0, model->tempco, dev->design_raw, dev->design_raw, 0};
    unsigned index;
    uint16_t config2;
    uint32_t elapsed_ms = 0;
    uint32_t start_ms;
    if (saved)
    {
        learned = *saved;
    }
    MAX17263_TRY(drv_max17263_write_verify(dev, M_MODELCFG, mode));
    dev->model_lock_pending = true;
    MAX17263_TRY(drv_max17263_write(dev, M_LOCK1, 0x0059));
    MAX17263_TRY(drv_max17263_write(dev, M_LOCK2, 0x00C4));
    for (index = 0; index < 32; index++)
    {
        MAX17263_TRY(drv_max17263_write_verify(dev, (uint8_t)(0x80 + index), model->table[index]));
    }
    MAX17263_TRY(drv_max17263_write_verify(dev, M_RCOMPSEG, model->rcompseg));
    MAX17263_TRY(drv_max17263_lock_model(dev));
    dev->bus.delay_ms(dev->bus.ctx, 100);
    MAX17263_TRY(drv_max17263_write_verify(dev, M_REPCAP, 0));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_DESIGNCAP, dev->design_raw));
    MAX17263_TRY(drv_max17263_load_capacity(dev, &learned));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_ICHGTERM, dev->term_raw));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_VEMPTY, dev->empty_raw));
    for (index = 0; index < 4; index++)
    {
        MAX17263_TRY(drv_max17263_write_verify(dev, (uint8_t)(M_QR00 + index * 0x10), model->qr[index]));
    }
    for (index = 0; index < model->optional_count; index++)
    {
        MAX17263_TRY(
            drv_max17263_write_verify(dev, model->optional[index].reg, model->optional[index].value));
    }
    MAX17263_TRY(drv_max17263_read(dev, M_CONFIG2, &config2));
    MAX17263_TRY(drv_max17263_write(dev, M_CONFIG2, (uint16_t)(config2 | 0x0020)));
    start_ms = dev->bus.now_ms(dev->bus.ctx);
    do
    {
        MAX17263_TRY(drv_max17263_read(dev, M_CONFIG2, &config2));
        if (!(config2 & 0x0020))
        {
            break;
        }
        MAX17263_TRY(drv_max17263_write(dev, M_CURRENT, 0));
        MAX17263_TRY(drv_max17263_write(dev, M_AVGCURRENT, 0));
        dev->bus.delay_ms(dev->bus.ctx, 10);
        elapsed_ms += 10;
    } while (elapsed_ms < dev->cfg.poll_timeout_ms &&
             (uint32_t)(dev->bus.now_ms(dev->bus.ctx) - start_ms) < dev->cfg.poll_timeout_ms);
    if (config2 & 0x0020)
    {
        return drv_max17263_record_error(dev, M_CONFIG2, M_ETIMEOUT);
    }
    MAX17263_TRY(drv_max17263_write_verify(dev, M_QR20, model->qr[2]));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_QR30, model->qr[3]));
    return drv_max17263_write_verify(dev, M_CYCLES, learned.cycles);
}

/*********************************************************************
 * @fn      drv_max17263_configure
 * @brief   配置磷酸铁锂模型、容量和温度策略。
 * @param   dev  驱动实例
 * @param   learned  可选学习参数；NULL不恢复历史学习值
 * @return  M_OK成功；否则返回配置过程中的错误码。
 * @note    保留VSEL/CSEL。无定制模型时配置LFP平台区ScOcvLim默认值。
********************************************************************/
static int drv_max17263_configure(max17263_t *dev, const max17263_learned_t *learned)
{
    uint16_t model_config;
    /* 保留上电自动检测出的多串/电流采样拓扑位，避免软件覆盖硬件识别结果。 */
    MAX17263_TRY(drv_max17263_read(dev, M_MODELCFG, &model_config));
    model_config = (uint16_t)((model_config & 0x000Cu) | 0x0060u | (dev->cfg.ntc_100k ? 0x2000u : 0u));
    if (dev->cfg.model)
    {
        MAX17263_TRY(drv_max17263_load_full_model(dev, learned, model_config));
    }
    else
    {
        MAX17263_TRY(drv_max17263_write_verify(dev, M_DESIGNCAP, dev->design_raw));
        MAX17263_TRY(drv_max17263_write_verify(dev, M_ICHGTERM, dev->term_raw));
        MAX17263_TRY(drv_max17263_write_verify(dev, M_VEMPTY, dev->empty_raw));
        /* EZ 无特征化模型: 按配置写 FullSOCThr (EZ 推荐 80% = 0x5005), 必须在 Refresh 前写入。 */
        if (dev->fullsocthr_raw)
        {
            MAX17263_TRY(drv_max17263_write_verify(dev, M_FULLSOCTHR, dev->fullsocthr_raw));
        }
        MAX17263_TRY(drv_max17263_write(dev, M_MODELCFG, (uint16_t)(model_config | 0x8000)));
        MAX17263_TRY(drv_max17263_wait_clear(dev, M_MODELCFG, 0x8000));
        if (learned)
        {
            MAX17263_TRY(drv_max17263_load_capacity(dev, learned));
            MAX17263_TRY(drv_max17263_write_verify(dev, M_CYCLES, learned->cycles));
        }
    }
    /* Keep-out default: 3275..3350mV; supplied full model may override it. */
    if (!dev->cfg.model)
    {
        MAX17263_TRY(drv_max17263_write_verify(dev, M_SCOCVLIM, 0x479E));
    }
    /* Override only temperature/shutdown/alert policy, preserve other model fields.
     * clear TSel,Tex,THSH,SHDN,COMMSH,FTHRM; set Ten, ETHRM as appropriate. */
    return drv_max17263_update_bits(dev, M_CONFIG, 0x87D8u, dev->cfg.external_ntc ? 0x8210u : 0x0200u);
}

/*********************************************************************
 * @fn      drv_max17263_initialize
 * @brief   执行上电初始化，或校验未掉电芯片的现有配置。
 * @param   dev  已成功绑定的驱动实例
 * @param   learned  可选学习记录；NULL按新电池配置初始化
 * @return  M_OK成功；M_EPROFILE配置不匹配；其他值见max17263_error_t。
 * @note    POR=0不覆盖学习容量；POR=1配置并恢复学习值，恢复HibCfg后最后清POR。失败保留清理标记。
********************************************************************/
int drv_max17263_initialize(max17263_t *dev, const max17263_learned_t *learned)
{
    uint16_t status;
    uint16_t value;
    int result;
    int cleanup_result;
    if (!dev || !dev->design_raw)
    {
        return M_EARG;
    }
    dev->initialized = false;
    if (learned && !drv_max17263_learned_valid(dev, learned))
    {
        return M_EPROFILE;
    }
    /* Retry cleanup before doing anything else after a partial previous attempt. */
    if (dev->model_lock_pending)
    {
        MAX17263_TRY(drv_max17263_lock_model(dev));
    }
    if (dev->hib_restore_pending)
    {
        MAX17263_TRY(drv_max17263_write_verify(dev, M_HIBCFG, dev->last_hibcfg));
        dev->hib_restore_pending = false;
    }
    MAX17263_TRY(drv_max17263_read(dev, M_STATUS, &status));
    MAX17263_TRY(drv_max17263_wait_clear(dev, M_FSTAT, 1));
    /* MCU复位不代表电量计复位：暖启动校验配置，保留芯片正在使用的学习结果。 */
    if (!(status & M_POR))
    {
        MAX17263_TRY(drv_max17263_read(dev, M_DESIGNCAP, &value));
        if (value != dev->design_raw)
        {
            return M_EPROFILE;
        }
        MAX17263_TRY(drv_max17263_read(dev, M_ICHGTERM, &value));
        if (value != dev->term_raw)
        {
            return M_EPROFILE;
        }
        MAX17263_TRY(drv_max17263_read(dev, M_VEMPTY, &value));
        if (value != dev->empty_raw)
        {
            return M_EPROFILE;
        }
        /* FullSOCThr/ScOcvLim 等判满相关参数也纳入暖启动校验, 不匹配时要求
         * 完整 POR 重新配置 (MCU 单独复位不会重写这些寄存器)。 */
        if (dev->fullsocthr_raw)
        {
            MAX17263_TRY(drv_max17263_read(dev, M_FULLSOCTHR, &value));
            if (value != dev->fullsocthr_raw)
            {
                return M_EPROFILE;
            }
        }
        /* EZ 路径固定写 ScOcvLim=0x479E (LFP 平台区), 一并校验。 */
        if (!dev->cfg.model)
        {
            MAX17263_TRY(drv_max17263_read(dev, M_SCOCVLIM, &value));
            if (value != 0x479Eu)
            {
                return M_EPROFILE;
            }
        }
        MAX17263_TRY(drv_max17263_read(dev, M_MODELCFG, &value));
        if ((value & 0xA4F0u) != (uint16_t)(0x0060u | (dev->cfg.ntc_100k ? 0x2000u : 0)))
        {
            return M_EPROFILE;
        }
        MAX17263_TRY(drv_max17263_read(dev, M_CONFIG, &value));
        if ((value & 0x87D8u) != (dev->cfg.external_ntc ? 0x8210u : 0x0200u))
        {
            MAX17263_TRY(
                drv_max17263_update_bits(dev, M_CONFIG, 0x87D8u, dev->cfg.external_ntc ? 0x8210u : 0x0200u));
        }
        MAX17263_TRY(drv_max17263_read(dev, M_STATUS, &status));
        if (status & M_POR)
        {
            return M_EPOR;
        }
        dev->initialized = true;
        return M_OK;
    }
    MAX17263_TRY(drv_max17263_read(dev, M_HIBCFG, &dev->last_hibcfg));
    dev->hib_restore_pending = true;
    /* 唤醒序列完成后才配置模型；中途失败也尽力恢复休眠和模型锁状态。 */
    result = drv_max17263_write(dev, M_SOFTWAKE, 0x0090);
    if (result == M_OK)
    {
        result = drv_max17263_write(dev, M_HIBCFG, 0);
    }
    cleanup_result = drv_max17263_write(dev, M_SOFTWAKE, 0);
    if (result == M_OK)
    {
        result = cleanup_result;
    }
    if (result == M_OK)
    {
        result = drv_max17263_configure(dev, learned);
    }
    if (dev->model_lock_pending)
    {
        cleanup_result = drv_max17263_lock_model(dev);
        if (result == M_OK)
        {
            result = cleanup_result;
        }
    }
    cleanup_result = drv_max17263_write_verify(dev, M_HIBCFG, dev->last_hibcfg);
    if (cleanup_result == M_OK)
    {
        dev->hib_restore_pending = false;
    }
    if (result == M_OK)
    {
        result = cleanup_result;
    }
    if (result != M_OK)
    {
        return result; /* POR remains set. */
    }
    MAX17263_TRY(drv_max17263_read(dev, M_STATUS, &status));
    MAX17263_TRY(drv_max17263_write(dev, M_STATUS, (uint16_t)((status | M_ALERT_FLAGS) & ~M_POR)));
    MAX17263_TRY(drv_max17263_read(dev, M_STATUS, &status));
    if (status & M_POR)
    {
        return M_EVERIFY;
    }
    dev->initialized = true;
    return M_OK;
}

/*********************************************************************
 * @fn      drv_max17263_sample
 * @brief   采集并换算电压、电流、温度、SOC、SOH及循环数据。
 * @param   dev  已初始化的驱动实例
 * @param   sample  输出快照；任何失败都会将valid置false
 * @return  M_OK得到完整快照；M_EPOR复位；M_ENOTREADY未就绪；M_ERANGE异常；其他值为通信错误。
 * @note    电压mV、电流mA且充电为正、温度℃、时间秒。内部温度不代表电芯温度；顺序读取不是同步ADC快照。
********************************************************************/
int drv_max17263_sample(max17263_t *dev, max17263_sample_t *sample)
{
    static const uint8_t registers[] = {M_STATUS, M_VCELL,  M_AVGVCELL, M_CURRENT,    M_AVGCURRENT,
                                        M_TEMP,   M_REPSOC, M_REPCAP,   M_FULLCAPREP, M_FULLCAPNOM,
                                        M_CYCLES, M_TTE,    M_TTF,      M_VFSOC};
    uint16_t raw[14];
    uint16_t status_end;
    uint16_t fstat;
    unsigned index;
    float cap_lsb;
    float current_lsb;
    max17263_sample_t snapshot = {0};
    if (!dev || !sample)
    {
        return M_EARG;
    }
    /* 先让输出失效，完整读取及范围检查通过后才整体提交局部快照。 */
    sample->valid = false;
    if (!dev->initialized)
    {
        return M_ENOTREADY;
    }
    for (index = 0; index < 14; index++)
    {
        MAX17263_TRY(drv_max17263_read(dev, registers[index], &raw[index]));
    }
    MAX17263_TRY(drv_max17263_read(dev, M_STATUS, &status_end));
    MAX17263_TRY(drv_max17263_read(dev, M_FSTAT, &fstat));
    /* 读取前后任一时刻发现POR，都不能使用这轮可能跨复位的测量数据。 */
    if ((raw[0] | status_end) & M_POR)
    {
        dev->initialized = false;
        return M_EPOR;
    }
    if (fstat & 1)
    {
        return M_ENOTREADY;
    }
    snapshot.status = (uint16_t)(raw[0] | status_end);
    snapshot.fstat_raw = fstat;
    snapshot.full_qualified = (fstat & M_FSTAT_FQ) != 0;
    snapshot.empty_detected = (fstat & M_FSTAT_EDET) != 0;
    snapshot.relaxed = (fstat & M_FSTAT_RELDT) != 0;
    snapshot.timestamp_ms = dev->bus.now_ms(dev->bus.ctx);
    cap_lsb = 5000.0f / (float)dev->cfg.rsense_uohm;
    current_lsb = 1562.5f / (float)dev->cfg.rsense_uohm;
    /* VCell是等效单体电压；只有整包电压乘串数，串联容量和电流不乘串数。 */
    snapshot.cell_mv = raw[1] * 0.078125f;
    snapshot.pack_mv = snapshot.cell_mv * dev->cfg.series_cells;
    snapshot.avg_cell_mv = raw[2] * 0.078125f;
    snapshot.current_ma = drv_max17263_decode_signed16(raw[3]) * current_lsb;
    snapshot.avg_current_ma = drv_max17263_decode_signed16(raw[4]) * current_lsb;
    snapshot.temp_c = drv_max17263_decode_signed16(raw[5]) / 256.0f;
    snapshot.soc_pct = raw[6] / 256.0f;
    snapshot.vfsoc_pct = raw[13] / 256.0f;
    snapshot.remaining_mah = raw[7] * cap_lsb;
    snapshot.full_mah = raw[8] * cap_lsb;
    snapshot.nominal_full_mah = raw[9] * cap_lsb;
    snapshot.soh_capacity_pct = 100.0f * raw[8] / dev->design_raw;
    snapshot.cycles_raw = raw[10];
    snapshot.cycles = raw[10] / 100.0f;
    /* 时间预测仅在对应充放电方向有效，0xFFFF不是可用的剩余时间。 */
    snapshot.tte_valid = snapshot.current_ma < 0 && raw[11] != 0xFFFF;
    snapshot.ttf_valid = snapshot.current_ma > 0 && raw[12] != 0xFFFF;
    snapshot.tte_s = snapshot.tte_valid ? raw[11] * 5.625f : -1.0f;
    snapshot.ttf_s = snapshot.ttf_valid ? raw[12] * 5.625f : -1.0f;
    snapshot.characterized = dev->cfg.model != NULL;
    /* Raw endpoints imply current ADC saturation. Never publish as real current. */
    if (snapshot.cell_mv < 1500 || snapshot.cell_mv > 4500 || snapshot.temp_c < -40 || snapshot.temp_c > 85 ||
        snapshot.soc_pct > 100.5f || snapshot.vfsoc_pct > 100.5f || !raw[8] || !raw[9] ||
        raw[3] == 0x8000 || raw[3] == 0x7FFF ||
        raw[4] == 0x8000 || raw[4] == 0x7FFF || (dev->cfg.external_ntc && (snapshot.status & M_BST)))
    {
        return M_ERANGE;
    }
    snapshot.valid = true;
    *sample = snapshot;
    return M_OK;
}

/*********************************************************************
 * @fn      drv_max17263_get_learned
 * @brief   读取可持久化的五项学习参数。
 * @param   dev  已初始化的驱动实例
 * @param   learned  输出原始学习参数；仅成功时更新
 * @return  M_OK成功；M_EPOR复位；M_ENOTREADY未就绪；M_ERANGE容量异常；其他值为通信错误。
 * @note    读取前后检查POR，并检查DNR；本函数不执行Flash写入。
********************************************************************/
int drv_max17263_get_learned(max17263_t *dev, max17263_learned_t *learned)
{
    max17263_learned_t snapshot;
    uint16_t status;
    uint16_t fstat;
    if (!dev || !learned || !dev->initialized)
    {
        return M_ENOTREADY;
    }
    MAX17263_TRY(drv_max17263_read(dev, M_STATUS, &status));
    if (status & M_POR)
    {
        return M_EPOR;
    }
    MAX17263_TRY(drv_max17263_read(dev, M_RCOMP0, &snapshot.rcomp0));
    MAX17263_TRY(drv_max17263_read(dev, M_TEMPCO, &snapshot.tempco));
    MAX17263_TRY(drv_max17263_read(dev, M_FULLCAPREP, &snapshot.fullcaprep));
    MAX17263_TRY(drv_max17263_read(dev, M_FULLCAPNOM, &snapshot.fullcapnom));
    MAX17263_TRY(drv_max17263_read(dev, M_CYCLES, &snapshot.cycles));
    MAX17263_TRY(drv_max17263_read(dev, M_STATUS, &status));
    if (status & M_POR)
    {
        return M_EPOR;
    }
    MAX17263_TRY(drv_max17263_read(dev, M_FSTAT, &fstat));
    if (fstat & 1u)
    {
        return M_ENOTREADY;
    }
    if (!drv_max17263_learned_valid(dev, &snapshot))
    {
        return M_ERANGE;
    }
    *learned = snapshot;
    return M_OK;
}

/*********************************************************************
 * @fn      drv_max17263_ack_alerts
 * @brief   确认已经观测并处理的Status告警。
 * @param   dev  有效且已绑定总线的驱动实例
 * @param   observed_flags  本次已经处理的告警位掩码
 * @return  M_OK成功；否则返回总线错误。
 * @note    遵循写0清除语义，保留POR和未包含在observed_flags中的告警。
********************************************************************/
int drv_max17263_ack_alerts(max17263_t *dev, uint16_t observed_flags)
{
    uint16_t status;
    MAX17263_TRY(drv_max17263_read(dev, M_STATUS, &status));
    /* W0C: ones preserve other pending flags, including events since observation. */
    return drv_max17263_write(
        dev, M_STATUS,
        (uint16_t)((status | M_ALERT_FLAGS | M_POR) & (uint16_t) ~(observed_flags & M_ALERT_FLAGS)));
}

/*********************************************************************
 * @fn      drv_max17263_alerts
 * @brief   配置电压、温度、SOC和电流告警阈值。
 * @param   dev  有效驱动实例
 * @param   min_mv  等效单体欠压阈值，mV，20mV步进
 * @param   max_mv  等效单体过压阈值，mV，20mV步进
 * @param   min_c  最低温度，℃
 * @param   max_c  最高温度，℃
 * @param   min_soc  低SOC阈值，0～100%
 * @param   min_ma  放电电流下限，负数，mA
 * @param   max_ma  充电电流上限，正数，mA
 * @return  M_OK成功；M_EARG参数错误；M_ERANGE电流不可编码；其他值为访问错误。
 * @note    1mΩ时电流步进400mA。使能粘滞告警与ALRT输出；不配置充电器或BMS。
********************************************************************/
int drv_max17263_alerts(max17263_t *dev, uint16_t min_mv, uint16_t max_mv, int8_t min_c, int8_t max_c,
                        uint8_t min_soc, int32_t min_ma, int32_t max_ma)
{
    int64_t min_scaled;
    int64_t max_scaled;
    uint16_t current_threshold;
    if (!dev || min_mv >= max_mv || max_mv > 5100 || min_mv % 20 || max_mv % 20 || min_c >= max_c ||
        min_soc > 100 || min_ma >= 0 || max_ma <= 0)
    {
        return M_EARG;
    }
    min_scaled = (int64_t)min_ma * dev->cfg.rsense_uohm;
    max_scaled = (int64_t)max_ma * dev->cfg.rsense_uohm;
    /* Explicitly reject unrepresentable thresholds instead of silently truncating. */
    if (min_scaled % 400000 || max_scaled % 400000 || min_scaled / 400000 < -128 || max_scaled / 400000 > 127)
    {
        return M_ERANGE;
    }
    current_threshold =
        (uint16_t)(((uint16_t)(uint8_t)(max_scaled / 400000) << 8) | (uint8_t)(min_scaled / 400000));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_VALRT, (uint16_t)(((max_mv / 20) << 8) | (min_mv / 20))));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_TALRT,
                                           (uint16_t)(((uint16_t)(uint8_t)max_c << 8) | (uint8_t)min_c)));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_SALRT, (uint16_t)(0xFF00u | min_soc)));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_IALRT, current_threshold));
    /* Sticky V/T/S/I; Aen enabled. Disable dSOC interrupt to reduce IRQ traffic. */
    MAX17263_TRY(drv_max17263_update_bits(dev, M_CONFIG, 0x7804, 0x7804));
    return drv_max17263_update_bits(dev, M_CONFIG2, 0x0080, 0);
}
/* LED接口共用的状态检查，定义见下方。 */
static int drv_max17263_led_ready(max17263_t *dev);
/*********************************************************************
 * @fn      drv_max17263_led
 * @brief   设置自动SOC灯条显示，并退出自定义图案模式。
 * @param   dev  已初始化的驱动实例
 * @param   bars  灯数，3～12
 * @param   mode  0关闭、1按键定时、2直接按键、3持续显示
 * @param   brightness  亮度，0～31
 * @param   led_mv  LED标称正向电压，0～2520mV，40mV步进
 * @param   while_charging  true允许充电状态强制显示
 * @return  M_OK成功；M_EARG参数错误；其他值为状态或访问错误。
 * @note    保留LEDCfg3.DNC，手动配置灯数并开启空电闪烁。
********************************************************************/
int drv_max17263_led(max17263_t *dev, uint8_t bars, uint8_t mode, uint8_t brightness, uint16_t led_mv,
                     bool while_charging)
{
    uint16_t led_config1;
    uint16_t led_config2;
    if (!dev || bars < 3 || bars > 12 || mode > 3 || brightness > 31 || led_mv > 2520 || led_mv % 40)
    {
        return M_EARG;
    }
    MAX17263_TRY(drv_max17263_led_ready(dev));
    led_config1 = (uint16_t)((4u << 13) | (3u << 10) | ((uint16_t)mode << 6) | (while_charging ? 0x20u : 0) |
                             0x10u | bars);
    led_config2 = (uint16_t)(((led_mv / 40u) << 9) | 0x40u | brightness); /* manual bar count; empty blink */
    MAX17263_TRY(drv_max17263_write_verify(dev, M_LEDCFG2, led_config2));
    MAX17263_TRY(drv_max17263_write_verify(dev, M_LEDCFG1, led_config1));
    return drv_max17263_update_bits(dev, M_LEDCFG3, 0xBFFF, 0x8000); /* Preserve DNC bit 14. */
}

/*********************************************************************
 * @fn      drv_max17263_led_ready
 * @brief   检查LED操作前的初始化和POR状态。
 * @param   dev  驱动实例
 * @return  M_OK可操作；M_ENOTREADY未初始化；M_EPOR检测到复位；其他值为通信错误。
 * @note    发现POR时清除initialized，由上层安排重新初始化。
********************************************************************/
static int drv_max17263_led_ready(max17263_t *dev)
{
    uint16_t status;
    if (!dev || !dev->initialized)
    {
        return M_ENOTREADY;
    }
    MAX17263_TRY(drv_max17263_read(dev, M_STATUS, &status));
    if (status & M_POR)
    {
        dev->initialized = false;
        return M_EPOR;
    }
    return M_OK;
}

/*********************************************************************
 * @fn      drv_max17263_led_mask
 * @brief   切换到12灯自定义模式并更新点亮掩码。
 * @param   dev  已初始化的驱动实例
 * @param   mask12  bit0～11对应LED0～11，1亮0灭，高4位必须为0
 * @return  M_OK成功；M_EARG掩码错误；其他值为状态或访问错误。
 * @note    先预写CustLED再切模式；保留亮度、VLED和DNC。恢复SOC模式调用max17263_led。
********************************************************************/
int drv_max17263_led_mask(max17263_t *dev, uint16_t mask12)
{
    if (!dev || (mask12 & 0xF000u))
    {
        return M_EARG;
    }
    MAX17263_TRY(drv_max17263_led_ready(dev));
    /* Preload the pattern before switching from SOC to custom mode.
     * In existing custom mode the mask takes effect on the next IC scan. */
    /* 先准备新图案再切换控制源，避免短暂显示之前遗留的自定义图案。 */
    MAX17263_TRY(drv_max17263_write_verify(dev, M_CUSTLED, mask12));
    /* Preserve VLED and brightness; disable DLED, auto-count and blink modes. */
    MAX17263_TRY(drv_max17263_update_bits(dev, M_LEDCFG2, 0x81E0u, 0));
    /* NBARS=12, LEDMd=3, no charging override, gray or animation. */
    MAX17263_TRY(drv_max17263_update_bits(dev, M_LEDCFG1, 0x03FFu, 0x00CCu));
    return drv_max17263_update_bits(dev, M_LEDCFG3, 0xBFFFu,
                                    0xA000u); /* FullSpd + CustLEDCtrl; preserve DNC */
}

/*********************************************************************
 * @fn      drv_max17263_led_set
 * @brief   修改当前自定义图案中的单颗LED。
 * @param   dev  已初始化的驱动实例
 * @param   index  LED索引，0～11
 * @param   on  true点亮；false熄灭
 * @return  M_OK成功；M_EARG索引错误；M_EPROFILE尚未进入自定义模式；其他值为访问错误。
 * @note    必须先调用max17263_led_mask进入自定义模式。
********************************************************************/
int drv_max17263_led_set(max17263_t *dev, uint8_t index, bool on)
{
    uint16_t mode;
    uint16_t mask;
    if (!dev || index >= 12)
    {
        return M_EARG;
    }
    MAX17263_TRY(drv_max17263_led_ready(dev));
    MAX17263_TRY(drv_max17263_read(dev, M_LEDCFG3, &mode));
    if (!(mode & 0x2000u))
    {
        return M_EPROFILE; /* Do not reuse a stale custom pattern in SOC mode. */
    }
    MAX17263_TRY(drv_max17263_read(dev, M_CUSTLED, &mask));
    mask &= 0x0FFFu;
    if (on)
    {
        mask |= (uint16_t)(1u << index);
    }
    else
    {
        mask &= (uint16_t) ~(1u << index);
    }
    return drv_max17263_led_mask(dev, mask);
}

/*********************************************************************
 * @fn      drv_max17263_error_string
 * @brief   将错误码转换为静态诊断字符串。
 * @param   error  max17263_error_t错误码
 * @return  静态只读字符串；未知错误返回unknown，不需要释放。
 * @note    不访问硬件，可用于日志。
********************************************************************/
const char *drv_max17263_error_string(int error)
{
    switch (error)
    {
        case M_OK:
            return "OK";
        case M_EARG:
            return "argument";
        case M_EIO:
            return "I2C";
        case M_ETIMEOUT:
            return "timeout";
        case M_EVERIFY:
            return "readback";
        case M_EPOR:
            return "gauge reset";
        case M_ENOTREADY:
            return "not ready";
        case M_ERANGE:
            return "range/sensor";
        case M_EPROFILE:
            return "profile mismatch";
        case M_ENVM:
            return "storage";
        default:
            return "unknown";
    }
}
