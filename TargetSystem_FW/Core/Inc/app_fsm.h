/**
  ******************************************************************************
  * @file    app_fsm.h
  * @brief   finite state machine application
  ******************************************************************************
  *          Author: Zach Brewer
  *             
  ******************************************************************************
  */
  /**
----- STATE MACHINE LOGIC -----
STATE_IDLE: 
    - Wait system to be armed    
    - transition to STATE_WAIT_FOR_TRIGGER
STATE_WAIT_FOR_TRIGGER:
    - Wait for all microphones to trigger
    - When all microphones have triggered, transition to STATE_TRIGGERED
STATE_TRIGGERED:
    - Wait for command to send timestamps
    - If command received, send timestamps and transition to STATE_IDLE (or maybe wait for trigger)
STATE_ERROR:
    - Still able to poll status,
    - if maybe able to recover and transistion to idle
    - maybe also just reset system to recover
  */

#ifndef __APP_FSM_H__
#define __APP_FSM_H__

#include "result.h"  
#include <stdint.h>
#include "cmd_ctrl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CAPTURE_TIMEOUT_MS 5000U

typedef enum{
    STATE_IDLE = 0,
    STATE_WAIT_FOR_TRIGGER = 1,
    STATE_TRIGGERED = 2,
    STATE_ERROR = -1,
} FSMState_t;




Result_t AppFSM_Initialize(void);
Result_t AppFSM_Update(void);
Result_t AppFSM_ProcessCommand(scpiCommand_t cmd, uint32_t arg);
FSMState_t AppFSM_GetState(void);
Result_t AppFSM_GetLastError(void);


#ifdef __cplusplus
}
#endif

#endif /* __APP_FSM_H__ */