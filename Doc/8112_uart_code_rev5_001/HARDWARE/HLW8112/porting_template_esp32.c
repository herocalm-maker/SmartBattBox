/**
 * @file    hlw8112_hw_esp32.c
 * @brief   HLW8112/HLW8110 硬件抽象层实现 — ESP32 平台模板
 * @details 此为 ESP-IDF 平台的移植模板，工程师需根据实际硬件连接修改此文件。
 *          将此文件重命名为 hlw8112_hw.c 放入工程即可。
 *
 *          假设硬件连接：
 *            EN   → GPIO5
 *            CS   → GPIO18
 *            SCLK → GPIO19
 *            UART → UART2 (GPIO16=TX, GPIO17=RX)
 *
 * @note    重构后（2026-06-05）：缓冲区为全局变量（定义在 hlw8112_protocol.c），
 *          HW层 RxPoll 直接操作 g_hlw811x_rx_buf / g_hlw811x_rx_idx / g_hlw811x_rx_done。
 * @note    ESP-IDF 使用 FreeRTOS，延时用 vTaskDelay()。
 * @version V4.0 适配重构架构
 * @date    2026-06-05
 */

#include "hlw8112_hw.h"
/* #include "hlw8112_protocol.h"       /* g_hlw811x_rx_buf, g_hlw811x_rx_len, g_hlw811x_rx_idx, g_hlw811x_rx_done */

/*============================================================
 * ESP-IDF 平台头文件
 *============================================================*/
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

/*============================================================
 * ESP32 本地 GPIO 引脚定义
 *============================================================*/
#define HLW811X_PIN_EN      GPIO_NUM_5
#define HLW811X_PIN_CS      GPIO_NUM_18
#define HLW811X_PIN_SCLK    GPIO_NUM_19

#define HLW811X_UART_NUM    UART_NUM_2
#define HLW811X_UART_TXD    GPIO_NUM_16
#define HLW811X_UART_RXD    GPIO_NUM_17

#define HLW811X_UART_BAUDRATE   9600

/*============================================================
 * GPIO 控制函数实现
 *============================================================*/

void HLW811X_HW_SetEN(u8 level)
{
    gpio_set_level(HLW811X_PIN_EN, (level != 0) ? 1 : 0);
}

void HLW811X_HW_SetCS(u8 level)
{
    gpio_set_level(HLW811X_PIN_CS, (level != 0) ? 1 : 0);
}

void HLW811X_HW_SetSCLK(u8 level)
{
    gpio_set_level(HLW811X_PIN_SCLK, (level != 0) ? 1 : 0);
}

/*============================================================
 * 延时函数实现
 *============================================================*/

void HLW811X_HW_DelayMs(u16 ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

/*============================================================
 * 调试输出函数 (ESP32 平台)
 *============================================================*/

#if HLW811X_LOG_ENABLE

void HLW811X_HW_Print(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    esp_log_writev(ESP_LOG_INFO, "HLW811X", fmt, args);
    va_end(args);
}

/* #endif /* HLW811X_LOG_ENABLE */

/*============================================================
 * 硬件初始化
 *============================================================*/

void HLW811X_HW_Init(void)
{
    /***** GPIO 初始化 *****/
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << HLW811X_PIN_EN) |
                        (1ULL << HLW811X_PIN_CS) |
                        (1ULL << HLW811X_PIN_SCLK),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

/*     /* 初始化引脚默认电平 */
    gpio_set_level(HLW811X_PIN_EN,   0);
    gpio_set_level(HLW811X_PIN_CS,   0);
    gpio_set_level(HLW811X_PIN_SCLK, 1);

    /***** UART 初始化 *****/
    uart_config_t uart_conf = {
        .baud_rate  = HLW811X_UART_BAUDRATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_EVEN,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(HLW811X_UART_NUM, &uart_conf);
    uart_set_pin(HLW811X_UART_NUM, HLW811X_UART_TXD, HLW811X_UART_RXD,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(HLW811X_UART_NUM, 256, 256, 0, NULL, 0);
}

/*============================================================
 * UART 数据收发
 *============================================================*/

void HLW811X_HW_SendData(const u8 *data, u16 len)
{
    uart_write_bytes(HLW811X_UART_NUM, (const char *)data, len);
    uart_wait_tx_done(HLW811X_UART_NUM, pdMS_TO_TICKS(100));
}

void HLW811X_HW_ClearRxBuf(void)
{
/*     /* 清空 UART 硬件 FIFO，软件缓冲区由协议层 HLW811X_Proto_ClearRxBuf() 管理 */
    uart_flush_input(HLW811X_UART_NUM);
}

/*============================================================
 * ESP32 UART 接收轮询 — 使用协议层接口
 *============================================================*/

u8 HLW811X_HW_IsTxBusy(void)
{
/*     /* ESP32 uart_write_bytes 是阻塞的，所以 tx 不会忙 */
    return 0;
}

void HLW811X_HW_UART_RxPoll(void)
{
    int len;
    uart_get_buffered_data_len(HLW811X_UART_NUM, (size_t *)&len);

    while (len > 0 && g_hlw811x_rx_idx < g_hlw811x_rx_len)
    {
        u8 byte;
        if (uart_read_bytes(HLW811X_UART_NUM, &byte, 1, 0) > 0)
        {
            g_hlw811x_rx_buf[g_hlw811x_rx_idx] = byte;
            g_hlw811x_rx_idx++;
            g_hlw811x_rx_ing = 1;

            if (g_hlw811x_rx_idx >= g_hlw811x_rx_len)
            {
                g_hlw811x_rx_done = 1;
                break;
            }
        }
        len--;
    }
}

/*============================================================
 * 发送超时处理
 *============================================================*/

void HLW811X_HW_TxTimeoutIRQ(void)
{
/*     /* ESP32 使用 uart_write_bytes 阻塞发送，无需超时管理 */
}
