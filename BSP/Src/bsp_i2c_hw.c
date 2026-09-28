/**
 * @file    bsp_i2c_hw.c
 * @brief   硬件 I2C1 外设驱动 (HAL 阻塞收发)
 *
 * 实现说明:
 *   - I2C1 挂 APB1 (42MHz @168M主频), 标准模式 100kHz (MAX1726x 支持, 上限 1MHz)
 *   - 引脚 PB6(SCL)/PB7(SDA) 复用 AF4_I2C1, 开漏+上拉
 *   - HAL_I2C_Mem_Read/Write 自带 Repeated-START 时序 (MAX1726x 要求), 阻塞收发带超时
 *   - HAL 地址参数为 8bit 写方向地址 (7bit<<1), 与 bsp_i2c 软件实现约定一致
 *   - NACK 细分: HAL_I2C_ERROR_AF → BSP_I2C_ERR_ADDR, 其余错误 → BSP_I2C_ERR
 */
#include "bsp_i2c_hw.h"
#include "Board.h"
#include "comm_logger.h"

/* I2C1 外设句柄 (本文件私有, 不对外暴露) */
static I2C_HandleTypeDef hi2c1_hw;

/**
 * @brief   初始化硬件 I2C1 外设及 GPIO 复用引脚.
 * @param   none
 * @return  none
 * @note    引脚来自 Board.h; 若此前跑过软件模拟 bsp_i2c_init, 本函数会以 AF 复用
 *          配置覆盖原普通 GPIO 输出模式, 两者不可同时收发.
 */
void bsp_i2c_hw_init(void)
{
    GPIO_InitTypeDef gpio_conf = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    /* PB6=SCL, PB7=SDA: 复用开漏+上拉, 边沿速率高 */
    gpio_conf.Pin       = BOARD_I2C1_SCL_PIN | BOARD_I2C1_SDA_PIN;
    gpio_conf.Mode      = GPIO_MODE_AF_OD;
    gpio_conf.Pull      = GPIO_PULLUP;
    gpio_conf.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_conf.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(BOARD_I2C1_SCL_PORT, &gpio_conf);

    hi2c1_hw.Instance             = I2C1;
    hi2c1_hw.Init.ClockSpeed      = 100000U;                 /* 100kHz 标准模式 */
    hi2c1_hw.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1_hw.Init.OwnAddress1     = 0x00U;
    hi2c1_hw.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1_hw.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1_hw.Init.OwnAddress2     = 0x00U;
    hi2c1_hw.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1_hw.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;
    if(HAL_I2C_Init(&hi2c1_hw) != HAL_OK)
    {
        log_error("bsp_i2c_hw_init: HAL_I2C_Init failed");
    }
}

/**
 * @brief   从指定寄存器连续读 len 个字节 (Repeated-START 时序).
 * @param   deviceAddr: 8位写地址 (7位地址<<1), HAL 内部自动 |0x01 切读方向
 * @param   RegAddr:    起始寄存器地址 (从机内部地址指针自动递增)
 * @param   rbuff:      读出数据缓冲区 (调用方保证容量 >= len)
 * @param   len:        读取字节数 (1~255)
 * @return  BSP_I2C_OK 成功; BSP_I2C_ERR 参数错误/总线错误/超时; BSP_I2C_ERR_ADDR 地址NACK
 * @note    与软件模拟 bsp_i2c_readData 签名一致, 可直接替换.
 */
uint8_t bsp_i2c_hw_readData(uint8_t deviceAddr, uint8_t RegAddr, uint8_t *rbuff, uint8_t len)
{
    HAL_StatusTypeDef st;

    if(rbuff == NULL || len == 0U)
    {
        return BSP_I2C_ERR;
    }

    st = HAL_I2C_Mem_Read(&hi2c1_hw, deviceAddr, RegAddr, I2C_MEMADD_SIZE_8BIT,
                          rbuff, (uint16_t)len, BSP_I2C_HW_TIMEOUT_MS);
    if(st == HAL_OK)
    {
        return BSP_I2C_OK;
    }
    if((st == HAL_ERROR) && ((HAL_I2C_GetError(&hi2c1_hw) & HAL_I2C_ERROR_AF) != 0U))
    {
        return BSP_I2C_ERR_ADDR;    /* 地址NACK: 芯片不在线/地址错/无上拉/无供电 */
    }
    return BSP_I2C_ERR;             /* 总线错误/超时 */
}

/**
 * @brief   向指定寄存器连续写 len 个字节.
 * @param   deviceAddr: 8位写地址 (7位地址<<1)
 * @param   RegAddr:    起始寄存器地址 (从机内部地址指针自动递增)
 * @param   wbuff:      待写数据缓冲区
 * @param   len:        写入字节数 (1~255)
 * @return  BSP_I2C_OK 成功; BSP_I2C_ERR 参数错误/总线错误/超时; BSP_I2C_ERR_ADDR 地址NACK
 * @note    与软件模拟 bsp_i2c_writeData 签名一致, 可直接替换.
 */
uint8_t bsp_i2c_hw_writeData(uint8_t deviceAddr, uint8_t RegAddr, uint8_t *wbuff, uint8_t len)
{
    HAL_StatusTypeDef st;

    if(wbuff == NULL || len == 0U)
    {
        return BSP_I2C_ERR;
    }

    st = HAL_I2C_Mem_Write(&hi2c1_hw, deviceAddr, RegAddr, I2C_MEMADD_SIZE_8BIT,
                           wbuff, (uint16_t)len, BSP_I2C_HW_TIMEOUT_MS);
    if(st == HAL_OK)
    {
        return BSP_I2C_OK;
    }
    if((st == HAL_ERROR) && ((HAL_I2C_GetError(&hi2c1_hw) & HAL_I2C_ERROR_AF) != 0U))
    {
        return BSP_I2C_ERR_ADDR;
    }
    return BSP_I2C_ERR;
}
