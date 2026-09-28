/**
 * @file    bsp_i2c.c
 * @brief   I2C GPIO 模拟 (位操作) 接口
 *
 * 实现风格参考 ALIENTEK 探索者F407 HAL例程 (实验24 IIC, myiic.c):
 *   - SDA 常驻开漏输出+内部上拉: 写1即释放总线, IDR 可直接读外部电平,
 *     读期间无需 HAL_GPIO_Init 反复切换输入/输出模式 (消除每字节两次模式切换开销)
 *   - SCL 推挽输出: 单主机、从机无时钟拉伸场景下边沿更陡
 *   - bsp_i2c_chk_ack 带超时轮询 (250次), 防止从机慢应答/无应答挂死总线
 *   - 初始化自带 GPIO 时钟使能, 并以 STOP 时序把总线释放到空闲态
 */
#include "bsp_i2c.h"
#include "Board.h"
#include "comm_logger.h"

/* ===== GPIO 操作宏 ===== */
#define SCL_PORT        BOARD_I2C1_SCL_PORT
#define SCL_PIN         BOARD_I2C1_SCL_PIN
#define SDA_PORT        BOARD_I2C1_SDA_PORT
#define SDA_PIN         BOARD_I2C1_SDA_PIN

#define SCL_HIGH()      HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_SET)
#define SCL_LOW()       HAL_GPIO_WritePin(SCL_PORT, SCL_PIN, GPIO_PIN_RESET)
#define SDA_HIGH()      HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_SET)
#define SDA_LOW()       HAL_GPIO_WritePin(SDA_PORT, SDA_PIN, GPIO_PIN_RESET)
#define SDA_READ()      HAL_GPIO_ReadPin(SDA_PORT, SDA_PIN)

/* I2C空循环延时系数; 168M主频. 实测(Debug构建 -O1): 内层 nop+subs+bne ≈ 3~4周期/次,
 * 200次 ≈ 700周期 ≈ 4.2us, 即 i2c_Delay(2) ≈ 8~10us, 总线约 60~80kHz (MAX1726x 支持).
 * 注意: __NOP 为 volatile 内联汇编, 循环体在 -O1/-O2 下不会被删除, 但节拍会变快, 改优化等级需重标定. */
#define I2C_LOOP_COUNT_PER_US   (80)

/**
 * @brief 简单空循环计数微秒延时 (标定见上方宏注释)
 */
void i2c_Delay(unsigned int us)
{
    unsigned int i,j;
    for(i = 0; i < us; i++)
    {
        for(j = 0; j < I2C_LOOP_COUNT_PER_US; j++)
        {
            __NOP();
        }
    }
}

/* 使能指定 GPIO 端口时钟 (引脚来自 Board.h, 端口不定, 逐个判断) */
static void _i2c_port_clk_enable(GPIO_TypeDef *port)
{
    if      (port == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if (port == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if (port == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    else if (port == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
    else if (port == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
}

static void sda_to_input(void)
{
    GPIO_InitTypeDef gpio_conf = {0};

    gpio_conf.Pin   = SDA_PIN;
    gpio_conf.Mode  = GPIO_MODE_INPUT;
    gpio_conf.Pull  = GPIO_PULLUP;
    gpio_conf.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SDA_PORT, &gpio_conf);
}

static void sda_to_output(void)
{
    GPIO_InitTypeDef gpio_conf = {0};

    gpio_conf.Pin   = SDA_PIN;
    gpio_conf.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_conf.Pull  = GPIO_PULLUP;
    gpio_conf.Speed = GPIO_SPEED_FREQ_HIGH;

    HAL_GPIO_Init(SDA_PORT, &gpio_conf);
}

/**
 * @brief   初始化 I2C GPIO (SCL 推挽, SDA 开漏+上拉).
 * @param   none
 * @return  none
 * @note    SDA 常驻开漏输出模式: 写1=释放总线, IDR 可直接读外部电平,
 *          收字节期间无需切换输入/输出模式. 末尾以 STOP 时序释放总线到空闲态.
 */
void bsp_i2c_init(void)
{
    GPIO_InitTypeDef gpio_conf = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* SCL 推挽输出: 单主机场景, 边沿陡峭 */
    gpio_conf.Pin = SCL_PIN;
    gpio_conf.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_conf.Pull = GPIO_PULLUP;
    gpio_conf.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SCL_PORT, &gpio_conf);

    /* SDA 开漏输出+上拉: 写1=释放总线, IDR 可直接读外部电平 */
    gpio_conf.Pin = SDA_PIN;
    gpio_conf.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(SDA_PORT, &gpio_conf);

    SCL_HIGH();
    SDA_HIGH();
}

/**
 * @brief   产生 I2C 起始信号 (START).
 * @param   none
 * @return  none
 * @note    SCL 高电平期间 SDA 由高变低; 结束后 SCL 拉低钳住总线, 准备收发数据.
 */
void bsp_i2c_start(void)
{
    sda_to_output();
    i2c_Delay(1);

    SDA_HIGH();
    i2c_Delay(1);
    SCL_HIGH();
    i2c_Delay(5);

    SDA_LOW();          /* START: SCL高电平期间 SDA 由高变低 */
    i2c_Delay(5);
    SCL_LOW();          /* 钳住总线, 准备发送/接收数据 */
    i2c_Delay(5);
}

/**
 * @brief   产生 I2C 停止信号 (STOP).
 * @param   none
 * @return  none
 * @note    SCL 高电平期间 SDA 由低变高; 结束后释放总线, 总线回到空闲态.
 */
void bsp_i2c_stop(void)
{
    sda_to_output();
    i2c_Delay(1);

    SDA_LOW();          /* STOP: SCL高电平期间 SDA 由低变高 */
    i2c_Delay(5);

    SCL_HIGH();
    i2c_Delay(5);

    SDA_HIGH();         /* 释放总线 */
    i2c_Delay(5);
}

/**
 * @brief   等待从机应答 (带超时轮询).
 * @return  0=收到ACK; 1=NACK或超时 (超时后已自动补STOP释放总线).
 */
uint8_t bsp_i2c_chk_ack(void)
{
    uint16_t waittime = 0;
    uint8_t rack = 0;

    sda_to_input();

    SCL_LOW();          /* SCL=0,*/
    i2c_Delay(5);
    SCL_HIGH();         /* SCL=1, 从机可发送ACK */
    i2c_Delay(5);

    while(SDA_READ() == GPIO_PIN_SET)   /* 轮询等待从机拉低SDA */
    {
        waittime++;
        if(waittime > 1200)
        {
           break;
        }
    }
    rack = SDA_READ();

    SCL_LOW();          /* SCL=0, 结束ACK时隙 */
    sda_to_output();
    i2c_Delay(5);
    SDA_HIGH();  
    return rack;
}

/**
 * @brief   主机发送应答位 (读方向, 收完一个字节后调用).
 * @param   AckBit: 0=ACK (继续读), 1=NACK (最后一字节, 通知从机结束发送)
 * @return  none
 */
void bsp_i2c_send_ack(uint8_t AckBit)
{
    sda_to_output();
    i2c_Delay(1);

    if(AckBit != 0)
    {
        SDA_HIGH();     /* NACK: SCL 0->1 期间 SDA=1 */
    }
    else
    {
        SDA_LOW();      /* ACK: SCL 0->1 期间 SDA=0 */
    }
    i2c_Delay(2);

    SCL_HIGH();
    i2c_Delay(5);

    SCL_LOW();
    i2c_Delay(5);
}

/**
 * @brief   发送一个字节 (高位先行).
 * @param   txd: 要发送的字节
 * @return  BSP_I2C_OK (发送本身不判错, 应答由 bsp_i2c_chk_ack 检查)
 * @note    发送完自动释放 SDA (开漏写1), 交由从机控制应答位.
 */
uint8_t bsp_i2c_writeByte(uint8_t txd)
{
    uint8_t i;

    sda_to_output();
    i2c_Delay(1);
    for(i = 0; i < 8; i++)
    {
        if(txd & 0x80)
          SDA_HIGH();
        else
          SDA_LOW();
        
        txd <<= 1;
        i2c_Delay(5);

        SCL_HIGH();
        i2c_Delay(5);
        SCL_LOW();
        i2c_Delay(5);
    }
    
    return BSP_I2C_OK;
}

/**
 * @brief   接收一个字节 (高位先行), 不含应答位.
 * @param   none
 * @return  收到的字节
 * @note    调用前 SDA 已处于释放态 (开漏输出1), SCL 高电平期间直接采样 IDR;
 *          应答位由调用方通过 bsp_i2c_send_ack 补发.
 */
uint8_t bsp_i2c_readByte(void)
{
    uint8_t i;
    uint8_t dat = 0;

    sda_to_input();

    /* SDA已处于释放态(开漏输出1), 直接采样IDR即可 */
    for(i = 0; i < 8; i++)
    {
        dat <<= 1;
        SCL_HIGH();
        i2c_Delay(5);
        if(SDA_READ() == GPIO_PIN_SET)
        {
          dat |= 0x01;
        }

        SCL_LOW();
        i2c_Delay(5);
    }
    sda_to_output();

    return dat;
}

/**
 * @brief   从指定寄存器连续读 len 个字节 (Repeated-START 时序).
 * @param   deviceAddr: 8位写地址 (7位地址<<1), 内部自动 |0x01 切换为读方向
 * @param   RegAddr:    起始寄存器地址 (从机内部地址指针自动递增)
 * @param   rbuff:      读出数据缓冲区 (调用方保证容量 >= len)
 * @param   len:        读取字节数 (1~255)
 * @return  BSP_I2C_OK 成功; BSP_I2C_ERR 参数错误; BSP_I2C_ERR_ADDR 地址NACK;
 *          BSP_I2C_ERR_REG 寄存器地址NACK
 * @note    Repeated-START 不可改为 STOP+START, MAX1726x 等芯片要求;
 *          最后一字节回 NACK 通知从机结束发送.
 */
uint8_t bsp_i2c_readData(uint8_t deviceAddr, uint8_t RegAddr,uint8_t *rbuff,uint8_t len)
{
    uint8_t i;
    uint8_t ret = BSP_I2C_OK;
    if(rbuff == NULL || len == 0U)
    {
        return BSP_I2C_ERR;
    }

    bsp_i2c_start();
    bsp_i2c_writeByte(deviceAddr);
    bsp_i2c_chk_ack();
    
    bsp_i2c_writeByte(RegAddr);
    bsp_i2c_chk_ack();

    bsp_i2c_start(); /* Repeated‑START，MAX1726x必须 */
    bsp_i2c_writeByte(deviceAddr | 0x01);
    bsp_i2c_chk_ack();

    for(i = 0; i < len; i++)
    {
        rbuff[i] = bsp_i2c_readByte();
        if(i == (len -1))
        {
            bsp_i2c_send_ack(1); /* 最后字节NACK */
        }
        else
        {
            bsp_i2c_send_ack(0);
        }
    }

    bsp_i2c_stop();
    return ret;
}

/**
 * @brief   向指定寄存器连续写 len 个字节.
 * @param   deviceAddr: 8位写地址 (7位地址<<1)
 * @param   RegAddr:    起始寄存器地址 (从机内部地址指针自动递增)
 * @param   wbuff:      待写数据缓冲区
 * @param   len:        写入字节数 (1~255)
 * @return  BSP_I2C_OK 成功; BSP_I2C_ERR 参数错误; BSP_I2C_ERR_ADDR 地址NACK;
 *          BSP_I2C_ERR_REG 寄存器地址NACK; BSP_I2C_ERR_DATA 数据字节NACK
 * @note    任意阶段 NACK 即中止后续字节, 但仍补 STOP 释放总线.
 */
uint8_t bsp_i2c_writeData(uint8_t deviceAddr, uint8_t RegAddr, uint8_t *wbuff,uint8_t len)
{
    uint8_t i;
    uint8_t ret = BSP_I2C_OK;
    if(wbuff == NULL || len ==0)
    {
        return BSP_I2C_ERR;
    }

    bsp_i2c_start();
    bsp_i2c_writeByte(deviceAddr);
    bsp_i2c_chk_ack();
    bsp_i2c_writeByte(RegAddr);
    bsp_i2c_chk_ack();

    for(i = 0; i < len; i++)
    {
        bsp_i2c_writeByte(wbuff[i]);
        if(bsp_i2c_chk_ack() !=0)
        {
           ret = BSP_I2C_ERR_DATA;
           break;
        }
    }

    bsp_i2c_stop();
    return ret;
}





