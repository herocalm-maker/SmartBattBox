/**
 * @file    bsp_wdg.c
 * @brief   独立看门狗 (IWDG) BSP 实现
 *
 * 上层只需指定 timeout_ms, 内部自动计算 prescaler/reload:
 *   LSI 频率 ≈ 32 kHz
 *   timeout = prescaler × reload / LSI_freq
 *   算法: 从最小 prescaler(4) 开始, 计算 reload, 若 > 0xFFF 则升级 prescaler
 */
#include "bsp_wdg.h"
#include "Board.h"
#include "stm32f4xx_hal.h"

#if BOARD_WDG_ENABLE

/* LSI 典型频率 (Hz) */
#define LSI_FREQ_HZ       32000u

/* IWDG 12-bit reload 最大值 */
#define IWDG_RELOAD_MAX   0xFFFu

/* Prescaler 分频表 (从小到大) */
typedef struct {
    uint32_t div;           /* 分频系数 */
    uint32_t hal_prescaler; /* HAL 枚举值 */
} wdg_prescaler_t;

static const wdg_prescaler_t s_prescaler_table[] = {
    {   4, IWDG_PRESCALER_4   },
    {   8, IWDG_PRESCALER_8   },
    {  16, IWDG_PRESCALER_16  },
    {  32, IWDG_PRESCALER_32  },
    {  64, IWDG_PRESCALER_64  },
    { 128, IWDG_PRESCALER_128 },
    { 256, IWDG_PRESCALER_256 },
};

static IWDG_HandleTypeDef s_hiwdg = {0};

/* =====================================================================
 *  公开 API 实现
* ===================================================================== */

/**
 * @brief   初始化独立看门狗
 * @param   cfg  配置 (timeout_ms)
 * @return  true=成功, false=超时值超出范围或 HAL 初始化失败
 */
bool bsp_wdg_init(const bsp_wdg_config_t *cfg)
{
    if (cfg == NULL || cfg->timeout_ms == 0) {
        return false;
    }

    /* 根据 timeout_ms 计算 prescaler 和 reload */
    uint32_t reload = 0;
    uint32_t prescaler_hal = 0;
    bool found = false;

    for (uint32_t i = 0; i < sizeof(s_prescaler_table) / sizeof(s_prescaler_table[0]); i++) {
        uint32_t div = s_prescaler_table[i].div;
        /* reload = timeout_ms × LSI_freq / (prescaler × 1000) */
        reload = (cfg->timeout_ms * LSI_FREQ_HZ) / (div * 1000u);
        if (reload > 0 && reload <= IWDG_RELOAD_MAX) {
            prescaler_hal = s_prescaler_table[i].hal_prescaler;
            found = true;
            break;
        }
    }

    if (!found) {
        /* timeout_ms 超出 IWDG 范围 (太小或太大) */
        return false;
    }

    s_hiwdg.Instance       = IWDG;
    s_hiwdg.Init.Prescaler = prescaler_hal;
    s_hiwdg.Init.Reload    = reload;

    if (HAL_IWDG_Init(&s_hiwdg) != HAL_OK) {
        return false;
    }
    return true;
}

/**
 * @brief   喂狗 (刷新计数器)
 *          HAL_IWDG_Refresh 写 KR 寄存器, 无阻塞, 可在任意上下文调用
 */
void bsp_wdg_refresh(void)
{
    HAL_IWDG_Refresh(&s_hiwdg);
}



/*====================================================================
 *  任务心跳监控 (由50ms 周期调用)
 *  - 每 20 tick (1s)  喂狗一次
 *  - 每 1200 tick (60s) 检测心跳, 异常则停止喂狗 → IWDG 复位
 *====================================================================*/

#define WDG_FEED_DIV       20     /* 20 × 50ms = 1s  喂狗周期 */
#define WDG_CHECK_DIV      1200   /* 1200 × 50ms = 60s  心跳检测周期 */

/* 各任务心跳计数 (ISR 安全的 volatile) */
static volatile uint32_t s_task_cnt[WDG_TASK_COUNT] = {0};

/* 监控状态 */
static uint32_t s_cnt_snap[WDG_TASK_COUNT] = {0};  /* 上次快照 */
static uint16_t s_tick = 0;                       /* 50ms tick 计数器 */
static bool     s_feed_ok = true;             /* true=正常喂狗, false=已停止 */

/**
 * @brief  任务心跳 (各 RTOS 任务主循环中调用)
 */
void bsp_wdg_task_heartbeat(wdg_task_id_t id)
{
    if (id < WDG_TASK_COUNT) {
        s_task_cnt[id]++;
    }
}

/**
 * @brief  读取任务心跳计数
 */
uint32_t bsp_wdg_get_task_cnt(wdg_task_id_t id)
{
    if (id < WDG_TASK_COUNT) {
        return s_task_cnt[id];
    }
    return 0;
}

/**
 * @brief  监控 tick — TIM4 ISR 50ms 周期调用
 *         - 每 20 tick (1s) 喂狗
 *         - 每 1200 tick (60s) 检测心跳, 异常则停止喂狗
 */
void bsp_wdg_monitor_tick(void)
{
    if (!s_feed_ok) {
        return;  /* 已判定异常, 停止喂狗 → 等待 IWDG 复位 */
    }

    s_tick++;

    if (s_tick >= WDG_FEED_DIV) {
        s_tick = 0;
        bsp_wdg_refresh();  /* 每秒喂狗 */
    }

    if (s_tick == 0) {
        /* 每整秒检测 (利用 s_tick 刚归零的时机) */
        static uint16_t sec_cnt = 0;
        sec_cnt++;
        if (sec_cnt >= 60) {
            sec_cnt = 0;
            for (uint8_t i = 0; i < WDG_TASK_COUNT; i++) {
                uint32_t cur = s_task_cnt[i];
                if (cur == s_cnt_snap[i]) {
                    s_feed_ok = false;  /* 任务 i 60s 内心跳无变化 → 异常 */
                    break;
                }
                s_cnt_snap[i] = cur;
            }
        }
    }
}

/**
 * @brief  初始化监控状态 (清零计数, 由 main 在 bsp_wdg_init 后调用)
 */
void bsp_wdg_clear_task_cnt(void)
{

    for (uint8_t i = 0; i < WDG_TASK_COUNT; i++) {
        s_task_cnt[i] = 0;
        s_cnt_snap[i] = 0;
    }
    s_tick = 0;
    s_feed_ok = true;
}

/**
 * @brief  初始化监控 (IWDG 初始化 + 清零计数, 由 main 调用)
 * @note   调试器连接时跳过 IWDG 并使能 halt 冻结:
 *           IWDG 由 LSI 独立时钟驱动, 内核被调试器 halt 后不再喂狗,
 *           8.19s 后必然复位 —— 表现为 J-Link 烧写报 "Failed to erase
 *           sectors" / 单步时目标突然重启 / 寄存器读回 0xDEADBEEF。
 *         Debug 构建(-DDEBUG)下检测到调试器 (DHCSR.C_DEBUGEN) 就不启动
 *         IWDG; 正式 Release 构建不受影响。
 */
void bsp_wdg_monitor_init(void)
{
#if defined(DEBUG)
  /* 内核 halt 时冻结 IWDG, 便于单步/烧写; 该位只影响调试暂停状态 */
  __HAL_DBGMCU_FREEZE_IWDG();

  /* 调试器已连接: 不启动 IWDG, 避免调试期间被看门狗复位 */
  if ((CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) != 0u)
  {
    return;
  }
#endif

  bsp_wdg_config_t wdg_cfg = {
    .timeout_ms = BOARD_IWDG_TIMEOUT_MS,
  };

  bsp_wdg_init(&wdg_cfg);
  bsp_wdg_clear_task_cnt();
}



#endif /* BOARD_WDG_ENABLE */
