/* The Paragon AI. See ge_paragon.h and docs/PARAGON.md. */

#include "ge/ge_paragon.h"

#include <math.h>
#include <string.h>

enum { ST_CROUCH_ = 2, ST_AIR_ = 4 };    /* mirrors ge_fight.c's stance codes */

/* What it can choose to do in neutral, scored against what the player might do. */
enum { A_WAIT, A_POKE, A_LOW, A_OVERHEAD, A_THROW, A_ZONE, A_RUSH, A_WALK_IN, A_WALK_OUT, A_JUMP_IN, A_COUNT };

/* Payoff of my action (row) if the player does this (column), -1 .. +1. Columns follow GE_INT_*:
 *   POKE LOW OVHD ANTI ZONE RUSH COMBO SUPER AIR THROW | WALK_F WALK_B STAND CROUCH JUMP DASH BLOCK BLOCK_LOW
 * Rows are the Paragon's "style": change a number and it plays differently. */
static const float PAY[A_COUNT][GE_INT_COUNT] = {
    /* WAIT     */ { 0.2f, 0.2f, 0.2f, 0.1f, 0.1f, 0.3f, 0.2f,-0.2f, 0.8f,-0.4f,  0.2f,-0.1f,-0.2f,-0.2f, 0.8f, 0.3f,-0.1f,-0.1f },
    /* POKE     */ { 0.0f,-0.1f, 0.3f, 0.1f, 0.2f, 0.4f, 0.0f,-0.2f,-0.4f, 0.5f,  0.6f, 0.1f, 0.3f, 0.2f,-0.3f, 0.5f, 0.1f, 0.1f },
    /* LOW      */ {-0.2f,-0.1f, 0.4f, 0.2f, 0.2f, 0.0f,-0.1f,-0.3f,-0.5f, 0.3f,  0.3f, 0.0f, 0.4f, 0.0f,-0.4f, 0.2f, 0.7f,-0.2f },
    /* OVERHEAD */ {-0.4f, 0.0f,-0.1f, 0.0f, 0.2f,-0.3f,-0.2f,-0.3f,-0.6f, 0.3f,  0.1f, 0.1f, 0.3f, 0.5f,-0.5f,-0.2f, 0.0f, 0.8f },
    /* THROW    */ {-0.3f,-0.2f,-0.2f, 0.0f, 0.2f, 0.0f,-0.2f,-0.3f,-0.5f, 0.0f,  0.3f, 0.0f, 0.4f, 0.4f,-0.3f, 0.2f, 0.8f, 0.7f },
    /* ZONE     */ {-0.4f,-0.3f,-0.2f,-0.2f, 0.2f,-0.3f,-0.4f,-0.3f, 0.3f,-0.2f,  0.0f, 0.3f, 0.2f, 0.1f,-0.4f, 0.0f, 0.1f, 0.1f },
    /* RUSH     */ {-0.4f,-0.3f,-0.2f,-0.2f, 0.5f,-0.3f,-0.3f,-0.3f,-0.3f, 0.0f, -0.2f, 0.5f, 0.3f, 0.2f,-0.2f,-0.2f, 0.2f, 0.1f },
    /* WALK_IN  */ {-0.3f,-0.2f,-0.1f,-0.1f, 0.3f,-0.2f,-0.2f,-0.3f,-0.1f, 0.0f, -0.1f, 0.4f, 0.2f, 0.2f,-0.2f,-0.1f, 0.1f, 0.1f },
    /* WALK_OUT */ { 0.3f, 0.3f, 0.3f, 0.1f, 0.0f, 0.3f, 0.3f, 0.0f, 0.3f, 0.1f,  0.1f,-0.2f,-0.1f,-0.1f, 0.0f, 0.2f,-0.1f,-0.1f },
    /* JUMP_IN  */ {-0.3f,-0.3f,-0.2f,-0.6f, 0.6f,-0.1f,-0.2f,-0.3f,-0.2f, 0.4f, -0.1f, 0.2f, 0.0f, 0.1f, 0.0f,-0.2f, 0.2f, 0.3f },
};

static int ge_sign_dir(const GeFight *f, int s) { return f->world.ents[f->f[s].ent].facing >= 0 ? 1 : -1; }
static GeEntity *ent(GeFight *f, int s) { return &f->world.ents[f->f[s].ent]; }
static const GeClip *clip_of(const GeFight *f, int s, int mi) { return &f->spr[s]->clips[f->f[s].def->moves[mi].clip_idx]; }

static int move_damage(const GeFight *f, int s, int mi) {
    const GeMove *m = &f->f[s].def->moves[mi];
    return m->is_throw ? f->f[s].def->throw_damage : clip_of(f, s, mi)->damage;
}

/* Is this fighter past the last active frame of its move (so a counter-attack is safe)? */
static bool recovering(const GeFight *f, int s) {
    const GeFighter *F = &f->f[s];
    if (F->state != GE_FS_MOVE) return false;
    const GeMove *m = &F->def->moves[F->move];
    if (m->is_throw) return false;
    const GeClip *c = clip_of(f, s, F->move);
    const GeEntity *e = &f->world.ents[F->ent];
    int last = -1;
    for (int i = 0; i < c->nframes; i++) if (c->frames[i].has_hit) last = i;
    if (m->shot && c->shoot > last) last = c->shoot;
    return e->anim.done || e->anim.frame > last;
}

/* The best move of the allowed kinds that can land `edge` pixels away within `max_startup` ticks.
 * fast = prefer the quickest, otherwise the hardest hitting. */
static int best_move(const GeFight *f, int s, int role, bool air, int max_startup, float edge, int min_level, bool fast) {
    const GeFighterDef *d = f->f[s].def;
    int best = -1;
    float best_score = -1e9f;
    for (int i = 0; i < d->nmoves; i++) {
        const GeMove *m = &d->moves[i];
        bool role_ok = role >= 0 ? m->ai == role : (m->ai != GE_AI_NONE && m->ai != GE_AI_ZONE);
        if (!role_ok || (m->stance == ST_AIR_) != air || m->level < min_level) continue;
        if (!ge_fight_can_start(f, s, i)) continue;
        int su = ge_fight_move_startup(f, s, i);
        if (su > max_startup) continue;
        if (!m->is_throw && !m->shot && edge > (float)ge_fight_move_reach(f, s, i) + 2.0f) continue;
        float score = fast ? (float)(100 - su) + (float)move_damage(f, s, i) * 0.1f : (float)move_damage(f, s, i) - (float)su * 0.05f;
        if (score > best_score) { best_score = score; best = i; }
    }
    return best;
}

void ge_paragon_init(GeParagon *p, GePlayerModel *m) {
    memset(p, 0, sizeof *p);
    p->model = m;
    p->decay_rate = 0.0f;
    p->block_low = true;
    ge_paragon_set_learn(p, 0, 0);
}

void ge_paragon_set_learn(GeParagon *p, int learn, int react_bonus) {
    p->learn = ge_clampi(learn, 0, 100);
    p->react = ge_clampi(12 - (7 * p->learn) / 100 - react_bonus, 3, GE_PARAGON_VIEW - 2);
    p->explore = 0.25f - 0.20f * (float)p->learn / 100.0f;
}

/* Raise the guard for `ticks`: low attacks need a crouch, overheads need a stand, mids don't care (keep the current pose). */
static void give_block(GeParagon *p, int guard, int ticks) {
    p->block_for = ticks;
    if (guard == GE_GUARD_LOW) p->block_low = true;
    else if (guard == GE_GUARD_HIGH) p->block_low = false;
}

/* Furthest an opponent's ordinary attacks can reach from where they stand (projectiles and throws excluded). */
static float threat_reach(const GeFight *f, int victim) {
    const GeFighter *O = &f->f[1 - victim];
    float r = 0;
    for (int i = 0; i < O->def->nmoves; i++) {
        const GeMove *m = &O->def->moves[i];
        if (m->is_throw || m->shot || m->ai == GE_AI_NONE || m->stance == ST_AIR_) continue;
        float reach = (float)ge_fight_move_reach(f, 1 - victim, i);
        if (m->ai == GE_AI_RUSH || m->ai == GE_AI_ANTIAIR) reach += 14.0f;
        if (reach > r) r = reach;
    }
    return r;
}

static void think(GeParagon *p, GeFight *f, int s, GeFightInput *out) {
    memset(out, 0, sizeof *out);
    out->force_move = -1;
    if (f->phase != GE_FP_FIGHT) return;
    GeFighter *F = &f->f[s], *O = &f->f[1 - s];
    GeEntity *e = ent(f, s), *oe = ent(f, 1 - s);
    GeRng *rng = &f->world.rng;
    float dx = oe->pos.x - e->pos.x, dist = fabsf(dx);
    int toward = dx >= 0 ? 1 : -1;
    float body = (F->def->body.body_w + O->def->body.body_w) * 0.5f;
    float edge = dist - body;

    /* what the opponent looks like to a pair of human eyes: record now, read `react` ticks back */
    p->head = (p->head + 1) % GE_PARAGON_VIEW;
    p->view[p->head] = (GeParagonSnap){ O->state, O->move, dist, O->state == GE_FS_AIR || (O->state == GE_FS_MOVE && !oe->on_ground) };
    const GeParagonSnap *v = &p->view[(p->head - p->react + GE_PARAGON_VIEW * 2) % GE_PARAGON_VIEW];
    if (p->cd > 0) p->cd--;
    if (p->hold_for > 0) p->hold_for--;

    /* while fighting, forget old habits so a changed style is noticed within a few exchanges */
    if (p->model && p->decay_rate > 0.0f && ++p->decay_timer >= 60) {
        p->decay_timer = 0;
        ge_model_decay(p->model, 1.0f - p->decay_rate);
    }

    /* ---- being thrown: break it (a quick read of the grab) ---- */
    if (F->state == GE_FS_THROWN) {
        if (!p->tech_decided) { p->tech_decided = true; p->tech_yes = p->learn >= 30 || ge_rng_float(rng) < 0.5f; }
        if (p->tech_yes) out->pressed = out->held = GE_FB_L | GE_FB_M;
        return;
    }
    p->tech_decided = false;

    /* ---- not free to act: keep the guard up for when it is ---- */
    if (F->state == GE_FS_BLOCKSTUN) { out->x = -toward; out->y = F->crouch_block ? 1 : 0; return; }
    if (F->state == GE_FS_HITSTUN || F->state == GE_FS_DOWN) { out->x = -toward; return; }
    if (F->state == GE_FS_KO || F->state == GE_FS_WIN || F->state == GE_FS_DASH) return;

    /* ---- my attack connected: extend the combo ---- */
    if (F->state == GE_FS_MOVE) {
        if (F->hit_landed) {
            const GeMove *cur = &F->def->moves[F->move];
            int mv = -1;
            if (F->meter >= GE_FIGHT_METER) mv = best_move(f, s, GE_AI_SUPER, false, 12, edge, cur->level + 1, false);
            if (mv < 0) mv = best_move(f, s, -1, cur->stance == ST_AIR_, 14, edge, cur->level + 1, false);
            out->force_move = mv;
        }
        return;
    }

    /* ---- in the air: swing on the way down ---- */
    if (F->state == GE_FS_AIR) {
        if (!F->air_attacked && edge < 40.0f && e->vel.y > -1.0f) out->force_move = best_move(f, s, GE_AI_AIR, true, 14, edge, 0, false);
        else out->x = dist > 30.0f ? toward : 0;
        return;
    }

    /* From here I am standing or crouching, free to act. */

    /* ---- punish: the opponent is stuck, so spend exactly the time it is stuck for ---- */
    int stuck = 0;
    bool meaty = false;
    if (O->state == GE_FS_MOVE && recovering(f, 1 - s)) stuck = ge_fight_frames_until_free(f, 1 - s);
    else if (O->state == GE_FS_HITSTUN) stuck = ge_fight_frames_until_free(f, 1 - s);
    else if (O->state == GE_FS_DOWN) { stuck = ge_fight_frames_until_free(f, 1 - s); meaty = true; }
    if (stuck > 0) {
        bool ground_hit = !meaty && O->state != GE_FS_HITSTUN;
        int mv = -1;
        if (meaty) {
            /* land the first active frame just as they get up (they are invulnerable for 6 ticks after rising) */
            mv = best_move(f, s, GE_AI_POKE, false, 40, edge, 0, true);
            if (mv >= 0 && stuck <= ge_fight_move_startup(f, s, mv) - 5) { out->force_move = mv; p->stat_punishes++; return; }
            if (edge > 30.0f) out->x = toward;
            return;
        }
        int window = stuck - 2;
        if (F->meter >= GE_FIGHT_METER) mv = best_move(f, s, GE_AI_SUPER, false, window, edge, 0, false);
        if (mv < 0) mv = best_move(f, s, -1, false, window, edge, 0, false);
        if (mv >= 0) {
            out->force_move = mv;
            if (F->def->moves[mv].stance == ST_CROUCH_) out->y = 1;
            p->stat_punishes++;
            p->block_for = 0;
            return;
        }
        if (ground_hit || O->state == GE_FS_HITSTUN) {
            if (edge > 20.0f && stuck > 10) { out->x = toward; return; }   /* walk into range while they're stuck */
        }
    }

    /* ---- defend: react to what I could see `react` ticks ago ---- */
    if (v->state == GE_FS_MOVE && !recovering(f, 1 - s)) {
        const GeMove *om = &O->def->moves[v->move];
        if (!om->is_throw && !om->shot) {
            float reach = (float)ge_fight_move_reach(f, 1 - s, v->move) + 22.0f;   /* lunges carry further than the box shows */
            if (v->dist - body <= reach) give_block(p, clip_of(f, 1 - s, v->move)->guard, 9);
        }
    }
    for (int i = 0; i < GE_MAX_SHOTS; i++) {
        const GeShot *sh = &f->world.shots[i];
        if (sh->active && sh->team != s && fabsf(sh->pos.x - e->pos.x) < 120.0f && (sh->vel.x > 0) == (sh->pos.x < e->pos.x)) {
            float gap = fabsf(sh->pos.x - e->pos.x);
            if (p->learn >= 20 && gap > 55.0f && gap < 85.0f && F->state == GE_FS_STAND) {   /* hop it: it passes underneath */
                out->x = toward; out->y = -1;
                return;
            }
            give_block(p, GE_GUARD_MID, 8);
        }
    }

    /* ---- anti-air: they left the ground, so meet them ---- */
    if (v->airborne && v->dist < 120.0f) {
        int mv = best_move(f, s, GE_AI_ANTIAIR, false, 12, edge, 0, true);
        if (mv >= 0) { out->force_move = mv; p->block_for = 0; return; }
        give_block(p, GE_GUARD_MID, 6);
    }

    if (p->block_for > 0) {
        p->block_for--;
        out->x = -toward;
        out->y = p->block_low ? 1 : 0;
        return;
    }
    if (p->hold_for > 0) { out->x = p->hold_x; out->y = p->hold_y; return; }

    /* In their reach and they are free to swing: keep the guard up by default. Crouching beats the
     * most common attacks (lows and mids); the slow overhead is seen coming and answered by standing. */
    bool danger = edge <= threat_reach(f, s) + 16.0f && O->state != GE_FS_HITSTUN && O->state != GE_FS_DOWN && !recovering(f, 1 - s);
    if (p->cd > 0) {
        if (danger) { out->x = -toward; out->y = p->block_low ? 1 : 0; }
        return;
    }

    /* ---- neutral: ask the model what they will do, pick what beats it ---- */
    float prob[GE_INT_COUNT];
    for (int i = 0; i < GE_INT_COUNT; i++) prob[i] = 1.0f / (float)GE_INT_COUNT;
    int bucket = ge_dist_bucket(dist);
    if (p->model) {
        const GeModelWatch *w = &p->model->w;
        int last = (w->last_intent >= 0 && w->tick - w->last_tick < 90) ? w->last_intent : -1;
        ge_model_predict(p->model, bucket, last, prob);
    }
    /* how well they guard each height is worth knowing too */
    float low_rate = p->model ? ge_model_block_rate(p->model, GE_GUARD_LOW) : -1.0f;
    float high_rate = p->model ? ge_model_block_rate(p->model, GE_GUARD_HIGH) : -1.0f;

    int mv_for[A_COUNT];
    bool ok[A_COUNT];
    for (int a = 0; a < A_COUNT; a++) { mv_for[a] = -1; ok[a] = false; }
    ok[A_WAIT] = true;
    mv_for[A_POKE] = best_move(f, s, GE_AI_POKE, false, 12, edge, 0, true);
    mv_for[A_LOW] = best_move(f, s, GE_AI_LOW, false, 12, edge, 0, true);
    mv_for[A_OVERHEAD] = best_move(f, s, GE_AI_OVERHEAD, false, 30, edge, 0, true);
    mv_for[A_THROW] = (edge < F->def->throw_range) ? best_move(f, s, GE_AI_THROW, false, 99, 0, 0, true) : -1;
    mv_for[A_ZONE] = (dist > 90.0f) ? best_move(f, s, GE_AI_ZONE, false, 99, 0, 0, true) : -1;
    mv_for[A_RUSH] = (dist > 60.0f) ? best_move(f, s, GE_AI_RUSH, false, 99, 0, 0, true) : -1;
    for (int a = A_POKE; a <= A_RUSH; a++) ok[a] = mv_for[a] >= 0;
    ok[A_WALK_IN] = edge > 24.0f;
    ok[A_WALK_OUT] = dist < 220.0f;
    ok[A_JUMP_IN] = dist > 70.0f && dist < 180.0f;

    float ev[A_COUNT];
    int best = A_WAIT;
    for (int a = 0; a < A_COUNT; a++) {
        ev[a] = -9.0f;
        if (!ok[a]) continue;
        float sum = 0;
        for (int i = 0; i < GE_INT_COUNT; i++) sum += prob[i] * PAY[a][i];
        if (a == A_LOW && low_rate >= 0.0f) sum += 0.6f * (0.5f - low_rate);          /* they rarely block low: go low */
        if (a == A_OVERHEAD && high_rate >= 0.0f) sum += 0.6f * (0.5f - high_rate);
        if (a == A_WAIT && danger) sum += 0.45f;           /* inside their reach, patience is worth something */
        ev[a] = sum;
        if (ev[a] > ev[best]) best = a;
    }
    if (ge_rng_float(rng) < p->explore) {                       /* now and then, try something else */
        int n = 0, cand[A_COUNT];
        for (int a = 0; a < A_COUNT; a++) if (ok[a] && a != A_WAIT) cand[n++] = a;
        if (n) best = cand[ge_rng_next(rng) % (uint32_t)n];
    }
    p->stat_reads++;

    switch (best) {
    case A_WAIT:     out->x = -toward; out->y = (danger && p->block_low) ? 1 : 0; p->cd = 4 + (int)(ge_rng_float(rng) * 6.0f); break;
    case A_WALK_IN:  p->hold_x = toward;  p->hold_y = 0; p->hold_for = 8 + (int)(ge_rng_float(rng) * 8.0f); out->x = toward; break;
    case A_WALK_OUT: p->hold_x = -toward; p->hold_y = 0; p->hold_for = 8 + (int)(ge_rng_float(rng) * 8.0f); out->x = -toward; break;
    case A_JUMP_IN:  out->x = toward; out->y = -1; p->cd = 8; break;
    default:
        out->force_move = mv_for[best];
        if (F->def->moves[mv_for[best]].stance == ST_CROUCH_) out->y = 1;
        p->cd = 6 + (int)(ge_rng_float(rng) * 6.0f);
        break;
    }
}

/* A double-tap (6, release, 6) is a dash, and a dash can't block. The brain never taps by accident:
 * if a walk would complete one, it stays neutral until the first tap has aged out of the input window. */
static int numpad(int x, int y) { return y < 0 ? 8 + x : y > 0 ? 2 + x : 5 + x; }

void ge_paragon_think(GeParagon *p, GeFight *f, int s, GeFightInput *out) {
    think(p, f, s, out);
    if (f->phase != GE_FP_FIGHT) return;
    int face = ge_sign_dir(f, s);
    int d = numpad(out->x * face, out->y);
    if (out->y == 0 && out->x != 0 && p->odir[0] != d) {
        int need = 2;                           /* looking back: neutral, then the same direction */
        for (int k = 0; k < 14; k++) {
            int h = p->odir[k];
            if (need == 2 && h == 5) need = 1;
            else if (need == 1 && h == d) { out->x = 0; d = 5; break; }
        }
    }
    memmove(p->odir + 1, p->odir, sizeof p->odir - sizeof p->odir[0]);
    p->odir[0] = d;
}
