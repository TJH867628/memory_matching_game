#ifndef GAME_H
#define GAME_H

#include "shared_state.h"

void *gameLoopThread(void *arg);
void sendBoardStateToAll(SharedGameState *state);
void sendTurnMessage(SharedGameState *state);
void setupBoard(SharedGameState *state, int rows, int cols);
void formatOfBoard(SharedGameState *state, char *buffer, size_t bufsize);

#endif