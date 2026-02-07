#include "scheduler.h"
#include "shared_state.h"
#include "logger.h"
#include "game.h"
#include "score.h"
#include <stdio.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>

void* schedulerLoopThread(void *arg){
    SharedGameState *gameState = (SharedGameState*) arg;
    char logMessage[LOG_MSG_LENGTH];

    printf("Scheduler thread started: TID=%lu\n", pthread_self());
    while(serverRunning){   
        sem_wait(&gameState->turnCompleteSemaphore);
        if(!serverRunning)
            break;

        pthread_mutex_lock(&gameState->mutex);
        if (!gameState->gameStarted)
        {
            pthread_mutex_unlock(&gameState->mutex);
            continue;
        }
        pthread_mutex_unlock(&gameState->mutex);

        pthread_mutex_lock(&gameState->mutex);
        int matchedPairs = gameState->matchedPaires;
        int totalPairs = gameState->totalPairs;
        int currentTurn = gameState->currentTurn;
        pthread_mutex_unlock(&gameState->mutex);
            
        if (gameState->matchedPaires == gameState->totalPairs) {
            
            pthread_mutex_lock(&gameState->mutex);
            int winner = gameState->currentTurn;
            char winnerName[PLAYER_NAME_LENGTH];
            if (winner >= 0 && winner < MAX_PLAYERS)
                strncpy(winnerName, gameState->players[winner].name, PLAYER_NAME_LENGTH);
            else
                strncpy(winnerName, "Unknown", PLAYER_NAME_LENGTH);
            winnerName[PLAYER_NAME_LENGTH - 1] = '\0';
            pthread_mutex_unlock(&gameState->mutex);

            scores_save(gameState);

            snprintf(logMessage, LOG_MSG_LENGTH,
                     "Game ended. Player %s score saved.\n", winnerName);
            pushLogEvent(gameState, LOG_GAME, logMessage);

            resetGameState(gameState);

            pushLogEvent(gameState, LOG_GAME,
                         "Game reset. Starting new round.\n");
            pthread_mutex_lock(&gameState->mutex);
            char notify[512];
            snprintf(notify, sizeof(notify),
                     "GAME_STOPPED\nAll pairs matched. Please type 1 to READY.\n<<END>>\n");
            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (gameState->players[i].connected)
                {
                    send(gameState->players[i].socket, notify, strlen(notify), 0);
                }
            }
            pthread_mutex_unlock(&gameState->mutex);
            continue;
        }

        
        pthread_mutex_lock(&gameState->mutex);
        int nextTurn = -1;
        int start = gameState->currentTurn;
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            int candidate = (start + 1 + i) % MAX_PLAYERS;
            if (gameState->players[candidate].connected)
            {
                nextTurn = candidate;
                break;
            }
        }

        if (nextTurn == -1)
        {
            gameState->currentTurn = -1;
            pthread_mutex_unlock(&gameState->mutex);
            continue;
        }

        gameState->currentTurn = nextTurn;
        if (gameState->players[nextTurn].flipsDone >= 2)
        {
            gameState->players[nextTurn].flipsDone = 0;
        }
        currentTurn = gameState->currentTurn;
        pthread_mutex_unlock(&gameState->mutex);

        sendTurnMessage(gameState);
        printf("It's now Player %d's turn.\n", gameState->currentTurn);
        snprintf(logMessage, LOG_MSG_LENGTH,"It's now Player %d's turn.\n", gameState->currentTurn);
        
        sem_post(&gameState->turnSemaphore);

        pushLogEvent(gameState, LOG_TURN, logMessage);
    }
}
