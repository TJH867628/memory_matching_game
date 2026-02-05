#ifndef PLAYER_H
#define PLAYER_H
#include <stdio.h>
#include "shared_state.h"

void handlePlayer(SharedGameState *gameState, int i);
bool allConnectedPlayerReady(SharedGameState *gameState);
static PlayerAction getPlayerAction(int playerID);
void applyAction(SharedGameState *gameState, PlayerAction action);
static PlayerTurn getPlayerTurn(SharedGameState *gameState, int playerID);
#endif
