/**
 * @file    bsp_i2c_hw.h
 * @brief   硬件 I2C1 外设驱动 (HAL 阻塞收发) 接口
 *
 * 设计目标:
 *   - 使用 I2C1 硬件外设收发, 引脚 PB6(SCL)/PB7(SDA) 复用 AF4 (Board.h)
 *   - 与软件模拟 bsp_i2c.c 并存, API 签名/返回码保持一致, 便于切换对比验证
 *   - 返回码复用 bsp_i2c.h 的 BSP_I2C_* 宏, 不重复定义
 */
#ifndef __BSP_I2C_HW_H__
#define __BSP_I2C_HW_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "bsp_i2c.h"    /* 复用 BSP_I2C_OK/BSP_I2C_ERR* 返回码 */
#include <stdint.h>

/* 单次阻塞收发超时, 单位 ms */
#define BSP_I2C_HW_TIMEOUT_MS   100U

/* 函数声明 */
extern void bsp_i2c_hw_init(void);
extern uint8_t bsp_i2c_hw_readData(uint8_t deviceAddr, uint8_t RegAddr, uint8_t *rbuff, uint8_t len);
extern uint8_t bsp_i2c_hw_writeData(uint8_t deviceAddr, uint8_t RegAddr, uint8_t *wbuff, uint8_t len);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_I2C_HW_H__ */
