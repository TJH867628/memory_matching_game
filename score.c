#include "score.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define SCORE_FILE "scores.txt"

void scores_init(SharedGameState *state) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&state->scoreBoard.scoreMutex, &attr);
    pthread_mutexattr_destroy(&attr);

    state->scoreBoard.count = 0;
}

void scores_load(SharedGameState *state) {
    FILE *fp = fopen(SCORE_FILE, "r");

    pthread_mutex_lock(&state->scoreBoard.scoreMutex);

    if (!fp) {
        state->scoreBoard.count = 0;
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd)))
            printf("scores_load: no scores.txt in %s\n", cwd);
        pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
        return;
    }

    state->scoreBoard.count = 0;
    char line[128];
    while (state->scoreBoard.count < MAX_PLAYERS && fgets(line, sizeof(line), fp))
    {
        char name[PLAYER_NAME_LENGTH];
        int score = 0;
        int fields = sscanf(line, "%31s %d", name, &score);
        if (fields == 2)
        {
            strncpy(state->scoreBoard.entries[state->scoreBoard.count].name, name, PLAYER_NAME_LENGTH - 1);
            state->scoreBoard.entries[state->scoreBoard.count].name[PLAYER_NAME_LENGTH - 1] = '\0';
            state->scoreBoard.entries[state->scoreBoard.count].wins = score;
            state->scoreBoard.count++;
        }
    }

    fclose(fp);
    pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
}

void scores_print(SharedGameState *state) {
    pthread_mutex_lock(&state->scoreBoard.scoreMutex);
    printf("\n=== SAVED SCORES ===\n");
    for (int i = 0; i < state->scoreBoard.count; i++) {
        printf("%s: %d\n",state->scoreBoard.entries[i].name,state->scoreBoard.entries[i].wins);
    }
    printf("====================\n\n");
    pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
    fflush(stdout);
}

void scores_save(SharedGameState *state) {
    FILE *fp = fopen(SCORE_FILE, "w");
    if (!fp) {
        perror("scores_save: fopen");
        return;
    }

    ScoreEntry entries[MAX_PLAYERS];
    int entryCount = 0;

    pthread_mutex_lock(&state->scoreBoard.scoreMutex);
    entryCount = state->scoreBoard.count;
    for (int i = 0; i < entryCount; i++)
    {
        entries[i] = state->scoreBoard.entries[i];
    }
    pthread_mutex_unlock(&state->scoreBoard.scoreMutex);

    pthread_mutex_lock(&state->mutex);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (state->players[i].connected && state->players[i].name[0] != '\0')
        {
            bool found = false;
            for (int j = 0; j < entryCount; j++)
            {
                if (strncmp(entries[j].name, state->players[i].name, PLAYER_NAME_LENGTH) == 0)
                {
                    entries[j].wins = state->players[i].score;
                    found = true;
                    break;
                }
            }
            if (!found && entryCount < MAX_PLAYERS)
            {
                strncpy(entries[entryCount].name, state->players[i].name, PLAYER_NAME_LENGTH - 1);
                entries[entryCount].name[PLAYER_NAME_LENGTH - 1] = '\0';
                entries[entryCount].wins = state->players[i].score;
                entryCount++;
            }
        }
    }
    pthread_mutex_unlock(&state->mutex);

    for (int i = 0; i < entryCount; i++)
    {
        fprintf(fp, "%s %d\n",entries[i].name,entries[i].wins);
    }

    fclose(fp);
}

void scores_add_win(SharedGameState *state, const char *name) {
    pthread_mutex_lock(&state->scoreBoard.scoreMutex);

    for (int i = 0; i < state->scoreBoard.count; i++) {
        if (strncmp(state->scoreBoard.entries[i].name, name, PLAYER_NAME_LENGTH) == 0) {
            state->scoreBoard.entries[i].wins++;
            pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
            return;
        }
    }

    if (state->scoreBoard.count < MAX_PLAYERS)
    {
        strncpy(state->scoreBoard.entries[state->scoreBoard.count].name, name, PLAYER_NAME_LENGTH - 1);
        state->scoreBoard.entries[state->scoreBoard.count].name[PLAYER_NAME_LENGTH - 1] = '\0';
        state->scoreBoard.entries[state->scoreBoard.count].wins = 1;
        state->scoreBoard.count++;
    }

    pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
}

int scores_get_wins(SharedGameState *state, const char *name) {
    int wins = 0;
    pthread_mutex_lock(&state->scoreBoard.scoreMutex);

    for (int i = 0; i < state->scoreBoard.count; i++) {
        if (strncmp(state->scoreBoard.entries[i].name, name, PLAYER_NAME_LENGTH) == 0) {
            wins = state->scoreBoard.entries[i].wins;
            break;
        }
    }

    pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
    return wins;
}
