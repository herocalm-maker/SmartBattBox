# app_main_task 工作状态机重组方案（评审稿）

> 目标：把主循环中割裂的 `work_status_process() / power_process() / auto_charge_update() / inverter_idle_watchdog()` 收敛为一个**工作状态机**，按"充电+旁路 / 逆变 / 空闲"三态统一管理，包含线路切换动作；充电策略并入；风扇独立管理。
> 本稿仅评审，不改正式代码。

---

## 1. 现状问题

当前主循环 4 个并列调用，各自维护半份业务，状态判定分散：

```
for (;;) {
    work_status_process();   /* 只做保护(风扇/温度/电池/充满) */
    power_process();         /* 只做通路: 掉电守卫/输入恢复/PHASE推进 */
    auto_charge_update();    /* 只做充电策略: 2s防抖→need_charge→请求 */
    inverter_idle_watchdog();/* 只做逆变空闲停机 */
    ...
}
```

割裂点：
| 割裂 | 说明 |
|---|---|
| 保护与切换分离 | `work_status_process` 只判定不切通路；`power_process` 只切通路不判保护，靠调用顺序隐式耦合 |
| 充电策略独立成环 | `auto_charge_update` 单独维护 AC 防抖、need_charge、目标合成、path_request，与 `power_process` 的 AC 掉电守卫/输入恢复逻辑重复（两处都在查 AC、合成目标） |
| 稳态管理散落 | 充满停充在 `work_status_process`，逆变空闲停机在独立 watchdog，同属"稳态附加管理"却分两处 |
| 输入恢复逻辑绕路 | `power_process` 专门写"逆变→有AC→转旁路"，其实 `resolve_target_path` 在 AC present 时已能合成出旁路目标，合并后自然消除 |

---

## 2. 目标架构

```
┌─────────────────────────────────────────────┐
│ AppMainTask 主循环 (20ms)                    │
│  ① fan_manage()          ← 风扇: 独立, 始终执行(故障/关机后仍散热)
│  ② work_status_process() ← 工作状态机: 保护+充电策略+通路切换+稳态管理
│  ③ mod_key_poll_handler() / work_led_process() / display_publish() / wdg
└─────────────────────────────────────────────┘
```

`work_status_process` 阶段流水（每 tick）：

```
门控(关机/故障) → ① 温度/电池保护 → ② AC掉电守卫 → ③ 充电策略(2s防抖)
→ ④ 目标通路合成+切换(含AC恢复转旁路) → ⑤ PHASE时序推进(预充/直连/输出/停充尾段)
→ ⑥ 稳态管理(按工作态分派: 充电→充满停充 / 逆变→空闲看门狗 / 空闲→无)
```

三态视角（由 s_target_path 派生，不新增状态变量）：

| 工作态 | 触发 | 稳态附加管理 |
|---|---|---|
| 充电+旁路 | `PATH_CHARGE` / `PATH_CHARGE_BYPASS` | 充满自动停充 (FQ) |
| 逆变输出 | `PATH_INVERT` | 无负载 60s / 计量离线自动停 |
| 空闲 | `PATH_NONE` | 无 |

---

## 3. 代码示例（核心）

### 3.1 主循环

```c
void AppMainTask(void *arg)
{
    (void)arg;
    param_init();
    drv_fan_off(FAN_AUX);
    drv_fan_off(FAN_MAIN);
    drv_led_off(LED_ALM);
    mod_key_init();
    mod_key_register_callback(app_main_task_key_event_handler);
    for (;;)
    {
        /* ① 风扇: 独立管理, 始终执行 (故障/关机后仍散热) */
        fan_manage(mod_temp_get());
        /* ② 工作状态机: 保护 + 充电策略 + 通路切换 + 稳态管理 */
        work_status_process();
        mod_key_poll_handler();
        work_led_process();
        display_publish();
        bsp_wdg_task_heartbeat(WDG_TASK_MAIN);
        osDelay(20);
    }
}
```

### 3.2 工作状态机（work_status_process）

```c
/**
 * @brief 工作状态机: 统一管理保护、充电策略、通路切换与稳态附加管理。
 *
 * 阶段流水 (每 20ms):
 *   门控(关机/故障) → ① 温度/电池保护 → ② AC掉电守卫 → ③ 充电策略(2s防抖)
 *   → ④ 目标通路合成+切换(含AC恢复转旁路) → ⑤ PHASE时序推进 → ⑥ 稳态管理
 *
 * 工作态 (由 s_target_path 派生, 不新增状态变量):
 *   充电+旁路(PATH_CHARGE/_BYPASS) / 逆变(PATH_INVERT) / 空闲(PATH_NONE)
 */
static void work_status_process(void)
{
    if (s_shutting_down)
    {
        shutdown_process();          /* 关机态: 推进释放/存参/断电 */
        return;
    }
    if (s_state == SYS_STATUS_FAULT)
    {
        return;                      /* 故障态: 保持全断, 等按键/复位 */
    }

    ac_input_state_t ac = mod_metering_get_ac_input().state;
    /* AC 状态变化立即标记 UI, display_publish 即时刷新 220V 图标 */
    static ac_input_state_t s_last_ac_state = AC_INPUT_UNKNOWN;
    if (ac != s_last_ac_state)
    {
        s_last_ac_state = ac;
        s_ui_dirty = true;
    }

    /* ① 异常监督: 温度/电池方向保护 (启动中立即, 稳态延时跳闸) */
    const TEMP_INFO_T *t = mod_temp_get();
    bool charge = path_active_charge();
    bool invert = path_active_invert();
    if (thermal_protect(t, charge, invert) || battery_protect(charge, invert))
    {
        return;
    }

    /* ② AC 掉电守卫: 需市电的通路在 220V 丢失/未知时立即停止 */
    if (path_needs_ac(s_target_path) && ac != AC_INPUT_PRESENT)
    {
        input_abort();
        return;
    }

    /* ③ 充电策略: 220V 稳定 2s 且未充满 → need_charge (波动清零防抖) */
    charge_strategy_update(ac);

    /* ④ 目标通路合成+切换: 输出请求 + 充电策略
     *    AC present 时 resolve 自然合成旁路/充电+旁路 → 输入恢复自动转旁路 */
    energy_path_t target = resolve_target_path(ac, s_output_requested, s_need_charge);
    if (target != s_target_path)
    {
        path_request(target, ac);    /* 拒绝/失败由保护与故障状态接管 */
    }

    /* ⑤ 通路时序推进 (PHASE 状态机: 预充/直连/输出/停充尾段) */
    if (s_phase != PHASE_IDLE)
    {
        uint32_t elapsed = (uint32_t)(HAL_GetTick() - s_phase_since);
        power_phase_process(ac, elapsed);
        return;
    }

    /* ⑥ 稳态附加管理: 按工作态分派 (风扇已独立, 此处不再涉及) */
    switch (s_target_path)
    {
    case PATH_CHARGE:
    case PATH_CHARGE_BYPASS:
        full_charge_auto_stop();     /* 充电/充电+旁路态: 充满自动停充 */
        break;
    case PATH_INVERT:
        inverter_idle_watchdog();    /* 逆变态: 无负载 60s / 计量离线自动停 */
        break;
    default:
        break;                       /* 空闲态: 无附加管理 */
    }
    drv_led_off(LED_ALM);
}
```

### 3.3 充电策略（原 auto_charge_update 并入，去门控 + ac 参数化）

```c
/**
 * @brief 充电策略: 220V 稳定 2s 且未充满时置 need_charge (由 work_status_process ③ 调用)。
 * @note AC 波动(掉电再接入)计时清零; 充满/无AC 自动收敛; 门控由状态机外部保证。
 */
static void charge_strategy_update(ac_input_state_t ac)
{
    static bool s_ac_stable;
    static uint32_t s_ac_stable_since;
    static ac_input_state_t s_ac_log = AC_INPUT_UNKNOWN; /* 诊断: AC 状态变化日志 */
    static bool s_chg_log = false;                       /* 诊断: need_charge 变化日志 */

    if (ac != s_ac_log)
    {
        s_ac_log = ac;
        logger_out(LOG_INFO, "auto chg: AC state=%d", (int)ac);
    }
    if (ac == AC_INPUT_PRESENT)
    {
        if (!s_ac_stable)
        {
            s_ac_stable = true;
            s_ac_stable_since = HAL_GetTick();
        }
        if ((uint32_t)(HAL_GetTick() - s_ac_stable_since) >= AC_STABLE_MS)
        {
            BATT_INFO_T bat = {0};
            bool snap_ok = battery_get_snapshot(&bat);
            s_need_charge = snap_ok && !bat.full_qualified;
            if (s_need_charge != s_chg_log)
            {
                s_chg_log = s_need_charge;
                logger_out(LOG_INFO, "auto chg: need=%d ok=%d soc=%u fq=%d",
                           (int)s_need_charge, (int)snap_ok, bat.soc, (int)bat.full_qualified);
            }
        }
    }
    else
    {
        s_ac_stable = false;
        s_need_charge = false;
    }
}
```

### 3.4 逆变空闲看门狗（去重复门控，仅稳态分派时调用）

```c
/**
 * @brief 逆变稳态管理: 无负载(60s)或输出计量离线时自动停止输出。
 * @note  - 输出 HLW8110 离线(检测不到输出) -> 立即关断
 *        - vout_p_w < 1W 连续 60s -> 自动关断
 *        - 关断即清除 s_output_requested, 不会自动重启, 等下次 AC OUT 键触发
 *        门控(s_state/s_target_path)由 work_status_process ⑥ 分派保证, 此处不再重复。
 */
static void inverter_idle_watchdog(void)
{
    METER_INFO_T *m = metering_get_info();
    uint32_t now = HAL_GetTick();
    ac_input_state_t ac = mod_metering_get_ac_input().state;

    if (!m || !m->online)
    {
        s_inv_idle_since = 0;
        logger_out(LOG_WARN, "INV meter offline: output off");
        s_output_requested = false;
        if (path_request(PATH_NONE, ac))
        {
            drv_buzzer_short_sound(2);
            display_publish();
        }
        return;
    }
    if (m->vout_p_w >= INV_IDLE_PWR_W)
    {
        s_inv_idle_since = 0;
        return;
    }
    if (s_inv_idle_since == 0)
    {
        s_inv_idle_since = now;
    }
    if ((uint32_t)(now - s_inv_idle_since) >= INV_IDLE_STOP_MS)
    {
        s_inv_idle_since = 0;
        logger_out(LOG_WARN, "INV idle %us no load: auto output off", INV_IDLE_STOP_MS / 1000u);
        s_output_requested = false;
        if (path_request(PATH_NONE, ac))
        {
            drv_buzzer_short_sound(2);
            display_publish();
        }
    }
}
```

---

## 4. 函数变化对照

| 函数 | 变化 | 说明 |
|---|---|---|
| `work_status_process` | **重组为主状态机** | 吸收 power_process 的 AC变化/守卫/推进 + 稳态分派 |
| `power_process` | **删除**（逻辑并入状态机 ②④⑤） | 掉电守卫/输入恢复/PHASE 推进全部并入 |
| `auto_charge_update` | **改名 `charge_strategy_update`** + 去门控 + ac 参数化 | 由状态机 ③ 调用 |
| `inverter_idle_watchdog` | **去重复门控** | 仅稳态分派时调用，内部不再自查 state/target |
| `fan_manage`（原 fan_hysteresis） | **独立** | 主循环 ① 调用，始终执行，不再被 work_status_process 包裹 |
| `power_phase_process` / `path_request` / `resolve_target_path` / `input_abort` / `shutdown_process` / `thermal_protect` / `battery_protect` / `full_charge_auto_stop` / `path_active_*` | 保留不动 | 底层零件 |

主循环并列调用：**4 个 → 2 个**（fan_manage + work_status_process）。

---

## 5. 行为等价性核对

| 场景 | 原行为 | 新行为 | 结论 |
|---|---|---|---|
| 温度/电池故障 | work_status 保护→进故障 | 状态机 ① 保护→进故障 | 等价 |
| AC 掉电（充电/旁路中） | power_process 守卫→input_abort | 状态机 ② 守卫→input_abort | 等价 |
| AC 稳定 2s 自动充电 | auto_charge_update | 状态机 ③ charge_strategy_update | 等价 |
| AC 恢复（逆变中） | power_process 专分支转旁路 | resolve 天然合成旁路目标→path_request | 等价（逻辑消除） |
| 充满自动停充 | work_status 第4步 | 状态机 ⑥ 充电态分派 | 等价 |
| 逆变空闲 60s 停机 | watchdog 主循环独立调用 | 状态机 ⑥ 逆变态分派 | 等价 |
| 逆变**启动中**（PHASE 非 IDLE）计量离线 | watchdog 立即停机（打断启动） | 启动完成进入稳态后才判（3.5s 内不判负载） | **微调**: 更合理，启动期无负载属正常 |
| 风扇散热 | work_status 最前（故障也执行） | 主循环 ① 独立（始终执行） | 等价 |
| 关机/故障门控 | 各函数各自 return | 状态机统一门控 | 等价 |

---

## 6. 评审确认项

1. 主循环收敛为 `fan_manage + work_status_process` 是否认可？
2. 状态机 ⑥ 用 `switch(s_target_path)` 按工作态分派稳态管理，是否认可（不新增 work_state 枚举，避免与 s_target_path 重复状态）？
3. 逆变启动中（3.5s）不再判空闲停机，是否为期望行为？
4. 确认后执行：替换正式代码 → CubeIDE 重编译（0 error/warning）→ git 提交。
