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
    state->currentTurn = -1;
    state->matchedPaires = 0;

    for(int i = 0; i < state->playerCount; i++){
        state->players[i].score = 0;
        state->players[i].pendingAction = false;
        state->players[i].pendingCardIndex = -1;
        state->players[i].firstFlipIndex = -1;
        state->players[i].secondFlipIndex = -1;
    }

    for(int i = 0; i < state->totalPairs * 2; i++){
        state->cards[i].isFlipped = false;
        state->cards[i].isMatched = false;
    }
}

void setupBoard(SharedGameState *state, int rows, int cols){
    state->boardRows = rows;
    state->boardCols = cols;
    int totalCards = rows * cols;

    if (totalCards > MAX_CARDS || totalCards % 2 != 0) {
        printf("error occured!\n");
        return;
    }

    state->totalPairs = totalCards / 2;

    int cardIndex = 0;
    for (int fv = 0; fv < state->totalPairs; fv++) {
        for (int pairC = 0; pairC < 2; pairC++) {
            state->cards[cardIndex].cardID = cardIndex;
            state->cards[cardIndex].faceValue = fv;
            state->cards[cardIndex].isFlipped = false;
            state->cards[cardIndex].isMatched = false;
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

void formatOfBoard(SharedGameState *state, char *buffer, size_t bufsize){
    if(!buffer || bufsize == 0) return;
    pthread_mutex_lock(&state->mutex);

    int rows = state->boardRows;
    int cols = state->boardCols;
    if(rows <= 0 || cols <= 0){
        snprintf(buffer, bufsize, "Board not set up yet.\n");
        pthread_mutex_unlock(&state->mutex);
        return;
    }

    size_t pos = 0;
    pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), "Current Board State:\n");
    for(int r = 0; r < rows && pos < bufsize; r++){
        for(int c = 0; c < cols && pos < bufsize; c++){
            int idx = r * cols + c;
            if(idx >= MAX_CARDS) continue;
            Card *card = &state->cards[idx];
            if(card->isMatched){
                pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), " [XX] ");
            }else if(card->isFlipped){
                pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), " [%02d] ", card->faceValue);
            }else{
                pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), " [--] ");
            }
            if(c < cols - 1){
                pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), " ");
            }
       }
       pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), "\n");
    }
    pthread_mutex_unlock(&state->mutex);
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

void formatBoard(SharedGameState *state, char *buffer, size_t bufsize){
    if(!buffer || bufsize == 0) return;
    pthread_mutex_lock(&state->mutex);

    int rows = state->boardRows;
    int cols = state->boardCols;
    if(rows <= 0 || cols <= 0){
        snprintf(buffer, bufsize, "Board not set up yet.\n");
        pthread_mutex_unlock(&state->mutex);
        return;
    }

    size_t pos = 0;
    pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), "Current Board State:\n");
    for(int r = 0; r < rows && pos < bufsize; r++){
        for(int c = 0; c < cols && pos < bufsize; c++){
            int idx = r * cols + c;
            if(idx >= MAX_CARDS) continue;  
            
            Card *card = &state->cards[idx];
            if(card->isMatched){
                pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), " [XX] ");
            }else if(card->isFlipped){
                pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), " [%02d] ", card->faceValue);
            }else{
                pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), " [--] ");
            }
            if(c < cols - 1){
                pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), " ");
            }
       }
       pos += snprintf(buffer + pos, (pos < bufsize ? bufsize - pos : 0), "\n");
    }
    pthread_mutex_unlock(&state->mutex);
}
