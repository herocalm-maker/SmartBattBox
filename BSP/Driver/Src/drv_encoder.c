/**
 * @file    drv_encoder.c
 * @brief   旋转编码器驱动 (Driver 层)
 *
 * 基于 EC11 编码器参考代码实现:
 *   - A 相跳变时采样 B 相判断方向
 *   - ScanResult: 1=正转, 2=反转
 *   - 仅在 A=B=1 (定位点) 时触发输出
 *   - 含抖动处理 (A 跳变但 B 未变)
 *
 * 调用方式: 定时器中断 (1ms) 或 20ms 轮询调用 drv_encoder_scan()
 */
#include "drv_encoder.h"
#include "bsp_io.h"
#include "Board.h"

/* 编码器引脚 */
#define ENC_A_PORT    BOARD_CODER1_PORT
#define ENC_A_PIN     BOARD_CODER1_PIN
#define ENC_B_PORT    BOARD_CODER2_PORT
#define ENC_B_PIN     BOARD_CODER2_PIN



/* 累积增量 */
static volatile int8_t s_delta = 0;

/* 状态变量 (参考 Encoder_Scanf) */
static uint8_t s_last_a = 0;
static uint8_t s_last_b = 0;
static int8_t  s_scan_result = 0;   /* 1=正转, 2=反转, 0=无 */

/*********************************************************************
 * @fn      drv_encoder_init
 * @brief   初始化编码器 GPIO (输入+上拉)
 ********************************************************************/
void drv_encoder_init(void)
{
    bsp_gpio_init(ENC_A_PORT, ENC_A_PIN, GPIO_MODE_INPUT, GPIO_PULLUP,
                  GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(ENC_B_PORT, ENC_B_PIN, GPIO_MODE_INPUT, GPIO_PULLUP,
                  GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);

    /* 读取初始状态 */
    s_last_a = bsp_gpio_read(ENC_A_PORT, ENC_A_PIN) ? 1 : 0;
    s_last_b = bsp_gpio_read(ENC_B_PORT, ENC_B_PIN) ? 1 : 0;
    s_delta  = 0;
    s_scan_result = 0;
}

/*********************************************************************
 * @fn      drv_encoder_deinit
 * @brief   反初始化 (释放 GPIO)
 ********************************************************************/
void drv_encoder_deinit(void)
{
    /* 可选: 恢复 GPIO 默认状态 */
}

/*********************************************************************
 * @fn      drv_encoder_scan
 * @brief   编码器扫描 (参考 Encoder_Scanf 逻辑)
 *
 * 检测逻辑:
 *   - A 相跳变时采样 B 相，判断方向存入 ScanResult
 *   - A 上升沿 + B 下降沿 → 正转 (ScanResult=1)
 *   - A 上升沿 + B 上升沿 → 反转 (ScanResult=2)
 *   - A 下降沿 + B 上升沿 → 正转 (ScanResult=1)
 *   - A 下降沿 + B 下降沿 → 反转 (ScanResult=2)
 *   - 抖动处理: A 跳变但 B 未变 → 按 B 状态推断方向
 *   - 仅在 A=B=1 (定位点) 时输出增量
 ********************************************************************/
void drv_encoder_scan(void)
{
    uint8_t current_a = bsp_gpio_read(ENC_A_PORT, ENC_A_PIN) ? 1 : 0;
    uint8_t current_b = bsp_gpio_read(ENC_B_PORT, ENC_B_PIN) ? 1 : 0;

    /* A 相跳变时检测方向 */
    if (current_a != s_last_a) 
    {
        if (current_a == 1) 
        {
            /* A 上升沿 */
            if (s_last_b == 1 && current_b == 0) 
            {
                s_scan_result = 1;      /* 正转 */
            } 
            else if (s_last_b == 0 && current_b == 1) 
            {
                s_scan_result = 2;      /* 反转 */
            } 
            else if (s_last_b == current_b && current_b == 0) 
            {
                s_scan_result = 1;      /* 抖动: B=0 不变 → 正转 */
            } 
            else if (s_last_b == current_b && current_b == 1) 
            {
                s_scan_result = 2;      /* 抖动: B=1 不变 → 反转 */
            }
        } 
        else 
        {
            /* A 下降沿 */
            if (s_last_b == 1 && current_b == 0) 
            {
                s_scan_result = 2;      /* 反转 */
            } 
            else if (s_last_b == 0 && current_b == 1) 
            {
                s_scan_result = 1;      /* 正转 */
            } 
            else if (s_last_b == current_b && current_b == 0) 
            {
                s_scan_result = 2;      /* 抖动: B=0 不变 → 反转 */
            } 
            else if (s_last_b == current_b && current_b == 1) 
            {
                s_scan_result = 1;      /* 抖动: B=1 不变 → 正转 */
            }
        }
        s_last_a = current_a;
        s_last_b = current_b;
    }

    /* 仅在定位点 (A=B=1) 时输出增量 */
    if (current_a == 1 && current_b == 1) 
    {
        if (s_scan_result == 1) 
        {
            s_delta++; //                 /* 正转，在这里处理事件 */
        } 
        else if (s_scan_result == 2) 
        {
            s_delta--;                  /* 反转 在这里处理事件*/
        }
        s_scan_result = 0;              /* 清除，等待下次 */
    }
}

/*********************************************************************
 * @fn      drv_encoder_read
 * @brief   读取并清除累积增量
 ********************************************************************/
int8_t drv_encoder_read(void)
{
    int8_t delta = s_delta;
    s_delta = 0;
    return delta;
}
