#include "shared_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

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
        state->players[i].pendingAction = false;
        state->players[i].pendingCardIndex = -1;
        state->players[i].firstFlipIndex = -1;
        state->players[i].secondFlipIndex = -1;
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
    state->currentTurn = 0;
    state->matchedPaires = 0;
    state->totalPairs = 0;
    state->boardRows = 0;
    state->boardCols = 0;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->players[i].connected) {
            state->players[i].score = 0;
            state->players[i].readyToStart = false;
            state->players[i].pendingAction = false;
            state->players[i].pendingCardIndex = -1;
            state->players[i].firstFlipIndex = -1;
            state->players[i].secondFlipIndex = -1;
            state->players[i].waitingNotified = false;
        }
    }

    for (int i = 0; i < MAX_CARDS; i++) {
        state->cards[i].isFlipped = false;
        state->cards[i].isMatched = false;
    }
}


