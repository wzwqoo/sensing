/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file         stm32h7xx_hal_msp.c
  * @brief        This file provides code for the MSP Initialization
  *               and de-Initialization codes.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */
extern DMA_HandleTypeDef hdma_fmac_rd;

extern DMA_HandleTypeDef hdma_fmac_wr;

extern DMA_HandleTypeDef hdma_pssi;

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN Define */

/* USER CODE END Define */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN Macro */

/* USER CODE END Macro */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */

/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);
                    /**
  * Initializes the Global MSP.
  */
void HAL_MspInit(void)
{

  /* USER CODE BEGIN MspInit 0 */

  /* USER CODE END MspInit 0 */

  __HAL_RCC_SYSCFG_CLK_ENABLE();

  /* System interrupt init*/
  /* PendSV_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(PendSV_IRQn, 15, 0);

  /* USER CODE BEGIN MspInit 1 */

  /* USER CODE END MspInit 1 */
}

/**
  * @brief CORDIC MSP Initialization
  * This function configures the hardware resources used in this example
  * @param hcordic: CORDIC handle pointer
  * @retval None
  */
void HAL_CORDIC_MspInit(CORDIC_HandleTypeDef* hcordic)
{
  if(hcordic->Instance==CORDIC)
  {
    /* USER CODE BEGIN CORDIC_MspInit 0 */

    /* USER CODE END CORDIC_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_CORDIC_CLK_ENABLE();
    /* USER CODE BEGIN CORDIC_MspInit 1 */

    /* USER CODE END CORDIC_MspInit 1 */

  }

}

/**
  * @brief CORDIC MSP De-Initialization
  * This function freeze the hardware resources used in this example
  * @param hcordic: CORDIC handle pointer
  * @retval None
  */
void HAL_CORDIC_MspDeInit(CORDIC_HandleTypeDef* hcordic)
{
  if(hcordic->Instance==CORDIC)
  {
    /* USER CODE BEGIN CORDIC_MspDeInit 0 */

    /* USER CODE END CORDIC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_CORDIC_CLK_DISABLE();
    /* USER CODE BEGIN CORDIC_MspDeInit 1 */

    /* USER CODE END CORDIC_MspDeInit 1 */
  }

}

/**
  * @brief FMAC MSP Initialization
  * This function configures the hardware resources used in this example
  * @param hfmac: FMAC handle pointer
  * @retval None
  */
void HAL_FMAC_MspInit(FMAC_HandleTypeDef* hfmac)
{
  if(hfmac->Instance==FMAC)
  {
    /* USER CODE BEGIN FMAC_MspInit 0 */

    /* USER CODE END FMAC_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_FMAC_CLK_ENABLE();

    /* FMAC DMA Init */
    /* FMAC_RD Init */
    hdma_fmac_rd.Instance = DMA1_Stream1;
    hdma_fmac_rd.Init.Request = DMA_REQUEST_FMAC_READ;
    hdma_fmac_rd.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_fmac_rd.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_fmac_rd.Init.MemInc = DMA_MINC_ENABLE;
    hdma_fmac_rd.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_fmac_rd.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_fmac_rd.Init.Mode = DMA_CIRCULAR;
    hdma_fmac_rd.Init.Priority = DMA_PRIORITY_LOW;
    hdma_fmac_rd.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_fmac_rd) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(hfmac,hdmaOut,hdma_fmac_rd);

    /* FMAC_WR Init */
    hdma_fmac_wr.Instance = DMA1_Stream2;
    hdma_fmac_wr.Init.Request = DMA_REQUEST_FMAC_WRITE;
    hdma_fmac_wr.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_fmac_wr.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_fmac_wr.Init.MemInc = DMA_MINC_ENABLE;
    hdma_fmac_wr.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_fmac_wr.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_fmac_wr.Init.Mode = DMA_CIRCULAR;
    hdma_fmac_wr.Init.Priority = DMA_PRIORITY_LOW;
    hdma_fmac_wr.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_fmac_wr) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(hfmac,hdmaIn,hdma_fmac_wr);

    /* FMAC interrupt Init */
    HAL_NVIC_SetPriority(FMAC_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(FMAC_IRQn);
    /* USER CODE BEGIN FMAC_MspInit 1 */

    /* USER CODE END FMAC_MspInit 1 */

  }

}

/**
  * @brief FMAC MSP De-Initialization
  * This function freeze the hardware resources used in this example
  * @param hfmac: FMAC handle pointer
  * @retval None
  */
void HAL_FMAC_MspDeInit(FMAC_HandleTypeDef* hfmac)
{
  if(hfmac->Instance==FMAC)
  {
    /* USER CODE BEGIN FMAC_MspDeInit 0 */

    /* USER CODE END FMAC_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_FMAC_CLK_DISABLE();

    /* FMAC DMA DeInit */
    HAL_DMA_DeInit(hfmac->hdmaOut);
    HAL_DMA_DeInit(hfmac->hdmaIn);

    /* FMAC interrupt DeInit */
    HAL_NVIC_DisableIRQ(FMAC_IRQn);
    /* USER CODE BEGIN FMAC_MspDeInit 1 */

    /* USER CODE END FMAC_MspDeInit 1 */
  }

}

/**
  * @brief PSSI MSP Initialization
  * This function configures the hardware resources used in this example
  * @param hpssi: PSSI handle pointer
  * @retval None
  */
void HAL_PSSI_MspInit(PSSI_HandleTypeDef* hpssi)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(hpssi->Instance==PSSI)
  {
    /* USER CODE BEGIN PSSI_MspInit 0 */

    /* USER CODE END PSSI_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_DCMI_PSSI_CLK_ENABLE();

    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**PSSI GPIO Configuration
    PE4     ------> PSSI_D4
    PE5     ------> PSSI_D6
    PE6     ------> PSSI_D7
    PA4     ------> PSSI_DE
    PA6     ------> PSSI_PDCK
    PC6     ------> PSSI_D0
    PC7     ------> PSSI_D1
    PC8     ------> PSSI_D2
    PC9     ------> PSSI_D3
    PD3     ------> PSSI_D5
    PB7     ------> PSSI_RDY
    */
    GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF13_PSSI;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_6;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF13_PSSI;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF13_PSSI;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF13_PSSI;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF13_PSSI;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* PSSI DMA Init */
    /* PSSI Init */
    hdma_pssi.Instance = DMA1_Stream0;
    hdma_pssi.Init.Request = DMA_REQUEST_DCMI_PSSI;
    hdma_pssi.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_pssi.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_pssi.Init.MemInc = DMA_MINC_ENABLE;
    hdma_pssi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_pssi.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_pssi.Init.Mode = DMA_CIRCULAR;
    hdma_pssi.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_pssi.Init.FIFOMode = DMA_FIFOMODE_ENABLE;
    hdma_pssi.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_HALFFULL;
    hdma_pssi.Init.MemBurst = DMA_MBURST_SINGLE;
    hdma_pssi.Init.PeriphBurst = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&hdma_pssi) != HAL_OK)
    {
      Error_Handler();
    }

    /* Several peripheral DMA handle pointers point to the same DMA handle.
     Be aware that there is only one channel to perform all the requested DMAs. */
    __HAL_LINKDMA(hpssi,hdmarx,hdma_pssi);
    __HAL_LINKDMA(hpssi,hdmatx,hdma_pssi);

    /* PSSI interrupt Init */
    HAL_NVIC_SetPriority(DCMI_PSSI_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DCMI_PSSI_IRQn);
    /* USER CODE BEGIN PSSI_MspInit 1 */

    /* USER CODE END PSSI_MspInit 1 */

  }

}

/**
  * @brief PSSI MSP De-Initialization
  * This function freeze the hardware resources used in this example
  * @param hpssi: PSSI handle pointer
  * @retval None
  */
void HAL_PSSI_MspDeInit(PSSI_HandleTypeDef* hpssi)
{
  if(hpssi->Instance==PSSI)
  {
    /* USER CODE BEGIN PSSI_MspDeInit 0 */

    /* USER CODE END PSSI_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_DCMI_PSSI_CLK_DISABLE();

    /**PSSI GPIO Configuration
    PE4     ------> PSSI_D4
    PE5     ------> PSSI_D6
    PE6     ------> PSSI_D7
    PA4     ------> PSSI_DE
    PA6     ------> PSSI_PDCK
    PC6     ------> PSSI_D0
    PC7     ------> PSSI_D1
    PC8     ------> PSSI_D2
    PC9     ------> PSSI_D3
    PD3     ------> PSSI_D5
    PB7     ------> PSSI_RDY
    */
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6);

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_4|GPIO_PIN_6);

    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9);

    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_3);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_7);

    /* PSSI DMA DeInit */
    HAL_DMA_DeInit(hpssi->hdmarx);
    HAL_DMA_DeInit(hpssi->hdmatx);

    /* PSSI interrupt DeInit */
    HAL_NVIC_DisableIRQ(DCMI_PSSI_IRQn);
    /* USER CODE BEGIN PSSI_MspDeInit 1 */

    /* USER CODE END PSSI_MspDeInit 1 */
  }

}

/**
  * @brief SPI MSP Initialization
  * This function configures the hardware resources used in this example
  * @param hspi: SPI handle pointer
  * @retval None
  */
void HAL_SPI_MspInit(SPI_HandleTypeDef* hspi)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(hspi->Instance==SPI4)
  {
    /* USER CODE BEGIN SPI4_MspInit 0 */

    /* USER CODE END SPI4_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SPI4;
    PeriphClkInitStruct.Spi45ClockSelection = RCC_SPI45CLKSOURCE_D2PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    /* Peripheral clock enable */
    __HAL_RCC_SPI4_CLK_ENABLE();

    __HAL_RCC_GPIOE_CLK_ENABLE();
    /**SPI4 GPIO Configuration
    PE12     ------> SPI4_SCK
    PE13     ------> SPI4_MISO
    PE14     ------> SPI4_MOSI
    */
    GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI4;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* SPI4 interrupt Init */
    HAL_NVIC_SetPriority(SPI4_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SPI4_IRQn);
    /* USER CODE BEGIN SPI4_MspInit 1 */

    /* USER CODE END SPI4_MspInit 1 */

  }

}

/**
  * @brief SPI MSP De-Initialization
  * This function freeze the hardware resources used in this example
  * @param hspi: SPI handle pointer
  * @retval None
  */
void HAL_SPI_MspDeInit(SPI_HandleTypeDef* hspi)
{
  if(hspi->Instance==SPI4)
  {
    /* USER CODE BEGIN SPI4_MspDeInit 0 */

    /* USER CODE END SPI4_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_SPI4_CLK_DISABLE();

    /**SPI4 GPIO Configuration
    PE12     ------> SPI4_SCK
    PE13     ------> SPI4_MISO
    PE14     ------> SPI4_MOSI
    */
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14);

    /* SPI4 interrupt DeInit */
    HAL_NVIC_DisableIRQ(SPI4_IRQn);
    /* USER CODE BEGIN SPI4_MspDeInit 1 */

    /* USER CODE END SPI4_MspDeInit 1 */
  }

}

/**
  * @brief TIM_Base MSP Initialization
  * This function configures the hardware resources used in this example
  * @param htim_base: TIM_Base handle pointer
  * @retval None
  */
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* htim_base)
{
  if(htim_base->Instance==TIM12)
  {
    /* USER CODE BEGIN TIM12_MspInit 0 */

    /* USER CODE END TIM12_MspInit 0 */
    /* Peripheral clock enable */
    __HAL_RCC_TIM12_CLK_ENABLE();
    /* TIM12 interrupt Init */
    HAL_NVIC_SetPriority(TIM8_BRK_TIM12_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(TIM8_BRK_TIM12_IRQn);
    /* USER CODE BEGIN TIM12_MspInit 1 */

    /* USER CODE END TIM12_MspInit 1 */

  }

}

void HAL_TIM_MspPostInit(TIM_HandleTypeDef* htim)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(htim->Instance==TIM12)
  {
    /* USER CODE BEGIN TIM12_MspPostInit 0 */

    /* USER CODE END TIM12_MspPostInit 0 */

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**TIM12 GPIO Configuration
    PB14     ------> TIM12_CH1
    */
    GPIO_InitStruct.Pin = GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF2_TIM12;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* USER CODE BEGIN TIM12_MspPostInit 1 */

    /* USER CODE END TIM12_MspPostInit 1 */
  }

}
/**
  * @brief TIM_Base MSP De-Initialization
  * This function freeze the hardware resources used in this example
  * @param htim_base: TIM_Base handle pointer
  * @retval None
  */
void HAL_TIM_Base_MspDeInit(TIM_HandleTypeDef* htim_base)
{
  if(htim_base->Instance==TIM12)
  {
    /* USER CODE BEGIN TIM12_MspDeInit 0 */

    /* USER CODE END TIM12_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_TIM12_CLK_DISABLE();

    /* TIM12 interrupt DeInit */
    HAL_NVIC_DisableIRQ(TIM8_BRK_TIM12_IRQn);
    /* USER CODE BEGIN TIM12_MspDeInit 1 */

    /* USER CODE END TIM12_MspDeInit 1 */
  }

}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
