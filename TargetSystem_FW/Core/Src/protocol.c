#include "app.h"
#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void ReplyOK(const char *command)
{
    Protocol_SendLine("OK");

    char response[96];
    snprintf(response, sizeof(response), "REPLY,OK,%s", command);
    Protocol_SendLine(response);
}

static void ReplyError(const char *code)
{
    char response[128];
    snprintf(response, sizeof(response), "REPLY,ERROR,%s", code);
    Protocol_SendLine(response);
}

void Protocol_ProcessLine(char *line)
{
    // Remove trailing CR/LF characters.
    line[strcspn(line, "\r\n")] = '\0';

    if (strcmp(line, "ARM SYSTEM") == 0)
    {
        App_Arm();
        ReplyOK("ARM_SYSTEM");
    }
    else if (strncmp(line, "SET DAC ", 8) == 0)
    {
        char *end = NULL;
        long value = strtol(line + 8, &end, 10);

        if (end == line + 8 || *end != '\0' || value < 0 || value > 4095)
        {
            ReplyError("INVALID_DAC");
            return;
        }

        if (!App_SetDAC((uint16_t)value))
        {
            ReplyError("DAC_FAILURE");
            return;
        }

        ReplyOK("SET_DAC");
    }
    else if (strcmp(line, "QUERY DAC") == 0)
    {
        char response[48];
        snprintf(response, sizeof(response), "DAC,%u", App_GetDAC());
        Protocol_SendLine(response);
        ReplyOK("QUERY_DAC");
    }
    else if (strcmp(line, "SEND LATEST TIMESTAMP") == 0)
    {
        uint16_t count = Capture_GetCount();

        if (count == 0)
        {
            ReplyError("NO_TIMESTAMPS");
            return;
        }

        char response[64];
        snprintf(response, sizeof(response), "TIMESTAMP,%lu,%lu",
                 (unsigned long)(count - 1), (unsigned long)Capture_GetLatest());

        Protocol_SendLine(response);
        ReplyOK("LATEST_TIMESTAMP");
    }
    else if (strcmp(line, "SEND ALL TIMESTAMPS") == 0)
    {
        uint16_t count = Capture_GetCount();
        char response[64];

        for (uint16_t i = 0; i < count; i++)
        {
            snprintf(response, sizeof(response), "TIMESTAMP,%u,%lu", i,
                     (unsigned long)Capture_GetAt(i));

            if (!Protocol_SendLine(response))
            {
                ReplyError("SEND_FAILURE");
                return;
            }
        }

        ReplyOK("ALL_TIMESTAMPS");
    }
    else if (strcmp(line, "STATUS") == 0)
    {
        char response[128];

        snprintf(response, sizeof(response),
                 "STATUS,armed=%u,dac=%u,adc=%u,timestamps=%u",
                 App_IsArmed() ? 1 : 0, App_GetDAC(), App_ReadADC(),
                 Capture_GetCount());

        Protocol_SendLine(response);
        ReplyOK("STATUS");
    }
    else if (strcmp(line, "RESET") == 0)
    {
        App_Reset();
        ReplyOK("RESET");
    }
    else if (strcmp(line, "REBOOT") == 0)
    {
        // Allow the response to be transmitted before rebooting.
        Protocol_SendLine("OK");
        Protocol_SendLine("REPLY,OK,REBOOT");
        Protocol_Flush();

        HAL_Delay(100);
        NVIC_SystemReset();
    }
    else
    {
        ReplyError("UNKNOWN_COMMAND");
    }
}
