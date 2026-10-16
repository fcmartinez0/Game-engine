/* Fighting-game mode. See ge_fight.h for the model and the move notation. */

#include "ge/ge_fight.h"
#include "ge/ge_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ST_ANY_GROUND = 0, ST_STAND = 1, ST_CROUCH = 2, ST_EXACT = 3, ST_AIR = 4 };

#define ARENA_TILES 36
#define FLOOR_ROW 13
#define INTRO_TICKS 100
#define ROUND_TICKS (99 * 60)
#define MOTION_WINDOW 18

typedef struct {
    int damage, hitstop, stun, bstun, chip, guard;
    float kb_x, kb_y;
    bool kd;
} Stats;

static GeEntity *ent(GeFight *f, int s) { return &f->world.ents[f->f[s].ent]; }
static const GeEntity *cent(const GeFight *f, int s) { return &f->world.ents[f->f[s].ent]; }
static const GeClip *clip_at(const GeFight *f, int s, int idx) { return &f->spr[s]->clips[idx]; }
static float approach(float v, float t, float step) { return v < t ? (v + step > t ? t : v + step) : (v - step < t ? t : v - step); }
static int hist_at(const GeFighter *F, int k) { return (F->hist_head - k + GE_FIGHT_HIST * 4) % GE_FIGHT_HIST; }

/* ---- move definitions -------------------------------------------------- */

int ge_fighter_def_finish(GeFighterDef *d, const GeSprite *spr) {
    for (int i = 0; i < d->nmoves; i++) {
        GeMove *m = &d->moves[i];
        m->clip_idx = ge_sprite_find_clip(spr, m->clip);
        if (m->clip_idx < 0 || !m->input) return -(1 + i);
        const char *p = m->input;
        bool air = false;
        if (p[0] == 'j' && p[1] == '.') { air = true; p += 2; }
        int digits[8], nd = 0;
        while (*p >= '1' && *p <= '9') {
            if (nd >= 8) return -(1 + i);
            digits[nd++] = *p++ - '0';
        }
        int nb = 0;
        m->buttons = 0;
        for (; *p; p++) {
            uint8_t b = *p == 'L' ? GE_FB_L : *p == 'M' ? GE_FB_M : *p == 'H' ? GE_FB_H : *p == 'S' ? GE_FB_S : 0;
            if (!b) return -(1 + i);
            m->buttons |= b;
            nb++;
        }
        if (nb == 0) return -(1 + i);
        m->nmotion = 0;
        m->stance = air ? ST_AIR : ST_ANY_GROUND;
        m->stance_dir = 5;
        if (nd == 1 && !air) {
            if (digits[0] == 5) m->stance = ST_STAND;
            else if (digits[0] == 2) m->stance = ST_CROUCH;
            else { m->stance = ST_EXACT; m->stance_dir = digits[0]; }
        } else if (nd >= 2) {
            for (int k = 0; k < nd; k++) m->motion[k] = digits[k];
            m->nmotion = nd;
        }
        m->is_throw = nb >= 2 && nd == 0 && !air && !(m->buttons & GE_FB_S);
    }
    return 0;
}

/* ---- input ------------------------------------------------------------- */

static int numpad(int rx, int y) {        /* rx: +1 = toward the facing direction */
    int row = y < 0 ? 2 : (y > 0 ? 0 : 1);
    return row * 3 + (rx + 1) + 1;
}

static void record_input(GeFighter *F, const GeEntity *e) {
    F->hist_head = (F->hist_head + 1) % GE_FIGHT_HIST;
    F->hist_dir[F->hist_head] = (int8_t)numpad(F->in.x * e->facing, F->in.y);
    F->hist_btn[F->hist_head] = F->in.pressed;
}

bool ge_fight_motion_done(const GeFighter *F, const int *motion, int n, int window) {
    int need = n - 1;
    for (int k = 0; k < window && k < GE_FIGHT_HIST; k++) {
        if (F->hist_dir[hist_at(F, k)] != motion[need]) continue;
        if (need == n - 1 && k > 8) return false;   /* the motion must end right before the press */
        if (--need < 0) return true;
    }
    return false;
}

static bool buttons_hit(const GeFighter *F, uint8_t b) {
    if (!(F->in.pressed & b)) return false;
    if ((b & (b - 1)) == 0) return true;          /* single button */
    uint8_t acc = 0;                              /* several: all pressed within 3 ticks */
    for (int k = 0; k < 3; k++) acc |= F->hist_btn[hist_at(F, k)];
    return (acc & b) == b;
}

static bool stance_ok(const GeMove *m, int dir, bool air) {
    if (air) return m->stance == ST_AIR;
    switch (m->stance) {
    case ST_AIR:    return false;
    case ST_STAND:  return dir >= 4 && dir <= 6;
    case ST_CROUCH: return dir <= 3;
    case ST_EXACT:  return dir == m->stance_dir;
    default:        return true;
    }
}

/* ---- rules helpers ----------------------------------------------------- */

static bool own_shot_alive(const GeFight *f, int s) {
    for (int i = 0; i < GE_MAX_SHOTS; i++)
        if (f->world.shots[i].active && f->world.shots[i].team == s) return true;
    return false;
}

bool ge_fight_can_start(const GeFight *f, int s, int mi) {
    const GeFighter *F = &f->f[s], *O = &f->f[1 - s];
    const GeMove *m = &F->def->moves[mi];
    if (F->meter < m->meter_cost) return false;
    if (m->shot && own_shot_alive(f, s)) return false;
    if (m->is_throw) {
        const GeEntity *e = cent(f, s), *oe = cent(f, 1 - s);
        if (!oe->on_ground || oe->invuln > 0) return false;
        if (O->state != GE_FS_STAND && O->state != GE_FS_CROUCH && O->state != GE_FS_DASH && O->state != GE_FS_MOVE) return false;
        if (fabsf(oe->pos.x - e->pos.x) > F->def->throw_range) return false;
    }
    return true;
}

GeRect ge_fighter_hurtbox(const GeFight *f, int s) {
    const GeFighter *F = &f->f[s];
    const GeEntity *e = cent(f, s);
    float w = F->def->body.body_w, h = F->def->body.body_h;
    bool crouch = F->state == GE_FS_CROUCH || (F->state == GE_FS_BLOCKSTUN && F->crouch_block) ||
                  (F->state == GE_FS_MOVE && F->def->moves[F->move].stance == ST_CROUCH);
    if (crouch) h = F->def->crouch_h;
    if (F->state == GE_FS_DOWN || F->state == GE_FS_KO) h = 10;
    return (GeRect){ e->pos.x - w * 0.5f, e->pos.y - h, w, h };
}

static void face_opponent(GeFight *f, int s) {
    GeEntity *e = ent(f, s), *oe = ent(f, 1 - s);
    if (fabsf(oe->pos.x - e->pos.x) > 1.0f) e->facing = oe->pos.x > e->pos.x ? 1 : -1;
}

static void gravity(const GeFighter *F, GeEntity *e) {
    e->vel.y += F->def->body.gravity;
    if (e->vel.y > F->def->body.max_fall) e->vel.y = F->def->body.max_fall;
}

static void play(GeEntity *e, int clip, bool restart) {
    if (clip >= 0) ge_anim_play(&e->anim, clip, restart);
}

static void start_move(GeFight *f, int s, int mi) {
    GeFighter *F = &f->f[s], *O = &f->f[1 - s];
    GeEntity *e = ent(f, s), *oe = ent(f, 1 - s);
    const GeMove *m = &F->def->moves[mi];
    const GeClip *c = clip_at(f, s, m->clip_idx);
    F->state = GE_FS_MOVE;
    F->move = mi;
    F->contact = F->hit_landed = F->shot_fired = false;
    e->hit_mask = 0;
    F->meter -= m->meter_cost;
    play(e, m->clip_idx, true);
    if (e->on_ground) e->vel.x = 0;
    if (c->lunge != 0.0f) e->vel.x = (float)e->facing * c->lunge;
    if (c->rise != 0.0f) { e->vel.y = -c->rise; e->on_ground = false; }
    if (c->inv > e->invuln) e->invuln = c->inv;
    if (m->level >= 4 || m->meter_cost >= GE_FIGHT_METER) {
        f->super_freeze = 36;
        f->super_side = s;
        for (int i = 0; i < 16; i++) {
            float a = (float)i * 0.3927f;
            ge_world_emit(&f->world, e->pos.x, e->pos.y - 16, cosf(a) * 2.5f, sinf(a) * 2.5f, 0, 30, GE_ARGB(255, 255, 230, 160), 3, GE_P_ADD);
        }
    }
    if (m->is_throw) {
        O->state = GE_FS_THROWN;
        O->timer = 26;
        oe->vel = (GeVec2){ 0, 0 };
        play(oe, O->def->clip_thrown, true);
        F->contact = true;
    }
}

static bool try_moves(GeFight *f, int s, bool air, int min_level) {
    GeFighter *F = &f->f[s];
    if (F->in.force_move >= 0 && F->in.force_move < F->def->nmoves) {
        const GeMove *m = &F->def->moves[F->in.force_move];
        if ((m->stance == ST_AIR) == air && m->level >= min_level && ge_fight_can_start(f, s, F->in.force_move)) {
            start_move(f, s, F->in.force_move);
            return true;
        }
    }
    int dir = F->hist_dir[F->hist_head];
    for (int i = 0; i < F->def->nmoves; i++) {
        const GeMove *m = &F->def->moves[i];
        if (m->level < min_level || !buttons_hit(F, m->buttons) || !stance_ok(m, dir, air)) continue;
        if (m->nmotion && !ge_fight_motion_done(F, m->motion, m->nmotion, MOTION_WINDOW)) continue;
        if (!ge_fight_can_start(f, s, i)) continue;
        start_move(f, s, i);
        return true;
    }
    return false;
}

static bool dash_input(const GeFighter *F, int d) {
    static int seq[2][3] = { { 6, 5, 6 }, { 4, 5, 4 } };
    const int *q = seq[d == 6 ? 0 : 1];
    return F->hist_dir[hist_at(F, 0)] == d && F->hist_dir[hist_at(F, 1)] != d && ge_fight_motion_done(F, q, 3, 14);
}

/* ---- per-fighter state machine ------------------------------------------ */

static void to_neutral(GeFighter *F, GeEntity *e) {
    F->state = (F->in.y > 0) ? GE_FS_CROUCH : GE_FS_STAND;
    F->timer = 0;
    (void)e;
}

static void fighter_step(GeFight *f, int s) {
    GeFighter *F = &f->f[s];
    GeEntity *e = ent(f, s);
    const GeFighterDef *d = F->def;
    GeWorld *w = &f->world;

    bool grounded_free = (F->state == GE_FS_STAND || F->state == GE_FS_CROUCH);
    if (grounded_free) face_opponent(f, s);
    record_input(F, e);                         /* always, so inputs buffer through hit-stop */
    if (e->hitstop > 0) { e->hitstop--; return; }
    ge_anim_update(&e->anim);
    if (e->invuln > 0) e->invuln--;
    if (e->flash > 0) e->flash--;

    switch (F->state) {
    case GE_FS_STAND:
    case GE_FS_CROUCH: {
        F->combo = 0; F->juggle = 0; F->air_hit = false;
        if (try_moves(f, s, false, 0)) break;
        int dir = F->hist_dir[F->hist_head];
        if (d->dash_ticks > 0 && (dash_input(F, 6) || dash_input(F, 4))) {
            int sgn = dir == 6 ? e->facing : -e->facing;
            F->state = GE_FS_DASH;
            F->timer = d->dash_ticks;
            e->state_timer = sgn;
            if (sgn != e->facing) e->invuln = 6;
            play(e, sgn == e->facing ? d->clip_dash : d->clip_backdash, true);
        } else if (F->in.y < 0) {
            e->vel.y = -d->body.jump_vel;
            e->vel.x = (float)F->in.x * d->jump_vx;
            e->on_ground = false;
            F->state = GE_FS_AIR;
            F->air_attacked = false;
            play(e, d->clip_jump, true);
            break;
        } else if (F->in.y > 0) {
            F->state = GE_FS_CROUCH;
            e->vel.x = 0;
            play(e, d->clip_crouch, false);
        } else {
            F->state = GE_FS_STAND;
            int rx = F->in.x * e->facing;
            e->vel.x = rx > 0 ? (float)e->facing * d->body.run_speed : rx < 0 ? -(float)e->facing * d->walk_back : 0.0f;
            play(e, rx > 0 ? d->clip_walk_f : rx < 0 ? d->clip_walk_b : d->clip_idle, false);
        }
        gravity(F, e);
        ge_entity_physics(w, F->ent);
        break;
    }
    case GE_FS_AIR:
        if (!F->air_attacked && try_moves(f, s, true, 0)) { F->air_attacked = true; break; }
        gravity(F, e);
        ge_entity_physics(w, F->ent);
        play(e, e->vel.y < 0 ? d->clip_jump : d->clip_fall, false);
        if (e->on_ground) { e->vel.x = 0; to_neutral(F, e); }
        break;
    case GE_FS_MOVE: {
        const GeMove *m = &d->moves[F->move];
        const GeClip *c = clip_at(f, s, m->clip_idx);
        if (m->shot && c->shoot >= 0 && !F->shot_fired && e->anim.frame >= c->shoot) {
            ge_world_shoot(w, s, F->ent, m->shot, e->pos.x + (float)e->facing * 14.0f, e->pos.y - d->body.body_h * 0.6f, e->facing);
            F->shot_fired = true;
        }
        if (F->contact && c->cancel_from >= 0 && e->anim.frame >= c->cancel_from && !e->anim.done &&
            try_moves(f, s, m->stance == ST_AIR && !e->on_ground, m->level + 1))
            break;
        if (e->on_ground) e->vel.x = approach(e->vel.x, 0.0f, 0.15f);
        gravity(F, e);
        ge_entity_physics(w, F->ent);
        if (m->stance == ST_AIR && e->on_ground) { e->vel.x = 0; to_neutral(F, e); break; } /* landing ends air attacks */
        if (e->anim.done) {
            if (e->on_ground) to_neutral(F, e);
            else { F->state = GE_FS_AIR; F->air_attacked = true; }
        }
        break;
    }
    case GE_FS_HITSTUN:
        F->timer--;
        gravity(F, e);   /* always: being pressed into the floor is what keeps on_ground true */
        if (!F->air_hit && e->on_ground) e->vel.x = approach(e->vel.x, 0.0f, 0.3f);
        ge_entity_physics(w, F->ent);
        if (F->air_hit) {
            if (e->on_ground && e->vel.y >= 0.0f) {
                F->state = GE_FS_DOWN;
                F->timer = 40;
                e->invuln = 48;
                e->vel.x = 0;
                play(e, d->clip_down, true);
                ge_world_shake(w, 2.0f);
                for (int i = 0; i < 6; i++)
                    ge_world_emit(w, e->pos.x + ge_rng_range(&w->rng, -10, 10), e->pos.y - 1, ge_rng_range(&w->rng, -1, 1), -0.5f, 0.03f,
                                  18, GE_ARGB(160, 210, 200, 180), 3, 0);
            }
        } else if (F->timer <= 0) {
            to_neutral(F, e);
        }
        break;
    case GE_FS_BLOCKSTUN:
        F->timer--;
        e->vel.x = approach(e->vel.x, 0.0f, 0.3f);
        gravity(F, e);
        ge_entity_physics(w, F->ent);
        if (F->timer <= 0) to_neutral(F, e);
        break;
    case GE_FS_DOWN:
        e->vel.x = approach(e->vel.x, 0.0f, 0.3f);
        gravity(F, e);
        ge_entity_physics(w, F->ent);
        if (--F->timer <= 0) { to_neutral(F, e); e->invuln = 6; F->combo = 0; }
        break;
    case GE_FS_THROWN: {
        GeFighter *T = &f->f[1 - s];
        GeEntity *te = ent(f, 1 - s);
        e->pos.x = ge_clampf(te->pos.x + (float)te->facing * 18.0f, d->body.body_w, (float)f->arena_w - d->body.body_w);
        e->pos.y = te->pos.y;
        if (F->timer > 18 && buttons_hit(F, GE_FB_L | GE_FB_M)) {    /* throw tech */
            to_neutral(F, e); to_neutral(T, te);
            e->vel.x = (float)te->facing * 3.0f;
            te->vel.x = -(float)te->facing * 3.0f;
            ge_world_spark(w, (e->pos.x + te->pos.x) * 0.5f, e->pos.y - 20, 1, 6, GE_ARGB(255, 180, 220, 255));
            break;
        }
        if (--F->timer <= 0) {
            e->hp -= T->def->throw_damage;
            if (e->hp < 0) e->hp = 0;
            F->state = GE_FS_HITSTUN;
            F->timer = 30;
            F->air_hit = true;
            F->combo++;
            e->vel.x = (float)te->facing * 3.5f;
            e->vel.y = -4.0f;
            e->on_ground = false;
            e->flash = 4;
            play(e, d->clip_hurt_air, true);
            ge_world_shake(w, 3.0f);
            T->meter = T->meter + 10 > GE_FIGHT_METER ? GE_FIGHT_METER : T->meter + 10;
        }
        break;
    }
    case GE_FS_DASH:
        e->vel.x = (float)e->state_timer * d->dash_speed;
        gravity(F, e);
        ge_entity_physics(w, F->ent);
        if (--F->timer <= 0) { e->vel.x = 0; to_neutral(F, e); }
        break;
    case GE_FS_KO:
        gravity(F, e);
        if (e->on_ground) e->vel.x = approach(e->vel.x, 0.0f, 0.2f);
        ge_entity_physics(w, F->ent);
        play(e, d->clip_ko, false);
        break;
    case GE_FS_WIN:
        gravity(F, e);
        e->vel.x = approach(e->vel.x, 0.0f, 0.3f);
        ge_entity_physics(w, F->ent);
        play(e, d->clip_win, false);
        break;
    }
    e->state = F->state == GE_FS_MOVE ? GE_ST_ATTACK : GE_ST_IDLE; /* lets the shared hitbox query work */
}

/* ---- hits -------------------------------------------------------------- */

static bool hittable(const GeFight *f, int s) {
    const GeFighter *D = &f->f[s];
    if (cent(f, s)->invuln > 0) return false;
    if (D->state == GE_FS_DOWN || D->state == GE_FS_THROWN || D->state == GE_FS_KO || D->state == GE_FS_WIN) return false;
    return !(D->air_hit && D->juggle >= GE_FIGHT_JUGGLE_MAX);  /* juggle limit: falls out of the combo */
}

static Stats clip_stats(const GeClip *c) {
    return (Stats){ c->damage, c->hitstop, c->stun, c->blockstun ? c->blockstun : c->stun * 2 / 3, c->chip, c->guard, c->kb_x, c->kb_y, c->knockdown };
}

static void connect(GeFight *f, int s, const Stats *st, int dir, bool melee, float x, float y) {
    GeFighter *A = &f->f[s], *D = &f->f[1 - s];
    GeEntity *ae = ent(f, s), *de = ent(f, 1 - s);
    GeWorld *w = &f->world;
    bool hold_back = D->in.x == dir;            /* away from the attack */
    bool low = D->in.y > 0;
    bool can_block = (D->state == GE_FS_STAND || D->state == GE_FS_CROUCH || D->state == GE_FS_BLOCKSTUN) && de->on_ground && hold_back;
    bool guard_ok = st->guard == GE_GUARD_MID || (st->guard == GE_GUARD_LOW && low) || (st->guard == GE_GUARD_HIGH && !low);

    if (can_block && guard_ok) {
        D->state = GE_FS_BLOCKSTUN;
        D->timer = st->bstun;
        D->crouch_block = low;
        de->hp -= st->chip;
        if (de->hp < 0) de->hp = 0;
        de->vel.x = (float)dir * st->kb_x * 0.8f;
        de->hitstop = st->hitstop / 2 + 1;
        if (melee) ae->hitstop = de->hitstop;
        play(de, low ? D->def->clip_block_low : D->def->clip_block, true);
        ge_world_spark(w, x, y, dir, 5, GE_ARGB(255, 150, 200, 255));
        D->meter = D->meter + 4 > GE_FIGHT_METER ? GE_FIGHT_METER : D->meter + 4;
        A->meter = A->meter + 3 > GE_FIGHT_METER ? GE_FIGHT_METER : A->meter + 3;
        if (melee) A->contact = true;
        if (de->hp <= 0) { D->state = GE_FS_HITSTUN; D->air_hit = true; de->vel.y = -3.0f; de->on_ground = false; }
        return;
    }

    float scale = 1.0f - 0.1f * (float)D->combo;   /* damage scaling keeps long combos in check */
    if (scale < 0.3f) scale = 0.3f;
    int dmg = (int)((float)st->damage * scale + 0.5f);
    if (dmg < 1) dmg = 1;
    de->hp -= dmg;
    if (de->hp < 0) de->hp = 0;
    D->combo++;
    bool launch = !de->on_ground || st->kb_y < 0.0f || st->kd || D->air_hit || de->hp == 0;
    D->state = GE_FS_HITSTUN;
    D->timer = st->stun;
    de->vel.x = (float)dir * st->kb_x;
    de->facing = -dir;
    if (launch) {
        D->air_hit = true;
        D->juggle++;
        de->vel.y = st->kb_y < 0.0f ? st->kb_y : -2.5f;
        de->on_ground = false;
        play(de, D->def->clip_hurt_air, true);
    } else {
        de->vel.y = 0.0f;
        play(de, D->def->clip_hurt, true);
    }
    if (ge_entity_wall_contact(w, de) == dir) ae->vel.x = -(float)dir * st->kb_x * 0.7f; /* cornered: attacker slides back */
    de->hitstop = st->hitstop;
    if (melee) ae->hitstop = st->hitstop;
    de->flash = 4;
    ge_world_spark(w, x, y, dir, 8 + st->damage / 2, GE_ARGB(255, 255, 220, 110));
    ge_world_shake(w, 1.0f + (float)st->damage * 0.15f);
    A->meter = A->meter + 6 + dmg / 2 > GE_FIGHT_METER ? GE_FIGHT_METER : A->meter + 6 + dmg / 2;
    D->meter = D->meter + 3 > GE_FIGHT_METER ? GE_FIGHT_METER : D->meter + 3;
    if (melee) A->contact = A->hit_landed = true;
    f->combo_show[s] = 70;
    f->combo_count[s] = D->combo;
}

static void centre(GeRect a, GeRect b, float *x, float *y) {
    float x0 = a.x > b.x ? a.x : b.x, x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    float y0 = a.y > b.y ? a.y : b.y, y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    *x = (x0 + x1) * 0.5f; *y = (y0 + y1) * 0.5f;
}

static void resolve_hits(GeFight *f) {
    /* gather first, then apply, so two attacks landing on the same tick trade */
    bool pending[2] = { false, false };
    GeRect hb[2], hurt[2];
    for (int s = 0; s < 2; s++) {
        GeEntity *ae = ent(f, s);
        hurt[1 - s] = ge_fighter_hurtbox(f, 1 - s);
        if (f->f[s].state == GE_FS_MOVE && !(ae->hit_mask & 1) && ge_entity_hitbox(ae, &hb[s]) && hittable(f, 1 - s) &&
            ge_rect_overlap(hb[s], hurt[1 - s]))
            pending[s] = true;
    }
    for (int s = 0; s < 2; s++) {
        if (!pending[s]) continue;
        GeEntity *ae = ent(f, s), *de = ent(f, 1 - s);
        ae->hit_mask |= 1;
        const GeMove *m = &f->f[s].def->moves[f->f[s].move];
        Stats st = clip_stats(clip_at(f, s, m->clip_idx));
        float x, y;
        centre(hb[s], hurt[1 - s], &x, &y);
        connect(f, s, &st, de->pos.x >= ae->pos.x ? 1 : -1, true, x, y);
    }

    GeWorld *w = &f->world;
    for (int i = 0; i < GE_MAX_SHOTS; i++) {
        GeShot *a = &w->shots[i];
        if (!a->active) continue;
        for (int j = i + 1; j < GE_MAX_SHOTS; j++) {    /* projectiles cancel each other out */
            GeShot *b = &w->shots[j];
            if (b->active && b->team != a->team && ge_rect_overlap(ge_shot_box(a), ge_shot_box(b))) {
                a->active = b->active = false;
                ge_world_spark(w, (a->pos.x + b->pos.x) * 0.5f, a->pos.y, 1, 10, GE_ARGB(255, 255, 255, 200));
                break;
            }
        }
        if (!a->active) continue;
        int ds = 1 - a->team;
        if (ds < 0 || ds > 1 || !hittable(f, ds)) continue;
        GeRect box = ge_shot_box(a), h = ge_fighter_hurtbox(f, ds);
        if (!ge_rect_overlap(box, h)) continue;
        const GeShotDef *sd = a->def;
        Stats st = { sd->damage, sd->hitstop, sd->stun, sd->blockstun ? sd->blockstun : sd->stun * 2 / 3, sd->chip, GE_GUARD_MID, sd->kb_x, sd->kb_y, false };
        float x, y;
        centre(box, h, &x, &y);
        a->active = false;
        connect(f, a->team, &st, a->vel.x >= 0 ? 1 : -1, false, x, y);
    }
}

/* fighters can't walk through each other, nor leave the screen */
static void push_apart(GeFight *f) {
    GeEntity *a = ent(f, 0), *b = ent(f, 1);
    float hwa = f->f[0].def->body.body_w * 0.5f, hwb = f->f[1].def->body.body_w * 0.5f;
    float lo_a = hwa, hi_a = (float)f->arena_w - hwa, lo_b = hwb, hi_b = (float)f->arena_w - hwb;
    bool vover = (a->pos.y - f->f[0].def->body.body_h < b->pos.y) && (b->pos.y - f->f[1].def->body.body_h < a->pos.y);
    bool thrown = f->f[0].state == GE_FS_THROWN || f->f[1].state == GE_FS_THROWN;
    float dx = b->pos.x - a->pos.x, minsep = hwa + hwb;
    if (vover && !thrown && fabsf(dx) < minsep) {
        int sgn = dx > 0.01f ? 1 : dx < -0.01f ? -1 : a->facing;
        float push = (minsep - fabsf(dx)) * 0.5f;
        a->pos.x -= (float)sgn * push;
        b->pos.x += (float)sgn * push;
        a->pos.x = ge_clampf(a->pos.x, lo_a, hi_a);
        b->pos.x = ge_clampf(b->pos.x, lo_b, hi_b);
        float gap = (b->pos.x - a->pos.x) * (float)sgn;     /* one was pinned to a wall: move the other */
        if (gap < minsep) {
            if (a->pos.x <= lo_a + 0.01f || a->pos.x >= hi_a - 0.01f) b->pos.x = ge_clampf(a->pos.x + (float)sgn * minsep, lo_b, hi_b);
            else a->pos.x = ge_clampf(b->pos.x - (float)sgn * minsep, lo_a, hi_a);
        }
    }
    float maxsep = (float)f->world.cam.vw - 40.0f;
    dx = b->pos.x - a->pos.x;
    if (fabsf(dx) > maxsep) {
        float fix = (fabsf(dx) - maxsep) * 0.5f * (dx > 0 ? 1.0f : -1.0f);
        a->pos.x += fix; b->pos.x -= fix;
    }
}

/* ---- rounds ------------------------------------------------------------ */

static void reset_round(GeFight *f) {
    for (int s = 0; s < 2; s++) {
        GeFighter *F = &f->f[s];
        GeEntity *e = ent(f, s);
        F->state = GE_FS_STAND;
        F->timer = 0;
        F->combo = F->juggle = 0;
        F->air_hit = F->contact = F->hit_landed = false;
        e->pos = (GeVec2){ (float)f->arena_w * 0.5f + (s == 0 ? -70.0f : 70.0f), (float)(FLOOR_ROW * GE_TILE) };
        e->vel = (GeVec2){ 0, 0 };
        e->facing = s == 0 ? 1 : -1;
        e->hp = (f->round == 1 && f->start_hp[s] > 0) ? f->start_hp[s] : F->def->max_hp;
        F->shown_hp = (float)e->hp;
        e->hitstop = e->invuln = e->flash = 0;
        e->on_ground = true;
        play(e, F->def->clip_idle, true);
    }
    for (int i = 0; i < GE_MAX_SHOTS; i++) f->world.shots[i].active = false;
    f->clock = ROUND_TICKS;
    f->phase = GE_FP_INTRO;
    f->phase_timer = 0;
    f->super_freeze = 0;
    f->world.cam.x = (float)f->arena_w * 0.5f - (float)f->world.cam.vw * 0.5f;
    f->world.cam.y = (float)((FLOOR_ROW + 1) * GE_TILE - f->world.cam.vh);
}

void ge_fight_init(GeFight *f, const GeFighterDef *d0, const GeSprite *s0, const GeFighterDef *d1, const GeSprite *s1,
                   const GeWorld *backdrop, uint32_t seed) {
    memset(f, 0, sizeof *f);
    GeWorld *w = &f->world;
    ge_world_init(w, 400, 240, seed);
    w->custom_combat = true;
    w->zoom = 2;                         /* fighting games frame the two fighters up close */
    w->cam.vw = 400 / 2;
    w->cam.vh = 240 / 2;
    if (backdrop) {
        for (int i = 0; i < backdrop->nlayers; i++) {
            GeParallaxLayer l = backdrop->layers[i];
            l.borrowed = true;
            ge_world_add_layer(w, l);
        }
        w->tileset = backdrop->tileset;
        w->tileset_borrowed = true;
        w->vignette = backdrop->vignette;
        w->low_detail = backdrop->low_detail;
    }
    ge_tilemap_create(&w->map, ARENA_TILES, 15);
    for (int x = 0; x < ARENA_TILES; x++) {
        w->map.type[FLOOR_ROW * ARENA_TILES + x] = w->map.type[(FLOOR_ROW + 1) * ARENA_TILES + x] = GE_TILE_SOLID;
        w->map.vis[FLOOR_ROW * ARENA_TILES + x] = 0;     /* tileset cell 0: floor top, 1: fill */
        w->map.vis[(FLOOR_ROW + 1) * ARENA_TILES + x] = 1;
    }
    f->arena_w = ARENA_TILES * GE_TILE;
    const GeFighterDef *defs[2] = { d0, d1 };
    const GeSprite *sprs[2] = { s0, s1 };
    for (int s = 0; s < 2; s++) {
        f->f[s].def = defs[s];
        f->spr[s] = sprs[s];
        f->f[s].ent = ge_world_spawn(w, &defs[s]->body, sprs[s], s, 0, 0, defs[s]->max_hp);
        ent(f, s)->external = true;
    }
    f->names[0] = "P1";
    f->names[1] = "P2";
    f->rounds_to_win = 2;
    f->round = 1;
    f->winner = -1;
    reset_round(f);
}

void ge_fight_free(GeFight *f) { ge_world_free(&f->world); }

static void end_round_decide(GeFight *f, bool by_time) {
    int hp0 = ent(f, 0)->hp, hp1 = ent(f, 1)->hp;
    if (by_time) {
        float p0 = (float)hp0 / (float)f->f[0].def->max_hp, p1 = (float)hp1 / (float)f->f[1].def->max_hp;
        f->round_winner = p0 > p1 ? 0 : p1 > p0 ? 1 : -1;
    } else {
        f->round_winner = (hp0 <= 0 && hp1 <= 0) ? -1 : hp0 <= 0 ? 1 : 0;
    }
    for (int s = 0; s < 2; s++)
        if (ent(f, s)->hp <= 0) {
            f->f[s].state = GE_FS_KO;
            if (ent(f, s)->on_ground) { ent(f, s)->vel.y = -3.0f; ent(f, s)->on_ground = false; }
        }
}

void ge_fight_update(GeFight *f, const GeFightInput in_[2]) {
    GeFightInput in[2] = { in_[0], in_[1] };
    f->phase_timer++;
    for (int s = 0; s < 2; s++) {
        if (f->combo_show[s] > 0) f->combo_show[s]--;
        GeFighter *F = &f->f[s];
        float hp = (float)ent(f, s)->hp;
        F->shown_hp = F->shown_hp > hp ? (F->shown_hp - 0.6f > hp ? F->shown_hp - 0.6f : hp) : hp;
        if (f->phase != GE_FP_FIGHT) in[s] = (GeFightInput){ 0, 0, 0, 0, -1 };
        F->in = in[s];
    }

    switch (f->phase) {
    case GE_FP_INTRO:
        if (f->phase_timer >= INTRO_TICKS) { f->phase = GE_FP_FIGHT; f->phase_timer = 0; }
        break;
    case GE_FP_FIGHT:
        if (ent(f, 0)->hp <= 0 || ent(f, 1)->hp <= 0) {
            f->phase = GE_FP_KO; f->phase_timer = 0;
            end_round_decide(f, false);
            ge_world_shake(&f->world, 5.0f);
        } else if (f->super_freeze == 0 && --f->clock <= 0) {
            f->phase = GE_FP_TIME; f->phase_timer = 0;
            end_round_decide(f, true);
        }
        break;
    case GE_FP_KO:
    case GE_FP_TIME:
        if (f->phase_timer >= 110) {
            f->phase = GE_FP_ROUND_END; f->phase_timer = 0;
            if (f->round_winner >= 0) {
                f->f[f->round_winner].wins++;
                f->f[f->round_winner].state = GE_FS_WIN;
            }
        }
        break;
    case GE_FP_ROUND_END:
        if (f->phase_timer >= 70) {
            int champ = f->f[0].wins >= f->rounds_to_win ? 0 : f->f[1].wins >= f->rounds_to_win ? 1 : -1;
            if (champ >= 0 || f->round >= 9) {
                f->winner = champ >= 0 ? champ : (f->f[0].wins > f->f[1].wins ? 0 : f->f[1].wins > f->f[0].wins ? 1 : -1);
                f->phase = GE_FP_MATCH_END; f->phase_timer = 0;
            } else {
                f->round++;
                reset_round(f);
            }
        }
        break;
    case GE_FP_MATCH_END:
        if (f->phase_timer >= 120) f->over = true;
        break;
    }

    if (f->super_freeze > 0) { /* super flash: everything but the effects stops */
        f->super_freeze--;
    } else if (!(f->phase == GE_FP_KO && f->phase_timer < 60 && (f->phase_timer & 1))) { /* slow-motion K.O. */
        fighter_step(f, 0);
        fighter_step(f, 1);
        push_apart(f);
        resolve_hits(f);
    }
    ge_world_update(&f->world);   /* shots, particles, camera shake */

    GeCamera *c = &f->world.cam;
    float mid = (ent(f, 0)->pos.x + ent(f, 1)->pos.x) * 0.5f;
    c->x += (mid - (float)c->vw * 0.5f - c->x) * 0.15f;
    c->x = ge_clampf(c->x, 0.0f, (float)(f->arena_w - c->vw));
    c->y = (float)((FLOOR_ROW + 1) * GE_TILE - c->vh);   /* floor one tile above the bottom edge */
}

/* ---- drawing ----------------------------------------------------------- */

static void bar(GeImage *fb, int x, int y, int w, int h, float frac, float trail, uint32_t col, bool mirrored) {
    ge_fill_rect(fb, x - 1, y - 1, w + 2, h + 2, GE_ARGB(220, 10, 8, 20), 0);
    ge_fill_rect(fb, x, y, w, h, GE_RGB(46, 18, 30), 0);
    int tw = (int)((float)w * ge_clampf(trail, 0, 1)), fw = (int)((float)w * ge_clampf(frac, 0, 1));
    if (mirrored) {
        ge_fill_rect(fb, x + w - tw, y, tw, h, GE_RGB(255, 240, 200), 0);
        ge_fill_rect(fb, x + w - fw, y, fw, h, col, 0);
    } else {
        ge_fill_rect(fb, x, y, tw, h, GE_RGB(255, 240, 200), 0);
        ge_fill_rect(fb, x, y, fw, h, col, 0);
    }
}

static void centered(GeImage *fb, const char *s, int y, int scale, uint32_t col) {
    ge_text_outlined(fb, (fb->w - ge_text_width(s, scale)) / 2, y, s, col, GE_RGB(20, 10, 30), scale);
}

void ge_fight_draw(GeFight *f, GeImage *fb) {
    GeWorld *w = &f->world;
    ge_world_draw(w, fb);
    int camx = (int)floorf(w->cam.x + w->cam.shake_x), camy = (int)floorf(w->cam.y + w->cam.shake_y);
    int z = w->zoom > 1 ? w->zoom : 1;
    if (f->super_freeze > 0) {
        ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(130, 10, 0, 30), 0);
        GeEntity *e = ent(f, f->super_side);
        ge_anim_draw_scaled(&e->anim, fb, ((int)e->pos.x - camx) * z, ((int)e->pos.y - camy) * z, e->facing < 0, 0, GE_WHITE, z);
    }

    const uint32_t hpcol[2] = { GE_RGB(240, 200, 70), GE_RGB(240, 200, 70) };
    char buf[48];
    for (int s = 0; s < 2; s++) {
        GeFighter *F = &f->f[s];
        GeEntity *e = ent(f, s);
        int x = s == 0 ? 10 : 222;
        float maxhp = (float)F->def->max_hp;
        bar(fb, x, 10, 168, 9, (float)e->hp / maxhp, F->shown_hp / maxhp, (float)e->hp < maxhp * 0.25f ? GE_RGB(240, 80, 70) : hpcol[s], s == 1);
        int nx = s == 0 ? x : x + 168 - ge_text_width(f->names[s], 1);
        ge_text_outlined(fb, nx, 23, f->names[s], GE_WHITE, GE_RGB(20, 10, 30), 1);
        for (int r = 0; r < f->rounds_to_win; r++) {   /* round pips */
            int px = s == 0 ? x + 168 - 8 - r * 9 : x + r * 9;
            ge_fill_rect(fb, px, 23, 6, 6, GE_ARGB(220, 10, 8, 20), 0);
            if (F->wins > r) ge_fill_rect(fb, px + 1, 24, 4, 4, GE_RGB(255, 220, 90), 0);
        }
        /* meter */
        int mx = s == 0 ? 10 : 290;
        bool full = F->meter >= GE_FIGHT_METER;
        bar(fb, mx, 226, 100, 5, (float)F->meter / GE_FIGHT_METER, 0, full && (w->tick & 8) ? GE_RGB(255, 255, 255) : GE_RGB(120, 190, 255), s == 1);
        if (full) ge_text_outlined(fb, s == 0 ? mx : mx + 100 - ge_text_width("SUPER", 1), 216, "SUPER", GE_RGB(160, 220, 255), GE_RGB(20, 10, 30), 1);
        if (f->combo_show[s] > 0 && f->combo_count[s] >= 2) {
            snprintf(buf, sizeof buf, "%d HITS", f->combo_count[s]);
            int cx = s == 0 ? 12 : fb->w - 12 - ge_text_width(buf, 2);
            ge_text_outlined(fb, cx, 70, buf, GE_RGB(255, 210, 90), GE_RGB(60, 10, 20), 2);
        }
    }
    snprintf(buf, sizeof buf, "%d", (f->clock + 59) / 60);
    centered(fb, buf, 8, 2, f->clock < 10 * 60 ? GE_RGB(255, 120, 100) : GE_WHITE);

    const char *msg = NULL;
    int scale = 3;
    switch (f->phase) {
    case GE_FP_INTRO:
        if (f->phase_timer < 60) {
            bool final_round = f->f[0].wins == f->rounds_to_win - 1 && f->f[1].wins == f->rounds_to_win - 1 && f->rounds_to_win > 1;
            if (final_round) msg = "FINAL ROUND";
            else { snprintf(buf, sizeof buf, "ROUND %d", f->round); msg = buf; }
        } else msg = "FIGHT!";
        break;
    case GE_FP_KO:   msg = (ent(f, 0)->hp <= 0 && ent(f, 1)->hp <= 0) ? "DOUBLE K.O." : "K.O."; scale = 4; break;
    case GE_FP_TIME: msg = "TIME"; break;
    case GE_FP_ROUND_END:
    case GE_FP_MATCH_END: {
        int wnr = f->phase == GE_FP_MATCH_END ? f->winner : f->round_winner;
        if (wnr < 0) msg = "DRAW";
        else { snprintf(buf, sizeof buf, "%s WINS", f->names[wnr]); msg = buf; }
        scale = 2;
        break;
    }
    default: break;
    }
    if (msg) centered(fb, msg, 96, scale, f->phase == GE_FP_KO ? GE_RGB(255, 90, 80) : GE_RGB(255, 236, 170));
}

/* ---- CPU --------------------------------------------------------------- */

static int pick(GeFight *f, int s, int role, bool air, int min_level) {
    const GeFighterDef *d = f->f[s].def;
    int cand[GE_FIGHT_MAX_MOVES], n = 0;
    for (int i = 0; i < d->nmoves; i++) {
        const GeMove *m = &d->moves[i];
        bool role_ok = role < 0 ? (m->ai != GE_AI_THROW && m->ai != GE_AI_ZONE && m->ai != GE_AI_NONE) : m->ai == role;
        if (role_ok && (m->stance == ST_AIR) == air && m->level >= min_level && ge_fight_can_start(f, s, i)) cand[n++] = i;
    }
    return n ? cand[ge_rng_next(&f->world.rng) % (uint32_t)n] : -1;
}

void ge_fight_cpu(GeFight *f, int s, int level, GeFightInput *out) {
    memset(out, 0, sizeof *out);
    out->force_move = -1;
    if (f->phase != GE_FP_FIGHT) return;
    GeFighter *F = &f->f[s], *O = &f->f[1 - s];
    GeEntity *e = ent(f, s), *oe = ent(f, 1 - s);
    GeRng *r = &f->world.rng;
    level = ge_clampi(level, 0, 3);
    float p = 0.3f + 0.2f * (float)level;      /* 0.3 .. 0.9 */
    float dx = oe->pos.x - e->pos.x, dist = fabsf(dx);
    int toward = dx >= 0 ? 1 : -1;
    if (F->ai_cd > 0) F->ai_cd--;

    /* defence: block what's coming (and read lows/overheads at higher levels) */
    bool threat = O->state == GE_FS_MOVE && dist < 95.0f;
    for (int i = 0; i < GE_MAX_SHOTS; i++) {
        const GeShot *sh = &f->world.shots[i];
        if (sh->active && sh->team != s && fabsf(sh->pos.x - e->pos.x) < 110.0f && (sh->vel.x > 0) == (sh->pos.x < e->pos.x)) threat = true;
    }
    bool free_ground = F->state == GE_FS_STAND || F->state == GE_FS_CROUCH || F->state == GE_FS_BLOCKSTUN;
    if (threat && free_ground) {
        if (F->ai_block_for == 0) F->ai_block_for = ge_rng_float(r) < p ? 1 : -1;
        if (F->ai_block_for == 1) {
            out->x = -toward;
            int guard = O->state == GE_FS_MOVE ? clip_at(f, 1 - s, O->def->moves[O->move].clip_idx)->guard : GE_GUARD_MID;
            out->y = (guard == GE_GUARD_LOW && level >= 1) ? 1 : 0;
            return;
        }
    } else if (!threat) {
        F->ai_block_for = 0;
    }

    /* anti-air */
    if (O->state == GE_FS_AIR && dist < 110.0f && F->state <= GE_FS_CROUCH && ge_rng_float(r) < p * 0.25f) {
        int mv = pick(f, s, GE_AI_ANTIAIR, false, 0);
        if (mv >= 0) { out->force_move = mv; return; }
    }
    /* combos: on a confirmed hit, cancel into something stronger */
    if (F->state == GE_FS_MOVE && F->hit_landed) {
        const GeMove *cur = &F->def->moves[F->move];
        if (ge_rng_float(r) < p + 0.1f) {
            int mv = F->meter >= GE_FIGHT_METER && ge_rng_float(r) < 0.5f ? pick(f, s, GE_AI_SUPER, false, cur->level + 1) : -1;
            if (mv < 0) mv = pick(f, s, -1, cur->stance == ST_AIR, cur->level + 1);
            out->force_move = mv;
        }
        return;
    }
    /* jump-ins: swing on the way down */
    if (F->state == GE_FS_AIR && !F->air_attacked && dist < 60.0f && e->vel.y > -1.0f) {
        out->force_move = pick(f, s, GE_AI_AIR, true, 0);
        return;
    }
    if (F->state != GE_FS_STAND && F->state != GE_FS_CROUCH) return;

    if (F->ai_cd > 0) { out->x = F->ai_hold_x; out->y = F->ai_hold_y; return; }

    /* neutral game */
    float roll = ge_rng_float(r);
    int mv = -1;
    F->ai_hold_x = 0; F->ai_hold_y = 0;
    if (F->meter >= GE_FIGHT_METER && dist < 90.0f && roll < p * 0.4f) mv = pick(f, s, GE_AI_SUPER, false, 0);
    else if (dist > 170.0f) {
        if (roll < 0.25f + p * 0.3f) mv = pick(f, s, GE_AI_ZONE, false, 0);
        else if (roll < 0.75f) F->ai_hold_x = toward;
        else mv = pick(f, s, GE_AI_RUSH, false, 0);
    } else if (dist > 62.0f) {
        if (roll < 0.45f) F->ai_hold_x = toward;
        else if (roll < 0.45f + 0.15f * p) { F->ai_hold_x = toward; F->ai_hold_y = -1; F->ai_cd = 1; }
        else if (roll < 0.72f) mv = pick(f, s, GE_AI_RUSH, false, 0);
        else if (roll < 0.85f) mv = pick(f, s, GE_AI_ZONE, false, 0);
        else F->ai_hold_x = -toward;
    } else {
        if (level >= 1 && roll < 0.14f) mv = pick(f, s, GE_AI_THROW, false, 0);
        else if (roll < 0.5f) mv = pick(f, s, GE_AI_POKE, false, 0);
        else if (roll < 0.68f) mv = pick(f, s, GE_AI_LOW, false, 0);
        else if (roll < 0.78f) mv = pick(f, s, GE_AI_OVERHEAD, false, 0);
        else F->ai_hold_x = -toward;
    }
    if (mv >= 0) {
        out->force_move = mv;
        if (F->def->moves[mv].stance == ST_CROUCH) out->y = 1;
        F->ai_cd = 10 + (int)(ge_rng_float(r) * (float)(26 - level * 6));
    } else if (F->ai_cd == 0) {
        F->ai_cd = 12 + (int)(ge_rng_float(r) * 24.0f);
    }
    out->x = F->ai_hold_x;
    if (out->y == 0) out->y = F->ai_hold_y;
}
