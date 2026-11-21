/**
 * @file    bsp_adc.c
 * @brief   ADC 通用外设接口 (BSP 层) — 多通道 ADC+DMA 实现
 *
 * 实现风格参考 ALIENTEK 探索者F407 HAL例程 (实验19-3 多通道ADC采集 DMA, adc.c):
 *   - 多通道硬件扫描 + DMA CIRCULAR 环形搬运 (连续转换持续刷新)
 *   - bsp_adc_init 一次性启动连续采集, 无需手动重启
 *   - HAL_ADC_MspInit/MspDeInit 由 HAL 内部调用, 配置 GPIO/DMA/中断
 *
 * 数据流:
 *   bsp_adc_init() 调 HAL_ADC_Start_DMA 启动连续转换 →
 *   DMA2_Stream0 以 CIRCULAR 模式持续将 ADC1 转换结果搬运到 s_adc1_dma_buf[] →
 *   上层任意时刻读取 bsp_adc_get_raw()/bsp_adc_get_raw_average() 即得最新值
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
 *  ADC1 DMA 环形缓冲区 (5 通道 × 32 帧交织)
 *
 *  内存布局: 索引 = 帧号 * BOARD_ADC_CHANNEL_CNT + 通道rank
 *  DMA CIRCULAR 持续刷新, 每完成一轮 5 通道序列即覆盖下一帧
 *
 *  通道顺序对应 Board.h "7. ADC 通道映射" Rank:
 *    [0] PC1  ADC1_IN11 充电模拟备份
 *    [1] PA3  ADC1_IN3  充电电压
 *    [2] PA4  ADC1_IN4  充电器温度 NTC
 *    [3] PA5  ADC1_IN5  电池温度 NTC
 *    [4] PA6  ADC1_IN6  逆变器温度
 * ===================================================================== */
#define BSP_ADC_AVG_SAMPLES     32u                        /* 多样本平均窗口 */
#define BSP_ADC_DMA_BUF_LEN     (BOARD_ADC_CHANNEL_CNT * BSP_ADC_AVG_SAMPLES)

static volatile uint16_t s_adc1_dma_buf[BSP_ADC_DMA_BUF_LEN];

/* =====================================================================
 *  HAL MSP 回调 (由 HAL 内部调用)
 *
 *  配置 GPIO 模拟引脚 + DMA2_Stream0 通道 + NVIC.
 * ===================================================================== */
/**
 * @fn      HAL_ADC_MspInit
 * @brief   ADC1 底层初始化 (HAL 回调): 使能时钟, 配置模拟输入引脚, 初始化 DMA2_Stream0 并使能其中断
 * @param   adcHandle  ADC 句柄 (由 HAL_ADC_Init 内部传入, Instance 已设置)
 * @return  无
 * @note    引脚掩码按端口分组初始化, 防止 PC1 的 PIN_1 混入 GPIOA 掩码后
 *          把 PA1 (UART4_RX) 误配成模拟输入.
 */
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
        g_dma_nch_adc_handle.Init.Mode = DMA_CIRCULAR;             /* 环形: 连续转换持续刷新缓冲区 */
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

/**
 * @fn      HAL_ADC_MspDeInit
 * @brief   ADC1 底层反初始化 (HAL 回调): 关时钟, 释放 GPIO/DMA/NVIC 资源
 * @param   adcHandle  ADC 句柄 (由 HAL_ADC_DeInit 内部传入)
 * @return  无
 */
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
 * @brief   初始化 ADC1 外设 (配置通道 + 启动连续 DMA 采集)
 * @return  bool true=成功 false=失败
 * @note    使用本文件持有的 g_adc_nch_dma_handle, Instance 固定 ADC1 (单板单 ADC).
 *          DMA_CIRCULAR + 连续模式下, 本函数一次性启动采集, 后续无需重启.
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

    /* 启动连续转换 + DMA 环形搬运 (CIRCULAR 自持续, 无需手动重启) */
    if (HAL_ADC_Start_DMA(&g_adc_nch_dma_handle, (uint32_t *)s_adc1_dma_buf, BSP_ADC_DMA_BUF_LEN) != HAL_OK) {
        return false;
    }

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
 * @fn      bsp_adc_get_raw
 * @brief   读取指定通道原始值 (DMA 环形缓冲区当前帧)
 * @param   ch  通道 (BSP_ADC_CH_*)
 * @return  uint16_t 采样值 (12bit, 0..4095); 失败返回 0
 * @note    多样本平均请使用 bsp_adc_get_raw_average().
********************************************************************/
uint16_t bsp_adc_get_raw(bsp_adc_ch_enum ch)
{
    if ((uint8_t)ch >= BOARD_ADC_CHANNEL_CNT) {
        return 0;
    }
    uint8_t rank = (uint8_t)ch;

    /* CIRCULAR 环形缓冲区: 返回当前窗口第 0 帧该通道样本 */
    return s_adc1_dma_buf[rank];
}

/*********************************************************************
 * @fn      bsp_adc_get_raw_average
 * @brief   读取指定通道原始值的多样本平均
 * @param   ch  通道 (BSP_ADC_CH_*)
 * @return  uint16_t 平均采样值 (12bit, 0..4095); 失败返回 0
 * @note    对环形缓冲中当前窗口 BSP_ADC_AVG_SAMPLES 帧该通道样本求平均,
 *          与头文件宣称的 "5 通道×32 块交织 + 整窗平均" 一致.
********************************************************************/
uint16_t bsp_adc_get_raw_average(bsp_adc_ch_enum ch)
{
    if ((uint8_t)ch >= BOARD_ADC_CHANNEL_CNT) {
        return 0;
    }
    uint32_t sum = 0;
    for (uint32_t i = 0; i < BSP_ADC_AVG_SAMPLES; i++) {
        sum += s_adc1_dma_buf[i * BOARD_ADC_CHANNEL_CNT + (uint8_t)ch];
    }
    return (uint16_t)(sum / BSP_ADC_AVG_SAMPLES);
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
/**
 * @fn      DMA2_Stream0_IRQHandler
 * @brief   ADC1 DMA 传输中断入口 (DMA2_Stream0), 转发 HAL 统一处理
 * @param   无
 * @return  无
 * @note    CIRCULAR 模式下缓冲区由 DMA 硬件自持续刷新, 本中断仅清标志/走回调,
 *          数据读取路径 (bsp_adc_get_raw*) 不依赖本中断.
 */
void DMA2_Stream0_IRQHandler(void)
{
    /* 标准 HAL DMA 中断处理: 清除 TC/HT/FE 标志并调用注册回调 */
    HAL_DMA_IRQHandler(&g_dma_nch_adc_handle);
}
