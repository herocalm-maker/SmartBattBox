/**
 * @file    bsp_i2c.h
 * @brief   I2C GPIO 模拟 (位操作) 接口
 *
 * 设计目标:
 *   - 使用 GPIO 开漏输出模拟 I2C 协议
 *   - 不依赖硬件 I2C 外设, 可任意映射引脚
 *   - 提供基础操作和寄存器读写封装
 */
#ifndef __BSP_I2C_H__
#define __BSP_I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include <stdbool.h>


#define BSP_I2C_OK        0U  /* 成功 */
#define BSP_I2C_ERR       1U  /* 参数错误 (NULL/len=0) */
#define BSP_I2C_ERR_ADDR  2U  /* 设备地址 NACK: 芯片不在线/地址错/无上拉/无供电 */
#define BSP_I2C_ERR_REG   3U  /* 寄存器地址 NACK (不应出现) */
#define BSP_I2C_ERR_DATA  4U  /* 数据字节 NACK */



/* 函数声明 */
extern void i2c_Delay(unsigned int us);
extern void bsp_i2c_init(void);
extern void bsp_i2c_start(void);
extern void bsp_i2c_stop(void);
extern uint8_t bsp_i2c_chk_ack(void);
extern void bsp_i2c_send_ack(uint8_t AckBit);
extern uint8_t bsp_i2c_writeByte(uint8_t txd);
extern uint8_t bsp_i2c_readByte(void);
extern uint8_t bsp_i2c_readData(uint8_t deviceAddr, uint8_t RegAddr,uint8_t *rbuff,uint8_t len);
extern uint8_t bsp_i2c_writeData(uint8_t deviceAddr, uint8_t RegAddr, uint8_t *wbuff,uint8_t len);

#ifdef __cplusplus
}
#endif
#endif /* __BSP_I2C_H__ */
