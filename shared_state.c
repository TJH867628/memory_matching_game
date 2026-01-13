#include "shared_state.h"
#include <stdio.h>

void initGameState(SharedGameState *state){
    state->gameStarted = false;
    state->playerCount = 0;
    state->currentTurn = 0;
    state->boardRows = 0;
    state->boardCols = 0;
    state->totalPairs = 0;
    state->matchedPaires = 0;
    state->logQueueHead = 0;
    state->logQueueTail = 0;

    for(int i = 0; i < MAX_PLAYERS; i++){
        state->players[i].playerID = -1;
        state->players[i].score = 0;
        state->players[i].connected = false;
        state->players[i].wantToJoin = false;
        state->players[i].readyToStart = false;
    }

    for(int i = 0; i < MAX_CARDS; i++){
        state->cards[i].cardID = -1;
        state->cards[i].faceValue = -1;
        state->cards[i].isFlipped = false;
        state->cards[i].isMatched = false;
    }
}

void resetGameState(SharedGameState *state){
    state->gameStarted = false;
    state->currentTurn = -1;
    state->matchedPaires = 0;

    for(int i = 0; i < state->playerCount; i++){
        state->players[i].score = 0;
    }

    for(int i = 0; i < state->totalPairs * 2; i++){
        state->cards[i].isFlipped = false;
        state->cards[i].isMatched = false;
    }
}


void printGameState(SharedGameState *state){
    printf("Game Started: %d\n", state->gameStarted);
    printf("Player Count: %d\n", state->playerCount);
    printf("Current Turn: %d\n", state->currentTurn);
    printf("Board Rows: %d\n", state->boardRows);
    printf("Board Columns: %d\n", state->boardCols);
    printf("Total Pairs: %d\n", state->totalPairs);
    printf("Matched Pairs: %d\n", state->matchedPaires);
}