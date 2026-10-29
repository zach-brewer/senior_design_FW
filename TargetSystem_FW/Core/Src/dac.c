/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    dac.c
  * @brief   This file provides code for the configuration
  *          of the DAC instances.
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
#include "dac.h"
#include "result.h"

/* USER CODE BEGIN 0 */
static uint32_t dac_value = 1861;
/* USER CODE END 0 */

DAC_HandleTypeDef hdac3;

/* DAC3 init function */
void MX_DAC3_Init(void)
{

  /* USER CODE BEGIN DAC3_Init 0 */

  /* USER CODE END DAC3_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN DAC3_Init 1 */

  /* USER CODE END DAC3_Init 1 */

  /** DAC Initialization
  */
  hdac3.Instance = DAC3;
  if (HAL_DAC_Init(&hdac3) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT1 config
  */
  sConfig.DAC_HighFrequency = DAC_HIGH_FREQUENCY_INTERFACE_MODE_AUTOMATIC;
  sConfig.DAC_DMADoubleDataMode = DISABLE;
  sConfig.DAC_SignedFormat = DISABLE;
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
  sConfig.DAC_Trigger2 = DAC_TRIGGER_NONE;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_DISABLE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_INTERNAL;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac3, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT2 config
  */
  if (HAL_DAC_ConfigChannel(&hdac3, &sConfig, DAC_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC3_Init 2 */

  /* USER CODE END DAC3_Init 2 */

}

void HAL_DAC_MspInit(DAC_HandleTypeDef* dacHandle)
{

  if(dacHandle->Instance==DAC3)
  {
  /* USER CODE BEGIN DAC3_MspInit 0 */

  /* USER CODE END DAC3_MspInit 0 */
    /* DAC3 clock enable */
    __HAL_RCC_DAC3_CLK_ENABLE();
  /* USER CODE BEGIN DAC3_MspInit 1 */

  /* USER CODE END DAC3_MspInit 1 */
  }
}

void HAL_DAC_MspDeInit(DAC_HandleTypeDef* dacHandle)
{

  if(dacHandle->Instance==DAC3)
  {
  /* USER CODE BEGIN DAC3_MspDeInit 0 */

  /* USER CODE END DAC3_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_DAC3_CLK_DISABLE();
  /* USER CODE BEGIN DAC3_MspDeInit 1 */

  /* USER CODE END DAC3_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
Result_t DAC_SetValue(uint32_t val)
{
    if (val > DAC_FULL_SCALE)
        return R_InvalidArgument;

    uint32_t previous = dac_value;

    if (HAL_DAC_SetValue(&hdac3, DAC_CHANNEL_1, DAC_ALIGN_12B_R, val) != HAL_OK)
    {
        return R_DacError;
    }

    if (HAL_DAC_SetValue(&hdac3,DAC_CHANNEL_2, DAC_ALIGN_12B_R, val) != HAL_OK)
    {
        HAL_DAC_SetValue(&hdac3, DAC_CHANNEL_1, DAC_ALIGN_12B_R,previous);
        return R_DacError;
    }
    dac_value = val;

    return R_Ok;
}

uint32_t DAC_GetValue(void)
{
  return dac_value;
}

Result_t DAC_SetThreshold_mV(uint32_t millivolts)
{
    if (millivolts > DAC_REFERENCE_MV)
        return R_InvalidArgument;

    uint32_t code =
        (millivolts * DAC_FULL_SCALE
         + DAC_REFERENCE_MV / 2U)
        / DAC_REFERENCE_MV;

    return DAC_SetValue(code);
}

uint32_t DAC_GetThreshold_mV(void)
{
    uint32_t code = DAC_GetValue();

    return (code * DAC_REFERENCE_MV
            + DAC_FULL_SCALE / 2U)
           / DAC_FULL_SCALE;
}

/* USER CODE END 1 */

