/* The overworld creatures' minds. See ai.h. */

#include "ai.h"
#include "demo.h"

#include <math.h>
#include <string.h>

#define T GE_TILE

/* What each creature is doing (one set of names for all four: a mode means different things to each) */
enum {
    AM_PATROL,      /* going about its business: pacing, hovering, waiting */
    AM_ALERT,       /* it has noticed you: a beat to turn and look (the tell) */
    AM_CHASE,       /* going for you, or for where it last saw you */
    AM_STRIKE,      /* mid-attack */
    AM_BACKOFF,     /* stepping away after an attack, so trading blows isn't free */
    AM_SEARCH,      /* got to where it last saw you: looking around, then giving up */
    AM_WINDUP,      /* bat: drawing back before a dive (the tell) */
    AM_DIVE,        /* bat: the dive, at where you were when it began */
    AM_RETREAT,     /* bat: going home after a dive */
    AM_COUNT
};
static const char *const MODE_NAMES[AM_COUNT] = { "patrol", "alert", "chase", "strike", "backoff", "search", "windup", "dive", "retreat" };

typedef struct {
    int mode, timer;            /* what it is doing, and for how many ticks */
    int dir;                    /* the way it paces (-1 / +1) */
    float seen_x;               /* where it last saw the hero */
    int unseen;                 /* ticks since it last saw the hero */
    int look;                   /* ticks until it looks again */
    bool sees;                  /* nothing solid between it and the hero, as of the last look */
    int cd;                     /* ticks before it may attack, spit or dive again */
    int jump_hold, jump_cd;     /* grunt: holding the jump button; time before it may hop again */
    int stuck;                  /* ticks it has been pushing against something */
    int pause;                  /* grunt: a stutter-step: ticks of standing still left */
    int burst;                  /* spitter: spits still to come in this burst */
    float tx, ty;               /* bat: the point it dives at */
    int slow;                   /* bat: ticks it has hardly moved while diving */
} Mind;

static Mind g_mind[GE_MAX_ENTS];

void ai_reset(int idx) { if (idx >= 0 && idx < GE_MAX_ENTS) memset(&g_mind[idx], 0, sizeof g_mind[idx]); }

const char *ai_mode_name(int idx, int kind) {
    (void)kind;
    int m = idx >= 0 && idx < GE_MAX_ENTS ? g_mind[idx].mode : 0;
    return MODE_NAMES[m >= 0 && m < AM_COUNT ? m : 0];
}

/* ---- eyes and feet ----------------------------------------------------------------------------------- */

bool ai_line_clear(const GeTilemap *m, float x0, float y0, float x1, float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)(sqrtf(dx * dx + dy * dy) / 6.0f) + 1;       /* a sample every 6 px: thinner than any wall */
    for (int i = 1; i < n; i++) {
        float t = (float)i / (float)n;
        if (ge_tilemap_get(m, (int)floorf((x0 + dx * t) / T), (int)floorf((y0 + dy * t) / T)) == GE_TILE_SOLID) return false;
    }
    return true;
}

/* Does it see the hero? Re-checked every 4 ticks (15 times a second is plenty, and the line costs a few samples).
 * rx, ry: how far away it can see, behind (bx) and in front (rx) of it, up or down. */
static void look(GeWorld *w, const GeEntity *e, const GeEntity *h, Mind *m, float rx, float bx, float ry) {
    if (m->look > 0) { m->look--; return; }
    m->look = 3;
    m->sees = false;
    if (!h) return;
    float dx = h->pos.x - e->pos.x, dy = h->pos.y - e->pos.y;
    float reach = dx * (float)e->facing >= 0.0f ? rx : bx;
    if (fabsf(dx) > reach || fabsf(dy) > ry) return;
    /* two lines: head to head (a one-tile step doesn't hide you from something tall enough to see over it) and
     * body to body (a low creature peers through the gap under a high ledge); a wall of two tiles stops both */
    float eh = (float)e->def->body_h - 3.0f, hh = (float)h->def->body_h - 3.0f;
    m->sees = ai_line_clear(&w->map, e->pos.x, e->pos.y - eh, h->pos.x, h->pos.y - hh) ||
              ai_line_clear(&w->map, e->pos.x, e->pos.y - (float)e->def->body_h * 0.5f, h->pos.x, h->pos.y - (float)h->def->body_h * 0.5f);
    if (m->sees) { m->seen_x = h->pos.x; m->unseen = 0; }      /* it remembers where it saw you when it looked, not after */
}

static int tile_at(const GeWorld *w, float x, float y) { return ge_tilemap_get(&w->map, (int)floorf(x / T), (int)floorf(y / T)); }

/* the row its feet are in, and the column a step ahead of it */
static int feet_row(const GeEntity *e) { return (int)floorf((e->pos.y - 2.0f) / T); }
static int front_col(const GeEntity *e, int dir) { return (int)floorf((e->pos.x + (float)dir * ((float)e->def->body_w * 0.5f + 3.0f)) / T); }

/* Can it take a step that way? Not into a wall (at any height of its body), not onto spikes, not off a drop
 * of more than max_drop tiles, not into a pit. */
static bool can_walk(const GeWorld *w, const GeEntity *e, int dir, int max_drop) {
    int c = front_col(e, dir), r = feet_row(e);
    int rows = (e->def->body_h + T - 1) / T;                 /* tiles tall */
    for (int k = 0; k < rows; k++) if (ge_tilemap_get(&w->map, c, r - k) == GE_TILE_SOLID) return false;
    for (int k = 1; k <= max_drop + 1; k++) {
        int t = ge_tilemap_get(&w->map, c, r + k);
        if (t == GE_TILE_SPIKE) return false;
        if (t == GE_TILE_SOLID || t == GE_TILE_ONEWAY) return true;
    }
    return false;                                            /* nothing to land on: a pit */
}

/* A one-tile step it can hop (a tile it can't walk into, with two clear tiles above it and room to rise) */
static bool hoppable(const GeWorld *w, const GeEntity *e, int dir) {
    int c = front_col(e, dir), r = feet_row(e), here = (int)floorf(e->pos.x / T);
    return ge_tilemap_get(&w->map, c, r) == GE_TILE_SOLID && ge_tilemap_get(&w->map, c, r - 1) != GE_TILE_SOLID &&
           ge_tilemap_get(&w->map, c, r - 2) != GE_TILE_SOLID && ge_tilemap_get(&w->map, here, r - 2) != GE_TILE_SOLID;
}

static float rnd(GeWorld *w) { return ge_rng_float(&w->rng); }
static int sign(float v) { return v >= 0.0f ? 1 : -1; }

/* pacing back and forth: turns at a wall, at a ledge it won't step off, and when it has been stuck */
static void pace(GeWorld *w, GeEntity *e, Mind *m, GeIntent *it, float speed, int max_drop) {
    if (m->dir == 0) m->dir = e->facing != 0 ? e->facing : -1;
    if (e->state == GE_ST_RUN && fabsf(e->vel.x) < 0.01f) m->stuck++; else m->stuck = 0;
    if (!can_walk(w, e, m->dir, max_drop) || m->stuck > 5) { m->dir = -m->dir; m->stuck = 0; }
    e->facing = m->dir;
    it->move_x = speed * (float)m->dir;
}

/* ---- the crawler: paces, notices you in front of it (or right behind), charges, gives up -------------------- */

static void crawler(GeWorld *w, int idx, GeEntity *e, const GeEntity *h, Mind *m, GeIntent *it) {
    (void)idx;
    if (m->unseen < 10000) m->unseen++;
    look(w, e, h, m, 110.0f, 40.0f, 26.0f);
    m->timer++;
    if (m->mode == AM_CHASE) {
        float tdx = (m->sees ? h->pos.x : m->seen_x) - e->pos.x;
        int dir = sign(tdx);
        if (fabsf(tdx) > 4.0f) e->facing = dir;
        if (m->unseen > 90) { m->mode = AM_PATROL; m->timer = 0; m->dir = e->facing; return; }
        if (fabsf(tdx) > 4.0f && can_walk(w, e, dir, 1)) it->move_x = (float)dir;      /* (waits at a ledge or a wall) */
        return;
    }
    if (m->mode == AM_ALERT) {                       /* a beat to turn and look: the tell */
        if (h) e->facing = sign(h->pos.x - e->pos.x);
        if (m->timer >= 10) { m->mode = AM_CHASE; m->timer = 0; }
        return;
    }
    pace(w, e, m, it, 0.6f, 1);
    if (m->sees) { m->mode = AM_ALERT; m->timer = 0; }
}

/* ---- the grunt: patrols, notices you, chases through hops and short drops, strikes, backs off ---------------- */

static void grunt(GeWorld *w, int idx, GeEntity *e, const GeEntity *h, Mind *m, GeIntent *it) {
    (void)idx;
    if (m->unseen < 10000) m->unseen++;
    look(w, e, h, m, 150.0f, 70.0f, 56.0f);
    if (m->cd > 0) m->cd--;
    if (m->jump_cd > 0) m->jump_cd--;
    if (m->jump_hold > 0) { m->jump_hold--; it->jump_held = true; }
    m->timer++;
    float dx = h ? h->pos.x - e->pos.x : 0.0f, dy = h ? h->pos.y - e->pos.y : 0.0f, adx = fabsf(dx);

    switch (m->mode) {
    case AM_PATROL:
        pace(w, e, m, it, 0.5f, 1);
        if (m->sees) { m->mode = AM_ALERT; m->timer = 0; }
        break;
    case AM_ALERT:                                   /* turns to look, for a quarter second */
        if (h) e->facing = sign(dx);
        if (m->timer >= 14) { m->mode = AM_CHASE; m->timer = 0; }
        break;
    case AM_CHASE: {
        float tdx = (m->sees ? h->pos.x : m->seen_x) - e->pos.x;
        int dir = sign(tdx);
        if (fabsf(tdx) > 4.0f) e->facing = dir;
        if (m->unseen > 20 && fabsf(tdx) < 10.0f) { m->mode = AM_SEARCH; m->timer = 0; break; }   /* it got there: you're not */
        if (m->unseen > 240) { m->mode = AM_PATROL; m->timer = 0; m->dir = e->facing; break; }
        if (!e->on_ground) { it->move_x = (float)dir; break; }                                     /* (steering a hop) */
        if (m->sees && adx < 26.0f && fabsf(dy) < 30.0f && m->cd == 0) {                            /* in reach: swing */
            it->attack_pressed = true;
            m->cd = 50 + (int)(rnd(w) * 70.0f);
            m->mode = AM_STRIKE; m->timer = 0;
            break;
        }
        if (m->pause > 0) { m->pause--; break; }                                                   /* a stutter-step: it isn't a metronome */
        if (m->sees && adx > 40.0f && adx < 90.0f && rnd(w) < 0.012f) { m->pause = 6 + (int)(rnd(w) * 10.0f); break; }
        if (m->sees && adx < 18.0f && m->cd > 0) { it->move_x = -0.5f * (float)dir; break; }        /* too close while it waits to swing */
        if (m->sees && dy < -34.0f && adx < 36.0f) break;                                           /* you're up where it can't go: it waits below */
        if (fabsf(tdx) <= 4.0f) break;
        if (hoppable(w, e, dir)) {
            if (m->jump_cd == 0) { it->jump_pressed = it->jump_held = true; m->jump_hold = 12; m->jump_cd = 30; }
            it->move_x = (float)dir;
        } else if (can_walk(w, e, dir, 3)) {                                                       /* (a drop of up to 3 tiles it will follow you down) */
            it->move_x = (float)dir;
        }                                                                                          /* else: a wall, a gate or a pit: it waits there */
        break;
    }
    case AM_STRIKE:
        if (h) e->facing = sign(dx);
        if (e->state != GE_ST_ATTACK && m->timer > 3) { m->mode = AM_BACKOFF; m->timer = 0; }
        break;
    case AM_BACKOFF: {                               /* a step or two back after the swing */
        int away = h ? -sign(dx) : -e->facing;
        if (can_walk(w, e, away, 1)) it->move_x = 0.5f * (float)away;
        if (m->timer >= 14 + (int)(rnd(w) * 14.0f)) { m->mode = AM_CHASE; m->timer = 0; }
        break;
    }
    case AM_SEARCH:                                  /* looks left, looks right, gives up */
        e->facing = (m->timer / 20) % 2 ? -m->dir : m->dir;
        if (m->timer == 1) m->dir = e->facing;
        if (m->timer >= 70) { m->mode = AM_PATROL; m->timer = 0; m->dir = e->facing; }
        if (m->sees) { m->mode = AM_ALERT; m->timer = 0; }
        break;
    default: m->mode = AM_PATROL; break;
    }
}

/* ---- the spitter: stays where it is, and only spits at what it can see -------------------------------------- */

static void spitter(GeWorld *w, int idx, GeEntity *e, const GeEntity *h, Mind *m, GeIntent *it) {
    (void)idx;
    look(w, e, h, m, 170.0f, 170.0f, 100.0f);          /* (it turns to face you, so front and back are the same) */
    if (m->cd > 0) m->cd--;
    m->mode = m->sees ? AM_ALERT : AM_PATROL;
    if (!m->sees) { m->burst = 0; return; }
    e->facing = sign(h->pos.x - e->pos.x);
    if (e->state == GE_ST_ATTACK) return;
    if (m->burst > 0 && m->cd == 0) {                  /* the second spit of a burst */
        m->burst--;
        it->tool_pressed = true;
        m->cd = 70 + (int)(rnd(w) * 60.0f);
    } else if (m->cd == 0) {
        it->tool_pressed = true;
        if (rnd(w) < 0.4f) { m->burst = 1; m->cd = 16; }            /* sometimes two in a row */
        else m->cd = 110 + (int)(rnd(w) * 80.0f);                   /* never on a metronome */
    }
}

/* ---- the bat: hovers at home, draws back (the tell), dives at where you were, goes home ----------------------- */

static float len2(float x, float y) { return sqrtf(x * x + y * y); }

static void bat(GeWorld *w, int idx, GeEntity *e, const GeEntity *h, Mind *m, GeIntent *it) {
    look(w, e, h, m, 130.0f, 130.0f, 100.0f);
    if (m->cd > 0) m->cd--;
    m->timer++;
    float home_x = e->ai_x0, home_y = e->ai_y0;
    switch (m->mode) {
    case AM_PATROL: {
        float tx = home_x + sinf((float)((int)w->tick + idx * 37) * 0.02f) * 30.0f, ty = home_y + sinf((float)w->tick * 0.05f) * 8.0f;
        if (tile_at(w, tx, ty) == GE_TILE_SOLID) { tx = home_x; ty = home_y; }          /* its beat runs into rock: it hovers at home */
        it->move_x = ge_clampf((tx - e->pos.x) * 0.05f, -1, 1);
        it->move_y = ge_clampf((ty - e->pos.y) * 0.05f, -1, 1);
        if (m->sees && m->cd == 0) { m->mode = AM_WINDUP; m->timer = 0; }
        break;
    }
    case AM_WINDUP: {                                 /* draws back and up: you can see it coming */
        if (h) e->facing = sign(h->pos.x - e->pos.x);
        int away = h ? -sign(h->pos.x - e->pos.x) : 1;
        it->move_x = 0.5f * (float)away;
        it->move_y = -0.4f;
        if (tile_at(w, e->pos.x + 14.0f * (float)away, e->pos.y) == GE_TILE_SOLID) it->move_x = 0.0f;
        if (!m->sees && m->timer > 6) { m->mode = AM_RETREAT; m->timer = 0; break; }   /* you ducked out of sight */
        if (m->timer >= 18 && h) { m->mode = AM_DIVE; m->timer = 0; m->slow = 0; m->tx = h->pos.x; m->ty = h->pos.y - 10.0f; }
        break;
    }
    case AM_DIVE: {
        if (h && m->timer < 12) { m->tx = h->pos.x; m->ty = h->pos.y - 10.0f; }          /* re-aims for a moment, then it is committed */
        float dx = m->tx - e->pos.x, dy = m->ty - e->pos.y, d = len2(dx, dy);
        if (d > 0.01f) { it->move_x = dx / d; it->move_y = dy / d; }
        e->facing = sign(dx);
        if (m->timer > 10 && len2(e->vel.x, e->vel.y) < 0.25f) m->slow++; else m->slow = 0;
        if (d < 14.0f || m->timer > 60 || m->slow > 8) { m->mode = AM_RETREAT; m->timer = 0; m->cd = 100 + (int)(rnd(w) * 60.0f); }
        break;
    }
    case AM_RETREAT: {                                /* back to its perch, rising if something is in the way */
        float dx = home_x - e->pos.x, dy = home_y - e->pos.y, d = len2(dx, dy);
        if (d > 0.01f) { it->move_x = 0.7f * dx / d; it->move_y = 0.7f * dy / d; }
        if (len2(e->vel.x, e->vel.y) < 0.15f && m->timer > 8) it->move_y = -1.0f;
        if (d < 20.0f || m->timer > 90) { m->mode = AM_PATROL; m->timer = 0; }
        break;
    }
    default: m->mode = AM_PATROL; break;
    }
}

/* ---- the front door ---------------------------------------------------------------------------------------- */

void ai_think(GeWorld *w, int idx, const GeEntity *hero, GeIntent *it) {
    GeEntity *e = &w->ents[idx];
    Mind *m = &g_mind[idx];
    if (!hero) m->sees = false;          /* (the last look said "I see you", but nobody is there now: the hero just died or left) */
    switch (e->kind) {
    case EN_CRAWLER: crawler(w, idx, e, hero, m, it); break;
    case EN_BAT:     bat(w, idx, e, hero, m, it); break;
    case EN_SPITTER: spitter(w, idx, e, hero, m, it); break;
    case EN_GRUNT:   grunt(w, idx, e, hero, m, it); break;
    default: break;
    }
}

void ai_provoke(int idx, const GeEntity *hero) {
    if (idx < 0 || idx >= GE_MAX_ENTS || !hero) return;
    Mind *m = &g_mind[idx];
    m->seen_x = hero->pos.x;
    m->unseen = 0;
    if (m->mode == AM_PATROL || m->mode == AM_SEARCH) { m->mode = AM_CHASE; m->timer = 0; }
}

/* ---- the boss: Thornwarden ---------------------------------------------------------------------------------- */

static const char *const BOSS_MODES[] = { "asleep", "intro", "idle", "swing", "charge", "leap", "throw", "dead", "dazed" };

void boss_reset(BossBrain *b) { memset(b, 0, sizeof *b); b->mode = B_SLEEP; b->last[0] = b->last[1] = -1; }
void boss_wake(BossBrain *b) { boss_reset(b); b->mode = B_INTRO; b->timer = 70; }
const char *boss_mode_name(const BossBrain *b) { return BOSS_MODES[b->mode >= 0 && b->mode <= B_DAZED ? b->mode : 0]; }

static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* The sideways speed to leave the ground with to land `dist` px away. In the air it slows down by half its air
 * acceleration every tick (nothing is steering it), so it has to start faster than dist / flight-time says. */
static float leap_speed(const GeEntity *e, float dist, float flight) {
    float a = e->def->air_accel * 0.5f;
    float v = dist >= 0.5f * a * flight * flight ? dist / flight + 0.5f * a * flight : sqrtf(2.0f * a * dist);
    return v > 4.5f ? 4.5f : v;
}

/* Is there floor under x? (Within three tiles below the feet; ledges and pits in an arena someone built.) */
static bool floor_at(const GeWorld *w, float x, float feet_y) {
    int c = (int)floorf(x / T), r = (int)floorf((feet_y - 2.0f) / T);
    for (int k = 1; k <= 3; k++) {
        int t = ge_tilemap_get(&w->map, c, r + k);
        if (t == GE_TILE_SOLID || t == GE_TILE_ONEWAY) return true;
    }
    return false;
}

/* a wall (at any height of its body) right in front of it, `dir` being the way it faces */
static bool wall_ahead(const GeWorld *w, const GeEntity *e, int dir) {
    float x = e->pos.x + (float)dir * ((float)e->def->body_w * 0.5f + 3.0f);
    for (float up = 4.0f; up < (float)e->def->body_h; up += 20.0f) if (tile_at(w, x, e->pos.y - up) == GE_TILE_SOLID) return true;
    return false;
}

/* Is the hero off the ground? (A dash along the floor takes them off it for a few ticks as far as the physics goes, and
 * it is the answer to a charge, not a jump: it doesn't count.) */
static bool hero_airborne(const GeEntity *h) { return !h->on_ground && h->state != GE_ST_WALL && h->state != GE_ST_DASH; }

/* What it has seen the hero do, kept as slow averages (a few seconds): a habit takes a while to notice and a while
 * to forget, and a hero who changes what they do is read again. */
static void boss_watch(BossBrain *b, const GeEntity *e, const GeEntity *h) {
    float dist = fabsf(h->pos.x - e->pos.x);
    bool air = hero_airborne(h);
    b->jumpy += ((air ? 1.0f : 0.0f) - b->jumpy) * 0.006f;
    b->ranged += ((dist > 140.0f ? 1.0f : 0.0f) - b->ranged) * 0.006f;
    b->hug_ticks = dist < 45.0f ? b->hug_ticks + 1 : 0;
    b->heal_ticks = h->state == GE_ST_CHANNEL ? b->heal_ticks + 1 : 0;
}

/* The pause after a move: longer when it is gentle (tempo), a few ticks of luck so the rhythm can't be learnt. */
static int boss_gap(GeWorld *w, const BossCtx *c, int base) {
    float t = c->tempo > 0.4f ? c->tempo : 1.0f;
    return (int)((float)base * t) + (int)(rnd(w) * 10.0f);
}

/* How far it could leap straight back (px of floor behind it, `behind` being -1 or +1): the arena's wall if the arena is
 * known (the leap's target is kept 2 tiles inside it), and any solid tile in the way. */
static float room_behind(const GeWorld *w, const GeEntity *e, int behind, const BossCtx *c) {
    float room = 9999.0f;
    if (c->wall1 > c->wall0) room = behind > 0 ? c->wall1 - 2.0f * GE_TILE - e->pos.x : e->pos.x - (c->wall0 + 2.0f * GE_TILE);
    for (float d = 8.0f; d <= 120.0f && d < room; d += 8.0f)
        if (tile_at(w, e->pos.x + (float)behind * d, e->pos.y - 4.0f) == GE_TILE_SOLID || !floor_at(w, e->pos.x + (float)behind * d, e->pos.y)) { room = d; break; }
    return room;
}

/* Which attack, from where the hero is and what they keep doing. */
static int boss_choose(BossBrain *b, GeWorld *w, const GeEntity *e, const GeEntity *h, const BossCtx *c, bool enraged, bool *away) {
    float dist = fabsf(h->pos.x - e->pos.x);
    bool air = hero_airborne(h);
    float jumpy = clamp01(b->jumpy * 2.5f), ranged = clamp01(b->ranged * 1.25f);
    float charge_reach = (enraged ? 6.5f : 5.5f) * 34.0f;          /* how far the charge's long frame carries it */
    int facing = h->pos.x >= e->pos.x ? 1 : -1;
    *away = false;

    /* a hero clinging to it: get clear (a leap away that sends waves back along the floor), if there is room */
    if (b->hug_ticks >= 50 && room_behind(w, e, -facing, c) >= 60.0f) {
        *away = true;
        return B_LEAP;
    }

    /* a hero who stops to heal is hit: the nearest thing that reaches */
    if (b->heal_ticks >= 10 && dist < 160.0f) {          /* (a heal takes about a second: what is further off than this lands too late, so it plays on as usual) */
        if (dist < 85.0f) return B_SWING;
        bool pit = false;
        for (float d = 16.0f; d <= dist + 16.0f; d += 16.0f) if (!floor_at(w, e->pos.x + (float)facing * d, e->pos.y)) pit = true;
        if (!pit) return B_CHARGE;
    }

    float W[4] = { 0, 0, 0, 0 };                                    /* swing, charge, leap, throw (indexed by move - B_SWING) */
    W[0] = dist < 80.0f ? 3.0f : dist < 110.0f ? 3.0f * (110.0f - dist) / 30.0f : 0.0f;
    W[1] = dist >= 80.0f && dist <= charge_reach ? 2.0f : dist < 80.0f ? 0.7f : 0.8f;
    for (float d = 16.0f; d <= charge_reach * 1.5f; d += 16.0f) {          /* it won't charge into a pit (or a wall: that is the dazing one); it slides on a way past the long frame */
        float x = e->pos.x + (float)facing * d;
        if (tile_at(w, x, e->pos.y - 20.0f) == GE_TILE_SOLID) break;
        if (!floor_at(w, x, e->pos.y)) { W[1] = 0.0f; break; }
    }
    W[2] = dist >= 60.0f && dist <= 200.0f ? 1.5f : dist < 60.0f ? 0.5f : 0.9f;
    W[3] = dist > 100.0f ? 1.5f : dist > 60.0f ? 0.6f : 0.2f;
    if (air) { W[0] *= dist < 90.0f ? 1.6f : 1.0f; W[1] *= 0.4f; W[2] *= 0.5f; W[3] *= 1.4f; }   /* (a hero in the air hops a charge and a wave) */
    W[0] *= 1.0f + 0.5f * jumpy; W[1] *= 1.0f - 0.6f * jumpy; W[2] *= 1.0f - 0.7f * jumpy; W[3] *= 1.0f + 0.5f * jumpy;
    W[1] *= 1.0f + 0.8f * ranged; W[2] *= 1.0f + 0.6f * ranged; W[3] *= 1.0f - 0.5f * ranged;
    if (enraged) { W[1] *= 1.3f; W[2] *= 1.2f; }
    for (int i = 0; i < 4; i++) {                                   /* variety: not the same thing again and again */
        if (b->last[0] == B_SWING + i) W[i] *= b->last[1] == b->last[0] ? 0.0f : 0.4f;
    }
    float total = W[0] + W[1] + W[2] + W[3];
    if (total <= 0.0f) return B_SWING;
    float roll = rnd(w) * total;
    for (int i = 0; i < 4; i++) { if (roll < W[i]) return B_SWING + i; roll -= W[i]; }
    return B_THROW;
}

void boss_think(GeWorld *w, int idx, const GeEntity *h, BossBrain *b, const BossCtx *c, GeIntent *it) {
    GeEntity *e = &w->ents[idx];
    memset(it, 0, sizeof *it);
    if (b->mode == B_SLEEP || b->mode == B_DEAD || e->state == GE_ST_DEAD) return;
    if (!h || !h->active || h->state == GE_ST_DEAD) return;
    float dx = h->pos.x - e->pos.x, dist = fabsf(dx);
    int toward = dx >= 0.0f ? 1 : -1;
    bool enraged = e->hp < e->max_hp / 2;
    if (e->hitstop > 0) return;              /* frozen by a hit this tick: the actor would drop what it is told, so decide nothing */
    if (b->timer > 0) b->timer--;
    boss_watch(b, e, h);
    b->age++;

    switch (b->mode) {
    case B_INTRO:
        e->facing = toward;
        if (b->timer == 0) { b->mode = B_IDLE; b->timer = boss_gap(w, c, 40); }
        break;
    case B_IDLE:
        e->facing = toward;
        if (dist > 60.0f && floor_at(w, e->pos.x + (float)toward * 20.0f, e->pos.y)) it->move_x = (float)toward * (enraged ? 0.8f : 0.6f);
        if (b->heal_ticks >= 10 && b->timer > 0) b->timer = 0;      /* it doesn't wait for its rhythm to hit a healer */
        if (b->timer == 0) {
            bool away = false;
            int mv = boss_choose(b, w, e, h, c, enraged, &away);
            b->last[1] = b->last[0]; b->last[0] = mv;
            b->mode = mv; b->timer = 10; b->age = 0; b->away = away; b->follow = false;
            switch (mv) {
            case B_SWING: it->attack_pressed = true; break;
            case B_CHARGE: it->attack_pressed = true; it->move_y = -1.0f; break;
            case B_LEAP:
                it->skill_pressed = true;                            /* the crouch: a beat to see it coming (the jump follows when it ends) */
                b->was_air = false; b->leaping = false;
                break;
            default:
                b->combo = enraged ? 3 : 2;
                it->tool_pressed = true;
                break;
            }
            if (mv == B_SWING && enraged && rnd(w) < 0.35f) b->follow = true;
        }
        break;
    case B_SWING:
        if (b->age < 10) e->facing = toward;                         /* it turns to you for the first beat of the windup, then it is committed */
        if (e->state != GE_ST_ATTACK && b->timer == 0) {
            if (b->follow && dist < 90.0f) { b->follow = false; it->attack_pressed = true; b->timer = 10; b->age = 0; }
            else { b->mode = B_IDLE; b->timer = boss_gap(w, c, enraged ? 25 : 45); }
        }
        break;
    case B_CHARGE:
        if (b->age < 12) e->facing = toward;
        if (e->state == GE_ST_ATTACK && e->anim.frame == 1) {       /* the long frame: barrel across the arena */
            if (wall_ahead(w, e, e->facing) && b->age > 28) {       /* ...into the wall: that hurts */
                e->vel.x = 0.0f;
                e->anim.frame = 2; e->anim.timer = 0;
                b->mode = B_DAZED; b->timer = 18 + (enraged ? 50 : 75);
                b->slams++;
                ge_world_shake(w, 8.0f);
                for (int i = 0; i < 14; i++)
                    ge_world_emit(w, e->pos.x + (float)e->facing * 12.0f, e->pos.y - ge_rng_range(&w->rng, 2, 46),
                                  -(float)e->facing * (float)ge_rng_range(&w->rng, 0, 3), (float)ge_rng_range(&w->rng, -3, 1), 0.1f, 40,
                                  GE_ARGB(255, 200, 175, 130), 3, GE_P_ADD);
                break;
            }
            e->vel.x = (float)e->facing * (enraged ? 6.5f : 5.5f);
        }
        if (e->state != GE_ST_ATTACK && b->timer == 0) {
            b->mode = B_IDLE; b->timer = boss_gap(w, c, enraged ? 30 : 50);
        }
        break;
    case B_LEAP:
        if (!b->leaping) {                                           /* crouching */
            if (b->age < 12) e->facing = toward;
            if (e->state != GE_ST_ATTACK && b->age > 3) {            /* up: aim at where the hero will be, and go */
                float flight = 2.0f * e->def->jump_vel / e->def->gravity;      /* ticks in the air */
                float tx = b->away ? e->pos.x - (float)toward * 110.0f : h->pos.x + h->vel.x * flight * 0.6f;
                if (c->wall1 > c->wall0) tx = ge_clampf(tx, c->wall0 + 2.0f * GE_TILE, c->wall1 - 2.0f * GE_TILE);
                while (fabsf(tx - e->pos.x) > 8.0f && !floor_at(w, tx, e->pos.y)) tx += tx > e->pos.x ? -8.0f : 8.0f;      /* (it lands on floor, not in a pit) */
                it->jump_pressed = it->jump_held = true;
                e->vel.x = (tx >= e->pos.x ? 1.0f : -1.0f) * leap_speed(e, fabsf(tx - e->pos.x), flight);
                b->leaping = true; b->was_air = false; b->timer = 10;
            } else if (b->age > 90) { b->mode = B_IDLE; b->timer = boss_gap(w, c, 20); }       /* (it never got to crouch) */
            break;
        }
        it->jump_held = true;
        if (!e->on_ground) b->was_air = true;
        else if (b->was_air) {                                       /* landed: shockwaves both ways */
            if (c->wave)
                for (int d = -1; d <= 1; d += 2) ge_world_shoot(w, 1, idx, c->wave, e->pos.x + (float)d * 18.0f, e->pos.y - 7.0f, d);
            ge_world_shake(w, 6.0f);
            b->mode = B_IDLE; b->timer = boss_gap(w, c, enraged ? 30 : 50);
        } else if (b->timer == 0) { b->mode = B_IDLE; b->timer = 20; }
        break;
    case B_THROW:
        e->facing = toward;
        if (e->state != GE_ST_ATTACK && b->timer == 0) {
            if (--b->combo > 0) { it->tool_pressed = true; b->timer = 8; }
            else { b->mode = B_IDLE; b->timer = boss_gap(w, c, 40); }
        }
        break;
    case B_DAZED:                                                    /* stunned against the wall: a free moment for the hero */
        if (b->timer == 0) { b->mode = B_IDLE; b->timer = boss_gap(w, c, 20); }
        break;
    default: break;
    }
}
