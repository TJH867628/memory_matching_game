#ifndef SCORES_H
#define SCORES_H

#include "shared_state.h"

void scores_init(SharedGameState *state);
void scores_load(SharedGameState *state);
void scores_save(SharedGameState *state);
void scores_add_win(SharedGameState *state, const char *name);
int scores_get_wins(SharedGameState *state, const char *name);
void scores_print(SharedGameState *state);

#endif
