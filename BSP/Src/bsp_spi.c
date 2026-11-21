/**
 * @file    bsp_spi.c
 * @brief   SPI 通用外设接口 (BSP 层)
 *
 * 设计目标:
 *   - 内部维护 SPI_HandleTypeDef 句柄表, 上层只通过枚举索引访问
 *   - 换芯片时只改 BSP 层, 上层驱动不受影响
 */
#include "bsp_spi.h"
#include "Board.h"
#include "stm32f4xx_hal.h"

/*====================================================================
 * 内部句柄表 + 初始化状态
 *====================================================================*/

static SPI_HandleTypeDef s_hspi[BSP_SPI_MAX];
static bool s_spi_inited[BSP_SPI_MAX] = {false};

/*====================================================================
 * HAL MSP 回调 (HAL_SPI_Init 内部调用, 配置 GPIO/时钟)
 *====================================================================*/

/**
 * @fn      HAL_SPI_MspInit
 * @brief   SPI2 底层初始化 (HAL 回调): 使能 GPIOB/SPI2 时钟, 三线配置为 AF5 复用推挽
 * @param   hspi  SPI 句柄 (由 HAL_SPI_Init 内部传入, Instance 已设置)
 * @return  无
 * @note    片选 CS 不在此配置, 由上层以普通 GPIO 软件方式控制 (与 Init 中 NSS_SOFT 对应).
 */
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI2) {
        __HAL_RCC_SPI2_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();

        GPIO_InitTypeDef gpio = {0};
        gpio.Pin       = BOARD_SPI2_SCK_PIN | BOARD_SPI2_MISO_PIN | BOARD_SPI2_MOSI_PIN;
        gpio.Mode      = GPIO_MODE_AF_PP;
        gpio.Pull      = GPIO_NOPULL;
        gpio.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF5_SPI2;
        HAL_GPIO_Init(BOARD_SPI2_SCK_PORT, &gpio);
    }
}

/**
 * @fn      HAL_SPI_MspDeInit
 * @brief   SPI2 底层反初始化 (HAL 回调): 关外设时钟并释放复用引脚
 * @param   hspi  SPI 句柄
 * @return  无
 */
void HAL_SPI_MspDeInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI2) {
        __HAL_RCC_SPI2_CLK_DISABLE();
        HAL_GPIO_DeInit(BOARD_SPI2_SCK_PORT,
                        BOARD_SPI2_SCK_PIN | BOARD_SPI2_MISO_PIN | BOARD_SPI2_MOSI_PIN);
    }
}

/*====================================================================
 * 内部辅助: 端口 → 句柄指针 (带初始化检查)
 *====================================================================*/

/**
 * @fn      _get_handle
 * @brief   按端口枚举取 SPI 句柄 (带越界与初始化状态检查)
 * @param   port  SPI 端口枚举
 * @return  SPI_HandleTypeDef* 有效句柄; 端口非法或未初始化返回 NULL
 * @note    收发类 API 统一经此入口, 未初始化即访问直接拒绝, 避免操作全 0 句柄.
 */
static SPI_HandleTypeDef *_get_handle(bsp_spi_port_t port)
{
    if (port >= BSP_SPI_MAX) return NULL;
    if (!s_spi_inited[port]) return NULL;  /* 未初始化则拒绝访问 */
    return &s_hspi[port];
}

/*====================================================================
 * 公开 API 实现
 *====================================================================*/

/**
 * @brief   初始化 SPI 外设
 * @param   port  SPI 端口枚举 (BSP_SPI_2)
 * @param   cfg   配置参数 (波特率预分频/CPOL/CPHA/数据位/首位)
 * @return  true=成功, false=参数错误或 HAL 初始化失败
 * @note    初始化后自动标记端口为已初始化, 后续 transfer/xchg 才可调用
 */
bool bsp_spi_init(bsp_spi_port_t port, const bsp_spi_config_t *cfg)
{
    if (cfg == NULL || port >= BSP_SPI_MAX) 
      return false;

    /* 直接访问句柄表 (初始化时允许访问未初始化的句柄) */
    SPI_HandleTypeDef *hspi = &s_hspi[port];

    /* 设置外设实例 (根据端口枚举映射到具体 SPI 外设) */
    switch (port) {
        case BSP_SPI_2: hspi->Instance = SPI2; break;
        default: return false;
    }

    hspi->Init.Mode               = SPI_MODE_MASTER;
    hspi->Init.Direction          = SPI_DIRECTION_2LINES;
    hspi->Init.DataSize           = cfg->data_size;
    hspi->Init.CLKPolarity        = cfg->cpol;
    hspi->Init.CLKPhase           = cfg->cpha;
    hspi->Init.NSS                = SPI_NSS_SOFT;      /* 软件片选: CS 由上层 GPIO 拉低/拉高, 便于多从机扩展 */
    hspi->Init.BaudRatePrescaler  = cfg->baudrate_prescaler;
    hspi->Init.FirstBit           = cfg->first_bit;
    hspi->Init.TIMode             = SPI_TIMODE_DISABLE;
    hspi->Init.CRCCalculation     = SPI_CRCCALCULATION_DISABLE;
    hspi->Init.CRCPolynomial      = 10;                 /* 硬件 CRC 已关闭, 多项式值仅为占位 */

    if (HAL_SPI_Init(hspi) != HAL_OK) {
        return false;
    }

    s_spi_inited[port] = true;  /* 标记为已初始化 */
    return true;
}

/**
 * @brief   反初始化 SPI 外设
 * @param   port  SPI 端口枚举
 * @return  true=成功, false=端口未初始化或 HAL 反初始化失败
 * @note    反初始化后清除初始化标记, 后续 transfer/xchg 将返回失败
 */
bool bsp_spi_deinit(bsp_spi_port_t port)
{
    if (port >= BSP_SPI_MAX) return false;

    SPI_HandleTypeDef *hspi = _get_handle(port);
    if (hspi == NULL) 
        return false;

    if (HAL_SPI_DeInit(hspi) != HAL_OK) {
        return false;
    }

    s_spi_inited[port] = false;  /* 标记为未初始化 */
    
    return true;
}

/**
 * @brief   全双工 SPI 传输 (阻塞)
 * @param   port        SPI 端口枚举
 * @param   tx          发送缓冲区 (可为 NULL, 发送 0x00)
 * @param   rx          接收缓冲区 (可为 NULL, 丢弃接收)
 * @param   len         传输长度 (字节)
 * @param   timeout_ms  超时时间 (ms)
 * @return  true=成功, false=端口未初始化/长度为 0/HAL 超时
 */
bool bsp_spi_transfer(bsp_spi_port_t port, const uint8_t *tx, uint8_t *rx,
                      uint16_t len, uint32_t timeout_ms)
{
    if (len == 0) return false;

    SPI_HandleTypeDef *hspi = _get_handle(port);
    if (hspi == NULL) return false;

    /* 静态哑元缓冲: tx/rx 任一为 NULL 时占位, 保证全双工传输期间读写地址始终有效 */
    static uint8_t s_dummy_tx;
    static uint8_t s_dummy_rx;
    const uint8_t *tx_p = (tx != NULL) ? tx : &s_dummy_tx;
    uint8_t       *rx_p = (rx != NULL) ? rx : &s_dummy_rx;

    if (tx == NULL) {
        s_dummy_tx = 0x00u;   /* 只收不发场景: 总线上输出空字节以驱动时钟 */
    }

    return HAL_SPI_TransmitReceive(hspi, (uint8_t *)tx_p, rx_p, len, timeout_ms) == HAL_OK;
}

/**
 * @brief   单字节全双工交换 (便捷封装)
 * @param   port  SPI 端口枚举
 * @param   tx    发送字节
 * @return  接收字节 (失败返回 0xFF)
 * @note    内部调用 HAL_SPI_TransmitReceive, 超时 100ms
 */
bool bsp_spi_xchg_checked(bsp_spi_port_t port,uint8_t tx,uint8_t *rx) {
    SPI_HandleTypeDef *hspi=_get_handle(port);
    return hspi && rx && HAL_SPI_TransmitReceive(hspi,&tx,rx,1,10)==HAL_OK;
}

/**
 * @brief   单字节全双工交换 (便捷版, 忽略错误码)
 * @param   port  SPI 端口枚举
 * @param   tx    发送字节
 * @return  接收字节; 端口未初始化或 HAL 失败返回 0xFF
 * @note    内部调用 HAL_SPI_TransmitReceive, 超时 100ms;
 *          需要区分失败/数据时改用 bsp_spi_xchg_checked
 */
uint8_t bsp_spi_xchg(bsp_spi_port_t port, uint8_t tx)
{
    SPI_HandleTypeDef *hspi = _get_handle(port);
    if (hspi == NULL) return 0xFFu;

    uint8_t rx = 0xFFu;
    if (HAL_SPI_TransmitReceive(hspi, &tx, &rx, 1u, 100u) != HAL_OK) {
        return 0xFFu;
    }
    return rx;
}
