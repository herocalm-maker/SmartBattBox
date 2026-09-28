/**
 * @file    drv_w25q128.c
 * @brief   W25Q128 外部 SPI Flash 驱动 (Driver 层)
 *
 * 功能:
 *   - SPI2 接口, 16MB (128Mbit) 容量
 *   - 支持读/写/扇区擦除/JEDEC ID 读取/Power-down 唤醒
 *   - 页编程自动处理 256 字节页对齐
 *
 * 注意:
 *   - init 使用 bsp_delay_ms (基于 DWT_CYCCNT, 不依赖 uwTick)
 *   - wait_busy 使用 HAL_GetTick 轮询状态寄存器
 */
#include "drv_w25q128.h"
#include "bsp_spi.h"
#include "bsp_io.h"
#include "bsp_tim.h"     /* bsp_delay_ms (不依赖 uwTick) */
#include "Board.h"
#include "cmsis_os2.h"








/*********************************************************************
 * @fn      _cs_low
 * @brief   拉低 CS 片选 (激活 W25Q128 SPI 通讯)
 * @return  none
********************************************************************/
static void _cs_low(void)
{
    bsp_gpio_write(BOARD_FLASH_CS_PORT, BOARD_FLASH_CS_PIN, GPIO_PIN_RESET);
}

/*********************************************************************
 * @fn      _cs_high
 * @brief   拉高 CS 片选 (释放 W25Q128 SPI 通讯)
 * @return  none
********************************************************************/
static void _cs_high(void)
{
    bsp_gpio_write(BOARD_FLASH_CS_PORT, BOARD_FLASH_CS_PIN, GPIO_PIN_SET);
}

/*********************************************************************
 * @fn      _spi_xchg
 * @brief   SPI2 全双工交换 1 字节
 * @param   tx  发送字节
 * @return  uint8_t 接收字节
********************************************************************/
static bool spi_failed;
static uint8_t _spi_xchg(uint8_t tx)
{
    uint8_t rx=0xFF;
    if(spi_failed) return rx;
    if(!bsp_spi_xchg_checked(BSP_SPI_2,tx,&rx)) spi_failed=true;
    return rx;
}

/*********************************************************************
 * @fn      _send_cmd
 * @brief   发送单字节命令 (拉低 CS → 发命令 → 拉高 CS)
 * @param   cmd  命令字节
 * @return  none
********************************************************************/
static void _send_cmd(uint8_t cmd)
{
    _cs_low();
    _spi_xchg(cmd);
    _cs_high();
}

/*********************************************************************
 * @fn      drv_w25q128_init
 * @brief   初始化 W25Q128: SPI2 + GPIO + 退出 Power-down + JEDEC ID 校验
 * @return  bool true=ID 匹配 (期望 0xEF4018), false=无响应或 ID 错误
 * @note    使用 bsp_delay_ms 而非 HAL_Delay (基于 DWT_CYCCNT, 不依赖 uwTick)
********************************************************************/
bool drv_w25q128_init(void)
{
    spi_failed=false;
    /* 初始化 SPI2 (通过 BSP 枚举索引, 不接触 HAL 句柄) */
    bsp_spi_config_t spi_cfg = {0};
    spi_cfg.baudrate_prescaler = BOARD_SPI_BAUDRATE_PRESCALER;
    spi_cfg.cpol               = BOARD_SPI_CPOL;
    spi_cfg.cpha               = BOARD_SPI_CPHA;
    spi_cfg.data_size          = BOARD_SPI_DATASIZE;
    spi_cfg.first_bit          = SPI_FIRSTBIT_MSB;
    if (!bsp_spi_init(BSP_SPI_2, &spi_cfg)) {
        return false;
    }

    /* 配置控制引脚: CS=高(释放) / WP=高(写允许) / RST=高(释放) */
    bsp_gpio_init(BOARD_FLASH_CS_PORT, BOARD_FLASH_CS_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,  GPIO_PIN_SET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_FLASH_WP_PORT, BOARD_FLASH_WP_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL,  GPIO_PIN_SET, GPIO_SPEED_FREQ_LOW);
    bsp_gpio_init(BOARD_FLASH_RST_PORT, BOARD_FLASH_RST_PIN, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_PIN_SET, GPIO_SPEED_FREQ_LOW);

    /* 释放复位并解除写保护 */
    bsp_gpio_write(BOARD_FLASH_RST_PORT, BOARD_FLASH_RST_PIN, GPIO_PIN_SET);
    bsp_gpio_write(BOARD_FLASH_WP_PORT, BOARD_FLASH_WP_PIN, GPIO_PIN_SET);
    bsp_delay_ms(1);     /* 等待 tVSL=3us (min), 留余量 */
    _send_cmd(W25_CMD_WAKE);  /* 退出 Power-down (命令本身需 tRES1=3us) */
    bsp_delay_ms(3);     /* 等待 tRES1 完成 */

    /* 校验 JEDEC ID: 读取 0x9F 返回 3 字节 (制造商+型号+容量) */
    uint32_t id = drv_w25q128_read_id();
    return (id == BOARD_W25Q128_EXPECTED_ID);
}

/*********************************************************************
 * @fn      drv_w25q128_read_id
 * @brief   读取 JEDEC 器件 ID (命令 0x9F, 返回 3 字节)
 * @return  uint32_t 器件 ID (期望 0xEF4018)
********************************************************************/
uint32_t drv_w25q128_read_id(void)
{
    spi_failed=false;
    uint32_t id = 0;
    _cs_low();
    _spi_xchg(W25_CMD_READ_ID);
    id  = ((uint32_t)_spi_xchg(0x00)) << 16;
    id |= ((uint32_t)_spi_xchg(0x00)) << 8;
    id |=  (uint32_t)_spi_xchg(0x00);
    _cs_high();
    return spi_failed?0:id;
}

/*********************************************************************
 * @fn      drv_w25q128_wait_busy
 * @brief   轮询状态寄存器 BUSY 位, 等待 Flash 空闲
 * @param   timeout_ms  超时时间 ms
 * @return  bool true=空闲 false=超时
********************************************************************/
bool drv_w25q128_wait_busy(uint32_t timeout_ms)
{
    uint32_t tick = HAL_GetTick();
    uint8_t sts;
    do {
        if(spi_failed) return false;
        if(osKernelGetState()==osKernelRunning) osDelay(1);
        _cs_low();
        _spi_xchg(W25_CMD_READ_STATUS);
        sts = _spi_xchg(0x00);
        _cs_high();
        if(spi_failed) return false;
        if ((sts & 0x01) == 0) {
            return true;
        }
    } while ((HAL_GetTick() - tick) < timeout_ms);
    return false;
}

/*********************************************************************
 * @fn      drv_w25q128_read
 * @brief   从指定地址连续读取数据 (命令 0x03)
 * @param   addr  起始地址 (24bit)
 * @param   buf   输出缓冲区
 * @param   len   读取长度
 * @return  bool true=成功 false=参数越界或缓冲区为空
********************************************************************/
bool drv_w25q128_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    spi_failed=false;
    if (buf == NULL || addr > W25Q128_SIZE_BYTES || len > W25Q128_SIZE_BYTES-addr) {
        return false;
    }
    _cs_low();
    _spi_xchg(W25_CMD_READ);
    _spi_xchg((addr >> 16) & 0xFF);
    _spi_xchg((addr >> 8)  & 0xFF);
    _spi_xchg( addr        & 0xFF);
    for (uint32_t i = 0; i < len; i++) buf[i] = _spi_xchg(0x00);
    _cs_high();
    return !spi_failed;
}

/*********************************************************************
 * @fn      drv_w25q128_erase_sector
 * @brief   擦除指定 4KB 扇区 (命令 0x20)
 * @param   sec_idx  扇区索引 (0 ~ W25Q128_SECTOR_COUNT-1)
 * @return  bool true=成功 false=索引越界或超时
********************************************************************/
bool drv_w25q128_erase_sector(uint32_t sec_idx)
{
    spi_failed=false;
    if (sec_idx >= W25Q128_SECTOR_COUNT) {
        return false;
    }
    uint32_t addr = sec_idx * W25Q128_SECTOR_SIZE;
    _send_cmd(W25_CMD_WRITE_ENABLE);
    _cs_low();
    _spi_xchg(W25_CMD_SECTOR_ERASE);
    _spi_xchg((addr >> 16) & 0xFF);
    _spi_xchg((addr >> 8)  & 0xFF);
    _spi_xchg( addr        & 0xFF);
    _cs_high();
    return drv_w25q128_wait_busy(500);   /* 扇区擦除最大 400ms */
}

/*********************************************************************
 * @fn      drv_w25q128_write
 * @brief   向指定地址写入数据 (自动处理 256 字节页编程对齐)
 * @param   addr  起始地址
 * @param   buf   数据缓冲区
 * @param   len   写入长度
 * @return  bool true=成功 false=参数越界或编程超时
********************************************************************/
bool drv_w25q128_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    spi_failed=false;
    if (buf == NULL || addr > W25Q128_SIZE_BYTES || len > W25Q128_SIZE_BYTES-addr) {
        return false;
    }
    uint32_t remaining = len;
    uint32_t cur = addr;
    const uint8_t *p = buf;

    while (remaining > 0) {
        /* 计算当前页剩余可写空间 */
        uint32_t page_off = cur % W25Q128_PAGE_SIZE;
        uint32_t chunk = W25Q128_PAGE_SIZE - page_off;
        if (chunk > remaining) {
            chunk = remaining;
        }

        _send_cmd(W25_CMD_WRITE_ENABLE);
        _cs_low();
        _spi_xchg(W25_CMD_PAGE_PROGRAM);
        _spi_xchg((cur >> 16) & 0xFF);
        _spi_xchg((cur >> 8)  & 0xFF);
        _spi_xchg( cur        & 0xFF);
        for (uint32_t i = 0; i < chunk; i++) _spi_xchg(*p++);
        _cs_high();

        if (!drv_w25q128_wait_busy(100)) {
            return false;
        }

        cur       += chunk;
        remaining -= chunk;
    }
    return !spi_failed;
}
