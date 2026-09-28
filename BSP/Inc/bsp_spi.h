/**
 * @file    bsp_spi.h
 * @brief   SPI 通用外设接口 (BSP 层)
 *
 * 设计目标:
 *   - 通过 BSP_SPI_NUM 枚举索引, 内部维护句柄
 *   - 上层不接触 SPI_HandleTypeDef, 换芯片时只改 BSP 层
 *   - 提供 init/deinit/transfer/xchg 基础函数
 */
#ifndef __BSP_SPI_H__
#define __BSP_SPI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/*====================================================================
 * SPI 端口枚举
 *====================================================================*/
typedef enum {
    BSP_SPI_2 = 0,    /* SPI2 -> W25Q128 Flash */
    BSP_SPI_MAX,
} bsp_spi_port_t;

/*====================================================================
 * SPI 配置参数
 *====================================================================*/
typedef struct {
    uint32_t baudrate_prescaler;
    uint32_t cpol;
    uint32_t cpha;
    uint32_t data_size;
    uint32_t first_bit;
} bsp_spi_config_t;

/*====================================================================
 * 公开 API
 *====================================================================*/

extern bool bsp_spi_init(bsp_spi_port_t port, const bsp_spi_config_t *cfg);
extern bool bsp_spi_deinit(bsp_spi_port_t port);
extern bool bsp_spi_transfer(bsp_spi_port_t port, const uint8_t *tx, uint8_t *rx,
                             uint16_t len, uint32_t timeout_ms);
extern bool bsp_spi_xchg_checked(bsp_spi_port_t port,uint8_t tx,uint8_t *rx);
extern uint8_t bsp_spi_xchg(bsp_spi_port_t port, uint8_t tx);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_SPI_H__ */
