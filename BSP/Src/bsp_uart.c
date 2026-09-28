/**
 * @file    bsp_uart.c
 * @brief   UART 通用外设接口 (BSP 层) — HAL_UART 封装
 *
 * 设计目标:
 *   - 通过 BSP_UART_NUM 枚举索引, 内部维护句柄查找表
 *   - 提供基础函数: init/deinit/send
 *   - RX: DMA+IDLE 中断处理, 回调在 ISR 上下文执行
 *   - TX: 阻塞 HAL_UART_Transmit
 *
 * 注意:
 *   - HAL_UART_MspInit/MspDeInit 由 HAL 内部调用, 配置 GPIO/DMA/中断
 *   - IRQ handlers 由 NVIC 调用, 转发到 HAL_UART_IRQHandler + 手动 IDLE 处理
 */
#include "bsp_uart.h"
#include "Board.h"
#include "stm32f4xx_hal.h"
#include "comm_logger.h"
#include <string.h>

/* =====================================================================
 *  UART / DMA 句柄定义 (全局, 供 IRQ handlers 使用)
 * ===================================================================== */
 /* USART1 -> 调试/log */
UART_HandleTypeDef huart1;      
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart1_tx;

/* USART2 -> HLW8110 U8 (输入) */
UART_HandleTypeDef huart2;      
DMA_HandleTypeDef hdma_usart2_rx;
DMA_HandleTypeDef hdma_usart2_tx;

/* USART3 -> HLW8110 U9 (输出) */
UART_HandleTypeDef huart3;   
DMA_HandleTypeDef hdma_usart3_rx;
DMA_HandleTypeDef hdma_usart3_tx;

/* UART4  -> 串口屏 */
UART_HandleTypeDef huart4;   
DMA_HandleTypeDef hdma_uart4_rx;
DMA_HandleTypeDef hdma_uart4_tx;

/* USART6 -> 蓝牙 PB-03F */
UART_HandleTypeDef huart6;   
DMA_HandleTypeDef hdma_usart6_rx;
DMA_HandleTypeDef hdma_usart6_tx;

static UART_HandleTypeDef *s_huart_List[BSP_UART_MAX]= {&huart1,&huart2,&huart3,&huart4,&huart6};
static bsp_uart_config_t s_huartCfg_List[BSP_UART_MAX]= {0};
static volatile bool s_Uart_TX_Ready[BSP_UART_MAX]= {0};


//**********************************************************************************
//**********************************************************************************


/* =====================================================================
 *  HAL MSP 回调 (由 HAL 内部调用)
 * ===================================================================== */
void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  
  if(uartHandle->Instance==USART1)
  {
    /* USER CODE BEGIN USART1_MspInit 0 */

    /* USER CODE END USART1_MspInit 0 */
    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = BOARD_UART1_TX_PIN|BOARD_UART1_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(BOARD_UART1_GPIO_PORT, &GPIO_InitStruct);

    /* USART1 DMA Init */
    __HAL_RCC_DMA2_CLK_ENABLE();
    /* USART1_RX Init */
    hdma_usart1_rx.Instance = DMA2_Stream2;
    hdma_usart1_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_NORMAL;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart1_rx);

    /* USART1_TX Init */
    hdma_usart1_tx.Instance = DMA2_Stream7;
    hdma_usart1_tx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_tx.Init.Mode = DMA_NORMAL;
    hdma_usart1_tx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart1_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart1_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmatx,hdma_usart1_tx);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    /* USART1 DMA TX/RX interrupt */
    HAL_NVIC_SetPriority(DMA2_Stream7_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream7_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);
    /* USER CODE BEGIN USART1_MspInit 1 */

    /* USER CODE END USART1_MspInit 1 */
  }
  else if(uartHandle->Instance==USART2)
  {
    /* USER CODE BEGIN USART2_MspInit 0 */

    /* USER CODE END USART2_MspInit 0 */
    /* USART2 clock enable */
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    /**USART2 GPIO Configuration
    PD5     ------> USART2_TX
    PD6     ------> USART2_RX
    */
    GPIO_InitStruct.Pin = BOARD_UART2_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(BOARD_UART2_GPIO_PORT, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = BOARD_UART2_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(BOARD_UART2_GPIO_PORT, &GPIO_InitStruct);

    /* USART2 DMA Init */
    __HAL_RCC_DMA1_CLK_ENABLE();
    /* USART2_RX Init */
    hdma_usart2_rx.Instance = DMA1_Stream5;
    hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart2_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart2_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_rx.Init.Mode = DMA_NORMAL;
    hdma_usart2_rx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart2_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart2_rx);

    /* USART2_TX Init */
    hdma_usart2_tx.Instance = DMA1_Stream6;
    hdma_usart2_tx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_tx.Init.Mode = DMA_NORMAL;
    hdma_usart2_tx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmatx,hdma_usart2_tx);

    /* USART2 interrupt Init */
    HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    /* USART2 DMA TX/RX interrupt */
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 5, 1);
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 5, 1);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    /* USER CODE BEGIN USART2_MspInit 1 */

    /* USER CODE END USART2_MspInit 1 */
  }
  else if(uartHandle->Instance==USART3)
  {
    /* USER CODE BEGIN USART3_MspInit 0 */

    /* USER CODE END USART3_MspInit 0 */
    /* USART3 clock enable */
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    /**USART3 GPIO Configuration
    PD8     ------> USART3_TX
    PD9     ------> USART3_RX
    */
    GPIO_InitStruct.Pin = BOARD_UART3_TX_PIN|BOARD_UART3_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(BOARD_UART3_GPIO_PORT, &GPIO_InitStruct);

    /* USART3 DMA Init */
    __HAL_RCC_DMA1_CLK_ENABLE();
    /* USART3_RX Init */
    hdma_usart3_rx.Instance = DMA1_Stream1;
    hdma_usart3_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart3_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart3_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart3_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart3_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart3_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart3_rx.Init.Mode = DMA_NORMAL;
    hdma_usart3_rx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart3_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart3_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart3_rx);

    /* USART3_TX Init */
    hdma_usart3_tx.Instance = DMA1_Stream3;
    hdma_usart3_tx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart3_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart3_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart3_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart3_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart3_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart3_tx.Init.Mode = DMA_NORMAL;
    hdma_usart3_tx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart3_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart3_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmatx,hdma_usart3_tx);

    /* USART3 interrupt Init */
    HAL_NVIC_SetPriority(USART3_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    /* USART3 DMA TX/RX interrupt */
    HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 5, 1);
    HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 5, 1);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
    /* USER CODE BEGIN USART3_MspInit 1 */

    /* USER CODE END USART3_MspInit 1 */
  }
  else if(uartHandle->Instance==UART4)
  {
    /* USER CODE BEGIN UART4_MspInit 0 */

    /* USER CODE END UART4_MspInit 0 */
    /* UART4 clock enable */
    __HAL_RCC_UART4_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**UART4 GPIO Configuration
    PA0-WKUP     ------> UART4_TX
    PA1     ------> UART4_RX
    */
    GPIO_InitStruct.Pin = BOARD_UART4_TX_PIN|BOARD_UART4_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_UART4;
    HAL_GPIO_Init(BOARD_UART4_GPIO_PORT, &GPIO_InitStruct);

    /* UART4 DMA Init */
    __HAL_RCC_DMA1_CLK_ENABLE();
    /* UART4_RX Init */
    hdma_uart4_rx.Instance = DMA1_Stream2;
    hdma_uart4_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_uart4_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_uart4_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_uart4_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_uart4_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_uart4_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_uart4_rx.Init.Mode = DMA_NORMAL;
    hdma_uart4_rx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_uart4_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_uart4_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_uart4_rx);

    /* UART4_TX Init */
    hdma_uart4_tx.Instance = DMA1_Stream4;
    hdma_uart4_tx.Init.Channel = DMA_CHANNEL_4;
    hdma_uart4_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_uart4_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_uart4_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_uart4_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_uart4_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_uart4_tx.Init.Mode = DMA_NORMAL;
    hdma_uart4_tx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_uart4_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_uart4_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmatx,hdma_uart4_tx);

    /* UART4 interrupt Init */
    HAL_NVIC_SetPriority(UART4_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
    /* UART4 DMA TX/RX interrupt */
    HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 6, 1);
    HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream2_IRQn, 6, 1);
    HAL_NVIC_EnableIRQ(DMA1_Stream2_IRQn);
    /* USER CODE BEGIN UART4_MspInit 1 */

    /* USER CODE END UART4_MspInit 1 */
  }
  else if(uartHandle->Instance==USART6)
  {
    /* USER CODE BEGIN USART6_MspInit 0 */

    /* USER CODE END USART6_MspInit 0 */
    /* USART6 clock enable */
    __HAL_RCC_USART6_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    /**USART6 GPIO Configuration
    PC6     ------> USART6_TX
    PC7     ------> USART6_RX
    */
    GPIO_InitStruct.Pin = BOARD_UART6_TX_PIN|BOARD_UART6_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF8_USART6;
    HAL_GPIO_Init(BOARD_UART6_GPIO_PORT, &GPIO_InitStruct);

    /* USART6 DMA Init */
    __HAL_RCC_DMA2_CLK_ENABLE();
    /* USART6_RX Init */
    hdma_usart6_rx.Instance = DMA2_Stream1;
    hdma_usart6_rx.Init.Channel = DMA_CHANNEL_5;
    hdma_usart6_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart6_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart6_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart6_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart6_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart6_rx.Init.Mode = DMA_NORMAL;
    hdma_usart6_rx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart6_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart6_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart6_rx);

    /* USART6_TX Init */
    hdma_usart6_tx.Instance = DMA2_Stream6;
    hdma_usart6_tx.Init.Channel = DMA_CHANNEL_5;
    hdma_usart6_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart6_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart6_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart6_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart6_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart6_tx.Init.Mode = DMA_NORMAL;
    hdma_usart6_tx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart6_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart6_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmatx,hdma_usart6_tx);

    /* USART6 interrupt Init */
    HAL_NVIC_SetPriority(USART6_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART6_IRQn);
    /* USART6 DMA TX/RX interrupt */
    HAL_NVIC_SetPriority(DMA2_Stream6_IRQn, 6, 1);
    HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream1_IRQn, 6, 1);
    HAL_NVIC_EnableIRQ(DMA2_Stream1_IRQn);
    /* USER CODE BEGIN USART6_MspInit 1 */

    /* USER CODE END USART6_MspInit 1 */
  }
}


void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
    /* USER CODE BEGIN USART1_MspDeInit 0 */

    /* USER CODE END USART1_MspDeInit 0 */
    /* Stop DMA before clock disable */
    HAL_UART_DMAStop(uartHandle);
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(BOARD_UART1_GPIO_PORT, BOARD_UART1_TX_PIN|BOARD_UART1_RX_PIN);

    /* USART1 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);
    HAL_DMA_DeInit(uartHandle->hdmatx);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
    /* USER CODE BEGIN USART1_MspDeInit 1 */

    /* USER CODE END USART1_MspDeInit 1 */
  }
  else if(uartHandle->Instance==USART2)
  {
    /* USER CODE BEGIN USART2_MspDeInit 0 */

    /* USER CODE END USART2_MspDeInit 0 */
    /* Stop DMA before clock disable */
    HAL_UART_DMAStop(uartHandle);
    /* Peripheral clock disable */
    __HAL_RCC_USART2_CLK_DISABLE();
    /**USART2 GPIO Configuration
    PD5     ------> USART2_TX
    PD6     ------> USART2_RX
    */
    HAL_GPIO_DeInit(BOARD_UART2_GPIO_PORT, BOARD_UART2_TX_PIN|BOARD_UART2_RX_PIN);

    /* USART2 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);
    HAL_DMA_DeInit(uartHandle->hdmatx);

    /* USART2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART2_IRQn);
    /* USER CODE BEGIN USART2_MspDeInit 1 */

    /* USER CODE END USART2_MspDeInit 1 */
  }
  else if(uartHandle->Instance==USART3)
  {
    /* USER CODE BEGIN USART3_MspDeInit 0 */

    /* USER CODE END USART3_MspDeInit 0 */
    /* Stop DMA before clock disable */
    HAL_UART_DMAStop(uartHandle);
    /* Peripheral clock disable */
    __HAL_RCC_USART3_CLK_DISABLE();
    /**USART3 GPIO Configuration
    PD8     ------> USART3_TX
    PD9     ------> USART3_RX
    */
    HAL_GPIO_DeInit(BOARD_UART3_GPIO_PORT, BOARD_UART3_TX_PIN|BOARD_UART3_RX_PIN);

    /* USART3 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);
    HAL_DMA_DeInit(uartHandle->hdmatx);

    /* USART3 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART3_IRQn);
    /* USER CODE BEGIN USART3_MspDeInit 1 */

    /* USER CODE END USART3_MspDeInit 1 */
  }
  else if(uartHandle->Instance==UART4)
  {
    /* USER CODE BEGIN UART4_MspDeInit 0 */

    /* USER CODE END UART4_MspDeInit 0 */
    /* Stop DMA before clock disable */
    HAL_UART_DMAStop(uartHandle);
    /* Peripheral clock disable */
    __HAL_RCC_UART4_CLK_DISABLE();
    /**UART4 GPIO Configuration
    PA0-WKUP     ------> UART4_TX
    PA1     ------> UART4_RX
    */
    HAL_GPIO_DeInit(BOARD_UART4_GPIO_PORT, BOARD_UART4_TX_PIN|BOARD_UART4_RX_PIN);

    /* UART4 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);
    HAL_DMA_DeInit(uartHandle->hdmatx);

    /* UART4 interrupt Deinit */
    HAL_NVIC_DisableIRQ(UART4_IRQn);
    /* USER CODE BEGIN UART4_MspDeInit 1 */

    /* USER CODE END UART4_MspDeInit 1 */
  }
  else if(uartHandle->Instance==USART6)
  {
    /* USER CODE BEGIN USART6_MspDeInit 0 */

    /* USER CODE END USART6_MspDeInit 0 */
    /* Stop DMA before clock disable */
    HAL_UART_DMAStop(uartHandle);
    /* Peripheral clock disable */
    __HAL_RCC_USART6_CLK_DISABLE();
    /**USART6 GPIO Configuration
    PC6     ------> USART6_TX
    PC7     ------> USART6_RX
    */
    HAL_GPIO_DeInit(BOARD_UART6_GPIO_PORT, BOARD_UART6_TX_PIN|BOARD_UART6_RX_PIN);

    /* USART6 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);
    HAL_DMA_DeInit(uartHandle->hdmatx);

    /* USART6 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART6_IRQn);
    /* USER CODE BEGIN USART6_MspDeInit 1 */

    /* USER CODE END USART6_MspDeInit 1 */
  }
}


void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{

  if(huart->Instance == USART1)
  {
    s_Uart_TX_Ready[BSP_UART_1] = true; 
  }
	
  if(huart->Instance == USART2)
  {
	s_Uart_TX_Ready[BSP_UART_2] = true; 
  }
	
  if(huart->Instance == USART3)
  {
	s_Uart_TX_Ready[BSP_UART_3] = true; 
  }

  if(huart->Instance == UART4)
  {
	s_Uart_TX_Ready[BSP_UART_4] = true; 
  }
	
  if(huart->Instance == USART6)
  {
	s_Uart_TX_Ready[BSP_UART_6] = true;
  }
}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  /* 本模块使用 IDLE+DMA 接收, RxCplt 仅在 DMA 缓冲区满时触发.
   * 通常数据在 IDLE 中断中已处理, 此处留作扩展. */
  (void)huart;
}


/* =====================================================================
 *  IRQ Handlers (由 NVIC 调用)
 * ===================================================================== */

/**
  * @brief This function handles DMA1 stream1 global interrupt.
  */
void DMA1_Stream1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream1_IRQn 0 */

  /* USER CODE END DMA1_Stream1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart3_rx);
  /* USER CODE BEGIN DMA1_Stream1_IRQn 1 */

  /* USER CODE END DMA1_Stream1_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream2 global interrupt.
  */
void DMA1_Stream2_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream2_IRQn 0 */

  /* USER CODE END DMA1_Stream2_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_uart4_rx);
  /* USER CODE BEGIN DMA1_Stream2_IRQn 1 */

  /* USER CODE END DMA1_Stream2_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream3 global interrupt.
  */
void DMA1_Stream3_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream3_IRQn 0 */

  /* USER CODE END DMA1_Stream3_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart3_tx);
  /* USER CODE BEGIN DMA1_Stream3_IRQn 1 */

  /* USER CODE END DMA1_Stream3_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream4 global interrupt.
  */
void DMA1_Stream4_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream4_IRQn 0 */

  /* USER CODE END DMA1_Stream4_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_uart4_tx);
  /* USER CODE BEGIN DMA1_Stream4_IRQn 1 */

  /* USER CODE END DMA1_Stream4_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream5 global interrupt.
  */
void DMA1_Stream5_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream5_IRQn 0 */

  /* USER CODE END DMA1_Stream5_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart2_rx);
  /* USER CODE BEGIN DMA1_Stream5_IRQn 1 */

  /* USER CODE END DMA1_Stream5_IRQn 1 */
}

/**
  * @brief This function handles DMA1 stream6 global interrupt.
  */
void DMA1_Stream6_IRQHandler(void)
{
  /* USER CODE BEGIN DMA1_Stream6_IRQn 0 */

  /* USER CODE END DMA1_Stream6_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart2_tx);
  /* USER CODE BEGIN DMA1_Stream6_IRQn 1 */

  /* USER CODE END DMA1_Stream6_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream1 global interrupt.
  */
void DMA2_Stream1_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream1_IRQn 0 */

  /* USER CODE END DMA2_Stream1_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart6_rx);
  /* USER CODE BEGIN DMA2_Stream1_IRQn 1 */

  /* USER CODE END DMA2_Stream1_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream2 global interrupt.
  */
void DMA2_Stream2_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream2_IRQn 0 */

  /* USER CODE END DMA2_Stream2_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_rx);
  /* USER CODE BEGIN DMA2_Stream2_IRQn 1 */

  /* USER CODE END DMA2_Stream2_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream6 global interrupt.
  */
void DMA2_Stream6_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream6_IRQn 0 */

  /* USER CODE END DMA2_Stream6_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart6_tx);
  /* USER CODE BEGIN DMA2_Stream6_IRQn 1 */

  /* USER CODE END DMA2_Stream6_IRQn 1 */
}

/**
  * @brief This function handles DMA2 stream7 global interrupt.
  */
void DMA2_Stream7_IRQHandler(void)
{
  /* USER CODE BEGIN DMA2_Stream7_IRQn 0 */

  /* USER CODE END DMA2_Stream7_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_usart1_tx);
  /* USER CODE BEGIN DMA2_Stream7_IRQn 1 */

  /* USER CODE END DMA2_Stream7_IRQn 1 */
}


/**
  * @brief This function handles USART1 global interrupt.
  */
void USART1_IRQHandler(void)
{
  /* USER CODE BEGIN USART1_IRQn 0 */
  uint32_t tmp_flag = 0;
  uint32_t temp = 0;
  bsp_uart_config_t *UartCfg = NULL;

  /* USER CODE END USART1_IRQn 0 */
  HAL_UART_IRQHandler(&huart1);
  /* USER CODE BEGIN USART1_IRQn 1 */

  tmp_flag =__HAL_UART_GET_FLAG(&huart1,UART_FLAG_IDLE);
  if(tmp_flag!= RESET)
  {
    UartCfg = &s_huartCfg_List[BSP_UART_1];
    __HAL_UART_CLEAR_IDLEFLAG(&huart1);
    temp = huart1.Instance->SR;  //清除串口空闲中断标志位
    temp = huart1.Instance->DR;
    HAL_UART_DMAStop(&huart1); 
    temp = UartCfg->rx_buf_size - hdma_usart1_rx.Instance->NDTR;
    if((temp > 0) && (UartCfg->rx_cb != NULL))
    {
      if(temp > UartCfg->rx_buf_size)
        temp = UartCfg->rx_buf_size;
      UartCfg->rx_cb(BSP_UART_1, UartCfg->rx_buf, (uint16_t)temp);
    }
    memset(UartCfg->rx_buf, 0, UartCfg->rx_buf_size);
    HAL_UART_Receive_DMA(&huart1, UartCfg->rx_buf, UartCfg->rx_buf_size);
  }
  /* USER CODE END USART1_IRQn 1 */
}

/**
  * @brief This function handles USART2 global interrupt.
  */
void USART2_IRQHandler(void)
{
  /* USER CODE BEGIN USART2_IRQn 0 */
  uint32_t tmp_flag = 0;
  uint32_t temp = 0;
  bsp_uart_config_t *UartCfg = NULL;

  /* USER CODE END USART2_IRQn 0 */
  HAL_UART_IRQHandler(&huart2);
  /* USER CODE BEGIN USART2_IRQn 1 */

  tmp_flag =__HAL_UART_GET_FLAG(&huart2,UART_FLAG_IDLE);
  if(tmp_flag!= RESET)
  {
    UartCfg = &s_huartCfg_List[BSP_UART_2];
    __HAL_UART_CLEAR_IDLEFLAG(&huart2);
    temp = huart2.Instance->SR;  //清除串口空闲中断标志位
    temp = huart2.Instance->DR;
    HAL_UART_DMAStop(&huart2); 
    temp = UartCfg->rx_buf_size - hdma_usart2_rx.Instance->NDTR;
    if((temp > 0) && (UartCfg->rx_cb != NULL))
    {
      if(temp > UartCfg->rx_buf_size)
        temp = UartCfg->rx_buf_size;
      UartCfg->rx_cb(BSP_UART_2, UartCfg->rx_buf, (uint16_t)temp);
    }
    memset(UartCfg->rx_buf, 0, UartCfg->rx_buf_size);
    HAL_UART_Receive_DMA(&huart2, UartCfg->rx_buf, UartCfg->rx_buf_size);
  }
  /* USER CODE END USART2_IRQn 1 */
}
/**
  * @brief This function handles USART3 global interrupt.
  */
void USART3_IRQHandler(void)
{
  /* USER CODE BEGIN USART3_IRQn 0 */
  uint32_t tmp_flag = 0;
  uint32_t temp = 0;
  bsp_uart_config_t *UartCfg = NULL;

  /* USER CODE END USART3_IRQn 0 */
  HAL_UART_IRQHandler(&huart3);
  /* USER CODE BEGIN USART3_IRQn 1 */

  tmp_flag =__HAL_UART_GET_FLAG(&huart3,UART_FLAG_IDLE);
  if(tmp_flag!= RESET)
  {
    UartCfg = &s_huartCfg_List[BSP_UART_3];
    __HAL_UART_CLEAR_IDLEFLAG(&huart3);
    temp = huart3.Instance->SR;  //清除串口空闲中断标志位
    temp = huart3.Instance->DR;
    HAL_UART_DMAStop(&huart3); 
    temp = UartCfg->rx_buf_size - hdma_usart3_rx.Instance->NDTR;
    if((temp > 0) && (UartCfg->rx_cb != NULL))
    {
      if(temp > UartCfg->rx_buf_size)
        temp = UartCfg->rx_buf_size;
      UartCfg->rx_cb(BSP_UART_3, UartCfg->rx_buf, (uint16_t)temp);
    }
    memset(UartCfg->rx_buf, 0, UartCfg->rx_buf_size);
    HAL_UART_Receive_DMA(&huart3, UartCfg->rx_buf, UartCfg->rx_buf_size);
  }
  /* USER CODE END USART3_IRQn 1 */
}

/**
  * @brief This function handles UART4 global interrupt.
  */
void UART4_IRQHandler(void)
{
  /* USER CODE BEGIN UART4_IRQn 0 */
  uint32_t tmp_flag = 0;
  uint32_t temp = 0;
  bsp_uart_config_t *UartCfg = NULL;

  /* USER CODE END UART4_IRQn 0 */
  HAL_UART_IRQHandler(&huart4);
  /* USER CODE BEGIN UART4_IRQn 1 */
  tmp_flag =__HAL_UART_GET_FLAG(&huart4,UART_FLAG_IDLE);
  if(tmp_flag!= RESET)
  {
    UartCfg = &s_huartCfg_List[BSP_UART_4];
    __HAL_UART_CLEAR_IDLEFLAG(&huart4);
    temp = huart4.Instance->SR;  //清除串口空闲中断标志位
    temp = huart4.Instance->DR;
    HAL_UART_DMAStop(&huart4); 
    temp = UartCfg->rx_buf_size - hdma_uart4_rx.Instance->NDTR;
    if((temp > 0) && (UartCfg->rx_cb != NULL))
    {
      if(temp > UartCfg->rx_buf_size)
        temp = UartCfg->rx_buf_size;
      UartCfg->rx_cb(BSP_UART_4, UartCfg->rx_buf, (uint16_t)temp);
    }
    memset(UartCfg->rx_buf, 0, UartCfg->rx_buf_size);
    HAL_UART_Receive_DMA(&huart4, UartCfg->rx_buf, UartCfg->rx_buf_size);
  }
  /* USER CODE END UART4_IRQn 1 */
}

/**
  * @brief This function handles USART6 global interrupt.
  */
void USART6_IRQHandler(void)
{
  /* USER CODE BEGIN USART6_IRQn 0 */
  uint32_t tmp_flag = 0;
  uint32_t temp = 0;
  bsp_uart_config_t *UartCfg = NULL;

  /* USER CODE END USART6_IRQn 0 */
  HAL_UART_IRQHandler(&huart6);
  /* USER CODE BEGIN USART6_IRQn 1 */
  tmp_flag =__HAL_UART_GET_FLAG(&huart6,UART_FLAG_IDLE);
  if(tmp_flag!= RESET)
  {
    UartCfg = &s_huartCfg_List[BSP_UART_6];
    __HAL_UART_CLEAR_IDLEFLAG(&huart6);
    temp = huart6.Instance->SR;  //清除串口空闲中断标志位
    temp = huart6.Instance->DR;
    HAL_UART_DMAStop(&huart6); 
    temp = UartCfg->rx_buf_size - hdma_usart6_rx.Instance->NDTR;
    if((temp > 0) && (UartCfg->rx_cb != NULL))
    {
      if(temp > UartCfg->rx_buf_size)
        temp = UartCfg->rx_buf_size;
      UartCfg->rx_cb(BSP_UART_6, UartCfg->rx_buf, (uint16_t)temp);
    }
    memset(UartCfg->rx_buf, 0, UartCfg->rx_buf_size);
    HAL_UART_Receive_DMA(&huart6, UartCfg->rx_buf, UartCfg->rx_buf_size);
  }
  /* USER CODE END USART6_IRQn 1 */
}


/*********************************************************************
 * @fn      bsp_uart_init
 * @brief   初始化 UART 外设
 * @param   huart  UART 句柄指针 (上层定义并传入)
 * @param   cfg    配置参数
 * @return  bool true=成功 false=失败
********************************************************************/
bool bsp_uart_init(bsp_uart_port_t port, const bsp_uart_config_t *cfg)
{
    UART_HandleTypeDef *huart = NULL;

    if((port >= BSP_UART_MAX)||
       (cfg == NULL)||
       (cfg->baudrate == 0)||
       (cfg->rx_buf == NULL)|| 
       (cfg->rx_buf_size == 0)||
       (cfg->rx_cb == NULL))
    {
      return false;
    }

    s_huartCfg_List[port].baudrate    = cfg->baudrate;
    s_huartCfg_List[port].rx_cb       = cfg->rx_cb;
    s_huartCfg_List[port].rx_buf      = cfg->rx_buf;
    s_huartCfg_List[port].rx_buf_size = cfg->rx_buf_size;
    s_Uart_TX_Ready[port] = true;  /* 初始状态: TX 空闲, 可发送 */
    huart = s_huart_List[port];

    /* ★ 设置 Instance, HAL_UART_MspInit 依赖此字段判断是哪个 UART ★ */
    switch(port)
    {
      case BSP_UART_1: huart->Instance = USART1; break;
      case BSP_UART_2: huart->Instance = USART2; break;
      case BSP_UART_3: huart->Instance = USART3; break;
      case BSP_UART_4: huart->Instance = UART4;  break;
      case BSP_UART_6: huart->Instance = USART6; break;
      default: return false;
    }

    huart->Init.BaudRate = cfg->baudrate;
    /* 位数 */
    switch (cfg->data_bits) {
       case BSP_UART_DATA_9B: 
         huart->Init.WordLength = UART_WORDLENGTH_9B; 
         break;
       default: 
         huart->Init.WordLength = UART_WORDLENGTH_8B; 
         break;
    }

    /* 停止位 */
    switch (cfg->stop_bits) {
        case BSP_UART_STOPBITS_2: 
          huart->Init.StopBits = UART_STOPBITS_2; 
          break;
        default:                  
          huart->Init.StopBits = UART_STOPBITS_1;
          break;
    }

    /* 校验 */
    switch (cfg->parity) {
        case BSP_UART_PARITY_EVEN: 
          huart->Init.Parity = UART_PARITY_EVEN;
          break;
        case BSP_UART_PARITY_ODD: 
          huart->Init.Parity = UART_PARITY_ODD; 
          break;
        default:                  
          huart->Init.Parity = UART_PARITY_NONE;
          break;
    }

    huart->Init.Mode         = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(huart) != HAL_OK)
    {
      return false;
    }

    __HAL_UART_CLEAR_IDLEFLAG(huart);
    //__HAL_UART_ENABLE_IT(huart, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);
    HAL_UART_Receive_DMA(huart, cfg->rx_buf, cfg->rx_buf_size);
    
    return true;
}

/*********************************************************************
 * @fn      bsp_uart_deinit
 * @brief   反初始化 UART 外设
 * @param   huart  UART 句柄指针
 * @return  bool true=成功 false=失败
********************************************************************/
bool bsp_uart_deinit(bsp_uart_port_t port)
{
  UART_HandleTypeDef *huart = NULL;
  if(port >= BSP_UART_MAX)
  {
    return false;
  }
    
  huart = s_huart_List[port];
  if (HAL_UART_DeInit(huart) != HAL_OK) 
  {
    return false;
  }

  return true;
}

/*********************************************************************
 * @fn      bsp_uart_send
 * @brief   阻塞发送
 * @param   huart       UART 句柄指针
 * @param   data        发送数据
 * @param   len         数据长度
 * @param   timeout_ms  超时时间
 * @return  bool true=成功 false=失败
********************************************************************/
bool bsp_uart_send(bsp_uart_port_t port, const uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
  uint8_t count = 0;
  UART_HandleTypeDef *huart = NULL;

  if(port >= BSP_UART_MAX || data == NULL || len == 0)
  {
    return false;
  }

  while(count < 3)
  {
	  count++;
	  if(s_Uart_TX_Ready[port] == true)
    {
	    s_Uart_TX_Ready[port] = false;
	    break;	
	  }
    HAL_Delay(20);
  }

  huart = s_huart_List[port];
  if(HAL_UART_Transmit_DMA(huart, (uint8_t *)data, len) != HAL_OK)
  {
    return false;
  }
  HAL_Delay(40);

  return true;
}












