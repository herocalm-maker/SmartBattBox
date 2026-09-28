/**
 * @file    bsp_adc.c
 * @brief   ADC 通用外设接口 (BSP 层) — 多通道 ADC+DMA 实现
 *
 * 实现风格参考 ALIENTEK 探索者F407 HAL例程 (实验19-3 多通道ADC采集 DMA, adc.c):
 *   - 多通道硬件扫描 + DMA NORMAL 模式搬运
 *   - 传输完成后通过中断标志通知, 需手动调用 bsp_adc_dma_enable() 重启
 *   - HAL_ADC_MspInit/MspDeInit 由 HAL 内部调用, 配置 GPIO/DMA/中断
 *
 * 数据流:
 *   调用 bsp_adc_dma_enable(N) 启动 N 次 DMA 传输 →
 *   DMA2_Stream0 将 ADC1 转换结果搬运到 s_adc1_dma_buf[] →
 *   上层读取 bsp_adc_get_raw() 获取缓冲区数据
 *
 * 通道序列 (Board.h "7. ADC 通道映射"):
 *   Rank 1: PC1  ADC1_IN11 充电模拟备份
 *   Rank 2: PA3  ADC1_IN3   充电电压
 *   Rank 3: PA4  ADC1_IN4   充电器温度 NTC
 *   Rank 4: PA5  ADC1_IN5   电池温度 NTC
 *   Rank 5: PA6  ADC1_IN6   逆变器温度 NTC
 *
 * @note 引脚分配见 Board.h "3. GPIO 引脚分配总表 / 7. ADC 通道映射"
 */
#include "bsp_adc.h"
#include "Board.h"
#include "stm32f4xx_hal.h"

/* =====================================================================
 *  ADC / DMA 句柄定义 (全局, 供 IRQ handler 和上层使用)
 * ===================================================================== */
ADC_HandleTypeDef g_adc_nch_dma_handle;     /* 与DMA关联的ADC句柄 */
DMA_HandleTypeDef g_dma_nch_adc_handle;     /* 与ADC关联的DMA句柄 */

/* =====================================================================
 *  ADC1 DMA 缓冲区 (5 通道线性排布)
 *
 *  内存布局: 索引 = 通道rank (0-4)
 *  DMA 每完成一次传输, 每个通道各完成一次采样
 *
 *  通道顺序对应 Board.h "7. ADC 通道映射" Rank:
 *    [0] PC1  ADC1_IN11 充电模拟备份
 *    [1] PA3  ADC1_IN3  充电电压
 *    [2] PA4  ADC1_IN4  充电器温度 NTC
 *    [3] PA5  ADC1_IN5  电池温度 NTC
 *    [4] PA6  ADC1_IN6  逆变器温度
 * ===================================================================== */
#define BSP_ADC_DMA_BUF_LEN     (BOARD_ADC_CHANNEL_CNT)   /* 缓冲区长度 = 通道数 */

static volatile uint16_t s_adc1_dma_buf[BSP_ADC_DMA_BUF_LEN];

/* =====================================================================
 *  HAL MSP 回调 (由 HAL 内部调用)
 *
 *  配置 GPIO 模拟引脚 + DMA2_Stream0 通道 + NVIC.
 * ===================================================================== */
void HAL_ADC_MspInit(ADC_HandleTypeDef *adcHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (adcHandle->Instance == ADC1)
    {
        /* ADC1 clock enable */
        __HAL_RCC_ADC1_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        /**ADC1 GPIO Configuration
          PC1     ------> ADC1_IN11
          PA3     ------> ADC1_IN3
          PA4     ------> ADC1_IN4
          PA5     ------> ADC1_IN5
          PA6     ------> ADC1_IN6
        */
        /* 注意: 引脚掩码必须按端口分组. 若把 PC1 的 GPIO_PIN_1 混入 GPIOA 的
         * 掩码一起初始化, 会把 PA1 (UART4_RX, 见 Board.h) 误配成模拟输入. */
        GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
        GPIO_InitStruct.Pull = GPIO_NOPULL;

        GPIO_InitStruct.Pin = BOARD_CHG_AD_BACKUP_PIN;            /* PC1 */
        HAL_GPIO_Init(BOARD_CHG_AD_BACKUP_PORT, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = BOARD_CHG_VOL_AD_PIN | BOARD_CHG_TMP_AD_PIN
                            | BOARD_BAT_TMP_AD_PIN | BOARD_INV_TMP_AD_PIN;  /* PA3~PA6 */
        HAL_GPIO_Init(BOARD_CHG_VOL_AD_PORT, &GPIO_InitStruct);

         /* ADC1 DMA Init: DMA2_Stream0 Channel0 */
        __HAL_RCC_DMA2_CLK_ENABLE();
        g_dma_nch_adc_handle.Instance = DMA2_Stream0;
        g_dma_nch_adc_handle.Init.Channel = DMA_CHANNEL_0;
        g_dma_nch_adc_handle.Init.Direction = DMA_PERIPH_TO_MEMORY;
        g_dma_nch_adc_handle.Init.PeriphInc = DMA_PINC_DISABLE;
        g_dma_nch_adc_handle.Init.MemInc = DMA_MINC_ENABLE;
        g_dma_nch_adc_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
        g_dma_nch_adc_handle.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
        g_dma_nch_adc_handle.Init.Mode = DMA_NORMAL;              /* NORMAL模式, 需手动重启 */
        g_dma_nch_adc_handle.Init.Priority = DMA_PRIORITY_MEDIUM; /* 参考正点原子例程: 中等优先级 */
        g_dma_nch_adc_handle.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        if (HAL_DMA_Init(&g_dma_nch_adc_handle) != HAL_OK) {
            Error_Handler();
        }
        __HAL_LINKDMA(adcHandle, DMA_Handle, g_dma_nch_adc_handle);

        /* ADC1 DMA NVIC */
        HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef *adcHandle)
{
    if (adcHandle->Instance == ADC1)
    {
        /* Peripheral clock disable */
        __HAL_RCC_ADC1_CLK_DISABLE();

        /**ADC1 GPIO Configuration */
        HAL_GPIO_DeInit(BOARD_CHG_AD_BACKUP_PORT, BOARD_CHG_AD_BACKUP_PIN);
        HAL_GPIO_DeInit(BOARD_CHG_VOL_AD_PORT, BOARD_CHG_VOL_AD_PIN);
        HAL_GPIO_DeInit(BOARD_CHG_TMP_AD_PORT, BOARD_CHG_TMP_AD_PIN);
        HAL_GPIO_DeInit(BOARD_BAT_TMP_AD_PORT, BOARD_BAT_TMP_AD_PIN);
        HAL_GPIO_DeInit(BOARD_INV_TMP_AD_PORT, BOARD_INV_TMP_AD_PIN);

        HAL_DMA_DeInit(adcHandle->DMA_Handle);
        HAL_NVIC_DisableIRQ(DMA2_Stream0_IRQn);
    }
}

/*********************************************************************
 * @fn      bsp_adc_init
 * @brief   初始化 ADC1 外设 (配置通道 + 准备 DMA 传输)
 * @return  bool true=成功 false=失败
 * @note    使用本文件持有的 g_adc_nch_dma_handle, Instance 固定 ADC1 (单板单 ADC).
 *          初始化后需调用 bsp_adc_dma_enable() 启动首次 DMA 传输.
********************************************************************/
bool bsp_adc_init(void)
{
    ADC_HandleTypeDef *hadc = &g_adc_nch_dma_handle;
    ADC_ChannelConfTypeDef sConfig = {0};

    hadc->Instance = ADC1;

    hadc->Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc->Init.Resolution = ADC_RESOLUTION_12B;
    hadc->Init.ScanConvMode = ENABLE;                       /* 多通道扫描 */
    hadc->Init.ContinuousConvMode = ENABLE;                 /* 连续转换 */
    hadc->Init.DiscontinuousConvMode = DISABLE;
    hadc->Init.NbrOfDiscConversion = 0;                     /* 非不连续模式, 无意义置0 */
    hadc->Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc->Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc->Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc->Init.NbrOfConversion = BOARD_ADC_CHANNEL_CNT;     /* 5 通道 */
    hadc->Init.DMAContinuousRequests = ENABLE;              /* DMA 连续请求 */
    hadc->Init.EOCSelection = ADC_EOC_SEQ_CONV;             /* 序列完成 */

    if (HAL_ADC_Init(hadc) != HAL_OK) {
        return false;
    }

    /* 配置通道序列 (Rank 1..5), 采样时间取自 Board.h */
    static const uint32_t ch_list[] = {
         ADC_CHANNEL_11,  /* Rank 1: PC1 */
         ADC_CHANNEL_3,   /* Rank 2: PA3 */
         ADC_CHANNEL_4,   /* Rank 3: PA4 */
         ADC_CHANNEL_5,   /* Rank 4: PA5 */
         ADC_CHANNEL_6,   /* Rank 5: PA6 */
    };
    for (uint8_t i = 0; i < BOARD_ADC_CHANNEL_CNT; i++) {
        sConfig.Channel = ch_list[i];
        sConfig.Rank = i + 1u;
        sConfig.SamplingTime = BOARD_ADC_SAMPLETIME;
        if (HAL_ADC_ConfigChannel(hadc, &sConfig) != HAL_OK) {
            return false;
        }
    }

    /* 配置 DMA 传输参数 (不启动, 等待 bsp_adc_dma_enable() 调用) */
    if (HAL_DMA_Start(&g_dma_nch_adc_handle, (uint32_t)&ADC1->DR, (uint32_t)s_adc1_dma_buf, BSP_ADC_DMA_BUF_LEN) != HAL_OK) {
        return false;
    }

    /* 使能 DMA 传输完成中断 */
    __HAL_DMA_ENABLE_IT(&g_dma_nch_adc_handle, DMA_IT_TC);

    return true;
}

/*********************************************************************
 * @fn      bsp_adc_deinit
 * @brief   反初始化 ADC 外设 (停止 DMA + 释放资源)
 * @return  bool true=成功 false=失败
********************************************************************/
bool bsp_adc_deinit(void)
{
    HAL_ADC_Stop_DMA(&g_adc_nch_dma_handle);   /* 停止 DMA 转换 */
    HAL_ADC_DeInit(&g_adc_nch_dma_handle);       /* 触发 MspDeInit */
    return true;
}

/*********************************************************************
 * @fn      bsp_adc_dma_enable
 * @brief   使能一次 ADC DMA 传输
 * @param   ndtr: DMA 传输的次数 (通常为 BOARD_ADC_CHANNEL_CNT)
 * @return  无
 * @note    每次传输完成后需手动调用此函数重启下一次传输
********************************************************************/
void bsp_adc_dma_enable(uint16_t ndtr)
{
    __HAL_ADC_DISABLE(&g_adc_nch_dma_handle);       /* 先关闭 ADC */

    __HAL_DMA_DISABLE(&g_dma_nch_adc_handle);       /* 关闭 DMA 传输 */
    g_dma_nch_adc_handle.Instance->NDTR = ndtr;     /* 重设 DMA 传输数据量 */
    __HAL_DMA_ENABLE(&g_dma_nch_adc_handle);        /* 开启 DMA 传输 */

    __HAL_ADC_ENABLE(&g_adc_nch_dma_handle);        /* 重新启动 ADC */
    ADC1->CR2 |= 1 << 30;                           /* 启动规则转换通道 */
}

/*********************************************************************
 * @fn      bsp_adc_get_raw
 * @brief   读取指定通道原始值 (DMA 缓冲区直接读取)
 * @param   ch  通道 (BSP_ADC_CH_*)
 * @return  uint16_t 采样值 (12bit, 0..4095); 失败返回 0
 * @note    读取后应调用 bsp_adc_dma_enable() 重启下一次传输.
********************************************************************/
uint16_t bsp_adc_get_raw(bsp_adc_ch_enum ch)
{
    if ((uint8_t)ch >= BOARD_ADC_CHANNEL_CNT) {
        return 0;
    }
    uint8_t rank = (uint8_t)ch;

    /* 直接从 DMA 缓冲区读取 (传输已完成) */
    return s_adc1_dma_buf[rank];
}

/*********************************************************************
 * @fn      bsp_adc_get_raw_average
 * @brief   读取指定通道原始值的多样本平均
 * @param   ch  通道 (BSP_ADC_CH_*)
 * @return  uint16_t 平均采样值 (12bit, 0..4095); 失败返回 0
 * @note    当前 DMA_NORMAL 模式下, 每次传输只产生一个样本,
 *          此函数暂时直接返回单次采样值; 若需多样本平均,
 *          需外部循环调用 bsp_adc_dma_enable() 并累加.
********************************************************************/
uint16_t bsp_adc_get_raw_average(bsp_adc_ch_enum ch)
{
    /* DMA_NORMAL 模式下, 每次传输只产生一个样本, 直接返回 */
    return bsp_adc_get_raw(ch);
}

/*********************************************************************
 * @fn      bsp_adc_get_mv
 * @brief   读取指定通道最新换算电压 (mV)
 * @param   ch  通道 (BSP_ADC_CH_*)
 * @return  uint16_t 电压 mV; 失败返回 0
********************************************************************/
uint16_t bsp_adc_get_mv(bsp_adc_ch_enum ch)
{
    uint16_t raw = bsp_adc_get_raw(ch);
    return (uint16_t)((uint32_t)raw * BOARD_ADC_VREF_MV / BOARD_ADC_RESOLUTION);
}

/*********************************************************************
 * @fn      bsp_adc_get_mv_average
 * @brief   读取指定通道多样本平均后的换算电压 (mV)
 * @param   ch  通道 (BSP_ADC_CH_*)
 * @return  uint16_t 电压 mV; 失败返回 0
********************************************************************/
uint16_t bsp_adc_get_mv_average(bsp_adc_ch_enum ch)
{
    uint16_t raw = bsp_adc_get_raw_average(ch);
    return (uint16_t)((uint32_t)raw * BOARD_ADC_VREF_MV / BOARD_ADC_RESOLUTION);
}

/* =====================================================================
 *  DMA2_Stream0 中断处理 (ADC1 DMA)
 * ===================================================================== */
void DMA2_Stream0_IRQHandler(void)
{
    /* 判断 DMA2_Stream0 传输完成标志 (LISR bit5) */
    if (DMA2->LISR & (1 << 5))
    {
        DMA2->LIFCR |= 1 << 5;      /* 清除传输完成标志 */
    }
}
