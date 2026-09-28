/**
 * @file    bsp_flash.h
 * @brief   MCU 内部 Flash 读写擦除 (BSP 层)
 *
 * 通用扇区级访问: 由调用方在 bsp_flash_init 指定 base/size/sector.
 *   read:  内部 Flash 内存映射直接读
 *   write: 编程只能 1→0, 写前必须擦除 (bsp_flash_erase)
 *   erase: 擦除 bsp_flash_init 指定的整个扇区
 *
 * 参数分区 (primary, Sector 11 0x080E0000) 布局与备份策略由上层 (project_param) 定义.
 */
#ifndef __BSP_FLASH_H__
#define __BSP_FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief   初始化内部 Flash 访问窗口 (记录 base/size/sector)
 * @param   base    扇区起始地址 (须扇区对齐)
 * @param   size    可用窗口大小, 字节
 * @param   sector  HAL FLASH_Sector 枚举值, 用于擦除
 * @return  true=成功
 */
bool bsp_flash_init(uint32_t base, uint32_t size, uint32_t sector);

/**
 * @brief   从窗口内读取数据 (内存映射直接读)
 * @param   addr  目标地址 ([base, base+size) 内)
 * @param   buf   输出缓冲区
 * @param   len   读取长度
 * @return  true=成功
 */
bool bsp_flash_read(uint32_t addr, void *buf, uint16_t len);

/**
 * @brief   向窗口内写入数据 (word 编程, 只能 1→0)
 * @note    不做擦除; 调用前需 bsp_flash_erase()
 * @param   addr  目标地址 ([base, base+size) 内)
 * @param   buf   数据缓冲区
 * @param   len   写入长度
 * @return  true=成功
 */
bool bsp_flash_write(uint32_t addr, const void *buf, uint16_t len);

/**
 * @brief   擦除 bsp_flash_init 指定的整个扇区
 * @warning 擦除后扇区内全部为 0xFF
 * @return  true=成功
 */
bool bsp_flash_erase(void);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_FLASH_H__ */
