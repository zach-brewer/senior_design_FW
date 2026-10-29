#include "capture.h"
#include "tim.h"

static CaptureStatus_t status_t;



static void handle_trigger(TIM_HandleTypeDef *htim, uint32_t channel, uint8_t index)
{

    if ((status_t.captured & (1 << index)) == 0)
    {
        status_t.timestamps[index] = 
            HAL_TIM_ReadCapturedValue(htim, channel);
        status_t.captured |= (uint8_t)(1 << index);
        //HAL_TIM_IC_Stop_IT(htim, channel);
    }
}

Result_t Capture_Initialize()
{
    status_t.capture_armed = 0;
    status_t.capture_complete = 0;
    status_t.captured = 0;
    status_t.error = 0;
    for(uint8_t i = 0; i < NUM_CHANNELS; i++)
        status_t.timestamps[i] = 0;
    return R_Ok;
}

static Result_t TIM2_CaptureHandler(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2)
        return R_ValueError;
    switch (htim->Channel)
    {
        case HAL_TIM_ACTIVE_CHANNEL_1:
        
            handle_trigger(htim, TIM_CHANNEL_1, 0);
            break;
        case HAL_TIM_ACTIVE_CHANNEL_2:
            handle_trigger(htim, TIM_CHANNEL_2, 1);
            break;        
        case HAL_TIM_ACTIVE_CHANNEL_3:
            handle_trigger(htim, TIM_CHANNEL_3, 2);
            break;
        case HAL_TIM_ACTIVE_CHANNEL_4:
            handle_trigger(htim, TIM_CHANNEL_4, 3);
            break;  
        default:
            break;      
    }
    return R_Ok;
}


// Result_t Capture_Start(void)
// {
//     if(status_t.capture_armed)
//         return R_Busy;

//     status_t.capture_armed = 0;
//     status_t.capture_complete = 0;
//     status_t.captured = 0;
//     status_t.error = 0;

//     for (uint8_t i = 0; i < NUM_CHANNELS; i++)
//     {
//         status_t.timestamps[i]=0;
//     }
//     __HAL_TIM_SET_COUNTER(&htim2, 0U);
//     __HAL_TIM_CLEAR_FLAG(&htim2,
//                         TIM_FLAG_CC1 |
//                         TIM_FLAG_CC2 |
//                         TIM_FLAG_CC3 |
//                         TIM_FLAG_CC4);

//     HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);

//     if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1) != HAL_OK){
//         (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_1);
//         return R_TimerError;
//     }
//     if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_2) != HAL_OK){
//         (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_1);
//         (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_2);

//         return R_TimerError;
//     }
//     if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_3) != HAL_OK){
//         (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_1);
//         (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_2);
//         (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_3);
//         return R_TimerError;
//     }
//     if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_4) != HAL_OK){
//         (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_1);
//         (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_2);
//         (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_3);
//         (void)HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_4);
//         return R_TimerError;
//     }
    
//     status_t.capture_armed = 1;
    
//     return R_Ok;
// }

Result_t Capture_Start(void)
{
    if (status_t.capture_armed)
        return R_Busy;

    status_t.capture_armed = 0U;
    status_t.capture_complete = 0U;
    status_t.captured = 0U;
    status_t.error = 0U;

    for (uint8_t i = 0U; i < NUM_CHANNELS; ++i)
        status_t.timestamps[i] = 0U;

    /* Make sure TIM2 is stopped before preparing this acquisition. */
    __HAL_TIM_DISABLE(&htim2);

    __HAL_TIM_SET_COUNTER(&htim2, 0U);

    __HAL_TIM_CLEAR_FLAG(&htim2,
                         TIM_FLAG_CC1 |
                         TIM_FLAG_CC2 |
                         TIM_FLAG_CC3 |
                         TIM_FLAG_CC4 |
                         TIM_FLAG_CC1OF |
                         TIM_FLAG_CC2OF |
                         TIM_FLAG_CC3OF |
                         TIM_FLAG_CC4OF);

    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);

    /*
     * Start all capture channels while capture_armed is still zero.
     * The first HAL start enables the timer counter, so early events
     * must be ignored and cleared before the acquisition is armed.
     */

    if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1) != HAL_OK)
        goto start_error;

    if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_2) != HAL_OK)
        goto start_error;

    if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_3) != HAL_OK)
        goto start_error;

    if (HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_4) != HAL_OK)
        goto start_error;

    /*
     * All channels are now enabled. Establish the actual acquisition
     * baseline while interrupts are masked.
     */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    __HAL_TIM_DISABLE(&htim2);

    __HAL_TIM_SET_COUNTER(&htim2, 0U);

    __HAL_TIM_CLEAR_FLAG(&htim2,
                         TIM_FLAG_CC1 |
                         TIM_FLAG_CC2 |
                         TIM_FLAG_CC3 |
                         TIM_FLAG_CC4 |
                         TIM_FLAG_CC1OF |
                         TIM_FLAG_CC2OF |
                         TIM_FLAG_CC3OF |
                         TIM_FLAG_CC4OF);

    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);

    status_t.capture_armed = 1U;

    __HAL_TIM_ENABLE(&htim2);

    if (primask == 0U)
        __enable_irq();

    return R_Ok;

start_error:
    (void)Capture_Stop();
    status_t.capture_armed = 0U;
    status_t.capture_complete = 0U;
    status_t.captured = 0U;
    status_t.error = (uint8_t)R_TimerError;

    return R_TimerError;
}

Result_t Capture_Stop(void)
{
    Result_t res = R_Ok;

    status_t.capture_armed = 0;

    if (HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_1) != HAL_OK)
        res = R_TimerError;
    if (HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_2) != HAL_OK)
        res = R_TimerError;
    if (HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_3) != HAL_OK)
        res = R_TimerError;
    if (HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_4) != HAL_OK)
        res = R_TimerError;


    __HAL_TIM_CLEAR_FLAG(&htim2,
                         TIM_FLAG_CC1 | 
                         TIM_FLAG_CC2 |
                         TIM_FLAG_CC3 | 
                         TIM_FLAG_CC4);
 

    HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
    return res;
}

uint8_t Capture_IsComplete(void)
{
    return status_t.capture_complete;
}

Result_t Capture_GetStatus(CaptureStatus_t *stat)
{
    if (stat == NULL)
        return R_InvalidArgument;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    *stat = status_t;

    if (primask == 0U)
        __enable_irq();

    return R_Ok;
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (!status_t.capture_armed)
        return;
    if(htim->Instance != TIM2)
    {
        return;
    }
    Result_t res = TIM2_CaptureHandler(htim);
    if (res != R_Ok)
    {
    status_t.error = (uint8_t)res;
    status_t.capture_armed = 0U;
    return;
    }
    if ((status_t.captured & CAPTURE_MASK) == CAPTURE_MASK)
    {
        status_t.capture_complete = 1U;
        status_t.capture_armed = 0U;
    }
}

