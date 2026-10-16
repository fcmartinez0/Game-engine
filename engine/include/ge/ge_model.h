#ifndef GE_MODEL_H
#define GE_MODEL_H

#include "ge_fight.h"

/* ---------------------------------------------------------------------------
 * Player model: a small set of counters that watches how one fighter plays and
 * can say what they are likely to do next. It sees only what a spectator could
 * (states, positions, which attack started) plus the result of each hit; it
 * never looks at button presses. Counters are floats so they can decay, which
 * lets the model forget old habits quickly when a player changes style.
 *
 * Attach one to a fight with ge_fight_observe(); it is fed automatically.
 * Save it between sessions with ge_model_save / ge_model_load.
 * ------------------------------------------------------------------------- */

#define GE_DIST_N 4
enum { GE_DIST_CLOSE, GE_DIST_NEAR, GE_DIST_MID, GE_DIST_FAR };

/* Everything a player might be doing or about to do. The first ten are attack
 * roles (GE_AI_POKE .. GE_AI_THROW minus one); the rest are neutral behaviour. */
enum {
    GE_INT_POKE, GE_INT_LOW, GE_INT_OVERHEAD, GE_INT_ANTIAIR, GE_INT_ZONE, GE_INT_RUSH, GE_INT_COMBO, GE_INT_SUPER,
    GE_INT_AIR, GE_INT_THROW,
    GE_INT_WALK_F, GE_INT_WALK_B, GE_INT_STAND, GE_INT_CROUCH, GE_INT_JUMP, GE_INT_DASH, GE_INT_BLOCK, GE_INT_BLOCK_LOW,
    GE_INT_COUNT
};
#define GE_INT_ATTACKS 10
#define GE_INT_NEUTRALS (GE_INT_COUNT - GE_INT_ATTACKS)

/* how a player answered a situation */
enum { GE_RESP_ATTACK, GE_RESP_BLOCK, GE_RESP_JUMP, GE_RESP_DASH, GE_RESP_NOTHING, GE_RESP_COUNT };

/* overworld counters, bumped by the game */
enum { GE_OW_SLASH, GE_OW_NEEDLE, GE_OW_JUMP, GE_OW_DASH, GE_OW_POGO, GE_OW_HEAL, GE_OW_DEATH, GE_OW_HURT, GE_OW_COUNT };

typedef struct {                  /* transient: not saved */
    int tick, prev_state, prev_move;
    int last_intent, last_tick;
    int window, window_timer;     /* an open "how did they answer?" window: 0 none, 1 after block, 2 wake-up, 3 jump-in, 4 projectile */
    int window_flags;
    int cooldown;
} GeModelWatch;

struct GePlayerModel {
    float attacks[GE_DIST_N][GE_INT_ATTACKS];      /* attacks started, by range */
    float neutral[GE_DIST_N][GE_INT_NEUTRALS];     /* samples of neutral behaviour, by range */
    float bigram[GE_INT_ATTACKS][GE_INT_ATTACKS];  /* attack b started soon after attack a */
    float faced[4], blocked[4];                    /* attacks suffered / blocked, by guard height */
    float after_block[GE_RESP_COUNT], wakeup[GE_RESP_COUNT], vs_jumpin[GE_RESP_COUNT], vs_shot[GE_RESP_COUNT];
    float throws_faced, throws_teched, throws_made;
    float hits_landed, hits_taken;
    uint32_t duels, rounds, wins, losses, absorbed, finals;
    uint32_t ow[GE_OW_COUNT];
    GeModelWatch w;
};

void  ge_model_init(GePlayerModel *m);
void  ge_model_decay(GePlayerModel *m, float keep);   /* multiply every counter by `keep` (0..1) */
/* Probability of each GE_INT_* given the range bucket and the player's last attack
 * (-1 if none). `p` sums to 1. */
void  ge_model_predict(const GePlayerModel *m, int dist, int last_intent, float p[GE_INT_COUNT]);
float ge_model_confidence(const GePlayerModel *m);    /* 0 = knows nothing .. 1 = well informed */
/* Fraction of attacks of a guard height (GE_GUARD_*) the player blocked, or -1 if unseen. */
float ge_model_block_rate(const GePlayerModel *m, int guard);

int   ge_model_save(const GePlayerModel *m, const char *path);   /* 0 on success */
int   ge_model_load(GePlayerModel *m, const char *path);         /* 0 on success; counters it doesn't know are skipped */
/* Up to `max` short plain-English facts about this player, most distinctive first. Returns how many. */
int   ge_model_describe(const GePlayerModel *m, char out[][72], int max);

int   ge_dist_bucket(float dx);

/* ---- feeding it (called by ge_fight once ge_fight_observe is set) ---- */
void  ge_model_tick(GePlayerModel *m, const GeFight *f, int side);
void  ge_model_on_hit(GePlayerModel *m, const GeFight *f, int attacker, int guard, bool blocked, int observed);
void  ge_model_on_throw(GePlayerModel *m, int victim, bool teched, int observed);
void  ge_model_on_move_start(GePlayerModel *m, const GeFight *f, int side);

/* Attach a model to a fight: `side` is the fighter being studied. */
void  ge_fight_observe(GeFight *f, GePlayerModel *m, int side);

#endif
