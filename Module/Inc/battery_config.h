/* Board parameters: confirm capacity, charge termination and product thresholds.
 * 8S / R114 1mOhm / CELLX 1:20 / no external NTC / 12 LEDs.
 * Values retained from the existing project; not battery characterization results. */
#ifndef BATTERY_CONFIG_H
#define BATTERY_CONFIG_H
#include "app_battery.h"
#define BATTERY_STALE_MS 3500u
#define BATTERY_ACTION_DELAY_MS 2000u
/* 充电/判满口径 (2026-10-25 产品定义, 逻辑在 app_main_task.c):
 *   启动: 有 220V 且 SOC<100% 且不在 10min 停充冷却期
 *   判满A: SOC=100% 时, FQ=1 且 TTF 剩余=0 → 立即停; 否则保持 5min 强制停
 *   判满B: 物理兜底(SOC 到不了100%时): 单体>=3600mV(28.8V) 且 TTF=0 且 平均电流<500mA 持续 1h
 *   拔枪: 充电中 220V 丢失立即全断
 * 下面两个值仅作诊断参考 (电压/电流门槛已直接写在 app_main_task.c 的常量里)。 */
#define BATTERY_FULL_CELL_MV 3650u /* 诊断参考: 充电器 CV 对应的单体电压 (29.2V/8) */
#define BATTERY_TAPER_MA 500       /* 诊断参考: taper 电流量级, mA */
#define BATTERY_PROFILE_ID 7u /* FQ位/FullSOCThr/满电口径等配置变更后 bump, 强制POR重配 */
#define BATTERY_PACK_ID 1u /* Change on physical pack replacement. */
static const max17263_config_t battery_cfg={
 .profile_id=BATTERY_PROFILE_ID,.battery_id=BATTERY_PACK_ID,
 .rsense_uohm=1000,.design_mah=20000,.ichgterm_ma=200,
 .empty_mv=2500,.recovery_mv=3000,.full_soc_thr_pct=80,.series_cells=8,
 .external_ntc=false,.ntc_100k=false,.allow_uncharacterized=true,
 .model=NULL,.poll_timeout_ms=3000
};
static const battery_policy_t battery_policy={
 .min_cell_mv=2500,.max_cell_mv=3720,.min_temp_c=-20,.max_temp_c=60,
 .low_soc=10,.max_discharge_ma=42000,.max_charge_ma=10000,
 .period_ms=3000,.stale_ms=BATTERY_STALE_MS,
 .led_bars=12,.led_mode=3,.led_brightness=8,.led_mv=2000,
 .led_while_charging=false
};
#endif
