/**
 * @file    drv_max17263.h
 * @brief   MAX17263 电池计量驱动接口 (Driver 层)
 *
 * 8串磷酸铁锂 / 1mΩ采样 / 12灯；当前使用芯片内部温度。
 * 调用顺序：bind -> initialize -> sample/LED；总线回调由上层适配。
 * @note    同一个实例必须串行使用；model和optional数据的生命周期必须覆盖驱动运行期。
 */
#ifndef DRV_MAX17263_H
#define DRV_MAX17263_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* All physical units are explicit. I2C callbacks use an UNshifted 7-bit address.
 * Single owner only: serialize complete API calls, not just I2C transactions. */
#define MAX17263_ADDR 0x36u
enum
{
    M_STATUS = 0x00,
    M_VALRT = 0x01,
    M_TALRT = 0x02,
    M_SALRT = 0x03,
    M_REPCAP = 0x05,
    M_REPSOC = 0x06,
    M_TEMP = 0x08,
    M_VCELL = 0x09,
    M_CURRENT = 0x0A,
    M_AVGCURRENT = 0x0B,
    M_MIXCAP = 0x0F,
    M_FULLCAPREP = 0x10,
    M_TTE = 0x11,
    M_QR00 = 0x12,
    M_FULLSOCTHR = 0x13,
    M_CYCLES = 0x17,
    M_DESIGNCAP = 0x18,
    M_AVGVCELL = 0x19,
    M_CONFIG = 0x1D,
    M_ICHGTERM = 0x1E,
    M_AVCAP = 0x1F,
    M_TTF = 0x20,
    M_DEVNAME = 0x21,
    M_QR10 = 0x22,
    M_FULLCAPNOM = 0x23,
    M_LEARNCFG = 0x28,
    M_RELAXCFG = 0x2A,
    M_MISCCFG = 0x2B,
    M_TGAIN = 0x2C,
    M_TOFF = 0x2D,
    M_QR20 = 0x32,
    M_LEDCFG3 = 0x37,
    M_RCOMP0 = 0x38,
    M_TEMPCO = 0x39,
    M_VEMPTY = 0x3A,
    M_FSTAT = 0x3D,
    M_LEDCFG1 = 0x40,
    M_QR30 = 0x42,
    M_DQACC = 0x45,
    M_DPACC = 0x46,
    M_LEDCFG2 = 0x4B,
    M_SOFTWAKE = 0x60,
    M_LOCK1 = 0x62,
    M_LOCK2 = 0x63,
    M_CUSTLED = 0x64,
    M_RCOMPSEG = 0xAF,
    M_IALRT = 0xB4,
    M_CURVE = 0xB9,
    M_HIBCFG = 0xBA,
    M_CONFIG2 = 0xBB,
    M_SCOCVLIM = 0xD1,
    M_MODELCFG = 0xDB,
    M_VFSOC = 0xFF
};
#define M_POR 0x0002u
#define M_BST 0x0008u
#define M_ALERT_FLAGS 0xFFC4u
/* FStat(3Dh) 位定义 — 依据 UG6597 Rev3 Table 20 (p39):
 *   D15..D10 = X(Don't Care), D9 = RelDt, D8 = EDet, D7 = FQ, D6 = RelDt2,
 *   D5..D1 = X, D0 = DNR。
 * ⚠ 更正记录: 旧代码把 FQ 当 bit5(0x20)、EDet 当 bit3(0x08)、RelDt 当 bit6(0x40),
 *   全部是错的 —— bit5 是未定义位(X)，读到的"满电"是随机值。0x5660 实测可验证:
 *   bit9=1/bit6=1 表示长时间静置，bit7=0 表示未充满，与 SOC=3% 完全自洽。 */
#define M_FSTAT_DNR (1u << 0)    /* 0x0001 Data Not Ready: 上电/换电池后未就绪 (710ms) */
#define M_FSTAT_RELDT2 (1u << 6) /* 0x0040 RelDt2: 长静置 (48~96min 以上) */
#define M_FSTAT_FQ (1u << 7)     /* 0x0080 Full Qualified: 充电终止条件全部满足 */
#define M_FSTAT_EDET (1u << 8)   /* 0x0100 Empty Detection: 到达空电点, 回升到 VEmpty+VR 清除 */
#define M_FSTAT_RELDT (1u << 9)  /* 0x0200 Relaxed Cell: 完全静置检出 */
#define M_MISC_QUICK_START (1u << 10) /* 0x0400 MiscCfg.Quick Start: 触发基于OCV的重新学习 */
typedef enum
{
    M_OK = 0,
    M_EARG = -1,
    M_EIO = -2,
    M_ETIMEOUT = -3,
    M_EVERIFY = -4,
    M_EPOR = -5,
    M_ENOTREADY = -6,
    M_ERANGE = -7,
    M_EPROFILE = -8,
    M_ENVM = -9
} max17263_error_t;
typedef struct
{
    void *ctx; /* 上层私有上下文，原样传入所有回调 */
    /* 读写回调：返回0成功，非0失败；addr7不左移，len单位为字节。 */
    int (*read)(void *ctx, uint8_t addr7, uint8_t reg, uint8_t *data, size_t len);
    int (*write)(void *ctx, uint8_t addr7, uint8_t reg, const uint8_t *data, size_t len);
    /* 允许任务休眠的毫秒延时；now_ms须为单调递增的32位毫秒时钟。 */
    void (*delay_ms)(void *ctx, uint32_t ms);
    uint32_t (*now_ms)(void *ctx);
} max17263_bus_t;
typedef struct
{
    uint8_t reg;
    uint16_t value;
} max17263_reg_value_t;
/* Only ADI-characterized values belong here. Never synthesize an OCV table. */
typedef struct
{
    uint16_t table[32];
    uint16_t rcompseg;
    uint16_t rcomp0;
    uint16_t tempco;
    uint16_t qr[4];
    const max17263_reg_value_t *optional;
    size_t optional_count;
} max17263_model_t;
typedef struct
{
    uint32_t profile_id; /* Change for cell model, Rsense, capacity or thermistor changes. */
    uint32_t battery_id; /* Change whenever the physical battery pack is replaced. */
    uint32_t rsense_uohm; /* 采样电阻，µΩ；本板1000 */
    uint32_t design_mah; /* 电池包设计容量，mAh，串联不乘节数 */
    uint32_t ichgterm_ma; /* 充电终止电流，mA */
    uint16_t empty_mv; /* 等效单体空电电压，mV，10mV步进 */
    uint16_t recovery_mv; /* 等效单体恢复电压，mV，40mV步进 */
    /* 判满 VFSOC 阈值%: EZ无模型 LFP 取90 (80 在平台区会提前判满并污染容量学习);
     * 0=EZ路径默认80, 特征化模型路径不写(由模型 optional 寄存器决定)。 */
    uint8_t full_soc_thr_pct;
    uint8_t series_cells; /* 串联节数，本板8 */
    bool external_ntc; /* true外部NTC；false芯片内部温度 */
    bool ntc_100k; /* 外部NTC阻值选择，false为10kΩ */
    bool allow_uncharacterized; /* Explicit bench evaluation only if model == NULL. */
    const max17263_model_t *model;
    uint32_t poll_timeout_ms; /* 100..10000, timeout for DNR/Refresh/LdMdl */
} max17263_config_t;
typedef struct
{
    uint16_t rcomp0;
    uint16_t tempco;
    uint16_t fullcaprep;
    uint16_t fullcapnom;
    uint16_t cycles; /* 原始学习计数，0.01次/LSB */
} max17263_learned_t;
typedef struct
{
    uint32_t timestamp_ms; /* 采样完成时刻，毫秒，可回绕 */
    uint16_t status;
    uint16_t cycles_raw; /* 原始循环计数，0.01次/LSB */
    float cell_mv; /* 等效平均单体电压，mV；不是逐节测量 */
    float pack_mv; /* 整包电压，mV */
    float avg_cell_mv; /* 平均等效单体电压，mV */
    float current_ma; /* 瞬时电流，mA，充电正放电负 */
    float avg_current_ma; /* 平均电流，mA */
    float temp_c; /* 温度，℃；由配置决定温度源 */
    float soc_pct; /* 报告SOC，% */
    float vfsoc_pct; /* 满电判据用SOC，%；FQ判满看VFSOC而非RepSOC */
    float remaining_mah; /* 报告剩余容量，mAh */
    float full_mah; /* 报告满容量，mAh */
    float nominal_full_mah; /* 标称满容量，mAh */
    float soh_capacity_pct; /* 容量型SOH，FullCapRep/DesignCap × 100 */
    float cycles; /* 等效完整循环次数，保留小数 */
    float tte_s; /* 预计放空时间，秒，仅tte_valid时有效 */
    float ttf_s; /* 预计充满时间，秒，仅ttf_valid时有效 */
    uint16_t fstat_raw; /* FStat(3Dh)原始值, 供诊断 */
    bool valid;
    bool characterized;
    bool tte_valid;
    bool ttf_valid;
    bool full_qualified; /* FStat.FQ(bit7,0x0080): 充电终止条件全部满足(实时位) */
    bool empty_detected; /* FStat.EDet(bit8,0x0100): 芯片空电检出 */
    bool relaxed; /* FStat.RelDt(bit9,0x0200): 完全静置检出, 不是满电标志 */
} max17263_sample_t;
typedef struct
{
    max17263_bus_t bus;
    max17263_config_t cfg;
    uint16_t design_raw;
    uint16_t term_raw;
    uint16_t empty_raw;
    uint16_t fullsocthr_raw; /* 0 表示不写/不校验 FullSOCThr (特征化模型路径) */
    uint16_t last_hibcfg;
    uint8_t failed_reg; /* 最近一次记录的访问错误地址；非所有错误都会更新 */
    int last_error; /* 最近一次记录的底层错误码 */
    bool initialized; /* 初始化完成标志 */
    bool hib_restore_pending; /* 上次操作失败后仍需恢复HibCfg */
    bool model_lock_pending; /* 上次操作失败后仍需重新锁定模型区 */
} max17263_t;

/*==================== 公开API ====================*/

/*********************************************************************
 * @fn      drv_max17263_bind
 * @brief   绑定总线和电池配置，校验范围并计算原始配置值。
 * @param   dev  待初始化的驱动实例
 * @param   bus  总线回调，地址使用未左移的7位值
 * @param   config  电池配置，容量mAh、采样电阻µΩ、终止电流mA
 * @return  M_OK成功；M_EARG参数缺失；M_ERANGE超范围；M_EPROFILE模型配置不允许。
 * @note    不访问芯片。配置按值复制，但model及其optional数组必须在驱动生命周期内保持有效。
********************************************************************/
extern int drv_max17263_bind(max17263_t *dev, const max17263_bus_t *bus, const max17263_config_t *config);

/*********************************************************************
 * @fn      drv_max17263_read
 * @brief   读取一个16位寄存器，按低字节在前组装结果。
 * @param   dev  已绑定总线的驱动实例
 * @param   reg  8位寄存器地址
 * @param   value  输出原始值；失败时不更新
 * @return  M_OK成功；M_EARG参数无效；M_EIO通信失败。
 * @note    读失败最多尝试3次，每次失败后延时2ms。
********************************************************************/
extern int drv_max17263_read(max17263_t *dev, uint8_t reg, uint16_t *value);

/*********************************************************************
 * @fn      drv_max17263_write
 * @brief   写入一个16位寄存器，先发送低字节。
 * @param   dev  已绑定总线的驱动实例
 * @param   reg  8位寄存器地址
 * @param   value  待写入的16位原始值
 * @return  M_OK成功；M_EARG参数无效；M_EIO通信失败。
 * @note    写失败不盲目重试，避免已经生效的命令被重复执行。
********************************************************************/
extern int drv_max17263_write(max17263_t *dev, uint8_t reg, uint16_t value);

/*********************************************************************
 * @fn      drv_max17263_quick_start
 * @brief   触发 Quick Start, 让芯片基于当前OCV重新估计初始SOC。
 * @param   dev  已绑定且已初始化的驱动实例
 * @return  M_OK成功；M_EARG未初始化；M_ETIMEOUT等待清零超时；其他为通信错误。
 * @note    RepSOC(06h)/RepCap(05h) 是结果只读寄存器, 不能强写; 换电池/深放后
 *          需要重估 SOC 时用本函数 (MiscCfg 2Bh bit10=0x0400, 自清零, 轮询等待)。
********************************************************************/
extern int drv_max17263_quick_start(max17263_t *dev);

/*********************************************************************
 * @fn      drv_max17263_initialize
 * @brief   执行上电初始化，或校验未掉电芯片的现有配置。
 * @param   dev  已成功绑定的驱动实例
 * @param   learned  可选学习记录；NULL按新电池配置初始化
 * @return  M_OK成功；M_EPROFILE配置不匹配；其他值见max17263_error_t。
 * @note    POR=0不覆盖学习容量；POR=1配置并恢复学习值，恢复HibCfg后最后清POR。失败保留清理标记。
********************************************************************/
extern int drv_max17263_initialize(max17263_t *dev, const max17263_learned_t *learned);

/*********************************************************************
 * @fn      drv_max17263_sample
 * @brief   采集并换算电压、电流、温度、SOC、SOH及循环数据。
 * @param   dev  已初始化的驱动实例
 * @param   sample  输出快照；任何失败都会将valid置false
 * @return  M_OK得到完整快照；M_EPOR复位；M_ENOTREADY未就绪；M_ERANGE异常；其他值为通信错误。
 * @note    电压mV、电流mA且充电为正、温度℃、时间秒。内部温度不代表电芯温度；顺序读取不是同步ADC快照。
********************************************************************/
extern int drv_max17263_sample(max17263_t *dev, max17263_sample_t *sample);

/*********************************************************************
 * @fn      drv_max17263_get_learned
 * @brief   读取可持久化的五项学习参数。
 * @param   dev  已初始化的驱动实例
 * @param   learned  输出原始学习参数；仅成功时更新
 * @return  M_OK成功；M_EPOR复位；M_ENOTREADY未就绪；M_ERANGE容量异常；其他值为通信错误。
 * @note    读取前后检查POR，并检查DNR；本函数不执行Flash写入。
********************************************************************/
extern int drv_max17263_get_learned(max17263_t *dev, max17263_learned_t *learned);

/*********************************************************************
 * @fn      drv_max17263_learned_valid
 * @brief   校验学习容量与设计容量的基本合理性。
 * @param   dev  已绑定配置的驱动实例
 * @param   learned  待检查的学习参数，使用寄存器原始值
 * @return  true基本合理；false无效。
 * @note    这是损坏筛查，不是SOH或电池健康验收标准。
********************************************************************/
extern bool drv_max17263_learned_valid(const max17263_t *dev, const max17263_learned_t *learned);

/*********************************************************************
 * @fn      drv_max17263_ack_alerts
 * @brief   确认已经观测并处理的Status告警。
 * @param   dev  有效且已绑定总线的驱动实例
 * @param   observed_flags  本次已经处理的告警位掩码
 * @return  M_OK成功；否则返回总线错误。
 * @note    遵循写0清除语义，保留POR和未包含在observed_flags中的告警。
********************************************************************/
extern int drv_max17263_ack_alerts(max17263_t *dev, uint16_t observed_flags);

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
extern int drv_max17263_alerts(max17263_t *dev, uint16_t min_mv, uint16_t max_mv, int8_t min_c, int8_t max_c,
                               uint8_t min_soc, int32_t min_ma, int32_t max_ma);

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
extern int drv_max17263_led(max17263_t *dev, uint8_t bars, uint8_t mode, uint8_t brightness, uint16_t led_mv,
                            bool while_charging);

/*********************************************************************
 * @fn      drv_max17263_led_mask
 * @brief   切换到12灯自定义模式并更新点亮掩码。
 * @param   dev  已初始化的驱动实例
 * @param   mask12  bit0～11对应LED0～11，1亮0灭，高4位必须为0
 * @return  M_OK成功；M_EARG掩码错误；其他值为状态或访问错误。
 * @note    先预写CustLED再切模式；保留亮度、VLED和DNC。恢复SOC模式调用max17263_led。
********************************************************************/
extern int drv_max17263_led_mask(max17263_t *dev, uint16_t mask12);

/*********************************************************************
 * @fn      drv_max17263_led_set
 * @brief   修改当前自定义图案中的单颗LED。
 * @param   dev  已初始化的驱动实例
 * @param   index  LED索引，0～11
 * @param   on  true点亮；false熄灭
 * @return  M_OK成功；M_EARG索引错误；M_EPROFILE尚未进入自定义模式；其他值为访问错误。
 * @note    必须先调用max17263_led_mask进入自定义模式。
********************************************************************/
extern int drv_max17263_led_set(max17263_t *dev, uint8_t index, bool on);

/*********************************************************************
 * @fn      drv_max17263_error_string
 * @brief   将错误码转换为静态诊断字符串。
 * @param   error  max17263_error_t错误码
 * @return  静态只读字符串；未知错误返回unknown，不需要释放。
 * @note    不访问硬件，可用于日志。
********************************************************************/
extern const char *drv_max17263_error_string(int error);

#ifdef __cplusplus
}
#endif

#endif /* DRV_MAX17263_H */
