/**
 * @file    project_param.c
 * @brief   系统参数管理 — 统一结构体 + 主/备双区 + CRC32
 */
#include "project_param.h"
#include "bsp_flash.h"
#include "mod_storage.h"
#include "stm32f4xx_hal.h"
#include "comm_logger.h"
#include <string.h>

/*====================================================================
 * 内部变量
 *====================================================================*/
static sys_param_t s_param;

/* 默认参数 */
static const sys_param_t s_default = {
    .magic = PARAM_MAGIC,
    .version = PARAM_VERSION,
    .bat_ovp_mv = 29200,
    .bat_uvp_mv = 24000,
    .otp_deg = 60,
    .low_soc = 10, /* 与 battery_config.h 实际生效口径一致 (低电量报警阈值) */
    .inv_ovp_v = 260,
    .inv_ocp_a = 30,
    .ac_in_present_v = 180,
    .ac_in_absent_v = 50,
};

/*====================================================================
 * CRC32 (多项式 0xEDB88320, 与旧 config_t 兼容)
 *====================================================================*/

/**
 * @brief 计算参数结构体CRC32，跳过crc32字段自身。
 * @param p 待校验的参数结构体有效指针。
 * @return 参数结构体的CRC32校验值。
 */
static uint32_t project_param_crc(const sys_param_t *p)
{
    /* crc32 字段偏移 = offsetof(sys_param_t, crc32) = 8
     * CRC 覆盖: [magic, version] + [crc32 之后到末尾] */
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *base = (const uint8_t *)p;

    /* 前 8 字节 (magic + version) */
    for (uint32_t i = 0; i < 8; i++)
    {
        crc ^= base[i];
        for (uint8_t b = 0; b < 8; b++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
        }
    }
    /* crc32 字段之后 */
    uint32_t skip = 8u + sizeof(uint32_t); /* 跳过 magic+version+crc32 */
    for (uint32_t i = skip; i < sizeof(sys_param_t); i++)
    {
        crc ^= base[i];
        for (uint8_t b = 0; b < 8; b++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
        }
    }
    return ~crc;
}

/**
 * @brief 读取指定Flash槽位并检查魔数和CRC。
 * @param addr 参数槽位的Flash起始地址。
 * @param out 接收槽位内容的有效缓冲区指针。
 * @return true：读取和校验通过；false：读取失败或数据无效。
 */
static bool project_param_slot_valid(uint32_t addr, sys_param_t *out)
{
    if (!bsp_flash_read(addr, out, sizeof(sys_param_t)))
    {
        return false;
    }
    if (out->magic != PARAM_MAGIC)
    {
        return false;
    }
    if (project_param_crc(out) != out->crc32)
    {
        return false;
    }
    return true;
}

static bool project_param_backup_valid(sys_param_t *out)
{
    if (!out)
    {
        return false;
    }
    if (mod_storage_param_backup_read((uint8_t *)out, sizeof(sys_param_t)) != 0)
    {
        return false;
    }
    if (out->magic != PARAM_MAGIC)
    {
        return false;
    }
    if (project_param_crc(out) != out->crc32)
    {
        return false;
    }
    return true;
}

/*====================================================================
 * API
 *====================================================================*/

/**
 * @brief 按主区、备份区、默认值的优先级加载系统参数。
 */
void param_init(void)
{
    if (!bsp_flash_init(PARAM_FLASH_BASE, PARAM_FLASH_SIZE, FLASH_SECTOR_11))
    {
        logger_out(LOG_ERROR, "param: flash init failed");
    }

    sys_param_t tmp;

    /* 1. 尝试 primary */
    if (project_param_slot_valid(PARAM_PRIMARY_ADDR, &tmp))
    {
        s_param = tmp;
        logger_out(LOG_INFO, "param: primary OK (v%lu)", (unsigned long)s_param.version);
        return;
    }

    /* 2. 尝试 backup */
    if (project_param_backup_valid(&tmp))
    {
        s_param = tmp;
        logger_out(LOG_WARN, "param: primary BAD, external backup OK");
        /* 回写 primary */
        param_store();
        return;
    }

    /* 3. 均失败 → 默认值 */
    logger_out(LOG_WARN, "param: both BAD, load defaults");
    s_param = s_default;
    s_param.crc32 = project_param_crc(&s_param);
    param_store();
}

/**
 * @brief 获取内存中系统参数的只读指针。
 * @return 指向模块静态参数的非空指针。
 */
const sys_param_t *param_get(void)
{
    return &s_param;
}

/**
 * @brief 更新CRC并擦除参数扇区，依次写入主区和备份区。
 * @return true：两区写入成功；false：擦除或任一次写入失败。
 */
bool param_store(void)
{
    /* 计算 CRC (确保最新) */
    s_param.magic = PARAM_MAGIC;
    s_param.version = PARAM_VERSION;
    s_param.crc32 = project_param_crc(&s_param);

    /* 1. 先写外部 backup (W25Q128 扇区 0); 失败不阻断 primary */
    if (mod_storage_param_backup_write((const uint8_t *)&s_param, sizeof(s_param)) != 0)
    {
        logger_out(LOG_WARN, "param: external backup write failed");
    }

    /* 2. 擦除内部 Sector 11 */
    if (!bsp_flash_erase())
    {
        logger_out(LOG_ERROR, "param: erase failed");
        return false;
    }

    /* 3. 写 primary (总是最新) */
    if (!bsp_flash_write(PARAM_PRIMARY_ADDR, &s_param, sizeof(s_param)))
    {
        logger_out(LOG_ERROR, "param: write primary failed");
        return false;
    }

    return true;
}
