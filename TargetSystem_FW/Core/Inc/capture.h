/**
  ******************************************************************************
  * @file    capture.h
  * @brief   Input Capture Helper File
  ******************************************************************************
  *          Author: Zach Brewer
  *             
  ******************************************************************************
  */
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>
#include "result.h"
#include "tim.h"
#define NUM_CHANNELS 4

#if NUM_CHANNELS == 4
    #define CAPTURE_MASK 0x0F
#elif NUM_CHANNELS == 6
    #define CAPTURE_MASK 0x3F
#endif


typedef struct
{
    volatile uint8_t capture_armed;
    volatile uint8_t capture_complete;
    volatile uint32_t timestamps[NUM_CHANNELS];
    volatile uint8_t captured;
    volatile uint8_t error;

} CaptureStatus_t;


Result_t Capture_Initialize(void);
Result_t Capture_Start(void);
Result_t Capture_Stop(void);
uint8_t Capture_IsComplete(void);
Result_t Capture_GetStatus(CaptureStatus_t *stat);
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim);


#endif