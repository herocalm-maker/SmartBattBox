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
 * 内部状态: 由 bsp_flash_init 设置 (地址窗口 + 扇区)
 *--------------------------------------------------------------------*/
static uint32_t s_base;   /* Flash 窗口起始地址 */
static uint32_t s_size;   /* Flash 窗口大小 (字节) */
static uint32_t s_sector; /* 待擦除的扇区号 */

/*--------------------------------------------------------------------
 * 内部: 校验地址范围落在 PARAM 扇区内
 *--------------------------------------------------------------------*/
/**
 * @fn      bsp_flash_addr_valid
 * @brief   校验 [addr, addr+len) 是否完整落在初始化时设定的 Flash 窗口内
 * @param   addr  起始地址
 * @param   len   长度 (字节)
 * @return  bool true=范围合法
 * @note    读/写前统一经此检查, 防止越界改写窗口外的 Bootloader/App 区.
 */
static bool bsp_flash_addr_valid(uint32_t addr, uint16_t len)
{
    return (addr >= s_base) &&
           (addr <  s_base + s_size) &&
           ((uint32_t)len <= s_base + s_size - addr);
}

/*====================================================================
 * 函数实现
 *====================================================================*/

/**
 * @fn      bsp_flash_init
 * @brief   登记 Flash 操作窗口 (基地址/大小/待擦扇区号), 供后续读写擦使用
 * @param   base    窗口起始物理地址
 * @param   size    窗口大小 (字节)
 * @param   sector  待擦除的扇区号 (擦除粒度为整扇区)
 * @return  bool true=成功 false=参数越界
 * @note    仅记录参数不触碰硬件; 分区布局与备份策略由上层负责.
 */
bool bsp_flash_init(uint32_t base, uint32_t size, uint32_t sector)
{
    /* 窗口须落在 MCU 内部 Flash 地址空间内 (0x08000000-0x080FFFFF) */
    if (base < 0x08000000u || base > 0x080FFFFFu || size == 0u)
    {
        return false;
    }
    s_base = base;
    s_size = size;
    s_sector = sector;
    return true;
}

/**
 * @fn      bsp_flash_read
 * @brief   从内部 Flash 窗口内读取 len 字节到 buf
 * @param   addr  起始地址 (须在窗口内)
 * @param   buf   目标缓冲区 (调用方保证容量)
 * @param   len   读取长度 (字节)
 * @return  bool true=成功 false=参数/范围非法
 * @note    内部 Flash 统一内存映射编址, 读操作无需解锁, 直接 memcpy 即可.
 */
bool bsp_flash_read(uint32_t addr, void *buf, uint16_t len)
{
    if (buf == NULL || len == 0 || !bsp_flash_addr_valid(addr, len))
    {
        return false;
    }
    memcpy(buf, (const void *)addr, len);
    return true;
}

/**
 * @fn      bsp_flash_write
 * @brief   向内部 Flash 窗口内编程写入 len 字节 (不含擦除)
 * @param   addr  起始地址 (须在窗口内, 且目标区域已擦除为 0xFF)
 * @param   buf   源数据缓冲区
 * @param   len   写入长度 (字节)
 * @return  bool true=成功 false=参数非法/编程失败
 * @note    F4 的 Flash 只能把已擦除的位写 0, 写前必须保证区域已擦除;
 *          中途失败立即重新上锁, 避免把 Flash 留在解锁状态.
 */
bool bsp_flash_write(uint32_t addr, const void *buf, uint16_t len)
{
    if (buf == NULL || len == 0 || !bsp_flash_addr_valid(addr, len))
    {
        return false;
    }

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

/**
 * @fn      bsp_flash_erase
 * @brief   整扇区擦除 init 时登记的扇区 (内容全部变为 0xFF)
 * @param   无
 * @return  bool true=成功 false=擦除失败
 * @note    VoltageRange_3 对应 2.7~3.6V 供电下的 x32 并行擦写; 扇区擦除耗时较长,
 *          调用方需评估擦除期间关中断/阻塞对实时任务的影响.
 */
bool bsp_flash_erase(void)
{
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,  /* 2.7-3.6V, x32 parallelism */
        .Sector       = s_sector,
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
