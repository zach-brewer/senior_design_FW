#include "capture.h"
#include "tim.h"

static void handle_trigger(TIM_HandleTypeDef *htim, int channel, uint8_t index)
{
    if (!status.captured[index])
    {
        status.timestamps[index] = 
            HAL_TIM_ReadCapturedValue(htim, channel);
        status.captured |= 1 << index;
        HAL_TIM_IC_Stop_IT(htim, channel);
    }
}

Result_t TIM2_CaptureHandler(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2)
        return R_ValueError;
    switch (htim->Channel)
    {
        case HAL_TIM_ACTIVE_CHANNEL_1:
            handle_trigger(&htim, TIM_CHANNEL_1, 0);
            break;
        case HAL_TIM_ACTIVE_CHANNEL_2:
            handle_trigger(&htim, TIM_CHANNEL_2, 1);
            break;        
        case HAL_TIM_ACTIVE_CHANNEL_3:
            handle_trigger(&htim, TIM_CHANNEL_3, 2);
            break;
        case HAL_TIM_ACTIVE_CHANNEL_4:
            handle_trigger(&htim, TIM_CHANNEL_4, 3);
            break;        
    }
    return R_Ok;
}
Result_t TIM5_CaptureHandler(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2)
        return R_ValueError;
    switch (htim->Channel)
    {
        case HAL_TIM_ACTIVE_CHANNEL_1:
            handle_trigger(&htim, TIM_CHANNEL_1, 4);
            break;
        case HAL_TIM_ACTIVE_CHANNEL_2:
            handle_trigger(&htim, TIM_CHANNEL_2, 5);
            break;    
    }
    return R_Ok;
}

Result_t Capture_Arm(void)
{
    if(status.capture_armed)
        return R_Busy;

    status.capture_armed = 0;
    status.capture_complete = 0;
    for (uint8_t i = 0; i < NUM_CHANNELS; i++)
    {
        status.timestamps[i]=0;
        status.captured[i] = 0;
    }

    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    __HAL_TIM_CLEAR_FLAG(&htim2,
                        TIM_FLAG_CC1 |
                        TIM_FLAG_CC2 |
                        TIM_FLAG_CC3 |
                        TIM_FLAG_CC4);

    __HAL_TIM_CLEAR_FLAG(&htim5,
                        TIM_FLAG_CC1 |
                        TIM_FLAG_CC2);
    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
    HAL_NVIC_ClearPendingIRQ(TIM5_IRQn);

    if (HAL_TIM_IC_START_IT(*htim2, TIM_CHANNEL_1) != HAL_OK){
        (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_1);
        return R_TimerError;
    }
    if (HAL_TIM_IC_START_IT(*htim2, TIM_CHANNEL_2) != HAL_OK){
        (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_2);
        return R_TimerError;
    }
    if (HAL_TIM_IC_START_IT(*htim2, TIM_CHANNEL_3) != HAL_OK){
        (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_3);
        return R_TimerError;
    }
    if (HAL_TIM_IC_START_IT(*htim2, TIM_CHANNEL_4) != HAL_OK){
        (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_4);
        return R_TimerError;
    }

    if (HAL_TIM_IC_START_IT(*htim5, TIM_CHANNEL_1) != HAL_OK){
        (void)HAL_TIM_IC_Stop_IT(&htim5, TIM_CHANNEL_1);
        return R_TimerError;
    }
    if (HAL_TIM_IC_START_IT(*htim5, TIM_CHANNEL_2) != HAL_OK){
        (void)HAL_TIM_IC_Stop_IT(&htim5, TIM_CHANNEL_2);
        return R_TimerError;
    }
    

    status.capture_armed = 1;
    __HAL_TIM_SET_COUNTER(&htim1,0);
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    
    __HAL_TIM_GENERATE_EVENT(&htim1, TIM_EVENTSOURCE_UPDATE);

    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    return R_Ok;
}

Result_t Capture_Stop(void)
{
    Result_t res = R_Ok;

    captureArmed = 0;

    if (HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_1) != HAL_OK)
        res = R_TimerError;
    if (HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_2) != HAL_OK)
        res = R_TimerError;
    if (HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_3) != HAL_OK)
        res = R_TimerError;
    if (HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_4) != HAL_OK)
        res = R_TimerError;

    if (HAL_TIM_IC_Stop_IT(&htim5, TIM_CHANNEL_1) != HAL_OK)
        res = R_TimerError;
    if (HAL_TIM_IC_Stop_IT(&htim5, TIM_CHANNEL_2) != HAL_OK)
        res = R_TimerError;

    __HAL_TIM_CLEAR_FLAG(&htim2,
                         TIM_FLAG_CC1 | 
                         TIM_FLAG_CC2 |
                         TIM_FLAG_CC3 | 
                         TIM_FLAG_CC4);

    __HAL_TIM_CLEAR_FLAG(&htim5,
                         TIM_FLAG_CC1 | 
                         TIM_FLAG_CC2); 
    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
    HAL_NVIC_ClearPendingIRQ(TIM5_IRQn);   
    return res;
}

uint8_t Capture_IsComplete(void)
{
    return status.capture_complete;
}

Result_t Capture_GetStatus(CaptureStatus_t *stat)
{
    stat = &status;
    return R_Ok;
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (!status.capture_armed)
        return;
    if(htim->Instance == TIM2)
    {
        TIM2_CaptureHandler(&htim);
    }
    else if (htim->Instance == TIM5)
    {
        TIM5_CaptureHandler(&hitm);
    }

    if (captured == CAPTURE_MASK)
    {
        status.capture_complete = 1;
        status.capture_armed = 0;
        
        HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_1);
        HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_2);
        HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_3);
        HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_4);
        HAL_TIM_IC_Stop_IT(&htim5, TIM_CHANNEL_1);
        HAL_TIM_IC_Stop_IT(&htim5, TIM_CHANNEL_2);

    }
}

