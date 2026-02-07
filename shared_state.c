#include "shared_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>


void randomCardValues(SharedGameState *state, int rows, int cols){
    int totalCards = rows * cols;

    if (totalCards > MAX_CARDS || totalCards % 2 != 0) {
        printf("Error: Invalid board size for card values.\n");
        return;
    }

    state->totalPairs = totalCards / 2;

    int cardIndex = 0;
    for (int fv = 0; fv < state->totalPairs; fv++) {
        for (int pairC = 0; pairC < 2; pairC++) {
            state->cards[cardIndex].cardID = cardIndex;
            state->cards[cardIndex].faceValue = fv;
            cardIndex++;
        }
    }

    srand((unsigned int)time(NULL));
    for (int i = 0; i < totalCards; i++) {
        int j = rand() % totalCards;
        Card temp = state->cards[i];
        state->cards[i] = state->cards[j];
        state->cards[j] = temp;
    }
}

void initCard(SharedGameState *state){
    for(int i = 0; i < MAX_CARDS; i++){
        state->cards[i].cardID = -1;
        state->cards[i].faceValue = -1;
        state->cards[i].isFlipped = false;
        state->cards[i].isMatched = false;
    }
    

    randomCardValues(state, state->boardRows, state->boardCols);
}


void initGameState(SharedGameState *state){
    state->gameStarted = false;
    state->boardNeedsBroadcast = false;
    state->playerCount = 0;
    state->currentTurn = -1;
    state->boardRows = 3;
    state->boardCols = 4;
    state->totalPairs = 0;
    state->matchedPaires = 0;
    state->logQueueHead = 0;
    state->logQueueTail = 0;

    for(int i = 0; i < MAX_PLAYERS; i++){
        state->players[i].playerID = -1;
        state->players[i].score = 0;
        state->players[i].roundScore = 0;
        state->players[i].name[0] = '\0';
        state->players[i].connected = false;
        state->players[i].wantToJoin = false;
        state->players[i].readyToStart = false;
        state->players[i].pendingAction = false;
        state->players[i].flipsDone = 0;
        state->players[i].firstFlipIndex = -1;
        state->players[i].secondFlipIndex = -1;
    }

    initCard(state);
}

void resetGameState(SharedGameState *state){
    state->gameStarted = false;
    state->currentTurn = -1;
    state->matchedPaires = 0;
    state->totalPairs = 0;
    state->boardRows = 3;
    state->boardCols = 4;
    state->boardNeedsBroadcast = false;

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->players[i].connected) {
            state->players[i].readyToStart = false;
            state->players[i].pendingAction = false;
            state->players[i].flipsDone = 0;
            state->players[i].firstFlipIndex = -1;
            state->players[i].secondFlipIndex = -1;
            state->players[i].waitingNotified = false;
            state->players[i].roundScore = 0;
        }
    }

    initCard(state);
}
