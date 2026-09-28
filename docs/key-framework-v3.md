# 按键框架 V3 — 精简方案

## 设计目标

仅实现三种按键事件:
1. **单击 (CLICK)** — 按下去抖确认后**立即**触发 (不等释放)
2. **长按 (LONG)** — 按住达到阈值触发一次 (不连发)
3. **组合键 (COMBINED)** — 多键同时按住触发 (同一手势内已报按键去重)

移除: 双击、长按重复、释放触发

## 架构

```
┌─────────────────────────────────────────────────────────────┐
│  App 层 (app_init.c)                                        │
│  mod_key_register(KEY_ID_ONOFF, on_click, on_long)          │
│  mod_key_register_combined(KEY_MASK_ONOFF|KEY_MASK_K1, ...) │
└──────────────────────────┬──────────────────────────────────┘
                           │ callback(key_id)
┌──────────────────────────▼──────────────────────────────────┐
│  Module 层 (mod_key)                                        │
│  s_handlers[KEY_ID_MAX] = {on_click, on_long, user_data}   │
│  mod_key_poll() → drv_key_scan() → _dispatch() → callback  │
│  _check_combined() → pressed_mask 匹配 → combined_cb       │
└──────────────────────────┬──────────────────────────────────┘
                           │ key_snapshot_t + pressed_mask
┌──────────────────────────▼──────────────────────────────────┐
│  Driver 层 (drv_key)                                        │
│  状态机: IDLE → PRESSED → (释放) → CLICK                    │
│                   → (长按) → LONG → (释放) → IDLE           │
│  pressed_mask: 实时跟踪当前按下键位掩码                     │
└─────────────────────────────────────────────────────────────┘
```

## 数据结构

### drv_key.h — 驱动层

```c
/* 事件类型 */
typedef enum {
    KEY_EVT_NONE = 0,
    KEY_EVT_CLICK,    /* 单击 */
    KEY_EVT_LONG,     /* 长按 */
} key_event_enum;

/* 按键位掩码 */
#define KEY_MASK_ONOFF   (1u << 0)  /* 0x01 */
#define KEY_MASK_ACIN    (1u << 1)  /* 0x02 */
#define KEY_MASK_ACOUT   (1u << 2)  /* 0x04 */
#define KEY_MASK_K1      (1u << 3)  /* 0x08 */

/* 事件快照 */
typedef struct {
    key_event_enum onoff;
    key_event_enum acin;
    key_event_enum acout;
    key_event_enum k1;
    int8_t         coder_delta;    /* 编码器增量 */
    uint8_t        pressed_mask;   /* 当前按下键位掩码 */
} key_snapshot_t;
```

### mod_key.h — 模块层

```c
/* 按键 ID */
typedef enum {
    KEY_ID_ONOFF = 0,
    KEY_ID_ACIN,
    KEY_ID_ACOUT,
    KEY_ID_K1,
    KEY_ID_MAX
} key_id_enum;

/* 回调类型 */
typedef void (*key_click_cb_t)(key_id_enum key);
typedef void (*key_long_cb_t)(key_id_enum key);
typedef void (*key_combined_cb_t)(uint8_t key_mask);

/* 按键处理器 */
typedef struct {
    key_click_cb_t on_click;      /* 单击回调 (可为NULL) */
    key_long_cb_t  on_long;       /* 长按回调 (可为NULL) */
    void          *user_data;     /* 用户数据 (可选) */
} key_handler_t;

/* 组合键配置 */
typedef struct {
    uint8_t           key_mask;   /* 按键组合掩码 */
    uint16_t          hold_ms;    /* 同时按住时长 (ms) */
    key_combined_cb_t callback;
} key_combined_cfg_t;

/* API */
bool mod_key_init(void);
bool mod_key_register(key_id_enum key, const key_handler_t *handler);
bool mod_key_register_combined(uint8_t key_mask, uint16_t hold_ms,
                               key_combined_cb_t callback);
void mod_key_poll(void);  /* 20ms 调用 */
```

## 状态机 (20ms 轮询, 掩码方式)

```
IDLE ──有键按下──→ CONFIRM ──连续2次采样一致──→ PRESSED ──按住≥800ms──→ LONG
 ^                   │                            │                       │
 │  采样归零          │ 键数变化→重新去抖            │ 松手                   │ 松手
 +───────────────────+                            +                       +
                    (上报 CLICK, 同手势内已报键去重)  (上报 LONG 一次, 不连发)
```

## 组合键检测

```
每 20ms 扫描:
  pressed_mask = 当前所有按下键的位掩码

  for each registered combined key:
    if (pressed_mask & cfg->key_mask) == cfg->key_mask:
      timer++
      if timer >= cfg->hold_ms / 20:
        fire callback(cfg->key_mask)
        timer = 0  // 防止重复触发
    else:
      timer = 0
```

## 时序参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| 轮询周期 | 20ms | AppMainTask 主循环 |
| 去抖 | 40ms (2×20ms) | 连续采样一致后触发 CLICK |
| 长按阈值 | 800ms (40×20ms) | 达到后触发一次 LONG |
| 组合键时长 | 同长按阈值 | 键数变化重新去抖, 已报键去重 |

## 使用示例

```c
/* 1. 基础单击/长按 */
mod_key_register(KEY_ID_ONOFF, &(key_handler_t){
    .on_click = _on_onoff_click,
    .on_long  = _on_onoff_long,
});

/* 2. 仅单击 */
mod_key_register(KEY_ID_K1, &(key_handler_t){
    .on_click = _on_k1_switch,
});

/* 3. 组合键: ONOFF + K1 同时按 3s → 进入标定 */
mod_key_register_combined(
    KEY_MASK_ONOFF | KEY_MASK_K1,
    3000,
    _on_enter_calibrate
);
```

## 文件清单

| 文件 | 说明 |
|------|------|
| `BSP/Driver/Inc/drv_key.h` | 事件类型, 位掩码, snapshot, API |
| `BSP/Driver/Src/drv_key.c` | 状态机, pressed_mask, 编码器 |
| `Module/Inc/mod_key.h` | handler, combined_cfg, API |
| `Module/Src/mod_key.c` | dispatch, combined 检测 |
| `App/Src/app_init.c` | 注册回调 |

## 对比 V2

| 功能 | V2 | V3 (本方案) |
|------|----|----|
| 单击 | ✅ | ✅ |
| 双击 | ✅ | ❌ 移除 |
| 长按 | ✅ | ✅ |
| 长按重复 | ✅ | ❌ 移除 |
| 组合键 | ✅ | ✅ |
| pressed_mask | ✅ | ✅ |
| 代码复杂度 | 中 | 低 |

## 编译目标

```
0 errors, 0 warnings
App size: ~61KB (预估比 V2 减少 ~200B)
```
