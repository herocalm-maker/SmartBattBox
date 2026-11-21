/**
 * @file    drv_buzzer.c
 * @brief   蜂鸣器驱动实现 (Driver 层)
 *
 * 有源蜂鸣器, 纯软件状态机 (无专用硬件定时器).
 * 支持短鸣 (N 次间歇) 和长鸣 (持续 N ms).
 * 需周期调用 drv_buzzer_tick() (当前 TIM4 = 50ms).
 */
#include "drv_buzzer.h"
#include "bsp_io.h"
#include "Board.h"
#include "comm_logger.h"



/* 内部状态 */
static volatile BEEP_DATA_t s_BeepData = {0};
static volatile uint32_t s_short_on_ticks = BEEP_SHORT_ON_TICK;
static volatile uint32_t s_short_off_ticks = BEEP_SHORT_OFF_TICK;
static uint32_t s_tick_cnt = 0;    /* 当前计时 (ms) */

/* 蜂鸣器 GPIO 控制宏 */
#define BEEP_ON()   bsp_gpio_write(BOARD_BUZZER_PORT, BOARD_BUZZER_PIN, GPIO_PIN_SET)
#define BEEP_OFF()  bsp_gpio_write(BOARD_BUZZER_PORT, BOARD_BUZZER_PIN, GPIO_PIN_RESET)



/*********************************************************************
 * @fn      drv_buzzer_init
 * @brief   初始化蜂鸣器 (推挽输出, 默认静音)
 * @return  none
********************************************************************/
void drv_buzzer_init(void)
{
    bsp_gpio_init(BOARD_BUZZER_PORT, BOARD_BUZZER_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,
                  GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    /* 初值低电平 = 静音 (有源蜂鸣器高电平导通) */
    s_BeepData.SoundType = BEEP_SOUND_NONE;
    s_BeepData.ShortState = false;
    s_BeepData.ShortNumber = 0;
    s_BeepData.LongTimeMs = 0;
    s_tick_cnt = 0;
    BEEP_OFF();
    /* 上电自检: 短鸣 1 次, 顺带验证 GPIO 输出路径与时基正常 */
    drv_buzzer_short_sound(1);
    logger_out(LOG_INFO, "drv_buzzer: init");
}

/**
 * @fn      buzzer_short_pattern
 * @brief   短鸣通用实现: 按 on/off tick 参数生成 count 次间歇鸣叫
 * @param   count      鸣叫次数 (0 直接返回失败)
 * @param   on_ticks   单次响阶段 tick 数
 * @param   off_ticks  单次停阶段 tick 数
 * @return  bool true=成功启动 false=count 为 0
 * @note    关中断保护"参数+状态+首响"整体写入, 避免定时器中断
 *          观察到半更新的模式参数.
 */
/* 短鸣通用模式: 按 on/off 时长生成 count 次间歇鸣叫 (关中断保护状态一致性) */
static bool buzzer_short_pattern(uint8_t count, uint32_t on_ticks, uint32_t off_ticks)
{
    if (count == 0)
    {
        return false;
    }
    /* The timer ISR must not observe a partially updated pattern. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_short_on_ticks = on_ticks;
    s_short_off_ticks = off_ticks;
    s_BeepData.SoundType = BEEP_SOUND_SHORT;
    s_BeepData.ShortNumber = count;
    s_BeepData.ShortState = true;
    s_tick_cnt = 0;
    BEEP_ON();
    __set_PRIMASK(primask);
    return true;
}

/*********************************************************************
 * @fn      drv_buzzer_short_sound
 * @brief   普通短鸣 N 次 (单次响/停时长见头文件 BEEP_SHORT_ON/OFF_TICK)
 * @param   count 鸣叫次数 (1-255; 0 直接返回失败)
 * @return  true=成功启动, false=count 为 0
********************************************************************/
bool drv_buzzer_short_sound(uint8_t count)
{
    return buzzer_short_pattern(count, BEEP_SHORT_ON_TICK, BEEP_SHORT_OFF_TICK);
}

/*********************************************************************
 * @fn      drv_buzzer_reject_sound
 * @brief   拒绝提示音: 急促 3 连鸣 (各 150ms 响/150ms 停), 用于操作被拒场景
 * @return  true=成功启动
********************************************************************/
bool drv_buzzer_reject_sound(void)
{
    return buzzer_short_pattern(3, 3u, 3u); /* 50ms tick: 150ms on / 150ms off. */
}

/*********************************************************************
 * @fn      drv_buzzer_long_sound
 * @brief   长鸣指定毫秒数
 * @param   duration_ms 持续时长 (ms)
 * @return  true=成功启动
********************************************************************/
bool drv_buzzer_long_sound(uint32_t duration_ms)
{
    if (duration_ms == 0)
    {
        return false;
    }

    s_BeepData.SoundType   = BEEP_SOUND_LONG;
    s_BeepData.LongTimeMs  = duration_ms;
    s_BeepData.ShortNumber = 1;
    s_tick_cnt = 0;
    BEEP_ON();

    return true;
}

/*********************************************************************
 * @fn      drv_buzzer_stop
 * @brief   立即停止鸣叫
 * @return  true=成功
********************************************************************/
bool drv_buzzer_stop(void)
{
    s_BeepData.SoundType = BEEP_SOUND_NONE;
    s_BeepData.ShortNumber = 0;
    s_BeepData.ShortState = false;
    s_BeepData.LongTimeMs = 0;
    s_tick_cnt = 0;
    BEEP_OFF();

    return true;
}

/*********************************************************************
 * @fn      drv_buzzer_is_busy
 * @brief   查询蜂鸣器是否正在工作 (参考 Beep_IsBusy)
 * @return  true=忙 (短鸣剩余次数>0), false=空闲
********************************************************************/
bool drv_buzzer_is_busy(void)
{
    if (s_BeepData.ShortNumber > 0)
    {
        return true;
    }
    return false;
}

/*********************************************************************
 * @fn      drv_buzzer_tick
 * @brief   周期调用处理时序 (当前 TIMx = 50ms)
 * @note    由 user_timx_CallBack() 调用
 * @return  none
********************************************************************/
void drv_buzzer_tick(void)
{
    if (s_BeepData.SoundType == BEEP_SOUND_NONE) 
    {
        return;
    }

    s_tick_cnt++;

    switch (s_BeepData.SoundType) 
    {
        case BEEP_SOUND_SHORT:
            if (s_BeepData.ShortNumber > 0) 
            {
                if (s_BeepData.ShortState) 
                {
                    /* 响阶段 */
                    if (s_tick_cnt >= s_short_on_ticks) 
                    {
                        s_BeepData.ShortState = false;
                        s_tick_cnt = 0;
                        BEEP_OFF();
                    }
                } 
                else 
                {
                    /* 间隔阶段 */
                    if (s_tick_cnt >= s_short_off_ticks) 
                    {
                        s_BeepData.ShortNumber--;
                        if (s_BeepData.ShortNumber > 0)
                        {
                            s_BeepData.ShortState = true;
                            s_tick_cnt = 0;
                            BEEP_ON();
                        } 
                        else
                        {
                            /* 全部完成 */
                            s_BeepData.SoundType = BEEP_SOUND_NONE;
                        }
                    }
                }
            } 
            else 
            {
                /* 已完成 */
                s_BeepData.SoundType = BEEP_SOUND_NONE;
                BEEP_OFF();
            }
            break;

        case BEEP_SOUND_LONG:
            if (s_tick_cnt >= (s_BeepData.LongTimeMs / BEEP_TICK_PERIOD_MS))
            {
                s_BeepData.SoundType = BEEP_SOUND_NONE;
                s_BeepData.LongTimeMs = 0;
                s_BeepData.ShortNumber = 0;
                BEEP_OFF();
            }
            break;

        default:
            s_BeepData.SoundType = BEEP_SOUND_NONE;
            BEEP_OFF();
            break;
    }
}



