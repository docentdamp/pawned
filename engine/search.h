#ifndef _SEARCH_H
#define _SEARCH_H

#include "defines.h"
#include "state.h"
#include "ttable.h"

#define SEARCH_MIN_RESULT(depth) (-1000-(depth))
#define SEARCH_MAX_RESULT(depth) (1000+(depth))

typedef struct {
    ttable_t    *ttable;
    int         abort_search;
} search_state_t;

int SEARCH_perform_search(const chess_state_t *s, ttable_t *ttable, short depth, int *score);
int SEARCH_is_check(const chess_state_t *s, int color);
int SEARCH_is_mate(const chess_state_t *state);

static inline int SEARCH_clamp_score_to_valid_range(int score, int depth)
{
    int min = SEARCH_MIN_RESULT(depth);
    int max = SEARCH_MAX_RESULT(depth);
    if(score < min) {
        score = min;
    } else if(score > max) {
        score = max;
    }
    return score;
}

#endif

