#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>

bool Protocol_SendLine(const char *line);
void Protocol_Flush(void);
void Protocol_ProcessLine(char *line);

#endif
