/**
 * @file    drv_dwin.c
 * @brief   迪文 T5L 串口屏驱动 (Driver 层)
 *
 * 硬件: UART4 (PA0/PA1, 115200, 经 SP3232E 转 RS232)
 * 协议: DGUS II — 帧头 5A A5 + 长度 + 指令 + 数据
 *
 * 常用指令:
 *   写 VP:  5A A5 <LEN> 82 <VP_H> <VP_L> <DATA...>
 *   读 VP:  5A A5 04 83 <VP_H> <VP_L> <LEN>
 *
 * 数据流 (参考 drv_hlw8110):
 *   TX: bsp_uart_send(port, frame)  → DWIN 屏
 *   RX: DWIN 屏 → UART RX ISR → comm_ringbuf_push → drv_dwin_read 读取
 */
#include "drv_dwin.h"
#include "Board.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

static dwin_t s_dwin;           /* 单屏单实例 */
static bool   s_ready = false;   /* init 完成标志 */

/*====================================================================
 * 内部: UART RX 回调 (ISR 上下文)
 *====================================================================*/

/**
 * @brief  UART RX 回调 (ISR 上下文)
 * @note   由 bsp_uart IDLE 中断触发, 将接收数据推入环形缓冲.
 *         单生产者 (ISR) / 单消费者 (任务), 不关中断.
 * @param  port  UART 端口号
 * @param  buf   接收数据指针
 * @param  len   接收数据长度
 */
static void dwin_rx_cb(bsp_uart_port_t port, uint8_t *buf, uint16_t len)
{
    if (!s_ready || port != s_dwin.port)
    {
        return;
    }
    comm_ringbuf_push(&s_dwin.rx_ring, buf, len);
}

/*********************************************************************
 * @fn      dwin_send_frame
 * @brief   发送 DGUS 帧 (帧头 5A A5 + LEN + DATA)
 * @param   data_len  数据字节数 (不含帧头)
 * @param   data      数据缓冲区 (指令+数据)
 * @return  bool true=发送成功 false=未初始化或缓冲溢出
********************************************************************/
static bool dwin_send_frame(uint8_t data_len, const uint8_t *data)
{
    if (!s_ready)
    {
        return false;
    }
    if (data_len + 3 > DWIN_TX_MAX)
    {
        return false;
    }

    uint8_t frame[DWIN_TX_MAX];
    frame[0] = DWIN_FRAME_HEAD1;
    frame[1] = DWIN_FRAME_HEAD2;
    frame[2] = data_len;
    if (data && data_len > 0)
    {
        memcpy(&frame[3], data, data_len);
    }
    return bsp_uart_send(s_dwin.port, frame, (uint16_t)(data_len + 3u), DWIN_TIMEOUT_MS);
}

/*********************************************************************
 * @fn      dwin_write_vp
 * @brief   写 VP 寄存器 (指令 82)
 * @param   vp_addr   VP 地址 (16bit)
 * @param   data      数据缓冲区
 * @param   data_len  数据长度
 * @return  none
********************************************************************/
static void dwin_write_vp(uint16_t vp_addr, const uint8_t *data, uint16_t data_len)
{
    uint16_t cmd_len = data_len + 3u;
    uint8_t cmd[DWIN_TX_MAX];
    cmd[0] = DWIN_CMD_WRITE;
    cmd[1] = (uint8_t)(vp_addr >> 8);
    cmd[2] = (uint8_t)(vp_addr & 0xFFu);
    if (data && data_len > 0)
    {
        memcpy(&cmd[3], data, data_len);
    }
    dwin_send_frame(cmd_len, cmd);
}

/*====================================================================
 * API
 *====================================================================*/

/*********************************************************************
 * @fn      drv_dwin_init
 * @brief   初始化串口屏: 绑定 UART 端口 (115200 8N1) → 背光 80% → boot 页
 * @param   port  UART 端口 (通常 BSP_UART_4)
 * @return  bool true=成功 false=参数非法或 UART 初始化失败
********************************************************************/
bool drv_dwin_init(bsp_uart_port_t port)
{
    if (port >= BSP_UART_MAX)
    {
        return false;
    }

    memset(&s_dwin, 0, sizeof(s_dwin));
    s_dwin.port     = port;
    s_dwin.cur_page = (dwin_page_enum)0xFFu;  /* 无效值, 强制首次 set_page 发出命令 */

    comm_ringbuf_init(&s_dwin.rx_ring, s_dwin.ring_storage, DWIN_RING_BUF_SIZE);

    bsp_uart_config_t cfg = {
        .baudrate    = BOARD_UART_BAUD_DISPLAY,
        .data_bits   = BSP_UART_DATA_8B,
        .parity      = BSP_UART_PARITY_NONE,
        .stop_bits   = BSP_UART_STOPBITS_1,
        .rx_cb       = dwin_rx_cb,
        .rx_buf      = s_dwin.uart_rx_buf,
        .rx_buf_size = DWIN_RX_BUF_SIZE,
    };
    if (!bsp_uart_init(port, &cfg))
    {
        return false;
    }
    s_ready = true;

    drv_dwin_set_brightness(80);
    //drv_dwin_set_page(DWIN_PAGE_BOOT);
    return true;
}

/*********************************************************************
 * @fn      drv_dwin_deinit
 * @brief   反初始化: 释放 UART 端口, 清空实例状态
 * @return  none
********************************************************************/
void drv_dwin_deinit(void)
{
    if (!s_ready)
    {
        return;
    }
    s_ready = false;
    bsp_uart_deinit(s_dwin.port);
    memset(&s_dwin, 0, sizeof(s_dwin));
}

/*********************************************************************
 * @fn      drv_dwin_read
 * @brief   读取触摸/寄存器回传数据 (从环形缓冲弹出)
 * @param   buf  输出缓冲区
 * @param  len  最大读取长度
 * @return  uint16_t 实际读取字节数 (无数据返回 0)
********************************************************************/
uint16_t drv_dwin_read(uint8_t *buf, uint16_t len)
{
    if (!s_ready || buf == NULL || len == 0)
    {
        return 0;
    }
    return comm_ringbuf_pop(&s_dwin.rx_ring, buf, len);
}

/*--------------------------------------------------------------------
 * 页面控制
 *--------------------------------------------------------------------*/

/*********************************************************************
 * @fn      drv_dwin_set_page
 * @brief   切换页面 (DGUS 页面切换指令: 5A A5 07 82 0084 5A01 00<page_id>)
 * @param   page  目标页面
 * @return  none
********************************************************************/
void drv_dwin_set_page(dwin_page_enum page)
{
    if (page == s_dwin.cur_page)
    {
        return;
    }
    s_dwin.cur_page = page;
    uint8_t data[4] = { 0x5A, 0x01, 0x00, (uint8_t)page };
    dwin_write_vp(DWIN_VP_PAGE, data, 4u);
}

/*********************************************************************
 * @fn      drv_dwin_get_page
 * @brief   获取当前页面
 * @return  dwin_page_enum 当前页面
********************************************************************/
dwin_page_enum drv_dwin_get_page(void)
{
    return s_dwin.cur_page;
}

/*--------------------------------------------------------------------
 * 文本显示 (VP 地址 = DWIN_VP_TEXT_BASE + offset, 发送 ASCII 字节)
 *--------------------------------------------------------------------*/

/*--------------------------------------------------------------------
 * 文本写入残留防护
 * DWIN 文本控件不自动清尾: 写 "123MIN" 后再写 "12MIN" 会残留上次尾
 * 字符 (屏显 "12MINN")。记录各 VP 上次写入长度, 每次按 max(上次,本次)
 * 长度格式化写入 (尾部补空格), 一次帧完成无残留。
 *--------------------------------------------------------------------*/
#define DWIN_TEXT_SLOTS 8u   /* 最多同时跟踪的文本控件数 */

typedef struct
{
    uint16_t vp;   /* 文本控件 VP */
    uint16_t len;  /* 上次写入的文本长度 (不含补位空格) */
} dwin_text_last_t;

static dwin_text_last_t s_text_last[DWIN_TEXT_SLOTS];

static uint16_t dwin_text_get_last(uint16_t vp_addr)
{
    for (uint8_t i = 0; i < DWIN_TEXT_SLOTS; i++)
    {
        if (s_text_last[i].vp == vp_addr)
        {
            return s_text_last[i].len;
        }
    }
    return 0;
}

static void dwin_text_set_last(uint16_t vp_addr, uint16_t len)
{
    for (uint8_t i = 0; i < DWIN_TEXT_SLOTS; i++)
    {
        if (s_text_last[i].vp == vp_addr || s_text_last[i].vp == 0)
        {
            s_text_last[i].vp  = vp_addr;
            s_text_last[i].len = len;
            return;
        }
    }
}

/*********************************************************************
 * @fn      drv_dwin_set_text
 * @brief   在指定文本控件显示字符串
 * @param   vp_addr   VP 完整地址
 * @param   text       文本内容 (NUL 结束)
 * @return  none
 * @note    按长度格式化写入: 补齐到 max(上次长度, 本次长度), 尾部补
 *          空格一次写入, 防止短串残留上次尾字符 (如 "12MINN");
 *          空文本写全空格清空控件区。
********************************************************************/
void drv_dwin_set_text(uint16_t vp_addr, const char *text)
{
    if (!text)
    {
        return;
    }
    uint16_t len = (uint16_t)strlen(text);
    if (len > DWIN_TX_MAX - 4)
    {
        len = DWIN_TX_MAX - 4;
    }
    uint16_t last_len = dwin_text_get_last(vp_addr);
    uint16_t fill = (last_len > len) ? last_len : len;
    uint8_t buf[DWIN_TX_MAX - 4];
    if (len > 0)
    {
        memcpy(buf, text, len);
    }
    if (fill > len)
    {
        memset(buf + len, ' ', (size_t)(fill - len)); /* 尾部空格覆盖残留区 */
    }
    dwin_write_vp(vp_addr, buf, fill);
    dwin_text_set_last(vp_addr, len);
}

/*********************************************************************
 * @fn      drv_dwin_set_text_format
 * @brief   格式化文本显示 (printf 风格)
 * @param   vp_addr   VP 完整地址
 * @param   fmt        格式化字符串
 * @param   ...        可变参数
 * @return  none
********************************************************************/
void drv_dwin_set_text_format(uint16_t vp_addr, const char *fmt, ...)
{
    if (!fmt)
    {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf((char *)s_dwin.tx_buf, sizeof(s_dwin.tx_buf), fmt, ap);
    va_end(ap);
    if (n > 0)
    {
        drv_dwin_set_text(vp_addr, (const char *)s_dwin.tx_buf);
    }
}

/*--------------------------------------------------------------------
 * 数值显示 (vp_addr 为完整 VP 地址, 发送 2 字节大端整数)
 *--------------------------------------------------------------------*/

/*********************************************************************
 * @fn      drv_dwin_set_value
 * @brief   设置有符号 16bit 数值 (大端)
 * @param   vp_addr   VP 完整地址
 * @param   val        数值
 * @return  none
********************************************************************/
void drv_dwin_set_value(uint16_t vp_addr, int16_t val)
{
    uint8_t data[2] = { (uint8_t)((uint16_t)val >> 8),
                        (uint8_t)((uint16_t)val & 0xFFu) };
    dwin_write_vp(vp_addr, data, 2u);
}

/*********************************************************************
 * @fn      drv_dwin_set_value_u16
 * @brief   设置无符号 16bit 数值 (大端)
 * @param   vp_addr   VP 完整地址
 * @param   val        数值
 * @return  none
********************************************************************/
void drv_dwin_set_value_u16(uint16_t vp_addr, uint16_t val)
{
    uint8_t data[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFFu) };
    dwin_write_vp(vp_addr, data, 2u);
}

/*--------------------------------------------------------------------
 * 背光
 *--------------------------------------------------------------------*/

/*********************************************************************
 * @fn      drv_dwin_set_brightness
 * @brief   设置背光亮度 (VP 0082)
 * @param   percent  0~100
 * @return  none
********************************************************************/
void drv_dwin_set_brightness(uint8_t percent)
{
    if (percent > 100u)
    {
        percent = 100u;
    }
    uint8_t data[2] = { 0x00, percent };
    dwin_write_vp(0x0082u, data, 2u);
}

/*--------------------------------------------------------------------
 * 原始接口
 *--------------------------------------------------------------------*/

/*********************************************************************
 * @fn      drv_dwin_write_vp
 * @brief   直接写 VP 寄存器 (供扩展控件使用)
 * @param   vp_addr  VP 地址 (16bit)
 * @param   data     数据
 * @param   len      数据长度
 * @return  none
********************************************************************/
void drv_dwin_write_vp(uint16_t vp_addr, const uint8_t *data, uint16_t len)
{
    dwin_write_vp(vp_addr, data, len);
}

/*********************************************************************
 * @fn      drv_dwin_send_end
 * @brief   发送 DGUS 帧结束标记 FF FF FF
 * @note    部分 DGUS 控件 (如曲线) 需要此标记表示帧结束
 * @return  none
********************************************************************/
void drv_dwin_send_end(void)
{
    if (!s_ready)
    {
        return;
    }
    static const uint8_t end_marker[] = { 0xFFu, 0xFFu, 0xFFu };
    bsp_uart_send(s_dwin.port, end_marker, 3u, 20u);
}

