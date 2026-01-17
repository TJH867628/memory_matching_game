#ifndef SCORES_H
#define SCORES_H

#include "shared_state.h"

void scores_init(SharedGameState *state);
void scores_load(SharedGameState *state);
void scores_save(SharedGameState *state);
void scores_add_win(SharedGameState *state, int playerID);

#endif
