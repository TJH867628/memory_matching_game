#include "game.h"
#include "logger.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h> 
#include <time.h>   
#include <sys/socket.h>

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

void formatOfBoard(SharedGameState *state, char *buffer, size_t bufsize)
{
    if (!buffer || bufsize == 0) return;

    int rows = state->boardRows;
    int cols = state->boardCols;

    if (rows <= 0 || cols <= 0) {
        snprintf(buffer, bufsize, "Board not set up yet.\n");
        return;
    }

    buffer[0] = '\0';  // start clean

    strncat(buffer, "Current Board State:\n", bufsize - strlen(buffer) - 1);

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int idx = r * cols + c;
            Card *card = &state->cards[idx];

            char cell[32];

            if (card->isMatched)
                snprintf(cell, sizeof(cell), " [XX] ");
            else if (card->isFlipped)
                snprintf(cell, sizeof(cell), " [%02d] ", card->faceValue);
            else
                snprintf(cell, sizeof(cell), " [--] ");

            strncat(buffer, cell, bufsize - strlen(buffer) - 1);
        }
        strncat(buffer, "\n", bufsize - strlen(buffer) - 1);
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

void sendBoardStateToAll(SharedGameState *state)
{
    char boardMsg[4096];

    formatOfBoard(state, boardMsg, sizeof(boardMsg));

    strcat(boardMsg, "\nEnter move (e.g., FLIP 5):\n");

    pthread_mutex_lock(&state->mutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (state->players[i].connected) {
            send(state->players[i].socket,
                 boardMsg,
                 strlen(boardMsg),
                 0);
        }
    }
    pthread_mutex_unlock(&state->mutex);
}

void *gameLoopThread(void *arg)
{
    SharedGameState *state = (SharedGameState *)arg;
    bool boardInitialized = false;

    while (1)
    {
        pthread_mutex_lock(&state->mutex);

        if (!state->gameStarted && boardInitialized)
        {
            boardInitialized = false;

            pushLogEvent(state, LOG_PLAYER, "Game Restarted. Waiting for players.");
            printf("Connected players:\n");

            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (state->players[i].connected) {
                    printf(" - Player %d\n",i);
                }
            }

            pthread_mutex_unlock(&state->mutex);
            usleep(100000);
            continue;
        }

        if (!state->gameStarted) {
            pthread_mutex_unlock(&state->mutex);
            usleep(100000);
            continue;
        }
        

        if (!boardInitialized) {
            printf("Game Started!\n");
            const char *startMsg = "\nGAME STARTED\n";

            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (state->players[i].connected) {
                    send(state->players[i].socket,
                        startMsg,
                        strlen(startMsg),
                        0);
                }
            }
            setupBoard(state, 4, 6);
            boardInitialized = true;
            pthread_mutex_unlock(&state->mutex);


            // send AFTER unlocking
            sendBoardStateToAll(state);
            continue;
        }

        int current = state->currentTurn;

        if (state->players[current].pendingAction) {
            int idx = state->players[current].pendingCardIndex;

            // TODO: apply game rules here

            state->players[current].pendingAction = false;
            pthread_mutex_unlock(&state->mutex);

            sendBoardStateToAll(state);
            continue;
        }

        pthread_mutex_unlock(&state->mutex);
        usleep(100000);
    }
}