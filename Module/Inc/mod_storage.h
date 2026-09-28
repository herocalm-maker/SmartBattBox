/**
 * @file    mod_storage.h
  * @brief   存储模块 (Module 层)
 *
  *
 */
#ifndef __MOD_STORAGE_H__
#define __MOD_STORAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>



/* 模块状态枚举 */
typedef enum {
    STORAGE_STATE_IDLE,
    STORAGE_STATE_INIT,
    STORAGE_STATE_RUNNING,
    STORAGE_STATE_ERROR
} storage_state_enum;

/*====================================================================
 * W25Q128 Flash 分区布局 (扇区索引, 每扇区 4KB)
 *   注: 标定区 (calib) 占用扇区 255 (0x0FF000), 本模块避开该区域
 *====================================================================*/
#define STORAGE_SEC_BATTERY_START 9u /* 0x009000..0x00AFFF, 2 x 4KiB */
#define STORAGE_SEC_BATTERY_COUNT 2u
int mod_storage_battery_read(void *,uint32_t,uint8_t *,size_t);
int mod_storage_battery_program(void *,uint32_t,const uint8_t *,size_t);
int mod_storage_battery_erase(void *,unsigned);

#define STORAGE_SEC_CONFIG        0u     /* 配置区: 扇区 0 (0x000000), 4KB */
#define STORAGE_SEC_LOG_START     1u     /* 日志区起始扇区: 扇区 1 (0x001000) */
#define STORAGE_SEC_LOG_CNT       8u     /* 日志区扇区数: 8 (32KB 环形缓冲) */

/* 初始化 Flash 存储模块, 创建互斥锁并初始化 W25Q128 */
extern void mod_storage_init(void);


#ifdef __cplusplus
}
#endif
#endif /* __MOD_STORAGE_H__ */
