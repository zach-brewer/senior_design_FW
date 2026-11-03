#include “main.h”
#include “app.h”
#include “protocol.h”

#include “wizchip_conf.h”
#include “socket.h”

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define DATA_SOCKET 0
#define LOCAL_PORT 5006
#define PC_PORT 5005

static uint8_t pc_ip[4] = {192, 168, 1, 60};

static char rx_line[256];
static size_t rx_length = 0;

bool Network_Connect(void)
{
    int8_t result = socket(DATA_SOCKET, Sn_MR_TCP, LOCAL_PORT, Sn_MR_CLOSE);
    if (result != DATA_SOCKET)
        return false;

    result = connect(DATA_SOCKET, pc_ip, PC_PORT);
    if (result != SOCK_OK)
    {
        close(DATA_SOCKET);
        return false;
    }

    // Wait for the TCP connection to actually establish
    uint32_t start = HAL_GetTick();

    while (getSn_SR(DATA_SOCKET) != SOCK_ESTABLISHED)
    {
        if (getSn_SR(DATA_SOCKET) == SOCK_CLOSED ||
            (HAL_GetTick() - start) > 5000)
        {
            close(DATA_SOCKET);
            return false;
        }

        HAL_Delay(1);
    }

    return true;
}

bool Protocol_SendLine(const char *line)
{
    char tx[320];

    int length = snprintf(tx, sizeof(tx), “% s / n”, line);

    if (length <= 0 || length >= (int)sizeof(tx))
        return false;

    int32_t sent = send(DATA_SOCKET, (uint8_t *)tx, (uint16_t)length);

    return (sent == length);
}

void Network_Poll(void)
{
    if (getSn_SR(DATA_SOCKET) != SOCK_ESTABLISHED)
        return;

    uint16_t available = getSn_RX_RSR(DATA_SOCKET);

    while (available > 0)
    {
        uint8_t chunk[128];
        uint16_t amount = available > sizeof(chunk) ? sizeof(chunk) : available;

        int32_t received = recv(DATA_SOCKET, chunk, amount);

        if (received <= 0)
            break;

        for (int32_t i = 0; i < received; i++)
        {
            char c = (char)chunk[i];

            if (c == '\n')
            {
                rx_line[rx_length] = '\0';
                Protocol_ProcessLine(rx_line);
                rx_length = 0;
            }
            else if (c != '\r')
            {
                if (rx_length < sizeof(rx_line) - 1)
                {
                    rx_line[rx_length++] = c;
                }
                else
                {
                    // Discard an overlong command and report the error.
                    rx_length = 0;
                    Protocol_SendLine("REPLY,ERROR,COMMAND_TOO_LONG");
                }
            }
        }

        available = getSn_RX_RSR(DATA_SOCKET);
    }
}
