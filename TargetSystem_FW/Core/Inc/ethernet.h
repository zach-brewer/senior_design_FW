/**
  ******************************************************************************
  * @file    ethernet.h
  * @brief   Ethernet handler for tansmittion
  ******************************************************************************
  *          Author: Zach Brewer
  *             
  ******************************************************************************
  */
#include "result.h"  

#ifndef __ETHERNET_H__
#define __ETHERNET_H__

#ifdef __cplusplus
extern "C" {
#endif
#define CMD_NONE        0x00
#define CMD_ARM_SYSTEM  0x01
#define CMD_SET_DAC     0x02
#define CMD_QUERY_DAC   0x03
#define CMD_SEND_TIME   0x04
#define CMD_STATUS      0x05



#ifdef __cplusplus
}
#endif

#endif /* __ETHERNET_H__ */