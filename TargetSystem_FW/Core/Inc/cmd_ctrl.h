/**
  ******************************************************************************
  * @file    cmd_ctrl.h
  * @brief   Ethernet handler for tansmittion
  ******************************************************************************
  *          Author: Zach Brewer
  *             
  ******************************************************************************
  */


#ifndef __CMD_CTRL_H__
#define __CMD_CTRL_H__

#include "result.h"  
#include <stdint.h>

#define COM_TERMINAL 1

#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    CMD_NONE          = 0x00,
    CMD_ARM_SYSTEM    = 0x01,
    CMD_SET_DAC       = 0x02,
    CMD_QUERY_DAC     = 0x03,
    CMD_SEND_TIME     = 0x04,
    CMD_STATUS        = 0x05,
    CMD_RECOVER       = 0x06,
    CMD_RESET         = 0x07,
} scpiCommand_t;


#if COM_TERMINAL
Result_t Terminal_Init(void);
void Terminal_Poll(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CMD_CTRL_H__ */