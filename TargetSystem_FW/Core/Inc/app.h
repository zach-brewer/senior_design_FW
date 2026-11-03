#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

#define TIMESTAMP_BUFFER_SIZE 256

void App_Init(void);
void App_Arm(void);
void App_Reset(void);

bool App_IsArmed(void);

bool App_SetDAC(uint16_t value);
uint16_t App_GetDAC(void);
bool App_ReadADC(uint16_t *value);

void Capture_AddTimestamp(uint32_t timestamp);
uint16_t Capture_GetCount(void);
uint32_t Capture_GetLatest(void);
uint32_t Capture_GetAt(uint16_t index);

#endif