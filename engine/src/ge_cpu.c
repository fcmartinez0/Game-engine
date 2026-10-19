/* The CPU fighter: what plays the other side of a duel when nobody is holding the pad. See ge_fight.h.
 *
 * It is built the way a person plays, in four layers, so that each can be turned up or down:
 *   skill      how well it plays (the level: -1 beginner .. 3 sharp): reaction time, how often it punishes,
 *              breaks throws, blocks the right height
 *   style      how it likes to play (GeCpuStyle): rush-down, spacing, grappling, patient, mix-ups
 *   habits     what it has seen you do this fight (jump in, fireballs, turtling, repeating a move), and how
 *              much it changes its play because of it (style.adapt)
 *   frame data the same numbers a player reads off a move: startup, reach, how long until the other fighter
 *              can act (ge_fight_frames_until_free), so "can I hit them before they recover?" has a real answer
 * Analogy: a boxer with a trainer's plan (style), who gets better at spotting your habits as the round goes. */

#include "ge/ge_fight.h"

#include <math.h>
#include <string.h>

enum { ST_CROUCH_ = 2, ST_AIR_ = 4 };    /* mirrors ge_fight.c's stance codes */

static GeEntity *ent(GeFight *f, int s) { return &f->world.ents[f->f[s].ent]; }
static const GeClip *clip_of(const GeFight *f, int s, int mi) { return &f->spr[s]->clips[f->f[s].def->moves[mi].clip_idx]; }
static float rnd(GeFight *f) { return ge_rng_float(&f->world.rng); }
static float clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

/* how well it plays, 0 .. 1, from the level (-1 beginner .. 3) */
static float skill_of(int level) {
    static const float S[5] = { 0.0f, 0.35f, 0.6f, 0.8f, 1.0f };
    return S[ge_clampi(level, -1, 3) + 1];
}

static int move_damage(const GeFight *f, int s, int mi) {
    const GeMove *m = &f->f[s].def->moves[mi];
    return m->is_throw ? f->f[s].def->throw_damage : clip_of(f, s, mi)->damage;
}

/* Is this fighter past the last active frame of its move, but not yet free to act? (An attack that missed or was
 * blocked, in its recovery: the window to punish.) */
static bool in_recovery(const GeFight *f, int s) {
    const GeFighter *F = &f->f[s];
    if (F->state != GE_FS_MOVE) return false;
    const GeMove *m = &F->def->moves[F->move];
    if (m->is_throw) return false;
    const GeClip *c = clip_of(f, s, F->move);
    const GeEntity *e = &f->world.ents[F->ent];
    int last = -1;
    for (int i = 0; i < c->nframes; i++) if (c->frames[i].has_hit) last = i;
    if (m->shot && c->shoot > last) last = c->shoot;
    return !e->anim.done && e->anim.frame > last;
}

/* How far from its attacker a move can still touch: its reach, a little slack, and more for a move that lunges
 * forward (the attacker travels). An opponent further than this is out of range: the move can't hit them. */
static float touch_edge(const GeFight *f, int s, int mi) {
    const GeClip *c = clip_of(f, s, mi);
    return (float)ge_fight_move_reach(f, s, mi) + 6.0f + 14.0f * c->lunge;
}

/* Is the opponent attacking from too far away to touch us, so that its move is going to miss? That is an opening from
 * the first frame: a good player sees it coming and answers it, they don't wait for the recovery. `their_edge` is how
 * far its move has to reach, from where it stands, to touch us. */
static bool whiffing(const GeFight *f, int s, float their_edge) {
    const GeFighter *O = &f->f[1 - s];
    if (O->state != GE_FS_MOVE || O->contact) return false;
    const GeMove *m = &O->def->moves[O->move];
    if (m->shot || m->is_throw) return false;
    return their_edge > touch_edge(f, 1 - s, O->move);
}

/* The opponent's move was blocked and we are the first to be free again. The same opening a whiff gives, only it
 * starts the moment the move lands, while we are still in guard: a player counts the frames of blockstun. */
static bool blocked_advantage(const GeFight *f, int s) {
    const GeFighter *O = &f->f[1 - s];
    if (O->state != GE_FS_MOVE || !O->contact || f->f[s].state != GE_FS_BLOCKSTUN) return false;
    return ge_fight_frames_until_free(f, 1 - s) - ge_fight_frames_until_free(f, s) >= 4;
}

/* a random move of a role that can be started now; edge >= 0: only ones that would reach the opponent's near edge */
static int pick(GeFight *f, int s, int role, bool air, int min_level, float edge) {
    const GeFighterDef *d = f->f[s].def;
    int cand[GE_FIGHT_MAX_MOVES], n = 0;
    for (int i = 0; i < d->nmoves; i++) {
        const GeMove *m = &d->moves[i];
        bool role_ok = role < 0 ? (m->ai != GE_AI_THROW && m->ai != GE_AI_ZONE && m->ai != GE_AI_NONE) : m->ai == role;
        if (m->ai == GE_AI_SUPER && role != GE_AI_SUPER) continue;       /* (a super is only ever asked for by name, and only by a fighter good enough to) */
        if (!role_ok || (m->stance == ST_AIR_) != air || m->level < min_level || !ge_fight_can_start(f, s, i)) continue;
        if (edge >= 0.0f && !m->is_throw && !m->shot && edge > (float)ge_fight_move_reach(f, s, i) + 2.0f) continue;
        cand[n++] = i;
    }
    return n ? cand[ge_rng_next(&f->world.rng) % (uint32_t)n] : -1;
}

/* The hardest-hitting move that can land on an opponent `edge` away and starts up within `window` ticks. */
static int best_punish(const GeFight *f, int s, int window, float edge, bool supers) {
    const GeFighterDef *d = f->f[s].def;
    int best = -1, best_dmg = -1;
    for (int i = 0; i < d->nmoves; i++) {
        const GeMove *m = &d->moves[i];
        if (m->ai == GE_AI_NONE || m->ai == GE_AI_ZONE || m->ai == GE_AI_ANTIAIR || m->stance == ST_AIR_) continue;
        if (m->ai == GE_AI_SUPER && !supers) continue;
        if (!ge_fight_can_start(f, s, i)) continue;
        if (ge_fight_move_startup(f, s, i) + 2 > window) continue;
        if (!m->is_throw && !m->shot && edge > (float)ge_fight_move_reach(f, s, i) + 2.0f) continue;
        if (m->shot) continue;
        int dmg = move_damage(f, s, i);
        if (dmg > best_dmg) { best_dmg = dmg; best = i; }
    }
    return best;
}

/* ---- what it has seen you do -------------------------------------------------------------------------- */

static void observe(GeFight *f, int s, int level, float their_edge) {
    GeCpuMind *m = &f->cpu[s];
    const GeFighter *O = &f->f[1 - s];
    m->jumps *= 0.9985f; m->shots *= 0.9985f; m->blocks *= 0.9985f; m->throws *= 0.9985f; m->attacks *= 0.9985f;
    if (m->dash_cd > 0) m->dash_cd--;
    if (m->bait > 0) m->bait--;
    if ((int)O->state != m->prev_state || (O->state == GE_FS_MOVE && O->move != m->prev_move)) {
        if (O->state == GE_FS_AIR) m->jumps += 1.0f;
        if (O->state == GE_FS_BLOCKSTUN) m->blocks += 1.0f;
        if (O->state == GE_FS_MOVE) {
            const GeMove *mv = &O->def->moves[O->move];
            m->attacks += 1.0f;
            if (mv->shot) m->shots += 1.0f;
            if (mv->is_throw) m->throws += 1.0f;
            if (O->move == m->last_move) m->repeat++; else { m->repeat = 1; m->last_move = O->move; }
        }
    }
    m->prev_state = (int)O->state;
    m->prev_move = O->move;

    /* an opening: it notices when the opponent's attack is over but it can't act yet, then needs a reaction time
     * (a person doesn't punish on the first possible frame), and decides once whether to take it at all */
    bool open = (in_recovery(f, 1 - s) && ge_fight_frames_until_free(f, 1 - s) >= 5) || whiffing(f, s, their_edge) || blocked_advantage(f, s);
    if (open && !m->was_open) {
        float skill = skill_of(level);
        m->react = (int)(16.0f - 9.0f * skill + rnd(f) * 4.0f);                    /* sharp: 7-11 ticks, beginner: 16-20 */
        float p = level < 0 ? 0.10f : 0.25f + 0.70f * skill;                        /* a beginner takes one in ten */
        m->punish = rnd(f) < p + 0.12f * (f->cpu_style[s].patience - 0.4f);
    }
    m->was_open = open;
    if (m->react > 0) m->react--;
}

/* ---- the CPU ------------------------------------------------------------------------------------------- */

enum { NA_WAIT, NA_WALK_IN, NA_WALK_OUT, NA_POKE, NA_LOW, NA_OVERHEAD, NA_THROW, NA_ZONE, NA_RUSH, NA_DASH_IN, NA_BACKDASH, NA_JUMP_IN, NA_COUNT };

static bool has_role(const GeFight *f, int s, int role) {
    const GeFighterDef *d = f->f[s].def;
    for (int i = 0; i < d->nmoves; i++) if (d->moves[i].ai == role) return true;
    return false;
}

void ge_fight_cpu(GeFight *f, int s, int level, GeFightInput *out) {
    memset(out, 0, sizeof *out);
    out->force_move = -1;
    if (f->phase != GE_FP_FIGHT) return;
    GeFighter *F = &f->f[s], *O = &f->f[1 - s];
    GeCpuMind *mind = &f->cpu[s];
    const GeCpuStyle *st = &f->cpu_style[s];
    GeEntity *e = ent(f, s), *oe = ent(f, 1 - s);
    level = ge_clampi(level, -1, 3);
    float skill = skill_of(level);
    float p = level < 0 ? 0.12f : 0.3f + 0.2f * (float)level;      /* how often it guards what it sees coming: -1 beginner: 0.12, then 0.3 .. 0.9 */
    float dx = oe->pos.x - e->pos.x, dist = fabsf(dx);
    int toward = dx >= 0 ? 1 : -1;
    /* Reach is measured from a fighter's own centre to the far side of the other's body. So a move of mine touches them
     * when edge <= its reach, and a move of theirs touches me when their_edge <= its reach. */
    float edge = dist - O->def->body.body_w * 0.5f, their_edge = dist - F->def->body.body_w * 0.5f;
    if (F->ai_cd > 0) F->ai_cd--;
    observe(f, s, level, their_edge);

    /* what you keep doing, and how much it matters to this fighter */
    float a = st->adapt * skill;
    float jumpy = clamp01(mind->jumps / 3.0f) * a, shotty = clamp01(mind->shots / 3.0f) * a;
    float turtle = clamp01(mind->blocks / 4.0f) * a, repeater = mind->repeat >= 3 ? a : 0.0f;

    /* ---- being thrown: break it, if it can (L+M together inside a 16-tick window) ---- */
    if (F->state == GE_FS_THROWN) {
        if (!mind->tech_decided) {
            mind->tech_decided = true;
            static const float TECH[5] = { 0.0f, 0.12f, 0.35f, 0.60f, 0.85f };
            mind->tech = rnd(f) < TECH[level + 1];
            mind->tech_at = 25 - (int)(14.0f - 8.0f * skill + rnd(f) * 3.0f);      /* a reaction time into the window */
            if (mind->tech_at < 13) mind->tech_at = 13;
        }
        if (mind->tech && F->timer <= mind->tech_at && F->timer > 11) out->pressed = GE_FB_L | GE_FB_M;
        return;
    }
    mind->tech_decided = false;

    /* ---- defence: guard what is coming (and read lows and overheads at higher levels) ---- */
    bool threat = false;
    if (O->state == GE_FS_MOVE && !in_recovery(f, 1 - s)) {     /* (a move that is over its active frames is no threat: it is an opening) */
        if (level <= 0) threat = dist < 95.0f;
        else {                                                    /* from level 1: only what can actually touch it */
            const GeMove *om = &O->def->moves[O->move];
            threat = om->shot ? false : their_edge < touch_edge(f, 1 - s, O->move);
        }
    }
    for (int i = 0; i < GE_MAX_SHOTS; i++) {
        const GeShot *sh = &f->world.shots[i];
        if (sh->active && sh->team != s && fabsf(sh->pos.x - e->pos.x) < 110.0f && (sh->vel.x > 0) == (sh->pos.x < e->pos.x)) threat = true;
    }
    bool free_ground = F->state == GE_FS_STAND || F->state == GE_FS_CROUCH || F->state == GE_FS_BLOCKSTUN;
    if (threat && free_ground) {
        if (F->ai_block_for == 0) {
            float pb = p + 0.4f * repeater;                        /* a move you keep repeating gets guarded */
            F->ai_block_for = rnd(f) < (pb > 0.97f ? 0.97f : pb) ? 1 : -1;
        }
        if (F->ai_block_for == 1) {
            out->x = -toward;
            int guard = O->state == GE_FS_MOVE ? clip_of(f, 1 - s, O->move)->guard : GE_GUARD_MID;
            out->y = (guard == GE_GUARD_LOW && level >= 1) ? 1 : 0;
            return;
        }
    } else if (!threat) {
        F->ai_block_for = 0;
    }

    /* ---- punish: the opponent's attack missed or was blocked and it is still recovering ---- */
    if (level >= 0 || mind->punish) {
        if (mind->punish && mind->react == 0 && (F->state == GE_FS_STAND || F->state == GE_FS_CROUCH) && (in_recovery(f, 1 - s) || whiffing(f, s, their_edge) || O->contact)) {
            int window = ge_fight_frames_until_free(f, 1 - s);
            int mv = -1;
            if (F->meter >= GE_FIGHT_METER && skill >= 0.6f) {
                for (int i = 0; i < F->def->nmoves && mv < 0; i++)
                    if (F->def->moves[i].ai == GE_AI_SUPER && ge_fight_can_start(f, s, i) && edge <= (float)ge_fight_move_reach(f, s, i) + 2.0f &&
                        ge_fight_move_startup(f, s, i) + 2 <= window) mv = i;
            }
            if (mv < 0) mv = best_punish(f, s, window, edge, skill >= 0.6f);
            if (mv >= 0) {
                out->force_move = mv;
                if (F->def->moves[mv].stance == ST_CROUCH_) out->y = 1;
                mind->punish = false;
                return;
            }
            if (skill >= 0.8f && window > 24 && edge < 150.0f && mind->dash_cd == 0) {     /* too far to reach: a sharp fighter closes in */
                out->pressed |= GE_FB_DASH; out->x = toward;
                mind->dash_cd = 40;
                return;
            }
        }
    }

    /* ---- combos: on a confirmed hit, cancel into something stronger; on a block, press on (a frame trap) ---- */
    if (F->state == GE_FS_MOVE && (F->hit_landed || (F->contact && level >= 1))) {
        const GeMove *cur = &F->def->moves[F->move];
        float chance = F->hit_landed ? p + 0.1f : (0.15f + 0.35f * st->aggression) * skill;
        if (rnd(f) < chance) {
            int mv = F->meter >= GE_FIGHT_METER && F->hit_landed && skill >= 0.6f && rnd(f) < 0.5f ? pick(f, s, GE_AI_SUPER, false, cur->level + 1, -1.0f) : -1;
            if (mv < 0) mv = pick(f, s, -1, cur->stance == ST_AIR_, cur->level + 1, F->hit_landed ? -1.0f : edge);
            out->force_move = mv;
        }
        return;
    }
    /* ---- anti-air, jump-ins: swing on the way down ---- */
    if (O->state == GE_FS_AIR && dist < 110.0f && F->state <= GE_FS_CROUCH && rnd(f) < p * 0.25f + 0.55f * jumpy) {
        int mv = pick(f, s, GE_AI_ANTIAIR, false, 0, -1.0f);
        if (mv >= 0) { out->force_move = mv; return; }
    }
    if (F->state == GE_FS_AIR && !F->air_attacked && dist < 60.0f && e->vel.y > -1.0f) {
        out->force_move = pick(f, s, GE_AI_AIR, true, 0, -1.0f);
        return;
    }
    if (F->state != GE_FS_STAND && F->state != GE_FS_CROUCH) return;

    if (F->ai_cd > 0) { out->x = F->ai_hold_x; out->y = F->ai_hold_y; return; }

    /* ---- stepping out of reach, to make a repeated attack miss and then punishing it ---- */
    if (repeater > 0.0f && mind->bait == 0 && O->state == GE_FS_STAND && skill >= 0.8f && rnd(f) < 0.05f) mind->bait = 14;
    if (mind->bait > 0 && edge < 90.0f) { out->x = -toward; F->ai_hold_x = -toward; F->ai_hold_y = 0; return; }

    /* ---- neutral: choose what to do by weight, from its style, the distance and what it has seen ---- */
    float w[NA_COUNT];
    memset(w, 0, sizeof w);
    float A = st->aggression, S = st->spacing, G = st->grab, P = st->patience, M = st->mixup;
    bool can_dash = skill >= 0.3f && mind->dash_cd == 0;
    bool zone = has_role(f, s, GE_AI_ZONE), rush = has_role(f, s, GE_AI_RUSH);
    if (dist > 170.0f) {                                                     /* far */
        w[NA_WALK_IN] = 0.35f + 0.5f * A - 0.2f * P + 0.4f * shotty;
        w[NA_DASH_IN] = can_dash ? (0.25f * A + 0.35f * shotty) * skill : 0.0f;
        w[NA_ZONE] = zone ? (0.15f + 0.5f * S) * (1.0f - 0.8f * shotty) : 0.0f;
        w[NA_RUSH] = rush ? 0.10f + 0.10f * A : 0.0f;
        w[NA_WAIT] = 0.10f + 0.40f * P;
        w[NA_JUMP_IN] = 0.04f * A + 0.35f * shotty;                          /* over a fireball, not into it */
    } else if (dist > 62.0f) {                                               /* in the middle */
        w[NA_WALK_IN] = 0.30f + 0.45f * A - 0.35f * S - 0.10f * P + 0.3f * shotty;
        w[NA_POKE] = 0.12f + 0.50f * S;
        w[NA_RUSH] = rush ? 0.10f + 0.30f * A : 0.0f;
        w[NA_ZONE] = zone ? (0.05f + 0.25f * S) * (1.0f - 0.8f * shotty) : 0.0f;
        w[NA_WALK_OUT] = 0.05f + 0.30f * S;
        w[NA_BACKDASH] = can_dash ? 0.08f * S * skill : 0.0f;
        w[NA_JUMP_IN] = 0.03f + 0.12f * A + 0.25f * shotty;
        w[NA_WAIT] = 0.10f + 0.50f * P;
        w[NA_DASH_IN] = can_dash ? 0.10f * A * skill : 0.0f;
    } else {                                                                 /* up close */
        w[NA_POKE] = 0.25f + 0.15f * (1.0f - P);
        w[NA_LOW] = 0.12f + 0.25f * M;
        w[NA_OVERHEAD] = 0.06f + 0.20f * M + 0.15f * turtle;
        w[NA_THROW] = level >= 1 ? 0.04f + (0.28f * G * (skill > 0.3f ? skill : 0.3f)) + 0.30f * turtle : 0.0f;
        w[NA_WALK_OUT] = 0.08f + 0.20f * S + 0.10f * P;
        w[NA_WAIT] = 0.10f + 0.50f * P;
        w[NA_BACKDASH] = can_dash ? 0.06f * S * skill : 0.0f;
    }
    if (level < 0) { w[NA_WAIT] += 1.2f; w[NA_DASH_IN] = w[NA_BACKDASH] = 0.0f; }   /* a beginner hangs back and gives you openings */

    float total = 0.0f;
    for (int i = 0; i < NA_COUNT; i++) total += w[i];
    float roll = rnd(f) * total;
    int act = NA_WAIT;
    for (int i = 0; i < NA_COUNT; i++) { if (roll < w[i]) { act = i; break; } roll -= w[i]; }

    int mv = -1;
    F->ai_hold_x = 0; F->ai_hold_y = 0;
    bool purposeful = level >= 0 && rnd(f) < 0.5f + 0.5f * skill;            /* picks moves that can reach, instead of swinging at air */
    switch (act) {
    case NA_WALK_IN: F->ai_hold_x = toward; break;
    case NA_WALK_OUT: F->ai_hold_x = -toward; break;
    case NA_WAIT: break;
    case NA_POKE:
        mv = pick(f, s, GE_AI_POKE, false, 0, purposeful ? edge : -1.0f);
        if (mv < 0 && purposeful && edge > 12.0f) F->ai_hold_x = toward;     /* nothing reaches: step in first */
        break;
    case NA_LOW: mv = pick(f, s, GE_AI_LOW, false, 0, purposeful ? edge : -1.0f); break;
    case NA_OVERHEAD: mv = pick(f, s, GE_AI_OVERHEAD, false, 0, purposeful ? edge : -1.0f); break;
    case NA_THROW: mv = pick(f, s, GE_AI_THROW, false, 0, edge); break;
    case NA_ZONE: mv = pick(f, s, GE_AI_ZONE, false, 0, -1.0f); break;
    case NA_RUSH: mv = pick(f, s, GE_AI_RUSH, false, 0, -1.0f); break;
    case NA_DASH_IN: out->pressed |= GE_FB_DASH; out->x = toward; mind->dash_cd = 45; F->ai_cd = 14; return;
    case NA_BACKDASH: out->pressed |= GE_FB_DASH; out->x = -toward; mind->dash_cd = 45; F->ai_cd = 14; return;
    case NA_JUMP_IN: F->ai_hold_x = toward; F->ai_hold_y = -1; F->ai_cd = 1; break;
    }
    if (mv >= 0) {
        out->force_move = mv;
        if (F->def->moves[mv].stance == ST_CROUCH_) out->y = 1;
        F->ai_cd = 10 + (int)(rnd(f) * (float)(26 - level * 6));
    } else if (F->ai_cd == 0) {
        F->ai_cd = (act == NA_WAIT ? 8 : 12) + (int)(rnd(f) * 24.0f);
    }
    out->x = F->ai_hold_x;
    if (out->y == 0) out->y = F->ai_hold_y;
}
