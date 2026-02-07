#include "game.h"
#include "logger.h"
#include "shared_state.h"
#include "scheduler.h"
#include "score.h"
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h>

static void sendInfoToAll(SharedGameState *state, const char *message)
{
    pthread_mutex_lock(&state->mutex);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (state->players[i].connected)
        {
            send(state->players[i].socket, message, strlen(message), 0);
        }
    }
    pthread_mutex_unlock(&state->mutex);
}

void setupBoard(SharedGameState *state, int rows, int cols)
{
    pthread_mutex_lock(&state->mutex);
    state->boardRows = rows;
    state->boardCols = cols;
    pthread_mutex_unlock(&state->mutex);
    int totalCards = rows * cols;

    for (int i = 0; i < totalCards; i++)
    {
        state->cards[i].isFlipped = false;
        state->cards[i].isMatched = false;
    }

    if (totalCards > MAX_CARDS || totalCards % 2 != 0)
    {
        printf("error occured!\n");
        return;
    }

    state->totalPairs = totalCards / 2;

    int cardIndex = 0;
    for (int i = 0; i < MAX_CARDS; i++)
    {
        state->cards[i].isFlipped = false;
        state->cards[i].isMatched = false;
    }

    for (int fv = 0; fv < state->totalPairs; fv++)
    {
        for (int pairC = 0; pairC < 2; pairC++)
        {
            state->cards[cardIndex].cardID = cardIndex;
            state->cards[cardIndex].faceValue = fv;
            cardIndex++;
        }
    }

    srand((unsigned int)time(NULL));
    for (int i = 0; i < totalCards; i++)
    {
        int j = rand() % totalCards;
        Card temp = state->cards[i];
        state->cards[i] = state->cards[j];
        state->cards[j] = temp;
    }
}

void formatOfBoard(SharedGameState *state, char *buffer, size_t bufsize)
{
    if (!buffer || bufsize == 0)
        return;

    pthread_mutex_lock(&state->mutex);
    int rows = 3;
    int cols = 4;
    pthread_mutex_unlock(&state->mutex);

    buffer[0] = '\0';
    strncat(buffer, "Board State (VALUES / IDs):\n", bufsize - strlen(buffer) - 1);

    for (int r = 0; r < rows; r++)
    {
        strncat(buffer, "Values: ", bufsize - strlen(buffer) - 1);
        for (int c = 0; c < cols; c++)
        {
            int idx = r * cols + c;
            Card *card = &state->cards[idx];
            char cell[16];

            if (card->isMatched)
                snprintf(cell, sizeof(cell), " [%02d] ", card->faceValue);
            else if (card->isFlipped)
                snprintf(cell, sizeof(cell), " [%02d] ", card->faceValue);
            else
                snprintf(cell, sizeof(cell), " [--] ");

            strncat(buffer, cell, bufsize - strlen(buffer) - 1);
        }

        strncat(buffer, "\n", bufsize - strlen(buffer) - 1);

        strncat(buffer, "IDs:    ", bufsize - strlen(buffer) - 1);
        for (int c = 0; c < cols; c++)
        {
            int idx = r * cols + c;
            char hint[16];

            snprintf(hint, sizeof(hint), " (%02d) ", idx);
            strncat(buffer, hint, bufsize - strlen(buffer) - 1);
        }

        strncat(buffer, "\n", bufsize - strlen(buffer) - 1);
    }

}

void formatOfBoardForServer(SharedGameState *state, char *buffer, size_t bufsize)
{
    if (!buffer || bufsize == 0)
        return;

    pthread_mutex_lock(&state->mutex);
    int rows = 3;
    int cols = 4;
    pthread_mutex_unlock(&state->mutex);

    buffer[0] = '\0';
    strncat(buffer, "Board State (VALUES / IDs):\n", bufsize - strlen(buffer) - 1);

    for (int r = 0; r < rows; r++)
    {
        strncat(buffer, "Values: ", bufsize - strlen(buffer) - 1);
        for (int c = 0; c < cols; c++)
        {
            int idx = r * cols + c;
            Card *card = &state->cards[idx];
            char cell[16];

            if (card->isMatched)
                snprintf(cell, sizeof(cell), " [XX] ");
            else if (card->isFlipped)
                snprintf(cell, sizeof(cell), " [%02d] ", card->faceValue);
            else
                snprintf(cell, sizeof(cell), " [%02d] ", card->faceValue);

            strncat(buffer, cell, bufsize - strlen(buffer) - 1);
        }

        strncat(buffer, "\n", bufsize - strlen(buffer) - 1);

        strncat(buffer, "IDs:    ", bufsize - strlen(buffer) - 1);
        for (int c = 0; c < cols; c++)
        {
            int idx = r * cols + c;
            char hint[16];

            snprintf(hint, sizeof(hint), " (%02d) ", idx);
            strncat(buffer, hint, bufsize - strlen(buffer) - 1);
        }

        strncat(buffer, "\n", bufsize - strlen(buffer) - 1);
    }
}

void printGameState(SharedGameState *state)
{
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
    char serverMsg[4096];
    char scoreMsg[512];
    char turnMsg[64];

    formatOfBoard(state, boardMsg, sizeof(boardMsg));
    scoreMsg[0] = '\0';
    pthread_mutex_lock(&state->mutex);
    strncat(scoreMsg, "\nScoreboard:\n", sizeof(scoreMsg) - strlen(scoreMsg) - 1);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (state->players[i].connected)
        {
            char line[128];
            const char *name = state->players[i].name[0] ? state->players[i].name : "Unknown";
            snprintf(line, sizeof(line), "%s (ID %d): Total Score %d | Score This Round %d\n",
                     name, i, state->players[i].score, state->players[i].roundScore);
            strncat(scoreMsg, line, sizeof(scoreMsg) - strlen(scoreMsg) - 1);
        }
    }
    pthread_mutex_unlock(&state->mutex);
    strncat(boardMsg, scoreMsg, sizeof(boardMsg) - strlen(boardMsg) - 1);
    pthread_mutex_lock(&state->mutex);
    snprintf(turnMsg, sizeof(turnMsg), "PLAYER TURN %d\n", state->currentTurn);
    pthread_mutex_unlock(&state->mutex);
    strncat(boardMsg, turnMsg, sizeof(boardMsg) - strlen(boardMsg) - 1);
    strcat(boardMsg, "<<END>>\n");

    pthread_mutex_lock(&state->mutex);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (state->players[i].connected)
        {
            send(state->players[i].socket, boardMsg, strlen(boardMsg), 0);
        }
    }
    pthread_mutex_unlock(&state->mutex);
    formatOfBoardForServer(state, serverMsg, sizeof(serverMsg));
    strncat(serverMsg, scoreMsg, sizeof(serverMsg) - strlen(serverMsg) - 1);
    strncat(serverMsg, turnMsg, sizeof(serverMsg) - strlen(serverMsg) - 1);
    printf("%s", serverMsg);
}

static void sendBoardStateToAllWithMessage(SharedGameState *state, const char *message)
{
    char boardMsg[4096];
    char serverMsg[4096];
    char scoreMsg[512];
    char turnMsg[64];

    formatOfBoard(state, boardMsg, sizeof(boardMsg));
    if (message && message[0] != '\0')
    {
        strncat(boardMsg, "\n", sizeof(boardMsg) - strlen(boardMsg) - 1);
        strncat(boardMsg, message, sizeof(boardMsg) - strlen(boardMsg) - 1);
    }
    scoreMsg[0] = '\0';
    pthread_mutex_lock(&state->mutex);
    strncat(scoreMsg, "\nScoreboard:\n", sizeof(scoreMsg) - strlen(scoreMsg) - 1);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (state->players[i].connected)
        {
            char line[128];
            const char *name = state->players[i].name[0] ? state->players[i].name : "Unknown";
            snprintf(line, sizeof(line), "%s (ID %d): Total Score %d | Score This Round %d\n",
                     name, i, state->players[i].score, state->players[i].roundScore);
            strncat(scoreMsg, line, sizeof(scoreMsg) - strlen(scoreMsg) - 1);
        }
    }
    pthread_mutex_unlock(&state->mutex);
    strncat(boardMsg, scoreMsg, sizeof(boardMsg) - strlen(boardMsg) - 1);
    pthread_mutex_lock(&state->mutex);
    snprintf(turnMsg, sizeof(turnMsg), "PLAYER TURN %d\n", state->currentTurn);
    pthread_mutex_unlock(&state->mutex);
    strncat(boardMsg, turnMsg, sizeof(boardMsg) - strlen(boardMsg) - 1);
    strncat(boardMsg, "\n<<END>>\n", sizeof(boardMsg) - strlen(boardMsg) - 1);

    pthread_mutex_lock(&state->mutex);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (state->players[i].connected)
        {
            send(state->players[i].socket, boardMsg, strlen(boardMsg), 0);
        }
    }
    pthread_mutex_unlock(&state->mutex);

    formatOfBoardForServer(state, serverMsg, sizeof(serverMsg));
    if (message && message[0] != '\0')
    {
        strncat(serverMsg, "\n", sizeof(serverMsg) - strlen(serverMsg) - 1);
        strncat(serverMsg, message, sizeof(serverMsg) - strlen(serverMsg) - 1);
    }
    strncat(serverMsg, scoreMsg, sizeof(serverMsg) - strlen(serverMsg) - 1);
    strncat(serverMsg, turnMsg, sizeof(serverMsg) - strlen(serverMsg) - 1);
    printf("%s", serverMsg);
}

static void printScoreboard(SharedGameState *state)
{
    pthread_mutex_lock(&state->mutex);
    printf("\n=== SCOREBOARD ===\n");
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (state->players[i].connected)
        {
            const char *name = state->players[i].name[0] ? state->players[i].name : "Unknown";
            printf("%s (ID %d): Total Score %d | Score This Round %d\n",name,i,state->players[i].score,state->players[i].roundScore);
        }
    }
    printf("==================\n\n");
    pthread_mutex_unlock(&state->mutex);
    fflush(stdout);
}

void sendTurnMessage(SharedGameState *state)
{
    char msg[256];

    pthread_mutex_lock(&state->mutex);
    int turn = state->currentTurn;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (!state->players[i].connected)
            continue;

        snprintf(msg, sizeof(msg), "PLAYER TURN %d\n<<END>>\n", turn);
        send(state->players[i].socket, msg, strlen(msg), 0);
    }
    pthread_mutex_unlock(&state->mutex);
}

void *gameLoopThread(void *arg)
{
    SharedGameState *state = (SharedGameState *)arg;
    bool boardInitialized = false;

    while (1)
    {
        if (!serverRunning)
        {
            printf("Game thread exiting...\n");
            break;
        }

        pthread_mutex_lock(&state->mutex);
        int current = state->currentTurn;
        bool gameStarted = state->gameStarted;
        bool broadcastStop = state->boardNeedsBroadcast;
        if (broadcastStop)
            state->boardNeedsBroadcast = false;
        pthread_mutex_unlock(&state->mutex);

        if (broadcastStop && !gameStarted)
        {
            const char *notify = "GAME_STOPPED\nWaiting for players...\nPlease type 1 to READY.\n<<END>>\n";
            pthread_mutex_lock(&state->mutex);
            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (state->players[i].connected)
                {
                    send(state->players[i].socket, notify, strlen(notify), 0);
                }
            }
            pthread_mutex_unlock(&state->mutex);
        }
        static int firstPick = -1;

        if (!gameStarted && boardInitialized)
        {
            boardInitialized = false;

            pushLogEvent(state, LOG_GAME, "Game restarted. Waiting for players.\n");
            pthread_mutex_lock(&state->mutex);
            const char *notify = "GAME_STOPPED\nWaiting for players...\nPlease type 1 to READY.\n<<END>>\n";
            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (state->players[i].connected)
                {
                    send(state->players[i].socket, notify, strlen(notify), 0);
                }
            }
            pthread_mutex_unlock(&state->mutex);
            printf("Connected players:\n");
            pthread_mutex_lock(&state->mutex);
            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (state->players[i].connected)
                {
                    printf(" - Player %d\n", i);
                }
            }

            pthread_mutex_unlock(&state->mutex);
            usleep(100000);
            continue;
        }

        if (gameStarted && !boardInitialized)
        {
            boardInitialized = true;

            printf("Game Started!\n");
            pushLogEvent(state, LOG_GAME, "Game started.\n");
            const char *startMsg = "\nGAME STARTED\n<<END>>\n";
            pthread_mutex_lock(&state->mutex);
            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (state->players[i].connected)
                {
                    send(state->players[i].socket,
                         startMsg,
                         strlen(startMsg),
                         0);
                }
            }
            if (state->currentTurn < 0)
            {
                for (int i = 0; i < MAX_PLAYERS; i++)
                {
                    if (state->players[i].connected)
                    {
                        state->currentTurn = i;
                        break;
                    }
                }
            }
            current = state->currentTurn;
            if (current >= 0 && current < MAX_PLAYERS)
            {
                printf("Player %d Flips Times : %d\n", state->players[current].playerID, state->players[current].flipsDone);
            }
            pthread_mutex_unlock(&state->mutex);
            sendBoardStateToAll(state);
            sendTurnMessage(state);
 
            continue;
        }

        if (gameStarted)
        {
            sem_wait(&state->flipDoneSemaphore);
            pthread_mutex_lock(&state->mutex);
            current = state->currentTurn;
            int flipsDone = state->players[current].flipsDone;
            pthread_mutex_unlock(&state->mutex);
            if (flipsDone == 1)
            {
                pthread_mutex_lock(&state->mutex);
                int flippedIndex = state->players[current].firstFlipIndex;
                Card *flippedCard = &state->cards[flippedIndex];
                flippedCard->isFlipped = true;
                pthread_mutex_unlock(&state->mutex);

                char logMsg[LOG_MSG_LENGTH];
                snprintf(logMsg, LOG_MSG_LENGTH,"Player %d flipped Card %d (Value: %d)\n",current,flippedIndex,flippedCard->faceValue);
                pushLogEvent(state, LOG_GAME, logMsg);
                char notifyMsg[256];
                snprintf(notifyMsg, sizeof(notifyMsg),"Player %d flipped card %d (Value: %d)",current,flippedIndex,flippedCard->faceValue);
                sendBoardStateToAllWithMessage(state, notifyMsg);
            }
            if (flipsDone == 2) 
            {
                printf("Player %d has done %d flips this turn.\n", current, state->players[current].flipsDone);
                pthread_mutex_lock(&state->mutex);
                int flipsDone = state->players[current].flipsDone;
                int firstCardIndex = state->players[current].firstFlipIndex;
                int secondCardIndex = state->players[current].secondFlipIndex;
                Card *firstCard = &state->cards[firstCardIndex];
                Card *secondCard = &state->cards[secondCardIndex];
                firstCard->isFlipped = true;
                secondCard->isFlipped = true;
                pthread_mutex_unlock(&state->mutex);
                sendBoardStateToAll(state);

                if (firstCard->faceValue == secondCard->faceValue)
                {
                    pthread_mutex_lock(&state->mutex);
                    firstCard->isMatched = true;
                    secondCard->isMatched = true;
                    state->matchedPaires++;
                    state->players[current].score++;
                    state->players[current].roundScore++;
                    pthread_mutex_unlock(&state->mutex);
                          printf("Player %d found a match! Total score: %d\n",current,state->players[current].score);
                    fflush(stdout);
                    char logMsg[LOG_MSG_LENGTH];
                    snprintf(logMsg, LOG_MSG_LENGTH,"Player %d found a match: Card %d and Card %d (Value: %d)\n",current,firstCardIndex,secondCardIndex,firstCard->faceValue);
                    pushLogEvent(state, LOG_GAME, logMsg);

                    snprintf(logMsg, LOG_MSG_LENGTH,"Player %d score updated: Round %d\n",current,state->players[current].score);
                    pushLogEvent(state, LOG_GAME, logMsg);

                    printScoreboard(state);

                    char notifyMsg[256];
                    snprintf(notifyMsg, sizeof(notifyMsg),"Player %d flipped card %d (Value: %d)\nPlayer %d flipped card %d (Value: %d)\nCards %d and %d match",current,firstCardIndex,firstCard->faceValue,current,secondCardIndex,secondCard->faceValue,firstCardIndex,secondCardIndex);
                    sendBoardStateToAllWithMessage(state, notifyMsg);
                }
                else
                {
                    char logMsg[LOG_MSG_LENGTH];
                    snprintf(logMsg, LOG_MSG_LENGTH,"Player %d did not match: Card %d and Card %d (Values: %d, %d)\n",current,firstCardIndex,secondCardIndex,firstCard->faceValue,secondCard->faceValue);
                    pushLogEvent(state, LOG_GAME, logMsg);

                    char notifyMsg[256];
                    snprintf(notifyMsg, sizeof(notifyMsg),"Player %d flipped card %d (Value: %d)\nPlayer %d flipped card %d (Value: %d)\nCards %d and %d not match",current,firstCardIndex,firstCard->faceValue,current,secondCardIndex,secondCard->faceValue,firstCardIndex,secondCardIndex);
                    sendBoardStateToAllWithMessage(state, notifyMsg);

                    usleep(2000000);
                    pthread_mutex_lock(&state->mutex);
                    firstCard->isFlipped = false;
                    secondCard->isFlipped = false;
                    pthread_mutex_unlock(&state->mutex);
                    sendBoardStateToAll(state);
                }
                usleep(500000);
                sem_post(&state->turnCompleteSemaphore);
                continue;
            }
        }
    }
}