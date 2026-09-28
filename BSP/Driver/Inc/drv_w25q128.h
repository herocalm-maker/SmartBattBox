/**
 * @file    drv_w25q128.h
  * @brief   W25Q128 外部 SPI Flash 驱动 (Driver 层)
 *
 * 原理图对应：U1 W25Q128JVSIQ
 *   SPI2_SCK  = PB13, SPI2_MISO = PB14, SPI2_MOSI = PB15
 *   CS        = PB10, WP         = PB11, RST       = PB12
 *
  *
 */
#ifndef __DRV_W25Q128_H__
#define __DRV_W25Q128_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "Board.h"           /* 板级硬件参数单一来源 */


#define W25Q128_SIZE_BYTES      BOARD_W25Q128_SIZE_BYTES     /* 16MB, Board.h */
#define W25Q128_SECTOR_SIZE     BOARD_W25Q128_SECTOR_SIZE    /* 4KB, Board.h */
#define W25Q128_SECTOR_COUNT    (W25Q128_SIZE_BYTES / W25Q128_SECTOR_SIZE)
#define W25Q128_PAGE_SIZE       BOARD_W25Q128_PAGE_SIZE      /* 256B, Board.h */

/* 常用指令 */
#define W25_CMD_READ            0x03
#define W25_CMD_FAST_READ       0x0B
#define W25_CMD_PAGE_PROGRAM    0x02
#define W25_CMD_SECTOR_ERASE    0x20
#define W25_CMD_CHIP_ERASE      0xC7
#define W25_CMD_WRITE_ENABLE    0x06
#define W25_CMD_READ_STATUS     0x05
#define W25_CMD_READ_ID         0x9F
#define W25_CMD_POWER_DOWN      0xB9
#define W25_CMD_WAKE            0xAB

/* 初始化 W25Q128, 退出 Power-down 并校验 JEDEC ID (0xEF4018).
 * @return bool true=ID 匹配, false=无响应或 ID 错误 */
extern bool     drv_w25q128_init(void);

/* 读取 W25Q128 器件 ID., uint32_t 器件 ID(期望 0xEF4018) */
extern uint32_t drv_w25q128_read_id(void);

/* 从指定地址读取数据, @retval bool true=成功 false=失败 */
extern bool drv_w25q128_read (uint32_t addr, uint8_t *buf, uint32_t len);

/* 向指定地址写入数据 */
/* 向指定地址写入数据(含页对齐处理)., bool true=成功 false=失败 */
extern bool drv_w25q128_write(uint32_t addr, const uint8_t *buf, uint32_t len);

/* 擦除指定扇区, @retval bool true=成功 false=失败 */
extern bool drv_w25q128_erase_sector(uint32_t sec_idx);

/* 等待 Flash 空闲, @retval bool true=空闲 false=超时 */
extern bool drv_w25q128_wait_busy(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
#endif /* __DRV_W25Q128_H__ */
