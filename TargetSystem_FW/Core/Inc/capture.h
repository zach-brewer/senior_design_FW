#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>
#include "result.h"

#define NUM_CHANNELS 6

#define CAPTURE_MASK 0x3F // set for 6 channels changing NUM_CHANNELS requires this value to be changed as well



typedef struct
{
    volatile uint8_t capture_armed = 0;
    volatile uint8_t capture_complete = 0;
    volatile uint32_t timestamp[NUM_CHANNELS];
    volatile uint8_t captured = 0;
    volatile uint8_t error = 0;

} CaptureStatus_t;

CaptureStatus_t status;

Result_t Capture_Arm(void);
Result_t Capture_Stop(void);
uint8_t Capture_IsComplete(void);
Result_t Capture_GetStatus(CaptureStatus_t *stat);
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim);


#endif