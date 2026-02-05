#include "shared_state.h"
#include "logger.h"
#include "player.h"
#include "score.h"
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

extern SharedGameState *gameState;
extern void sendBoardStateToAll(SharedGameState *state);

int countReadyPlayers(SharedGameState *gameState)
{
    int count = 0;

    for (int i = 0; i < gameState->playerCount; i++) {
        if (gameState->players[i].readyToStart)
            count++;
    }

    return count;
}

PlayerTurn getPlayerTurn(SharedGameState *gameState, int playerID){
    PlayerTurn turn;
    pthread_mutex_lock(&gameState->mutex);
    turn.currentPlayerID = gameState->currentTurn;
    turn.totalPlayers = gameState->playerCount;
    pthread_mutex_unlock(&gameState->mutex);
    return turn;
}

void handlePlayer(SharedGameState *gameState, int i){

    while(serverRunning){
        if (!gameState->gameStarted) {
            sleep(1);
            continue;
        }

        sem_wait(&gameState->turnSemaphore);

        pthread_mutex_lock(&gameState->mutex);
        printf("CurrentTurn :%d\n",gameState->currentTurn);
        if(!gameState->players[i].connected || !gameState->gameStarted || gameState->currentTurn != i) {
            pthread_mutex_unlock(&gameState->mutex);
            continue;
        }
        pthread_mutex_unlock(&gameState->mutex);
    
        PlayerAction action = getPlayerAction(i);

        applyAction(gameState,action);
        sem_post(&gameState->turnCompleteSemaphore);
        
    }
    _exit(0);
}

bool allConnectedPlayerReady(SharedGameState *gameState){
    bool ready = true;
    pthread_mutex_lock(&gameState->mutex);
    if(gameState->playerCount == 0) ready = false;

    for(int i = 0; i < MAX_PLAYERS; i++){
        if(gameState->players[i].connected && !gameState->players[i].readyToStart){
            ready = false;
            break;
        }
    }
    pthread_mutex_unlock(&gameState->mutex);

    return ready;
}

PlayerAction getPlayerAction(int playerID){
    PlayerAction action;

    action.type = ACTION_FLIP;
    action.cardIndex = -1; 
    action.playerID = playerID;
    while(serverRunning){
        pthread_mutex_lock(&gameState->mutex);
        if(gameState->players[playerID].pendingAction){
            action.cardIndex = gameState->players[playerID].pendingCardIndex;
            gameState->players[playerID].pendingAction = false;
            gameState->players[playerID].pendingCardIndex = -1;
            pthread_mutex_unlock(&gameState->mutex);
            break;
        }
        pthread_mutex_unlock(&gameState->mutex);
        usleep(100000); // Sleep for 100ms to avoid busy waiting
    }
    return action;
}

void applyAction(SharedGameState *gameState, PlayerAction action){
    pthread_mutex_lock(&gameState->mutex);

    if(action.type != ACTION_FLIP){
        pthread_mutex_unlock(&gameState->mutex);
        return;
    }

    int rows = gameState->boardRows;
    int cols = gameState->boardCols;
    int totalCards = rows * cols;
    int cardIndex = action.cardIndex;

    if(cardIndex < 0 || cardIndex >= totalCards){
        char msg[LOG_MSG_LENGTH];
        snprintf(msg, LOG_MSG_LENGTH, "Player %d attempted to flip invalid card index %d\n", action.playerID, cardIndex);
        pushLogEvent(gameState, LOG_GAME, msg);
        pthread_mutex_unlock(&gameState->mutex);
        return;
    }

    Card *card = &gameState->cards[cardIndex];
    if(card -> isMatched || card -> isFlipped){
        char msg[LOG_MSG_LENGTH];
        snprintf(msg, LOG_MSG_LENGTH, "Player %d attempted to flip already matched/flipped card %d\n", action.playerID, cardIndex);
        pushLogEvent(gameState, LOG_GAME, msg);
        pthread_mutex_unlock(&gameState->mutex);
        return;
    }

    card -> isFlipped = true;
    Player *p = &gameState->players[action.playerID];
    if(p -> firstFlipIndex == -1){
        p -> firstFlipIndex = cardIndex;
        char msg[LOG_MSG_LENGTH];
        snprintf(msg, LOG_MSG_LENGTH, "Player %d flipped card %d (Face Value: %d)\n", action.playerID, cardIndex, card -> faceValue);
        pushLogEvent(gameState, LOG_GAME, msg);
        pthread_mutex_unlock(&gameState->mutex);
        return;
    }

    if(p -> secondFlipIndex == -1){
        p -> secondFlipIndex = cardIndex;
        char msg[LOG_MSG_LENGTH];
        snprintf(msg, LOG_MSG_LENGTH, "Player %d flipped card %d (Face Value: %d)\n", action.playerID, cardIndex, card -> faceValue);
        pushLogEvent(gameState, LOG_GAME, msg);

        int firstIndex = p -> firstFlipIndex;
        int secondIndex = p -> secondFlipIndex;
        Card *firstCard = &gameState->cards[firstIndex];
        Card *secondCard = &gameState->cards[secondIndex];

        if(firstCard -> faceValue == secondCard -> faceValue){
            firstCard -> isMatched = true;
            secondCard -> isMatched = true;
            gameState->matchedPaires++;
            p -> score += 1;

            snprintf(msg, LOG_MSG_LENGTH, "Player %d found a MATCH! Cards %d and %d\n", action.playerID, firstIndex, secondIndex);
            pushLogEvent(gameState, LOG_GAME, msg);
        }else{
            firstCard -> isFlipped = false;
            secondCard -> isFlipped = false;

            snprintf(msg, LOG_MSG_LENGTH, "Player %d found a MISMATCH! Cards %d and %d\n", action.playerID, firstIndex, secondIndex);
            pushLogEvent(gameState, LOG_GAME, msg);
        }

        p -> firstFlipIndex = -1;
        p -> secondFlipIndex = -1;
    }
    pthread_mutex_unlock(&gameState->mutex);
    return;
}
