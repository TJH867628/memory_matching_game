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

            int maxScore = -1;
            int winners[MAX_PLAYERS];
            int winnerCount = 0;

            pthread_mutex_lock(&gameState->mutex);
            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (!gameState->players[i].connected)
                    continue;
                int s = gameState->players[i].roundScore;
                if (s > maxScore)
                {
                    maxScore = s;
                    winnerCount = 0;
                    winners[winnerCount++] = i;
                }
                else if (s == maxScore)
                {
                    winners[winnerCount++] = i;
                }
            }
            pthread_mutex_unlock(&gameState->mutex);

            scores_save(gameState);

            if (winnerCount == 1)
            {
                snprintf(logMessage, LOG_MSG_LENGTH,"Game ended. Winner: %s (Round Score %d).\n",winnerName,maxScore);
            }
            else
            {
                snprintf(logMessage, LOG_MSG_LENGTH,"Game ended. Draw with round score %d.\n",maxScore);
            }
            pushLogEvent(gameState, LOG_GAME, logMessage);

            resetGameState(gameState);

            pushLogEvent(gameState, LOG_GAME,
                         "Game reset. Starting new round.\n");
            pthread_mutex_lock(&gameState->mutex);
            char notify[1024];
            char result[256];
            if (winnerCount == 1)
            {
                snprintf(result, sizeof(result),"Winner: %s (Round Score %d)\n",winnerName,maxScore);
            }
            else
            {
                result[0] = '\0';
                strncat(result, "Draw between: ", sizeof(result) - strlen(result) - 1);
                for (int i = 0; i < winnerCount; i++)
                {
                    int idx = winners[i];
                    const char *nm = gameState->players[idx].name[0] ? gameState->players[idx].name : "Unknown";
                    strncat(result, nm, sizeof(result) - strlen(result) - 1);
                    if (i < winnerCount - 1)
                    {
                        strncat(result, ", ", sizeof(result) - strlen(result) - 1);
                    }
                }
                strncat(result, "\n", sizeof(result) - strlen(result) - 1);
            }

            snprintf(notify, sizeof(notify),"GAME_STOPPED\nAll pairs matched.\n%sPlease type 1 to READY.\n<<END>>\n",result);

            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (gameState->players[i].connected)
                {
                    send(gameState->players[i].socket, notify, strlen(notify), 0);
                }
            }
            pthread_mutex_unlock(&gameState->mutex);
            sem_post(&gameState->flipDoneSemaphore);
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
        snprintf(logMessage, LOG_MSG_LENGTH,"It's now Player %d's turn.\n",gameState->currentTurn);
        
        sem_post(&gameState->turnSemaphore);

        pushLogEvent(gameState, LOG_TURN, logMessage);
    }
}
