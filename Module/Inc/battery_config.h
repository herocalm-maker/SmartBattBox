/* Board parameters: confirm capacity, charge termination and product thresholds.
 * 8S / R114 1mOhm / CELLX 1:20 / no external NTC / 12 LEDs.
 * Values retained from the existing project; not battery characterization results. */
#ifndef BATTERY_CONFIG_H
#define BATTERY_CONFIG_H
#include "app_battery.h"
#define BATTERY_STALE_MS 3500u
#define BATTERY_ACTION_DELAY_MS 2000u
#define BATTERY_PROFILE_ID 4u
#define BATTERY_PACK_ID 1u /* Change on physical pack replacement. */
/* 1 enables 1Hz CSV for bench validation; default keeps 10s ordinary logs. */
#ifndef BATTERY_CSV_ENABLE
#define BATTERY_CSV_ENABLE 0
#endif
static const max17263_config_t battery_cfg={
 .profile_id=BATTERY_PROFILE_ID,.battery_id=BATTERY_PACK_ID,
 .rsense_uohm=1000,.design_mah=20000,.ichgterm_ma=1000,
 .empty_mv=2500,.recovery_mv=3000,.series_cells=8,
 .external_ntc=false,.ntc_100k=false,.allow_uncharacterized=true,
 .model=NULL,.poll_timeout_ms=3000
};
static const battery_policy_t battery_policy={
 .min_cell_mv=2500,.max_cell_mv=3700,.min_temp_c=-20,.max_temp_c=60,
 .low_soc=10,.max_discharge_ma=42000,.max_charge_ma=10000,
 .period_ms=1000,.stale_ms=BATTERY_STALE_MS,
 .led_bars=12,.led_mode=3,.led_brightness=8,.led_mv=2000,
 .led_while_charging=false
};
#endif
