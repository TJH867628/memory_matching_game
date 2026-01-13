#include "scheduler.h"
#include "shared_state.h"
#include "logger.h"
#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>

void* schedulerThread(void *arg){
    SharedGameState *gameState = (SharedGameState*) arg;
    char logMessage[LOG_MSG_LENGTH];

    while(serverRunning){
        //Wait for a player's turn to complete
        sem_wait(&gameState->turnCompleteSemaphore);

        //Round Robin scheduling to the next player
        pthread_mutex_lock(&gameState->mutex);
        //Move to the next player's turn
        gameState->currentTurn = (gameState->currentTurn + 1) % gameState->playerCount;
        pthread_mutex_unlock(&gameState->mutex);
        snprintf(logMessage, LOG_MSG_LENGTH, "It's now Player %d's turn.\n", gameState->currentTurn);
        pushLogEvent(gameState, LOG_TURN, logMessage);
        //Let the next player know that it's their turn
        sem_post(&gameState->turnSemaphore);
        
    }
}