/**
 * @file    bsp_flash.c
 * @brief   MCU 内部 Flash 读写擦除 (BSP 层)
 *
 * 使用 HAL_FLASH API 对 PARAM 扇区 (Sector 11) 读写擦除.
 *   读: 内部 Flash 内存映射, 直接 memcpy.
 *   写: Unlock → Program (word) → Lock, 不含擦除.
 *   擦: Unlock → Erase Sector 11 → Lock.
 *
 * 分区布局与备份策略 (config/calib 槽位如何共存于同一扇区) 由上层负责.
 */

#include "bsp_flash.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/*--------------------------------------------------------------------
 * 内部: 校验地址范围落在 PARAM 扇区内
 *--------------------------------------------------------------------*/
static bool _addr_valid(uint32_t addr, uint16_t len)
{
    return (addr >= BSP_FLASH_PARAM_BASE) &&
           (addr <  BSP_FLASH_PARAM_BASE + BSP_FLASH_PARAM_SIZE) &&
           ((uint32_t)len <= BSP_FLASH_PARAM_BASE + BSP_FLASH_PARAM_SIZE - addr);
}

/*====================================================================
 * 函数实现
 *====================================================================*/

bool bsp_flash_init(void)
{
    /* PARAM 扇区基址须落在扇区 11 起始 (0x080E0000) */
    if (BSP_FLASH_PARAM_BASE != 0x080E0000u) return false;
    return true;
}

bool bsp_flash_read(uint32_t addr, void *buf, uint16_t len)
{
    if (buf == NULL || len == 0 || !_addr_valid(addr, len)) return false;
    memcpy(buf, (const void *)addr, len);
    return true;
}

bool bsp_flash_write(uint32_t addr, const void *buf, uint16_t len)
{
    if (buf == NULL || len == 0 || !_addr_valid(addr, len)) return false;

    const uint8_t *src = (const uint8_t *)buf;
    HAL_FLASH_Unlock();

    /* 按 4 字节 word 编程 */
    while (len >= 4) {
        uint32_t word;
        memcpy(&word, src, 4);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        src  += 4;
        addr += 4;
        len  -= 4;
    }
    /* 剩余字节 (不足 4 字节补 0xFF 写入, 保持尾部长度不收缩) */
    if (len > 0) {
        uint32_t word = 0xFFFFFFFFu;
        memcpy(&word, src, len);
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
    }

    HAL_FLASH_Lock();
    return true;
}

bool bsp_flash_erase(void)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,  /* 2.7-3.6V, x32 parallelism */
        .Sector       = BSP_FLASH_PARAM_SECTOR,
        .NbSectors    = 1,
    };
    uint32_t sector_err = 0;
    if (HAL_FLASHEx_Erase(&erase, &sector_err) != HAL_OK) {
        HAL_FLASH_Lock();
        return false;
    }

    HAL_FLASH_Lock();
    return true;
}
