/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

#include "stm32g4xx_nucleo.h"
#include <stdio.h>

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define RCC_OSC32_IN_Pin GPIO_PIN_14
#define RCC_OSC32_IN_GPIO_Port GPIOC
#define RCC_OSC32_OUT_Pin GPIO_PIN_15
#define RCC_OSC32_OUT_GPIO_Port GPIOC
#define RCC_OSC_IN_Pin GPIO_PIN_0
#define RCC_OSC_IN_GPIO_Port GPIOF
#define RCC_OSC_OUT_Pin GPIO_PIN_1
#define RCC_OSC_OUT_GPIO_Port GPIOF
#define MIC_3_Pin GPIO_PIN_0
#define MIC_3_GPIO_Port GPIOA
#define MIC_1_Pin GPIO_PIN_1
#define MIC_1_GPIO_Port GPIOA
#define MIC_2_Pin GPIO_PIN_7
#define MIC_2_GPIO_Port GPIOA
#define MIC_4_Pin GPIO_PIN_0
#define MIC_4_GPIO_Port GPIOB
#define MIC_5_Pin GPIO_PIN_13
#define MIC_5_GPIO_Port GPIOB
#define MIC_6_Pin GPIO_PIN_14
#define MIC_6_GPIO_Port GPIOB
#define ETH_RST_Pin GPIO_PIN_9
#define ETH_RST_GPIO_Port GPIOC
#define T_SWDIO_Pin GPIO_PIN_13
#define T_SWDIO_GPIO_Port GPIOA
#define T_SWCLK_Pin GPIO_PIN_14
#define T_SWCLK_GPIO_Port GPIOA
#define ETH_SCK_Pin GPIO_PIN_10
#define ETH_SCK_GPIO_Port GPIOC
#define ETH_MISO_Pin GPIO_PIN_11
#define ETH_MISO_GPIO_Port GPIOC
#define ETH_MOSI_Pin GPIO_PIN_12
#define ETH_MOSI_GPIO_Port GPIOC
#define ETH_CS_Pin GPIO_PIN_2
#define ETH_CS_GPIO_Port GPIOD
#define T_SWO_Pin GPIO_PIN_3
#define T_SWO_GPIO_Port GPIOB
#define ETH_IRQ_Pin GPIO_PIN_9
#define ETH_IRQ_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
