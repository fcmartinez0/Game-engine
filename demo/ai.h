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
 * The duelists and the boss keep their own code (main.c). */

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

#endif
