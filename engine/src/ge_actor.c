/* Platformer character controller + combat. One code path drives the player
 * and every enemy: callers only fill a GeIntent. */

#include "ge/ge_world.h"

#include <string.h>

/* ---- tile collision ---------------------------------------------------- */

static float approach(float v, float target, float step) {
    if (v < target) return v + step > target ? target : v + step;
    return v - step < target ? target : v - step;
}

static void move_x(const GeTilemap *m, GeEntity *e) {
    float hw = e->def->body_w * 0.5f;
    e->pos.x += e->vel.x;
    int ty0 = (int)floorf((e->pos.y - e->def->body_h) / GE_TILE);
    int ty1 = (int)floorf((e->pos.y - 0.01f) / GE_TILE);
    if (e->vel.x > 0) {
        int tx = (int)floorf((e->pos.x + hw - 0.01f) / GE_TILE);
        for (int ty = ty0; ty <= ty1; ty++)
            if (ge_tilemap_get(m, tx, ty) == GE_TILE_SOLID) { e->pos.x = tx * GE_TILE - hw; e->vel.x = 0; break; }
    } else if (e->vel.x < 0) {
        int tx = (int)floorf((e->pos.x - hw + 0.01f) / GE_TILE);
        for (int ty = ty0; ty <= ty1; ty++)
            if (ge_tilemap_get(m, tx, ty) == GE_TILE_SOLID) { e->pos.x = (tx + 1) * GE_TILE + hw; e->vel.x = 0; break; }
    }
}

static void move_y(const GeTilemap *m, GeEntity *e) {
    float hw = e->def->body_w * 0.5f;
    float prev_y = e->pos.y;
    e->pos.y += e->vel.y;
    e->on_ground = false;
    int tx0 = (int)floorf((e->pos.x - hw + 0.01f) / GE_TILE);
    int tx1 = (int)floorf((e->pos.x + hw - 0.01f) / GE_TILE);
    if (e->vel.y > 0) {
        int ty = (int)floorf(e->pos.y / GE_TILE);
        for (int tx = tx0; tx <= tx1; tx++) {
            int t = ge_tilemap_get(m, tx, ty);
            /* one-way platforms only catch you if your feet were above them last tick */
            bool land = t == GE_TILE_SOLID ||
                        (t == GE_TILE_ONEWAY && e->drop_timer <= 0 && prev_y <= ty * GE_TILE + 0.01f);
            if (land) { e->pos.y = (float)(ty * GE_TILE); e->vel.y = 0; e->on_ground = true; break; }
        }
    } else if (e->vel.y < 0) {
        int ty = (int)floorf((e->pos.y - e->def->body_h) / GE_TILE);
        for (int tx = tx0; tx <= tx1; tx++)
            if (ge_tilemap_get(m, tx, ty) == GE_TILE_SOLID) {
                e->pos.y = (float)((ty + 1) * GE_TILE) + e->def->body_h;
                e->vel.y = 0;
                break;
            }
    }
}

/* returns false if the entity fell out of the world and was removed */
static bool physics(GeWorld *w, int idx) {
    GeEntity *e = &w->ents[idx];
    e->vel.x = ge_clampf(e->vel.x, -12.0f, 12.0f);
    e->vel.y = ge_clampf(e->vel.y, -12.0f, 12.0f);
    move_x(&w->map, e);
    move_y(&w->map, e);
    if (e->pos.y > (float)(w->map.h * GE_TILE) + 48.0f) {
        e->active = false;
        if (w->on_event) w->on_event(w->user, GE_EV_FELL, idx, idx);
        return false;
    }
    return true;
}

static void gravity(GeEntity *e) {
    e->vel.y += e->def->gravity;
    if (e->vel.y > e->def->max_fall) e->vel.y = e->def->max_fall;
}

static void play(GeEntity *e, int clip, bool restart) {
    if (clip >= 0) ge_anim_play(&e->anim, clip, restart);
}

/* ---- state helpers ----------------------------------------------------- */

static void start_attack(GeEntity *e, int clip, bool chained) {
    const GeClip *c = &e->anim.spr->clips[clip];
    e->state = GE_ST_ATTACK;
    e->hit_mask = 0;
    e->buffered_attack = false;
    e->combo = chained ? e->combo + 1 : 1;
    play(e, clip, true);
    if (c->lunge != 0.0f) e->vel.x = (float)e->facing * c->lunge;
}

static void end_state(GeEntity *e) {
    e->state = e->on_ground ? GE_ST_IDLE : GE_ST_AIR;
    e->state_timer = 0;
    e->combo = 0;
}

static void actor_free(GeWorld *w, int idx, const GeIntent *in) {
    GeEntity *e = &w->ents[idx];
    const GeActorDef *d = e->def;

    if (in->lane_shift && e->lane_cd <= 0) {
        int nl = ge_clampi(e->lane + in->lane_shift, GE_LANE_BACK, GE_LANE_FRONT);
        if (nl != e->lane) { e->lane = nl; e->lane_cd = 14; }
    }

    float accel = e->on_ground ? d->accel : d->air_accel;
    if (in->move_x != 0.0f) {
        e->vel.x = approach(e->vel.x, in->move_x * d->run_speed, accel);
        e->facing = in->move_x > 0 ? 1 : -1;
    } else {
        e->vel.x = approach(e->vel.x, 0.0f, e->on_ground ? d->friction : d->air_accel * 0.5f);
    }

    bool dropped = false;
    if (in->drop_held && in->jump_pressed && e->on_ground &&
        ge_tilemap_get(&w->map, (int)floorf(e->pos.x / GE_TILE), (int)floorf(e->pos.y / GE_TILE)) == GE_TILE_ONEWAY) {
        e->drop_timer = 10; e->jump_buf = 0; e->coyote = 0; dropped = true;
    }

    if (e->on_ground) e->coyote = d->coyote_ticks; else if (e->coyote > 0) e->coyote--;
    if (in->jump_pressed && !dropped) e->jump_buf = d->jump_buffer_ticks;
    else if (e->jump_buf > 0) e->jump_buf--;
    if (e->jump_buf > 0 && e->coyote > 0) {
        e->vel.y = -d->jump_vel;
        e->jump_buf = e->coyote = 0;
        e->on_ground = false;
        for (int i = 0; i < 4; i++) /* take-off dust */
            ge_world_emit(w, e->pos.x + ge_rng_range(&w->rng, -4, 4), e->pos.y - 1,
                          ge_rng_range(&w->rng, -0.6f, 0.6f), -0.3f, 0.02f, 14, GE_ARGB(150, 210, 200, 180), 2, 0);
    }
    if (!in->jump_held && e->vel.y < -d->jump_cut_vel) e->vel.y = -d->jump_cut_vel; /* variable jump height */

    if (in->attack_pressed && d->clip_attack >= 0) {
        start_attack(e, d->clip_attack, false);
    } else if (in->dash_pressed && e->dash_cd <= 0 && d->dash_ticks > 0) {
        e->state = GE_ST_DASH;
        e->state_timer = d->dash_ticks;
        play(e, d->clip_dash >= 0 ? d->clip_dash : d->clip_run, true);
    }

    if (e->state == GE_ST_ATTACK || e->state == GE_ST_DASH) return;

    gravity(e);
    if (!physics(w, idx)) return;
    if (e->on_ground) {
        e->state = fabsf(e->vel.x) > 0.3f ? GE_ST_RUN : GE_ST_IDLE;
        play(e, e->state == GE_ST_RUN ? d->clip_run : d->clip_idle, false);
    } else {
        e->state = GE_ST_AIR;
        play(e, e->vel.y < 0 ? d->clip_jump : d->clip_fall, false);
    }
}

static void actor_attack(GeWorld *w, int idx, const GeIntent *in) {
    GeEntity *e = &w->ents[idx];
    const GeActorDef *d = e->def;
    const GeClip *c = &e->anim.spr->clips[e->anim.clip];

    if (in->attack_pressed) e->buffered_attack = true;
    if (!e->on_ground) gravity(e);
    else e->vel.x = approach(e->vel.x, 0.0f, d->friction * 0.5f);
    if (!physics(w, idx)) return;

    if (c->next >= 0 && e->buffered_attack && (e->anim.frame >= c->cancel_from || e->anim.done) && c->cancel_from >= 0) {
        if (in->move_x != 0.0f) e->facing = in->move_x > 0 ? 1 : -1;
        start_attack(e, c->next, true);
    } else if (e->anim.done) {
        end_state(e);
    }
}

static void actor_dash(GeWorld *w, int idx) {
    GeEntity *e = &w->ents[idx];
    const GeActorDef *d = e->def;
    e->vel.x = (float)e->facing * d->dash_speed;
    e->vel.y = 0.0f;
    if (e->invuln < 2) e->invuln = 2;
    if ((e->state_timer & 1) == 0)
        ge_world_emit(w, e->pos.x, e->pos.y - d->body_h * 0.5f, 0, 0, 0, 10, GE_ARGB(120, 140, 200, 255), 4, GE_P_ADD);
    if (!physics(w, idx)) return;
    if (--e->state_timer <= 0) {
        e->dash_cd = d->dash_cooldown;
        e->vel.x = (float)e->facing * d->run_speed;
        end_state(e);
    }
}

void ge_actor_update(GeWorld *w, int idx) {
    GeEntity *e = &w->ents[idx];
    const GeActorDef *d = e->def;
    const GeIntent in = w->intents[idx];

    if (e->hitstop > 0) { e->hitstop--; return; } /* hit freeze */
    ge_anim_update(&e->anim);
    if (e->invuln > 0) e->invuln--;
    if (e->flash > 0) e->flash--;
    if (e->dash_cd > 0) e->dash_cd--;
    if (e->drop_timer > 0) e->drop_timer--;
    if (e->lane_cd > 0) e->lane_cd--;

    switch (e->state) {
    case GE_ST_DEAD:
        gravity(e);
        if (e->on_ground) e->vel.x = approach(e->vel.x, 0.0f, 0.2f);
        if (!physics(w, idx)) return;
        if (--e->dead_timer <= 0) {
            e->active = false;
            for (int i = 0; i < 12; i++)
                ge_world_emit(w, e->pos.x, e->pos.y - 6, ge_rng_range(&w->rng, -1.2f, 1.2f), ge_rng_range(&w->rng, -1.6f, -0.2f),
                              0.05f, 24, GE_ARGB(200, 220, 210, 200), 3, 0);
        }
        break;
    case GE_ST_HURT:
        gravity(e);
        if (e->on_ground) e->vel.x = approach(e->vel.x, 0.0f, 0.15f);
        if (!physics(w, idx)) return;
        if (--e->state_timer <= 0) end_state(e);
        break;
    case GE_ST_ATTACK: actor_attack(w, idx, &in); break;
    case GE_ST_DASH:   actor_dash(w, idx); break;
    default:           actor_free(w, idx, &in); break;
    }
    (void)d;
}

/* ---- combat ------------------------------------------------------------ */

static void apply_hit(GeWorld *w, int ai, int vi, GeRect hit, GeRect hurt) {
    GeEntity *a = &w->ents[ai], *v = &w->ents[vi];
    const GeClip *c = &a->anim.spr->clips[a->anim.clip];
    int dir = v->pos.x >= a->pos.x ? 1 : -1;

    v->hp -= c->damage;
    v->vel.x = (float)dir * c->kb_x;
    v->vel.y = c->kb_y;
    v->on_ground = false;
    v->facing = -dir;
    v->hitstop = a->hitstop = c->hitstop;
    v->flash = 5;
    v->invuln = v->def->invuln_ticks;
    v->buffered_attack = false;
    v->combo = 0;
    v->state = GE_ST_HURT;
    v->state_timer = c->stun;
    play(v, v->def->clip_hurt, true);

    if (v->hp <= 0) {
        v->hp = 0;
        v->state = GE_ST_DEAD;
        v->dead_timer = v->def->dead_ticks;
        v->vel.x = (float)dir * (c->kb_x + 1.5f);
        v->vel.y = c->kb_y < -3.0f ? c->kb_y : -3.0f;
        play(v, v->def->clip_dead, true);
        if (w->on_event) w->on_event(w->user, GE_EV_KILL, ai, vi);
    }

    float x0 = hit.x > hurt.x ? hit.x : hurt.x, x1 = hit.x + hit.w < hurt.x + hurt.w ? hit.x + hit.w : hurt.x + hurt.w;
    float y0 = hit.y > hurt.y ? hit.y : hurt.y, y1 = hit.y + hit.h < hurt.y + hurt.h ? hit.y + hit.h : hurt.y + hurt.h;
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    ge_world_emit(w, cx - 4, cy - 4, 0, 0, 0, 4, GE_ARGB(255, 255, 255, 230), 8, GE_P_ADD); /* impact flash */
    int n = 8 + c->damage / 2;
    for (int i = 0; i < n; i++) {
        float ang = ge_rng_range(&w->rng, -2.6f, 2.6f) + (dir > 0 ? 0.0f : 3.14159f);
        float sp = ge_rng_range(&w->rng, 1.0f, 3.2f);
        ge_world_emit(w, cx, cy, cosf(ang) * sp, sinf(ang) * sp - 0.6f, 0.12f,
                      10 + (int)ge_rng_range(&w->rng, 0, 8), GE_ARGB(255, 255, 220, 110), 2, GE_P_ADD);
    }
    ge_world_shake(w, 1.5f + (float)c->damage * 0.25f);
    if (w->on_event) w->on_event(w->user, GE_EV_HIT, ai, vi);
}

void ge_combat_resolve(GeWorld *w) {
    for (int ai = 0; ai < GE_MAX_ENTS; ai++) {
        const GeEntity *a = &w->ents[ai];
        GeRect hit;
        if (!a->active || !ge_entity_hitbox(a, &hit)) continue;
        for (int vi = 0; vi < GE_MAX_ENTS; vi++) {
            GeEntity *v = &w->ents[vi];
            if (vi == ai || !v->active || v->team == a->team || v->state == GE_ST_DEAD) continue;
            if (v->lane != a->lane) continue;              /* different depth lane: whiffs */
            if (w->ents[ai].hit_mask & (1ull << vi)) continue; /* one hit per swing */
            GeRect hurt = ge_entity_hurtbox(v);
            if (!ge_rect_overlap(hit, hurt) || v->invuln > 0) continue;
            w->ents[ai].hit_mask |= 1ull << vi;
            apply_hit(w, ai, vi, hit, hurt);
        }
    }
}
