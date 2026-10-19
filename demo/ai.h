#ifndef AI_H
#define AI_H

/* The minds of the overworld creatures (crawlers, bats, spitters, grunts). Each tick, ai_think() turns what a
 * creature can see and reach into the intent the actor controller already understands (the same move/jump/attack
 * buttons the hero presses), so a creature can do nothing the hero's physics wouldn't allow it.
 *
 * What they have in common: eyes (a creature only knows where you are while nothing solid is between you,
 * checked a few times a second, not every tick), a short memory (where it last saw you), and terrain sense (it
 * doesn't walk off a ledge it can't survive or into spikes, it hops a one-tile step and waits at a wall or gate
 * it can't climb). Analogy: before, each creature was a toy on a track that ran the same loop; now each is a
 * guard dog on a leash it can read: it notices you, goes where it last saw you, gives up after a while, and
 * can be fooled by a wall.
 *
 * The duelists have their own mind (engine/src/ge_cpu.c). The boss, Thornwarden, has one here too (below). */

#include "ge/ge_world.h"

void ai_reset(int idx);                      /* a creature was just spawned in entity slot idx */
/* One creature's tick. `hero` is NULL when there is nobody to see (dead, or gone); `it` has been cleared. */
void ai_think(GeWorld *w, int idx, const GeEntity *hero, GeIntent *it);
/* It was hurt by the hero: it knows where they are now, whatever it could see. */
void ai_provoke(int idx, const GeEntity *hero);
/* nothing solid between two points (a few samples per tile): what "sees" means */
bool ai_line_clear(const GeTilemap *m, float x0, float y0, float x1, float y1);
/* for tests and the debug overlay: what the creature is doing, as a name */
const char *ai_mode_name(int idx, int kind);


/* ---- the boss: Thornwarden ---------------------------------------------------------------------------------
 * One mind for the one big fight. It has four moves (a sweeping swing, a charge across the arena, a leap that sends
 * shockwaves along the floor, a volley of spears) and picks between them from where you are and what you have been
 * doing: it hits a hero who stops to heal, it won't charge at someone who keeps jumping, it charges and leaps at one who
 * stays away, it leaps clear of one who hugs it (if it has room). Slip a charge and it runs into the wall and is dazed for a moment.
 * Below half health it waits less, charges faster and sometimes swings twice. Analogy: before, it rolled a die
 * every second; now it plays like a person who watches you and has a plan. It never does anything the hero's own
 * moves wouldn't allow (the same intents), and every attack still has its tell. */
enum { B_SLEEP, B_INTRO, B_IDLE, B_SWING, B_CHARGE, B_LEAP, B_THROW, B_DEAD, B_DAZED };

typedef struct {
    int mode, timer;           /* what it is doing (B_*), and a countdown that means something different in each */
    int combo;                 /* spears left in a volley */
    int age;                   /* ticks into the current move */
    bool was_air, away;        /* leap: it has left the ground / it is leaping clear of the hero instead of at them */
    bool follow;               /* it will swing again when this one is over */
    int last[2];               /* the last two attacks it chose */
    float jumpy, ranged, hugs; /* what it has seen the hero do lately: share of time in the air / far away / close */
    int hug_ticks, heal_ticks; /* ticks in a row the hero has been close / healing */
    int slams;                 /* charges that ended in the wall (for tests and the play log) */
} BossBrain;

typedef struct {
    float wall0, wall1;        /* the arena's floor from its first free pixel to its last, in pixels (wall1 <= wall0: unknown) */
    float tempo;               /* pauses between attacks are multiplied by this: 1 normal, more is gentler */
    const GeShotDef *wave;     /* what a landing sends along the floor */
} BossCtx;

void boss_reset(BossBrain *b);                 /* asleep */
void boss_wake(BossBrain *b);                  /* the fight begins: its intro */
/* One tick of the fight. `hero` is NULL when there is nobody to fight. `it` is cleared first. */
void boss_think(GeWorld *w, int idx, const GeEntity *hero, BossBrain *b, const BossCtx *ctx, GeIntent *it);
const char *boss_mode_name(const BossBrain *b);

#endif
