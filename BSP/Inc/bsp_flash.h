/**
 * @file    bsp_flash.h
 * @brief   MCU 内部 Flash 读写擦除 (BSP 层)
 *
 * STM32F407VGT6 内部 Flash 1MB (0x08000000–0x080FFFFF):
 *   Bootloader  0x08000000  20KB
 *   App         0x08005000  860KB
 *   PARAM       0x080E0000  128KB (Sector 11 整扇区)
 *
 * 本层只提供 PARAM 扇区 (Sector 11) 范围内的通用读/写/擦除;
 * 参数分区布局 (config/calib 槽位、备份策略、CRC) 由上层定义.
 *
 * 写入约定: Flash 编程只能 1→0, 写前必须先擦除 (bsp_flash_erase);
 *           擦除会清空整个 PARAM 扇区, 上层需自行备份/写回其他槽位.
 */
#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* ---- PARAM 扇区 (Sector 11, 128KB) ---- */
#define BSP_FLASH_PARAM_BASE    0x080E0000u
#define BSP_FLASH_PARAM_SIZE    0x00020000u
#define BSP_FLASH_PARAM_SECTOR  FLASH_SECTOR_11

/**
 * @brief   内部 Flash 初始化
 * @note    校验 PARAM 扇区地址合法性 (范围/扇区对齐)
 * @return  true=成功
 */
bool bsp_flash_init(void);

/**
 * @brief   从 PARAM 扇区读取数据 (内存映射直接读)
 * @param   addr  目标地址 ([BASE, BASE+SIZE) 内)
 * @param   buf   输出缓冲区
 * @param   len   读取长度
 * @return  true=成功
 */
bool bsp_flash_read(uint32_t addr, void *buf, uint16_t len);

/**
 * @brief   向 PARAM 扇区写入数据 (word 编程, 只能 1→0)
 * @note    不做擦除; 调用前需 bsp_flash_erase(), 否则残留 1 位导致数据错误
 * @param   addr  目标地址 ([BASE, BASE+SIZE) 内, 建议偶数长度对齐)
 * @param   buf   数据缓冲区
 * @param   len   写入长度
 * @return  true=成功
 */
bool bsp_flash_write(uint32_t addr, const void *buf, uint16_t len);

/**
 * @brief   擦除整个 PARAM 扇区 (Sector 11)
 * @warning 擦除后扇区内全部为 0xFF, 其他槽位数据需上层提前备份
 * @return  true=成功
 */
bool bsp_flash_erase(void);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_FLASH_H__ */
