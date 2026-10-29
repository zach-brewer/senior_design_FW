#include "app_fsm.h"
#include "capture.h"
#include "dac.h"
#include "main.h"
#include "stm32g4xx_hal.h"

static FSMState_t current_state = STATE_IDLE;
static Result_t last_error = R_Ok;
static uint32_t capture_start_tick = 0U;


static Result_t enter_error(Result_t error)
{
    Result_t stop_res = Capture_Stop();

    current_state = STATE_ERROR;

    if (stop_res != R_Ok)
        last_error = stop_res;
    else
        last_error = error;

    return last_error;
}

static Result_t recover_to_idle() {
  Result_t res = Capture_Stop();
  if (res != R_Ok)
    return enter_error(res);
  res = AppFSM_Initialize();
  return res;
}

Result_t AppFSM_Initialize(void) {
  current_state = STATE_IDLE;
  last_error = R_Ok;
  capture_start_tick = 0U;
  Result_t res;
  res = Capture_Initialize();
  if (res != R_Ok) {
    current_state = STATE_ERROR;
    last_error = res;
  }
  return res;
}

Result_t AppFSM_Update(void) {
  CaptureStatus_t capture_status;
  Result_t res;
  if (current_state != STATE_WAIT_FOR_TRIGGER)
    return R_Ok;
  res = Capture_GetStatus(&capture_status);
  if (res != R_Ok)
    return enter_error(res);
  if (capture_status.error != 0U)
    return enter_error(R_TimerError);
  if (capture_status.capture_complete != 0U &&
      capture_status.captured == CAPTURE_MASK) {
    res = Capture_Stop();
    if (res != R_Ok)
      return enter_error(res);
    current_state = STATE_TRIGGERED;
    return R_Ok;
  }

  if (HAL_GetTick() - capture_start_tick > CAPTURE_TIMEOUT_MS) {
    res = Capture_Stop();
    last_error = R_CaptureTimeout;
    if (res != R_Ok)
      last_error = res;
    current_state = STATE_ERROR;
    return last_error;
  }
  return R_Ok;
}

Result_t AppFSM_ProcessCommand(scpiCommand_t cmd, uint32_t arg) {
  Result_t res = AppFSM_Update();

  if (res != R_Ok && cmd != CMD_STATUS && cmd != CMD_QUERY_DAC &&
      cmd != CMD_RECOVER && cmd != CMD_RESET) {
    return res;
  }
  if (current_state == STATE_ERROR && cmd != CMD_STATUS &&
      cmd != CMD_QUERY_DAC) {
    return R_InvalidState;
  }
  /* Read-only diagnostic commands remain available after errors. */
  if (cmd == CMD_STATUS)
    return R_Ok;

  if (cmd == CMD_RECOVER)
    return recover_to_idle();

  if (cmd == CMD_RESET)
    NVIC_SystemReset();

  if (cmd == CMD_NONE)
    return R_InvalidCMD;

  switch (cmd) {
  case CMD_ARM_SYSTEM:
    if (current_state != STATE_IDLE && current_state != STATE_TRIGGERED)
      return R_InvalidState;
    res = Capture_Start();
    if (res != R_Ok)
      return enter_error(res);
    capture_start_tick = HAL_GetTick();
    current_state = STATE_WAIT_FOR_TRIGGER;
    last_error = R_Ok;
    return R_Ok;
  case CMD_SEND_TIME:
    if (current_state != STATE_TRIGGERED)
      return R_InvalidState;
    return R_Ok;
  case CMD_SET_DAC:
    if (current_state != STATE_IDLE)
      return R_Busy;
    return DAC_SetThreshold_mV(arg);
  case CMD_QUERY_DAC:
    return R_Ok;
  case CMD_STATUS:
    return R_Ok;
  default:
    return R_InvalidCMD;
  }
}
FSMState_t AppFSM_GetState(void) { return current_state; }

Result_t AppFSM_GetLastError(void) { return last_error; }
