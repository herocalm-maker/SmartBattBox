/**
 * @file    mod_storage.c
 * @brief   存储模块 — W25Q128 电池学习记录后端 (Module 层)
 *
 * 分区约定 (W25Q128, 每扇区 4KB):
 *   - 扇区 9/10  : 电池学习记录专用 (双扇区, 见 STORAGE_SEC_BATTERY_*)
 *   - 扇区 0     : 参数备份 (Config 区, 由 project_param 调用)
 *   - 扇区 1..8  : 日志区, 扇区 255: 标定区 — 本模块均不触碰
 * 互斥锁覆盖整个 NOR 操作; 后续其他 W25Q 使用者必须共享本 owner.
 */
#include "mod_storage.h"
#include "drv_w25q128.h"
#include "cmsis_os2.h"
#include <string.h>

/* W25Q128 扇区大小 (4KB) */
#define STORAGE_SECTOR_BYTES 4096u

/* 电池学习区总容量 (扇区数 x 扇区大小) */
#define BATTERY_AREA_BYTES (STORAGE_SEC_BATTERY_COUNT * STORAGE_SECTOR_BYTES)

/* 内部状态 */
static osMutexId_t storage_mutex;   /* 串行化 NOR 访问的互斥锁 */
static bool        flash_ready;     /* W25Q128 硬件探测是否成功 */

/**
 * @brief  初始化存储模块: 创建互斥锁.
 * @note   硬件探测延后到首次任务请求电池存储时进行 (见 acquire()).
 */
void mod_storage_init(void)
{
    if (!storage_mutex)
    {
        storage_mutex = osMutexNew(NULL);
    }
}

/**
 * @brief  获取 NOR 访问权并确保 Flash 就绪.
 * @note   首次访问时探测硬件; 探测失败或 ID 不符则释放互斥锁并返回失败.
 * @return true = 已持有互斥锁且 Flash 可用; false = 获取失败.
 */
static bool acquire(void)
{
    if (!storage_mutex || osMutexAcquire(storage_mutex, 1000) != osOK)
    {
        return false;
    }

    if (!flash_ready)
    {
        flash_ready = drv_w25q128_init();
    }
    if (!flash_ready || drv_w25q128_read_id() != BOARD_W25Q128_EXPECTED_ID)
    {
        flash_ready = false;
        osMutexRelease(storage_mutex);
        return false;
    }
    return true;
}

/**
 * @brief  校验电池学习区读写边界.
 * @param  o  区内部偏移
 * @param  n  请求长度
 * @return true = 偏移+长度在电池区范围内; false = 越界.
 */
static bool bounds(uint32_t o, size_t n)
{
    return o <= BATTERY_AREA_BYTES && n <= BATTERY_AREA_BYTES - o;
}

/**
 * @brief  读取电池学习记录数据.
 * @param  c  预留 (兼容接口, 未使用)
 * @param  o  电池区内部偏移
 * @param  b  输出缓冲
 * @param  n  读取长度
 * @return 0 = 成功; -1 = 参数非法/越界/Flash 不可用
 */
int mod_storage_battery_read(void *c, uint32_t o, uint8_t *b, size_t n)
{
    (void)c;
    if (!b || !bounds(o, n) || !acquire())
    {
        return -1;
    }
    /* 电池区内部偏移映射到 W25Q128 全局地址 (电池区从 STORAGE_SEC_BATTERY_START 扇区起) */
    bool ok = drv_w25q128_read(STORAGE_SECTOR_BYTES * STORAGE_SEC_BATTERY_START + o, b, (uint32_t)n);
    osMutexRelease(storage_mutex);
    return ok ? 0 : -1;
}

/**
 * @brief  写入电池学习记录数据 (写后回读校验).
 * @param  c  预留 (兼容接口, 未使用)
 * @param  o  电池区内部偏移
 * @param  b  源数据
 * @param  n  写入长度 (<=64 校验缓冲)
 * @return 0 = 成功 (含回读一致); -1 = 失败
 */
int mod_storage_battery_program(void *c, uint32_t o, const uint8_t *b, size_t n)
{
    uint8_t check[64];
    (void)c;
    if (!b || n > sizeof(check) || !bounds(o, n) || !acquire())
    {
        return -1;
    }
    bool ok = drv_w25q128_write(STORAGE_SECTOR_BYTES * STORAGE_SEC_BATTERY_START + o, b, (uint32_t)n);
    if (ok)
    {
        ok = drv_w25q128_read(STORAGE_SECTOR_BYTES * STORAGE_SEC_BATTERY_START + o, check, (uint32_t)n) &&
             !memcmp(b, check, n);
    }
    osMutexRelease(storage_mutex);
    return ok ? 0 : -1;
}

/**
 * @brief  擦除电池学习记录的一个扇区, 并校验全 0xFF.
 * @param  c       预留 (兼容接口, 未使用)
 * @param  sector  扇区索引 (相对 STORAGE_SEC_BATTERY_START)
 * @return 0 = 成功; -1 = 扇区越界/Flash 不可用/擦除校验失败
 */
int mod_storage_battery_erase(void *c, unsigned sector)
{
    uint8_t b[64];
    bool ok;
    (void)c;
    if (sector >= STORAGE_SEC_BATTERY_COUNT || !acquire())
    {
        return -1;
    }
    ok = drv_w25q128_erase_sector(STORAGE_SEC_BATTERY_START + sector);
    /* NOR 擦除后只能为全 0xFF, 分块回读逐字节验证擦除干净, 残留 1→0 位会导致记录校验失败 */
    for (unsigned o = 0; ok && o < STORAGE_SECTOR_BYTES; o += sizeof(b))
    {
        ok = drv_w25q128_read(STORAGE_SECTOR_BYTES * (STORAGE_SEC_BATTERY_START + sector) + o, b, sizeof(b));
        for (unsigned j = 0; ok && j < sizeof(b); j++)
        {
            if (b[j] != 0xFF)
            {
                ok = false;
            }
        }
    }
    osMutexRelease(storage_mutex);
    return ok ? 0 : -1;
}

/**
 * @brief  备份参数到外部 Flash (扇区 0, Config 区).
 * @param  data  待备份数据
 * @param  len   数据长度 (1..4096)
 * @return 0 = 成功; -1 = 参数非法/越界/Flash 不可用
 */
int mod_storage_param_backup_write(const uint8_t *data, size_t len)
{
    if (!data || len == 0 || len > STORAGE_SEC_CONFIG_SIZE || !acquire())
    {
        return -1;
    }
    /* 先整扇区擦除再写: NOR 物理特性只能把位 1 写成 0, 不擦除会残留旧数据 */
    bool ok = drv_w25q128_erase_sector(STORAGE_SECTOR_BYTES * STORAGE_SEC_CONFIG);
    if (ok)
    {
        ok = drv_w25q128_write(STORAGE_SECTOR_BYTES * STORAGE_SEC_CONFIG, data, (uint32_t)len);
    }
    osMutexRelease(storage_mutex);
    return ok ? 0 : -1;
}

/**
 * @brief  从外部 Flash 读回参数备份 (扇区 0, Config 区).
 * @param  out  输出缓冲
 * @param  len  读取长度 (1..4096)
 * @return 0 = 成功; -1 = 参数非法/越界/Flash 不可用
 */
int mod_storage_param_backup_read(uint8_t *out, size_t len)
{
    if (!out || len == 0 || len > STORAGE_SEC_CONFIG_SIZE || !acquire())
    {
        return -1;
    }
    bool ok = drv_w25q128_read(STORAGE_SECTOR_BYTES * STORAGE_SEC_CONFIG, out, (uint32_t)len);
    osMutexRelease(storage_mutex);
    return ok ? 0 : -1;
}
