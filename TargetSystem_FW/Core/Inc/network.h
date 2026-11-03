#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>

bool Network_Connect(void);
bool Protocol_SendLine(const char *line);
void Network_Poll(void);

#endif