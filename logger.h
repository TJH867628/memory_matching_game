#ifndef LOGGER_H
#define LOGGER_H

#include "shared_state.h"

void* loggerThread(void *arg);
void logMessage(SharedGameState *state, const char *message);
void pushLogEvent(SharedGameState *state, LogType type, const char *message);

#endif