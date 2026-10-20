#pragma once

#include <stdbool.h>

#include "chess.h"

// Picks a move for the side to move. level is 0 (easy) .. 2 (hard).
// *abort may be set from another thread to stop the search early.
// Returns false if there are no legal moves.
bool ai_pick_move(const Position *p, int level, volatile bool *abort, Move *out);

// Strongest move the engine can find in about max_ms milliseconds.
bool ai_best_move(const Position *p, int max_ms, volatile bool *abort, Move *out);
