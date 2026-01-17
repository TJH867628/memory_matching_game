#include "score.h"
#include <stdio.h>
#include <string.h>

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
        // File doesn't exist yet
        state->scoreBoard.count = 0;
        pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
        return;
    }

    state->scoreBoard.count = 0;
    while (fscanf(fp, "%d %d",
        &state->scoreBoard.entries[state->scoreBoard.count].playerID,
        &state->scoreBoard.entries[state->scoreBoard.count].wins) != EOF) {

        state->scoreBoard.count++;
    }

    fclose(fp);
    pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
}

void scores_save(SharedGameState *state) {
    FILE *fp = fopen(SCORE_FILE, "w");
    if (!fp) return;

    pthread_mutex_lock(&state->scoreBoard.scoreMutex);

    for (int i = 0; i < state->scoreBoard.count; i++) {
        fprintf(fp, "%d %d\n",
            state->scoreBoard.entries[i].playerID,
            state->scoreBoard.entries[i].wins);
    }

    pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
    fclose(fp);
}

void scores_add_win(SharedGameState *state, int playerID) {
    pthread_mutex_lock(&state->scoreBoard.scoreMutex);

    for (int i = 0; i < state->scoreBoard.count; i++) {
        if (state->scoreBoard.entries[i].playerID == playerID) {
            state->scoreBoard.entries[i].wins++;
            pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
            return;
        }
    }

    // New player
    state->scoreBoard.entries[state->scoreBoard.count].playerID = playerID;
    state->scoreBoard.entries[state->scoreBoard.count].wins = 1;
    state->scoreBoard.count++;

    pthread_mutex_unlock(&state->scoreBoard.scoreMutex);
}
