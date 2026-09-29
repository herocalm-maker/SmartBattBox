/**
 * @file   drv_pb03f.c
 * @brief   PB-03F BLE 模块驱动 (Driver 层)
 *
 * 硬件:  UART6 (PC6/PC7, 115200) + WAKEUP (PA2)
 * 协议:  AT 指令集 + 透传模式
 * 参考:  Doc/pb系列模块_alimesh_at指令手册v0.1.pdf
 *
 * 框架参考 LoRa 驱动 (AppLora.c):
 *   - at_send_command() 发送指令 + 超时等待 + 响应前缀匹配
 *   - URC 回调表 + at_oob_match() 按行匹配异步事件
 *   - RX 数据经 bsp_uart RX 回调 → comm_ringbuf 在 ISR 与任务间传递
 */
#include "drv_pb03f.h"
#include "bsp_uart.h"
#include "bsp_io.h"
#include "comm_str.h"
#include "comm_ringbuf.h"
#include "Board.h"
#include "cmsis_os2.h"
#include <string.h>
#include <stdio.h>

/*--------------------------------------------------------------------
 * 内部常量
 *--------------------------------------------------------------------*/
#define URC_TABLE_MAX              8u      /* URC 回调表容量 */
#define EXIT_TP_SILENCE_MS         1000u   /* 退出透传 +++ 前后静默时间 */
#define PB03F_RING_BUF_SIZE        1024    /* 本地接收队列 */

/*--------------------------------------------------------------------
 * 静态变量
 *--------------------------------------------------------------------*/

static ble_info_t s_ble;                         /* 模块实例 */

/* 本地接收环形队列 (ISR→任务, 与 hlw8110 同样的架构) */
static comm_ringbuf_t s_rx_ring;
static uint8_t  s_ring_storage[PB03F_RING_BUF_SIZE] = {0};

/* DMA RX 缓冲区 (由 BSP IDLE 中断回调推入 s_rx_ring) */
static uint8_t  s_dma_rx_buf[300] = {0};

/*--------------------------------------------------------------------
 * URC 回调声明
 *--------------------------------------------------------------------*/
static int _urc_connected(ble_info_t *info, uint8_t *data, uint16_t len);
static int _urc_disconnected(ble_info_t *info, uint8_t *data, uint16_t len);
static int _urc_recv_data(ble_info_t *info, uint8_t *data, uint16_t len);

/* URC 回调表 (前缀 + 回调函数) */
static const ble_urc_item_t s_urc_table[URC_TABLE_MAX] = {
    { "+EVENT:BLE_CONNECTED",    _urc_connected },
    { "+EVENT:BLE_DISCONNECT",   _urc_disconnected },
    { "+DATA=",                  _urc_recv_data },
};

/*====================================================================
 * URC 回调实现
 *====================================================================*/

/* 蓝牙已连接: 自动切换透传模式 */
static int _urc_connected(ble_info_t *info, uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
    info->conn_state = BLE_STATE_CONNECTED;
    info->mode = BLE_MODE_TRANSPARENT;
    return 1;
}

/* 蓝牙已断开: 回到 AT 模式 */
static int _urc_disconnected(ble_info_t *info, uint8_t *data, uint16_t len)
{
    (void)data; (void)len;
    info->conn_state = BLE_STATE_DISCONNECTED;
    info->mode = BLE_MODE_AT;
    return 1;
}

/* 透传数据到达: +DATA=<len>,<data> */
static int _urc_recv_data(ble_info_t *info, uint8_t *data, uint16_t len)
{
    (void)info;
    const char *prefix = "+DATA=";
    uint16_t plen = (uint16_t)strlen(prefix);
    uint8_t *p = data + plen;

    /* 解析长度字段 */
    uint16_t data_len = 0;
    while (p < data + len && *p >= '0' && *p <= '9') {
        data_len = data_len * 10u + (uint16_t)(*p - '0');
        p++;
    }
    if (*p != ',' || data_len == 0)
    {
        return 0;
    }
    p++;

    /* 拷贝数据 */
    if (data_len > BLE_RX_BUF_SIZE)
    {
        data_len = BLE_RX_BUF_SIZE;
    }
    uint16_t remain = (uint16_t)(data + len - p);
    if (data_len > remain)
    {
        data_len = remain;
    }

    memcpy(s_ble.rx_buf, p, data_len);
    s_ble.rx_len = data_len;
    s_ble.rx_ready = true;
    if (s_ble.rx_sem)
    {
        osSemaphoreRelease(s_ble.rx_sem);
    }
    return 1;
}

/*====================================================================
 * URC 匹配分发 (参考 LoRa at_oob_match)
 *
 * 按 \r\n 行分割缓冲区, 逐行与 URC 表前缀匹配, 命中则调用回调.
 * 在多行响应中, 同一缓冲区可能触发多个 URC.
 *====================================================================*/
static int _at_oob_match(uint8_t *data, uint16_t len)
{
    int matched = 0;
    uint8_t item_num = 0;

    /* 统计有效表项 */
    for (uint8_t i = 0; i < URC_TABLE_MAX; i++) {
        if (s_urc_table[i].prefix != NULL) {
            item_num++;
        }
    }

    uint8_t *p = data;
    uint16_t remain = len;
    if (len < 2)
    {
        return 0;
    }

    while (p - data <= (int32_t)len - 2) {
        for (uint8_t i = 0; i < item_num; i++) {
            const ble_urc_item_t *oob = &s_urc_table[i];
            if (!oob->cb_func || !oob->prefix)
            {
                continue;
            }
            if (!comm_str_startswith(p, remain, oob->prefix))
            {
                continue;
            }
            matched += oob->cb_func(&s_ble, p, remain);
            p += strlen(oob->prefix);
            break;
        }
        /* 跳到下一行 */
        uint8_t *next = (uint8_t *)strstr((const char *)p, "\r\n");
        if (!next)
        {
            break;
        }
        p = next + 2;
        remain = len - (uint16_t)(p - data);
    }
    return matched;
}

/*====================================================================
 * BSP UART RX 回调 (ISR 上下文)
 *
 * bsp_uart IDLE 中断触发 → 将数据推入本地环形队列 → 通知驱动
 *====================================================================*/
void drv_pb03f_bsp_rx_cb(bsp_uart_port_t port, uint8_t *buff, uint16_t len)
{
    (void)port;
    comm_ringbuf_push(&s_rx_ring, buff, len);
    drv_pb03f_on_rx_done(len);
}

/*====================================================================
 * AT 指令引擎 (核心)
 *
 * 发送 AT 指令后轮询环形缓冲, 匹配响应前缀.
 * cmd=NULL 时仅读取已有数据 (用于纯 URC 查询).
 * prefix=NULL 时有数据即返回成功.
 *====================================================================*/
static at_result_enum _at_send_command(const char *cmd,
                                        const char *prefix,
                                        uint8_t *resp, uint16_t resp_max,
                                        uint32_t timeout)
{
    /* 1) 发送指令 */
    if (cmd && cmd[0]) 
    {
        uint16_t clen = (uint16_t)strlen(cmd);
        if (!bsp_uart_send(BSP_UART_6, (const uint8_t *)cmd, clen, 100u))
        {
            return AT_RESULT_FAIL;
        }
    }

    if (!prefix && !resp) 
    {
        return AT_RESULT_OK;
    }

    /* 2) 清空 AT 响应缓冲, 排空残留信号量 */
    memset(s_ble.at_resp, 0, sizeof(s_ble.at_resp));
    s_ble.at_resp_len = 0;
    s_ble.at_resp_done = false;
    while (s_ble.at_sem && osSemaphoreAcquire(s_ble.at_sem, 0) == osOK) 
    {

    }

    /* 3) 轮询等待 */
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout) 
    {
        osDelay(10);

        uint8_t tmp[128];
        uint16_t n = comm_ringbuf_pop(&s_rx_ring, tmp, sizeof(tmp));
        if (n == 0) 
        {
          continue;
        }

        /* 追加到 AT 响应缓冲 */
        uint16_t copy = n;
        if (s_ble.at_resp_len + copy > BLE_AT_RESP_SIZE)
        {
            copy = BLE_AT_RESP_SIZE - s_ble.at_resp_len;
        }
        memcpy(&s_ble.at_resp[s_ble.at_resp_len], tmp, copy);
        s_ble.at_resp_len += copy;

        /* 先处理 URC (不影响 AT 响应匹配) */
        _at_oob_match(s_ble.at_resp, s_ble.at_resp_len);

        /* 检查前缀匹配 */
        if (prefix)
        {
            if (strstr((const char *)s_ble.at_resp, prefix)) 
            {
                s_ble.at_resp_done = true;
                if (s_ble.at_sem) 
                {
                    osSemaphoreRelease(s_ble.at_sem);
                }
                if (resp && resp_max > 0) 
                {
                    uint16_t rlen = s_ble.at_resp_len;
                    if (rlen > resp_max)
                    {
                        rlen = resp_max;
                    }
                    memcpy(resp, s_ble.at_resp, rlen);
                }
                return AT_RESULT_OK;
            }
        } 
        else 
        {
            if (s_ble.at_resp_len > 0)
            {
                if (resp && resp_max > 0) 
                {
                    uint16_t rlen = s_ble.at_resp_len;
                    if (rlen > resp_max) 
                    {
                        rlen = resp_max;
                    }
                    memcpy(resp, s_ble.at_resp, rlen);
                }
                return AT_RESULT_OK;
            }
        }
    }
    return AT_RESULT_TIMEOUT;
}

/*====================================================================
 * API: 生命周期
 *====================================================================*/

/*********************************************************************
 * @fn      drv_pb03f_init
 * @brief   初始化 PB-03F 模块
 * @note    拉低 WAKEUP, 创建信号量, 初始化 BSP UART + 环形缓冲,
 *         注册 BSP RX 回调. 调度器启动前/后均可调用.
 ********************************************************************/
void drv_pb03f_init(void)
{
    memset(&s_ble, 0, sizeof(s_ble));
    s_ble.mode = BLE_MODE_AT;
    s_ble.conn_state = BLE_STATE_DISCONNECTED;

    /* 配置蓝牙唤醒引脚 PA2 为推挽输出, 初值低(待机) */
    bsp_gpio_init(BOARD_BLE_WKUP_PORT, BOARD_BLE_WKUP_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_PIN_RESET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_write(BOARD_BLE_WKUP_PORT, BOARD_BLE_WKUP_PIN, GPIO_PIN_RESET);

    /*没有多任务调用,暂时不用*/
    /*
    if (!s_ble.rx_sem) {
        s_ble.rx_sem = osSemaphoreNew(1, 0, NULL);
    }
    if (!s_ble.at_sem) {
        s_ble.at_sem = osSemaphoreNew(1, 0, NULL);
    }
    */
    
    /* 初始化本地接收队列 */
    comm_ringbuf_init(&s_rx_ring, s_ring_storage, sizeof(s_ring_storage));

    /* UART6 初始化 (DMA+IDLE), 注册 BSP RX 回调 */
    {
        bsp_uart_config_t uart_cfg = {0};
        uart_cfg.baudrate    = BOARD_UART_BAUD_BLE;
        uart_cfg.rx_cb       = drv_pb03f_bsp_rx_cb;
        uart_cfg.rx_buf      = s_dma_rx_buf;
        uart_cfg.rx_buf_size = sizeof(s_dma_rx_buf);
        bsp_uart_init(BSP_UART_6, &uart_cfg);
    }
}

/*********************************************************************
 * @fn      drv_pb03f_deinit
 * @brief   去初始化: 拉低 WAKEUP, 清空缓冲和状态
 ********************************************************************/
void drv_pb03f_deinit(void)
{
    bsp_gpio_write(BOARD_BLE_WKUP_PORT, BOARD_BLE_WKUP_PIN, GPIO_PIN_RESET);
    bsp_uart_deinit(BSP_UART_6);
    memset(&s_ble, 0, sizeof(s_ble));
}

/*====================================================================
 * API: 电源/唤醒
 *====================================================================*/

/*********************************************************************
 * @fn      drv_pb03f_wakeup
 * @brief   拉高 WAKEUP 唤醒模组 (5ms 稳定)
 ********************************************************************/
void drv_pb03f_wakeup(void)
{
    bsp_gpio_write(BOARD_BLE_WKUP_PORT, BOARD_BLE_WKUP_PIN, GPIO_PIN_SET);
    osDelay(5);
}

/*********************************************************************
 * @fn      drv_pb03f_sleep
 * @brief   拉低 WAKEUP 使模组进入待机
 ********************************************************************/
void drv_pb03f_sleep(void)
{
    bsp_gpio_write(BOARD_BLE_WKUP_PORT, BOARD_BLE_WKUP_PIN, GPIO_PIN_RESET);
}

/*====================================================================
 * API: 模式与状态
 *====================================================================*/

/*********************************************************************
 * @fn      drv_pb03f_get_mode
 * @brief   查询当前工作模式 (AT / 透传)
 ********************************************************************/
ble_mode_enum drv_pb03f_get_mode(void)
{
    return s_ble.mode;
}

/*********************************************************************
 * @fn      drv_pb03f_get_conn_state
 * @brief   查询连接状态 (基于 URC 事件, 非实时查询)
 ********************************************************************/
ble_state_enum drv_pb03f_get_conn_state(void)
{
    return s_ble.conn_state;
}

/*====================================================================
 * API: 模式切换
 *====================================================================*/

/*********************************************************************
 * @fn      drv_pb03f_enter_transparent
 * @brief   进入透传模式 (仅切换软件标志, 需蓝牙已连接)
 ********************************************************************/
bool drv_pb03f_enter_transparent(void)
{
    s_ble.mode = BLE_MODE_TRANSPARENT;
    s_ble.rx_len = 0;
    s_ble.rx_ready = false;
    return true;
}

/*********************************************************************
 * @fn      drv_pb03f_exit_transparent
 * @brief   退出透传模式回到 AT 模式
 * @note    前后静默 1s 后发送 "+++", 模组返回 "OK" 后切换标志
 ********************************************************************/
bool drv_pb03f_exit_transparent(void)
{
    if (s_ble.mode != BLE_MODE_TRANSPARENT)
    {
        return true;
    }
    osDelay(EXIT_TP_SILENCE_MS);
    if (!bsp_uart_send(BSP_UART_6, (const uint8_t *)"+++", 3u, 100u))
    {
        return false;
    }
    osDelay(EXIT_TP_SILENCE_MS);
    s_ble.mode = BLE_MODE_AT;
    return true;
}

/*====================================================================
 * API: 数据收发
 *====================================================================*/

/*********************************************************************
 * @fn      drv_pb03f_send
 * @brief   透传模式发送字节流
 ********************************************************************/
bool drv_pb03f_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0)
    {
        return false;
    }
    return bsp_uart_send(BSP_UART_6, data, len, 100u);
}

/*********************************************************************
 * @fn      drv_pb03f_start_rx
 * @brief   清空 RX 缓冲, 准备接收下一帧
 ********************************************************************/
void drv_pb03f_start_rx(void)
{
    s_ble.rx_len = 0;
    s_ble.rx_ready = false;
}

/*********************************************************************
 * @fn      drv_pb03f_take_rx
 * @brief   取出已收到的透传数据 (从 s_rx_ring 弹出)
 * @param   out    输出缓冲
 * @param   maxlen 最大长度
 * @return  实际拷贝的字节数
 ********************************************************************/
uint16_t drv_pb03f_take_rx(uint8_t *out, uint16_t maxlen)
{
    if (!out || maxlen == 0)
    {
        return 0;
    }
    return comm_ringbuf_pop(&s_rx_ring, out, maxlen);
}

/*********************************************************************
 * @fn      drv_pb03f_wait_rx
 * @brief   阻塞等待透传数据 (信号量, 1s 超时通常用于周期上报)
 ********************************************************************/
bool drv_pb03f_wait_rx(uint32_t timeout_ms)
{
    if (!s_ble.rx_sem)
    {
        return false;
    }
    return osSemaphoreAcquire(s_ble.rx_sem, timeout_ms) == osOK;
}

/*********************************************************************
 * @fn      drv_pb03f_wait_at_resp
 * @brief   阻塞等待 AT 响应完成
 ********************************************************************/
bool drv_pb03f_wait_at_resp(uint32_t timeout_ms)
{
    if (!s_ble.at_sem)
    {
        return false;
    }
    return osSemaphoreAcquire(s_ble.at_sem, timeout_ms) == osOK;
}

/*====================================================================
 * API: RX 回调 (透传 / AT 分流)
 *====================================================================*/

/*********************************************************************
 * @fn      drv_pb03f_on_rx_done
 * @brief   RX 通知 (由 BSP RX 回调间接调用)
 * @note    数据已由 drv_pb03f_bsp_rx_cb 推入 s_rx_ring.
 *         此处仅通知消费端: 透传模式释放 rx_sem, AT 模式释放 at_sem.
 ********************************************************************/
void drv_pb03f_on_rx_done(uint16_t len)
{
    if (s_ble.mode != BLE_MODE_TRANSPARENT)
    {
        drv_pb03f_at_on_rx_event(len);
        return;
    }
    /* 透传模式: 通知消费者从 s_rx_ring 取数据 */
    if (s_ble.rx_sem)
    {
        osSemaphoreRelease(s_ble.rx_sem);
    }
}

/*********************************************************************
 * @fn      drv_pb03f_at_on_rx_event
 * @brief   AT 模式 RX 通知: 释放 at_sem 唤醒 _at_send_command
 ********************************************************************/
void drv_pb03f_at_on_rx_event(uint16_t len)
{
    (void)len;
    if (s_ble.at_sem)
    {
        osSemaphoreRelease(s_ble.at_sem);
    }
}

/*====================================================================
 * API: 包类型判断
 *====================================================================*/

/*********************************************************************
 * @fn      drv_pb03f_pkt_type
 * @brief   判断接收到的数据包类型
 * @return  BLE_PKT_ALIGENIE (包含 aliGenie_data JSON) / BLE_PKT_RAW
 ********************************************************************/
ble_pkt_type_enum drv_pb03f_pkt_type(const uint8_t *buf, uint16_t len)
{
    if (!buf || len == 0)
    {
        return BLE_PKT_NONE;
    }
    return comm_str_find(buf, len, "aliGenie_data") >= 0
           ? BLE_PKT_ALIGENIE : BLE_PKT_RAW;
}

/*********************************************************************
 * @fn      drv_pb03f_extract_aligenie
 * @brief   提取 aliGenie_data 字段值 (去除外层双引号)
 * @param   buf    原始数据
 * @param   len    数据长度
 * @param   out    输出缓冲
 * @param   maxlen 输出缓冲最大长度
 * @return  提取到的字节数, 0=失败
 ********************************************************************/
uint16_t drv_pb03f_extract_aligenie(const uint8_t *buf, uint16_t len,
                                    char *out, uint16_t maxlen)
{
    if (!buf || !out || maxlen == 0)
    {
        return 0;
    }

    int16_t idx = comm_str_find(buf, len, "aliGenie_data");
    if (idx < 0)
    {
        return 0;
    }

    uint16_t p = (uint16_t)idx + (uint16_t)strlen("aliGenie_data");
    /* 跳过空白和冒号 */
    while (p < len && (buf[p] == ' ' || buf[p] == '\t')) p++;
    if (p >= len || buf[p] != ':')
    {
        return 0;
    }
    p++;
    while (p < len && (buf[p] == ' ' || buf[p] == '\t')) p++;
    /* 期望 '"' */
    if (p >= len || buf[p] != '"')
    {
        return 0;
    }
    p++;

    uint16_t out_idx = 0;
    while (p < len && buf[p] != '"' && buf[p] != '\r' && buf[p] != '\n') {
        if (out_idx + 1u >= maxlen)
        {
            break;
        }
        out[out_idx++] = (char)buf[p++];
    }
    out[out_idx] = '\0';
    return out_idx;
}

/*====================================================================
 * API: AT 指令 - 基础
 *====================================================================*/
/*********************************************************************
 * @fn      drv_pb03f_at_test
 * @brief   AT 通讯测试, 发送 "AT\r\n" 等待 "OK"
 ********************************************************************/
bool drv_pb03f_at_test(void)
{
    return _at_send_command("AT\r\n", "OK", NULL, 0, BLE_AT_TIMEOUT_MS) == AT_RESULT_OK;
}

/*********************************************************************
 * @fn      drv_pb03f_reset
 * @brief   软重启模块, 等待 3s
 ********************************************************************/
bool drv_pb03f_reset(void)
{
    return _at_send_command("AT+RST\r\n", "OK", NULL, 0, 3000u) == AT_RESULT_OK;
}

/*********************************************************************
 * @fn      drv_pb03f_restore
 * @brief   恢复出厂设置, 等待 3s
 ********************************************************************/
bool drv_pb03f_restore(void)
{
    return _at_send_command("AT+RESTORE\r\n", "OK", NULL, 0, 3000u) == AT_RESULT_OK;
}

/*====================================================================
 * API: AT 指令 - 查询
 *====================================================================*/

/*********************************************************************
 * @fn      drv_pb03f_get_version
 * @brief   查询固件版本, 提取 "+VERSION:" 后的值
 ********************************************************************/
bool drv_pb03f_get_version(char *out, uint16_t maxlen)
{
    if (!out || maxlen == 0)
    {
        return false;
    }
    if (_at_send_command("AT+GMR\r\n", "+VERSION:", NULL, 0, BLE_AT_TIMEOUT_MS) != AT_RESULT_OK)
    {
        return false;
    }
    return comm_str_extract(s_ble.at_resp, s_ble.at_resp_len, "+VERSION:", out, maxlen);
}

/*********************************************************************
 * @fn      drv_pb03f_get_name
 * @brief   查询蓝牙名称, 提取 "+NAME:" 后的值
 ********************************************************************/
bool drv_pb03f_get_name(char *out, uint16_t maxlen)
{
    if (!out || maxlen == 0)
    {
        return false;
    }
    if (_at_send_command("AT+NAME?\r\n", "+NAME:", NULL, 0, BLE_AT_TIMEOUT_MS) != AT_RESULT_OK)
    {
        return false;
    }
    return comm_str_extract(s_ble.at_resp, s_ble.at_resp_len, "+NAME:", out, maxlen);
}

/*********************************************************************
 * @fn      drv_pb03f_get_mac
 * @brief   查询 MAC 地址, 提取 "+MAC:" 后的值
 ********************************************************************/
bool drv_pb03f_get_mac(char *out, uint16_t maxlen)
{
    if (!out || maxlen < (BLE_MAC_STR_LEN + 1))
    {
        return false;
    }
    if (_at_send_command("AT+MAC?\r\n", "+MAC:", NULL, 0, BLE_AT_TIMEOUT_MS) != AT_RESULT_OK)
    {
        return false;
    }
    return comm_str_extract(s_ble.at_resp, s_ble.at_resp_len, "+MAC:", out, maxlen);
}

/*********************************************************************
 * @fn      drv_pb03f_get_state
 * @brief   查询蓝牙连接状态
 * @note    发送 AT+STATE?, 响应 +STATE:0=空闲 / +STATE:1=已连接
 ********************************************************************/
bool drv_pb03f_get_state(ble_state_enum *out)
{
    if (!out)
    {
        return false;
    }
    if (_at_send_command("AT+STATE?\r\n", "+STATE:", NULL, 0, BLE_AT_TIMEOUT_MS) != AT_RESULT_OK)
    {
        return false;
    }
    char val[8] = {0};
    if (!comm_str_extract(s_ble.at_resp, s_ble.at_resp_len, "+STATE:", val, sizeof(val) - 1u))
    {
        return false;
    }
    if (val[0] == '0')
    {
        *out = BLE_STATE_DISCONNECTED;
        return true;
    }
    if (val[0] == '1')
    {
        *out = BLE_STATE_CONNECTED;
        return true;
    }
    return false;
}

/*====================================================================
 * API: AT 指令 - 设置
 *====================================================================*/

/*********************************************************************
 * @fn      drv_pb03f_set_name
 * @brief   设置蓝牙广播名称
 ********************************************************************/
bool drv_pb03f_set_name(const char *name)
{
    if (!name)
    {
        return false;
    }
    int n = snprintf(s_ble.cmd_buf, sizeof(s_ble.cmd_buf), "AT+NAME=%s\r\n", name);
    if (n <= 0 || n >= (int)sizeof(s_ble.cmd_buf))
    {
        return false;
    }
    return _at_send_command(s_ble.cmd_buf, "OK", NULL, 0, BLE_AT_TIMEOUT_MS) == AT_RESULT_OK;
}

/*********************************************************************
 * @fn      drv_pb03f_set_uart_baud
 * @brief   设置 UART 波特率 (重启后生效)
 ********************************************************************/
bool drv_pb03f_set_uart_baud(uint32_t baud)
{
    int n = snprintf(s_ble.cmd_buf, sizeof(s_ble.cmd_buf), "AT+UART=%u\r\n", (unsigned int)baud);
    if (n <= 0 || n >= (int)sizeof(s_ble.cmd_buf))
    {
        return false;
    }
    return _at_send_command(s_ble.cmd_buf, "OK", NULL, 0, BLE_AT_TIMEOUT_MS) == AT_RESULT_OK;
}

/*********************************************************************
 * @fn      drv_pb03f_set_aligenie
 * @brief   设置天猫精灵三元组 (PID, MAC, Secret)
 ********************************************************************/
bool drv_pb03f_set_aligenie(const char *pid, const char *mac, const char *secret)
{
    if (!pid || !mac || !secret)
    {
        return false;
    }
    int n = snprintf(s_ble.cmd_buf, sizeof(s_ble.cmd_buf),
                     "AT+AliGenie=%s,%s,%s\r\n", pid, mac, secret);
    if (n <= 0 || n >= (int)sizeof(s_ble.cmd_buf))
    {
        return false;
    }
    return _at_send_command(s_ble.cmd_buf, "OK", NULL, 0, BLE_AT_TIMEOUT_MS) == AT_RESULT_OK;
}

/*====================================================================
 * API: 调试
 *====================================================================*/

/*********************************************************************
 * @fn      drv_pb03f_take_at_resp
 * @brief   取出最近一条 AT 响应原始数据 (调试用, 取后清空)
 ********************************************************************/
uint16_t drv_pb03f_take_at_resp(uint8_t *out, uint16_t maxlen)
{
    uint16_t n = s_ble.at_resp_len;
    if (n == 0 || !out)
    {
        return 0;
    }
    if (n > maxlen)
    {
        n = maxlen;
    }
    memcpy(out, s_ble.at_resp, n);
    s_ble.at_resp_len = 0;
    return n;
}
