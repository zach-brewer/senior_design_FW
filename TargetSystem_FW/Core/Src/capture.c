#include "capture.h"
#include "tim.h"

static CaptureStatus_t status_t;

static void handle_trigger(TIM_HandleTypeDef *htim, uint32_t channel,
                           uint8_t index, uint32_t interrupt) {
  uint8_t bit = (uint8_t)(1U << index);

  /* Do not process channels that were not selected. */
  if ((status_t.active_mask & bit) == 0U)
    return;

  /* Preserve the first timestamp only. */
  if ((status_t.captured & bit) == 0U) {
    status_t.timestamps[index] = HAL_TIM_ReadCapturedValue(htim, channel);

    status_t.captured |= bit;

    /*
     * Disable only this channel's interrupt.
     * Do not stop the shared TIM2 counter.
     */
    __HAL_TIM_DISABLE_IT(htim, interrupt);
  }
}

Result_t Capture_Initialize() {
  status_t.capture_armed = 0;
  status_t.capture_complete = 0;
  status_t.captured = 0;
  status_t.error = 0;
  status_t.active_mask = 0;
  for (uint8_t i = 0; i < NUM_CHANNELS; i++)
    status_t.timestamps[i] = 0;
  return R_Ok;
}

static Result_t TIM2_CaptureHandler(TIM_HandleTypeDef *htim) {
  if (htim->Instance != TIM2)
    return R_ValueError;
  switch (htim->Channel) {
  case HAL_TIM_ACTIVE_CHANNEL_1:
    handle_trigger(htim, TIM_CHANNEL_1, 0U, TIM_IT_CC1);
    break;

  case HAL_TIM_ACTIVE_CHANNEL_2:
    handle_trigger(htim, TIM_CHANNEL_2, 1U, TIM_IT_CC2);
    break;

  case HAL_TIM_ACTIVE_CHANNEL_3:
    handle_trigger(htim, TIM_CHANNEL_3, 2U, TIM_IT_CC3);
    break;

  case HAL_TIM_ACTIVE_CHANNEL_4:
    handle_trigger(htim, TIM_CHANNEL_4, 3U, TIM_IT_CC4);
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

Result_t Capture_Start(uint8_t channel_mask) {
  /* Reject zero and masks containing unsupported channel bits. */
  if (channel_mask == 0U || (channel_mask & (uint8_t)~CAPTURE_MASK) != 0U) {
    return R_InvalidArgument;
  }

  if (status_t.capture_armed != 0U)
    return R_Busy;

  /*
   * Ensure all channels from any previous acquisition are stopped.
   * Capture_Stop() also disables the shared counter.
   */
  Result_t stop_res = Capture_Stop();

  if (stop_res != R_Ok) {
    status_t.error = (uint8_t)stop_res;
    return stop_res;
  }

  status_t.capture_armed = 0U;
  status_t.capture_complete = 0U;
  status_t.captured = 0U;
  status_t.error = 0U;
  status_t.active_mask = channel_mask;

  for (uint8_t i = 0U; i < NUM_CHANNELS; ++i)
    status_t.timestamps[i] = 0U;

  __HAL_TIM_DISABLE(&htim2);
  __HAL_TIM_SET_COUNTER(&htim2, 0U);

  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_CC3 |
                                   TIM_FLAG_CC4 | TIM_FLAG_CC1OF |
                                   TIM_FLAG_CC2OF | TIM_FLAG_CC3OF |
                                   TIM_FLAG_CC4OF);

  HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);

  /*
   * Start only the selected input-capture channels.
   * The first HAL start enables the shared timer counter.
   * The callback ignores edges while capture_armed is zero.
   */

  if ((channel_mask & CAPTURE_CH1_MASK) != 0U &&
      HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1) != HAL_OK)
    goto start_error;

  if ((channel_mask & CAPTURE_CH2_MASK) != 0U &&
      HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_2) != HAL_OK)
    goto start_error;

  if ((channel_mask & CAPTURE_CH3_MASK) != 0U &&
      HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_3) != HAL_OK)
    goto start_error;

  if ((channel_mask & CAPTURE_CH4_MASK) != 0U &&
      HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_4) != HAL_OK)
    goto start_error;

  /*
   * Establish a clean timebase after the selected channels are ready.
   * Temporarily mask interrupts to avoid a partially initialized state.
   */
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  __HAL_TIM_DISABLE(&htim2);
  __HAL_TIM_SET_COUNTER(&htim2, 0U);

  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_CC3 |
                                   TIM_FLAG_CC4 | TIM_FLAG_CC1OF |
                                   TIM_FLAG_CC2OF | TIM_FLAG_CC3OF |
                                   TIM_FLAG_CC4OF);

  HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);

  status_t.capture_armed = 1U;

  __HAL_TIM_ENABLE(&htim2);

  if (primask == 0U)
    __enable_irq();

  return R_Ok;

start_error:
  status_t.capture_armed = 0U;
  (void)Capture_Stop();

  status_t.capture_complete = 0U;
  status_t.captured = 0U;
  status_t.error = (uint8_t)R_TimerError;

  return R_TimerError;
}

Result_t Capture_Stop(void) {
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

  __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_CC3 |
                                   TIM_FLAG_CC4);

  HAL_NVIC_ClearPendingIRQ(TIM2_IRQn);
  return res;
}

uint8_t Capture_IsComplete(void) { return status_t.capture_complete; }

Result_t Capture_GetStatus(CaptureStatus_t *stat) {
  if (stat == NULL)
    return R_InvalidArgument;

  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  *stat = status_t;

  if (primask == 0U)
    __enable_irq();

  return R_Ok;
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim) {
  if (!status_t.capture_armed)
    return;
  if (htim->Instance != TIM2) {
    return;
  }
  Result_t res = TIM2_CaptureHandler(htim);
  if (res != R_Ok) {
    status_t.error = (uint8_t)res;
    status_t.capture_armed = 0U;
    return;
  }
  if (status_t.active_mask != 0U &&
      (status_t.captured & status_t.active_mask) == status_t.active_mask) {
    status_t.capture_complete = 1U;
    status_t.capture_armed = 0U;
  }
}
