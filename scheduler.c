#include "scheduler.h"
#include "shared_state.h"
#include "logger.h"
#include "score.h"
#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>

void* schedulerThread(void *arg){
    SharedGameState *gameState = (SharedGameState*) arg;
    char logMessage[LOG_MSG_LENGTH];

    while(serverRunning){

        sem_wait(&gameState->turnCompleteSemaphore);

        pthread_mutex_lock(&gameState->mutex);

        // CHECK GAME END
        if (gameState->matchedPaires == gameState->totalPairs) {

            int winner = gameState->currentTurn;

            pthread_mutex_unlock(&gameState->mutex);

            // Update persistent score
            scores_add_win(gameState, winner);
            scores_save(gameState);

            snprintf(logMessage, LOG_MSG_LENGTH,
                     "Game ended. Player %d wins.\n", winner);
            pushLogEvent(gameState, LOG_GAME, logMessage);

            // Reset game for next round
            resetGameState(gameState);

            pushLogEvent(gameState, LOG_GAME,
                         "Game reset. Starting new round.\n");

            sem_post(&gameState->turnSemaphore);
            continue;
        }

        // Normal Round Robin
        gameState->currentTurn =
            (gameState->currentTurn + 1) % gameState->playerCount;

        pthread_mutex_unlock(&gameState->mutex);

        snprintf(logMessage, LOG_MSG_LENGTH,
                 "It's now Player %d's turn.\n",
                 gameState->currentTurn);

        pushLogEvent(gameState, LOG_TURN, logMessage);

        sem_post(&gameState->turnSemaphore);
    }
}
