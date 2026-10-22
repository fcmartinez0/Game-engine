#ifndef GE_PARAGON_H
#define GE_PARAGON_H

#include "ge_fight.h"
#include "ge_model.h"

/* ---------------------------------------------------------------------------
 * The Paragon: a fighting AI that plays the *person*, not the character.
 *
 * Analogy: a chess engine that has also read your match history. It never
 * misses a move and knows every move's frame data exactly, but it sees the
 * world through a short reaction delay, and it chooses what to do from a
 * prediction of what *you* usually do (the GePlayerModel), so two players meet
 * two different opponents. See docs/PARAGON.md for the design.
 *
 * Use: ge_fight_observe(&fight, &model, human_side);   (feeds the model)
 *      ge_paragon_think(&brain, &fight, paragon_side, &input);
 * ------------------------------------------------------------------------- */

#define GE_PARAGON_VIEW 24

typedef struct {
    int state, move;        /* the opponent as it looked `react` ticks ago */
    float dist;
    bool airborne;
} GeParagonSnap;

typedef struct {
    GePlayerModel *model;   /* what it has learned about its opponent (may be NULL: plays on instinct) */
    int learn;              /* 0..100, see ge_paragon_set_learn */
    int react;              /* reaction delay in ticks */
    float explore;          /* chance to deliberately play a non-optimal option */
    float decay_rate;       /* per second: how fast the model forgets while fighting (0 = never) */

    /* internals */
    GeParagonSnap view[GE_PARAGON_VIEW];
    int head, cd, block_for, hold_x, hold_y, hold_for;
    bool block_low, tech_decided, tech_yes;
    int decay_timer;
    int odir[16];                      /* its own recent directions (numpad), newest first, to avoid accidental dashes */
    int stat_reads, stat_punishes;    /* for tests and tuning */
} GeParagon;

void ge_paragon_init(GeParagon *p, GePlayerModel *opponent_model);
/* learn: 0 = knows nothing (slow, guessy) .. 100 = has studied the whole journey. `react_bonus` shaves ticks. */
void ge_paragon_set_learn(GeParagon *p, int learn, int react_bonus);
void ge_paragon_think(GeParagon *p, GeFight *f, int side, GeFightInput *out);

#endif
