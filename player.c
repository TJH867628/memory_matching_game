#include "shared_state.h"
#include "logger.h"
#include "player.h"
#include "score.h"
#include <stdio.h>
#include <unistd.h>

int countReadyPlayers(SharedGameState *gameState)
{
    int count = 0;

    for (int i = 0; i < gameState->playerCount; i++) {
        if (gameState->players[i].readyToStart)
            count++;
    }

    return count;
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
    action.cardIndex = 1; 
    action.playerID = playerID;
    switch(action.type){
        case ACTION_FLIP:
            printf("Receive flip action from player %d\n", playerID);
            break;
        default:
            break;
    }
    sleep(1);

    return action;
}

void applyAction(SharedGameState *gameState, PlayerAction action){
    pthread_mutex_lock(&gameState->mutex);

    switch(action.type){
        case ACTION_FLIP: {
            int cardIndex = action.cardIndex;
            int numOfFlip = 0;

            while(numOfFlip < 3){

            if (cardIndex >= 0 && cardIndex < MAX_CARDS) {

                // Simulate card flip
                gameState->cards[cardIndex].isFlipped = true;
                numOfFlip++;
            }

            while(numOfFlip == 2){
                // Check for match
                int firstIndex = gameState->players[action.playerID].firstFlipIndex;
                int secondIndex = gameState->players[action.playerID].secondFlipIndex;
                if(firstIndex == secondIndex){
                    gameState->cards[firstIndex].isMatched = true;
                    gameState->matchedPaires++;
                }
            }

                char msg[LOG_MSG_LENGTH];
                snprintf(msg, LOG_MSG_LENGTH,
                        "Player %d flipped card %d (Matched: %d/%d)\n",
                        action.playerID,
                        cardIndex,
                        gameState->matchedPaires,
                        gameState->totalPairs);

                pushLogEvent(gameState, LOG_GAME, msg);
            }
            break;
        }
        default:
            break;
    }
    pthread_mutex_unlock(&gameState->mutex);
    
}