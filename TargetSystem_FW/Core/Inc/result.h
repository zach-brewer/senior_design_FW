/**
  ******************************************************************************
  * @file    result.h
  * @brief   This file provides error and Ok codes for function returns
  *             Mainly to be used in Ethernet message returns
  ******************************************************************************
  *          Author: Zach Brewer
  *             
  *
  *              New error messages should be added after R_TypeError for backwards
  *              compatability with an increased error code.
  *              Update error_codes.md accordingly
  *              Include error code addition in git commit message
  ******************************************************************************
  */

#ifndef __RESULT_H__
#define __RESULT_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum{
    R_Ok                = 0,  /** Operation completed successfully */
    
    R_Error             = 1,  /** General Error Return*/
    R_InvalidArgument   = 2,  /** Function Argument Invalid*/
    R_InvalidState      = 3,  /** Invalid state for requested operation*/
    R_Busy              = 4,  /** Resource is Busy*/
    R_TimeoutError      = 5,  /** Operation timed out*/

    R_SpiError          = 7,  /** SPI operation failed*/
    R_DacError          = 6,  /** DAC setup or opertation failed*/
    R_IndexError        = 8,  /** Target index is out of range (likely to not be used)*/
    R_TypeError         = 9,  /** Invalid type*/
    R_ValueError        = 10, /** Invalid variable value*/
    R_CaptureTimeout    = 11, /** Input Capture timed out*/
    R_TimerError        = 12, /** TIM error */

            
} Result_t;


#ifdef __cplusplus
}
#endif

#endif /* __RESULT_H__ */