/**
 * @file    bsp_i2c_hw.h
 * @brief   硬件 I2C1 外设驱动 (HAL 阻塞收发) 接口
 *
 * 设计目标:
 *   - 使用 I2C1 硬件外设收发, 引脚 PB6(SCL)/PB7(SDA) 复用 AF4 (Board.h)
 *   - 返回码 BSP_I2C_* 在本文件定义 (软件模拟 bsp_i2c 已移除)
 */
#ifndef __BSP_I2C_HW_H__
#define __BSP_I2C_HW_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

/* I2C 返回码 */
#define BSP_I2C_OK        0U  /* 成功 */
#define BSP_I2C_ERR       1U  /* 参数错误 (NULL/len=0) */
#define BSP_I2C_ERR_ADDR  2U  /* 设备地址 NACK: 芯片不在线/地址错/无上拉/无供电 */
#define BSP_I2C_ERR_REG   3U  /* 寄存器地址 NACK (不应出现) */
#define BSP_I2C_ERR_DATA  4U  /* 数据字节 NACK */

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
