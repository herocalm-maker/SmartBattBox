/**
 * @file    project_param.h
 * @brief   系统参数管理 — 统一结构体 + 内部主区 + 外部备份 + CRC32 校验
 *
 * 布局:
 *   primary: 内部 Flash Sector 11 (0x080E0000, 128KB 窗口)
 *   backup:  外部 W25Q128 扇区 0 (mod_storage, 4KB)
 *
 * 加载策略: primary CRC OK → 用 primary;
 *           否则 try 外部 backup → 用 backup;
 *           均失败 → 写默认值到 primary + 外部 backup.
 * 存储策略: 先写外部 backup → 擦除内部 Sector 11 → 写 primary.
 */
#ifndef __PROJECT_PARAM_H__
#define __PROJECT_PARAM_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdint.h>

/*====================================================================
 * Flash 地址定义 (Sector 11)
 *====================================================================*/
#define PARAM_FLASH_BASE  0x080E0000u /* primary 扇区基址 (Sector 11) */
#define PARAM_FLASH_SIZE  0x00020000u /* 窗口大小 128KB */
#define PARAM_PRIMARY_ADDR PARAM_FLASH_BASE /* primary 起始地址 */

/*====================================================================
 * 统一参数结构体
 *====================================================================*/
#define PARAM_MAGIC 0x534D5042u /* "SMPB" */
#define PARAM_VERSION 2u        /* 版本递增, 用于迁移 */

typedef struct
{
    uint32_t magic;   /* PARAM_MAGIC */
    uint32_t version; /* PARAM_VERSION */
    uint32_t crc32;   /* CRC32 (不含本字段, 即前 8 字节 + 后续数据) */

    /* ---- 电池保护 ---- */
    int16_t bat_ovp_mv; /* 电池过压保护 (mV)      默认 29200 */
    int16_t bat_uvp_mv; /* 电池欠压保护 (mV)      默认 24000 */
    int16_t otp_deg;    /* 过温保护 (°C)          默认 60    */
    int16_t low_soc;    /* 低电量阈值 (%)         默认 20    */

    /* ---- 逆变保护 ---- */
    int16_t inv_ovp_v; /* 逆变过压保护 (V)       默认 260   */
    int16_t inv_ocp_a; /* 逆变过流保护 (A)       默认 30    */

    /* ---- AC 检测 ---- */
    int16_t ac_in_present_v; /* AC 接入判定电压 (V)    默认 180   */
    int16_t ac_in_absent_v;  /* AC 断开判定电压 (V)    默认 50    */

    uint8_t reserved[16]; /* 预留扩展 (填 0) */
} sys_param_t;

/*====================================================================
 * API
 *====================================================================*/

/**
 * @brief 按主区、备份区、默认值的优先级加载系统参数。
 */
extern void param_init(void);

/**
 * @brief 获取内存中系统参数的只读指针。
 * @return 指向模块静态参数的非空指针。
 */
extern const sys_param_t *param_get(void);

/**
 * @brief 更新CRC并擦除参数扇区，依次写入主区和备份区。
 * @return true：两区写入成功；false：擦除或任一次写入失败。
 */
extern bool param_store(void);

#ifdef __cplusplus
}
#endif
#endif /* __PROJECT_PARAM_H__ */
