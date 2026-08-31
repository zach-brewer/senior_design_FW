/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
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
#include "adc.h"
#include "i2c.h"
#include "stm32l4xx_hal_gpio.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define I2C_ADDR 0x42

#define CMD_START_CAPTURE 0x01
#define CMD_SEND_STATUS   0x02

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

volatile uint32_t capture_ch1 = 0;
volatile uint32_t capture_ch2 = 0;
volatile uint32_t capture_ch3 = 0;
volatile uint32_t capture_ch4 = 0;

volatile uint8_t ch1_captured = 0;
volatile uint8_t ch2_captured = 0;
volatile uint8_t ch3_captured = 0;
volatile uint8_t ch4_captured = 0;

volatile uint8_t capture_active = 0;
volatile uint8_t capture_complete = 0;

uint8_t i2c_rx_command = 0;
uint8_t i2c_tx_buffer[16];



/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

void StartCapture(void);
static void format_uint32(uint8_t *buffer, uint32_t value);
static void prepare_i2c_packet(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC3_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */

  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);
  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_2);
  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_3);
  HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_4);

  HAL_GPIO_WritePin(DATA_READY_GPIO_Port, DATA_READY_Pin, GPIO_PIN_RESET);

  /* USER CODE END 2 */

  /* Initialize leds */
  BSP_LED_Init(LED_GREEN);

  /* Initialize USER push-button, will be used to trigger an interrupt each time it's pressed.*/
  BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI);

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
/*
     * Keep READY low while waiting for a command.
     */
    HAL_GPIO_WritePin(DATA_READY_GPIO_Port,
                      DATA_READY_Pin,
                      GPIO_PIN_RESET);
     HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);
    /*
     * Wait for one command from the Arduino.
     *
     * The Arduino sends:
     *     0x01 = start capture
     */
    i2c_rx_command = 0;

    if (HAL_I2C_Slave_Receive(&hi2c1,
                              &i2c_rx_command,
                              1,
                              HAL_MAX_DELAY) != HAL_OK)
    {
        continue;
    }

    /*
     * Start a new TIM2 measurement.
     */
    if (i2c_rx_command == CMD_START_CAPTURE)
    {
        StartCapture();
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 1);


        /*
         * Wait until all four channels have captured.
         *
         * TIM2 interrupts continue to operate while
         * the CPU waits here.
         */
        while (!capture_complete)
        {
            /*
             * Nothing needed here.
             */
        }
        HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, 0);

        /*
         * Prepare the 16-byte timestamp packet.
         */
        prepare_i2c_packet();

        /*
         * Tell the Arduino that all timestamps are ready.
         */
        HAL_GPIO_WritePin(DATA_READY_GPIO_Port,
                          DATA_READY_Pin,
                          GPIO_PIN_SET);

        /*
         * Wait for the Arduino master to request
         * the 16-byte packet.
         */
        if (HAL_I2C_Slave_Transmit(&hi2c1,
                                   i2c_tx_buffer,
                                   sizeof(i2c_tx_buffer),
                                   1000) != HAL_OK)
        {
            /*
             * I2C transaction failed.
             * Return to idle state.
             */
        }

        /*
         * Data has been sent.
         */
        HAL_GPIO_WritePin(DATA_READY_GPIO_Port,
                          DATA_READY_Pin,
                          GPIO_PIN_RESET);

        capture_complete = 0;
        capture_active = 0;
    }
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2)
        return;

    if (!capture_active)
        return;

    switch (htim->Channel)
    {
        case HAL_TIM_ACTIVE_CHANNEL_1:

            if (!ch1_captured)
            {
                capture_ch1 =
                    HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

                ch1_captured = 1;
            }

            break;

        case HAL_TIM_ACTIVE_CHANNEL_2:

            if (!ch2_captured)
            {
                capture_ch2 =
                    HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);

                ch2_captured = 1;
            }

            break;

        case HAL_TIM_ACTIVE_CHANNEL_3:

            if (!ch3_captured)
            {
                capture_ch3 =
                    HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_3);

                ch3_captured = 1;
            }

            break;

        case HAL_TIM_ACTIVE_CHANNEL_4:

            if (!ch4_captured)
            {
                capture_ch4 =
                    HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_4);

                ch4_captured = 1;
            }

            break;

        default:
            break;
    }

    /*
     * Once all four channels have captured an edge,
     * the measurement is complete.
     */
    if (ch1_captured && ch2_captured && ch3_captured && ch4_captured)
    {
        capture_active = 0;
        capture_complete = 1;
    }
}

void StartCapture(void)
{
    __disable_irq();

    /*
     * Clear previous capture values.
     */
    capture_ch1 = 0;
    capture_ch2 = 0;
    capture_ch3 = 0;
    capture_ch4 = 0;

    /*
     * Clear capture state.
     */
    ch1_captured = 0;
    ch2_captured = 0;
    ch3_captured = 0;
    ch4_captured = 0;

    capture_complete = 0;

    /*
     * Clear any pending TIM2 capture flags.
     */
    __HAL_TIM_CLEAR_FLAG(&htim2,
                         TIM_FLAG_CC1 |
                         TIM_FLAG_CC2 |
                         TIM_FLAG_CC3 |
                         TIM_FLAG_CC4);

    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);

    /*
     * Start this measurement at timer count = 0.
     */
    __HAL_TIM_SET_COUNTER(&htim2, 0);

    capture_active = 1;

    __enable_irq();
}

static void format_uint32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value);
    buffer[1] = (uint8_t)(value >> 8);
    buffer[2] = (uint8_t)(value >> 16);
    buffer[3] = (uint8_t)(value >> 24);
}

static void prepare_i2c_packet(void)
{
    format_uint32(&i2c_tx_buffer[0],  capture_ch1);
    format_uint32(&i2c_tx_buffer[4],  capture_ch2);
    format_uint32(&i2c_tx_buffer[8],  capture_ch3);
    format_uint32(&i2c_tx_buffer[12], capture_ch4);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line
     number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
     line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
