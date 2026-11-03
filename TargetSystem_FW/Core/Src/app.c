#include "main.h"
#include "app.h"

extern DAC_HandleTypeDef hdac1;
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;

static volatile bool system_armed = false;

static uint16_t current_dac = 0;

static volatile uint32_t timestamp_buffer[TIMESTAMP_BUFFER_SIZE];
static volatile uint16_t timestamp_count = 0;
static volatile uint16_t timestamp_write_index = 0;


void App_Init(void)
{
    HAL_DAC_Start(&hdac3, DAC_CHANNEL_1);
    HAL_DAC_Start(&hdac3, DAC_CHANNEL_2);
    HAL_DAC_Start(&hdac4, DAC_CHANNEL_1);

    App_SetDAC(0);
}


void App_Arm(void)
{
    system_armed = true;

    // Start timer input capture.
    HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1);
}


void App_Reset(void)
{
    system_armed = false;

    HAL_TIM_IC_Stop_IT(&htim2, TIM_CHANNEL_1);

    __disable_irq();

    timestamp_count = 0;
    timestamp_write_index = 0;

    __enable_irq();
}


bool App_IsArmed(void)
{
    return system_armed;
}


bool App_SetDAC(uint16_t value)
{
    if (value > 4095)
        return false;

    if (HAL_DAC_SetValue(
            &hdac1,
            DAC_CHANNEL_1,
            DAC_ALIGN_12B_R,
            value) != HAL_OK)
    {
        return false;
    }

    current_dac = value;

    return true;
}


uint16_t App_GetDAC(void)
{
    return current_dac;
}


bool App_ReadADC(uint16_t *value)
{
    if (value == NULL)
        return false;

    if (HAL_ADC_Start(&hadc1) != HAL_OK)
        return false;

    if (HAL_ADC_PollForConversion(&hadc1, 10) != HAL_OK)
    {
        HAL_ADC_Stop(&hadc1);
        return false;
    }

    *value = (uint16_t)HAL_ADC_GetValue(&hadc1);

    HAL_ADC_Stop(&hadc1);

    return true;
}


// Call this when a timer capture occurs.
void Capture_AddTimestamp(uint32_t timestamp)
{
    if (!system_armed)
        return;

    timestamp_buffer[timestamp_write_index] = timestamp;

    timestamp_write_index =
        (timestamp_write_index + 1) % TIMESTAMP_BUFFER_SIZE;

    if (timestamp_count < TIMESTAMP_BUFFER_SIZE)
        timestamp_count++;
}


uint16_t Capture_GetCount(void)
{
    return timestamp_count;
}


uint32_t Capture_GetLatest(void)
{
    if (timestamp_count == 0)
        return 0;

    uint16_t index =
        (timestamp_write_index + TIMESTAMP_BUFFER_SIZE - 1)
        % TIMESTAMP_BUFFER_SIZE;

    return timestamp_buffer[index];
}


// Returns timestamps oldest-to-newest.
// Index 0 is the oldest timestamp currently stored.
uint32_t Capture_GetAt(uint16_t index)
{
    if (index >= timestamp_count)
        return 0;

    uint16_t oldest =
        (timestamp_write_index + TIMESTAMP_BUFFER_SIZE
         - timestamp_count) % TIMESTAMP_BUFFER_SIZE;

    uint16_t actual_index =
        (oldest + index) % TIMESTAMP_BUFFER_SIZE;

    return timestamp_buffer[actual_index];
}


// Example timer input capture callback.
// Change the timer and channel to match your configuration.
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2 &&
        htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    {
        uint32_t captured =
            HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);

        Capture_AddTimestamp(captured);
    }
}