/* Platformer character controller + action combat. One code path drives the
 * player and every enemy: callers only fill a GeIntent. */

#include "ge/ge_world.h"

#include <string.h>

#define SPIKE_BIT (1ull << 63)

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

bool ge_entity_physics(GeWorld *w, int idx) {
    GeEntity *e = &w->ents[idx];
    e->vel.x = ge_clampf(e->vel.x, -12.0f, 12.0f);
    e->vel.y = ge_clampf(e->vel.y, -12.0f, 12.0f);
    move_x(&w->map, e);
    move_y(&w->map, e);
    if (e->pos.y > (float)(w->map.h * GE_TILE) + 48.0f) {
        e->active = false;
        ge_world_event(w, GE_EV_FELL, idx, idx);
        return false;
    }
    if (e->state != GE_ST_DEAD && e->invuln <= 0) {
        GeRect body = ge_entity_hurtbox(e);
        body.x += 2; body.w -= 4; body.h -= 2; /* a little forgiving */
        if (ge_world_touches_tile(w, body, GE_TILE_SPIKE)) ge_world_event(w, GE_EV_HAZARD, idx, -1);
    }
    return true;
}

int ge_entity_wall_contact(const GeWorld *w, const GeEntity *e) {
    float hw = e->def->body_w * 0.5f;
    float ys[3] = { e->pos.y - e->def->body_h + 3.0f, e->pos.y - e->def->body_h * 0.5f, e->pos.y - 3.0f };
    for (int side = -1; side <= 1; side += 2) {
        int tx = (int)floorf((e->pos.x + side * (hw + 0.5f)) / GE_TILE);
        for (int i = 0; i < 3; i++)
            if (ge_tilemap_get(&w->map, tx, (int)floorf(ys[i] / GE_TILE)) == GE_TILE_SOLID) return side;
    }
    return 0;
}

static void gravity(GeEntity *e) {
    if (e->def->flying) return;
    e->vel.y += e->def->gravity;
    if (e->vel.y > e->def->max_fall) e->vel.y = e->def->max_fall;
}

static void play(GeEntity *e, int clip, bool restart) {
    if (clip >= 0) ge_anim_play(&e->anim, clip, restart);
}

static void dust(GeWorld *w, float x, float y, int n) {
    for (int i = 0; i < n; i++)
        ge_world_emit(w, x + ge_rng_range(&w->rng, -4, 4), y - 1, ge_rng_range(&w->rng, -0.6f, 0.6f), -0.3f, 0.02f, 14,
                      GE_ARGB(150, 210, 200, 180), 2, 0);
}

/* ---- state helpers ----------------------------------------------------- */

static void start_attack(GeEntity *e, int clip, bool chained) {
    const GeClip *c = &e->anim.spr->clips[clip];
    e->state = GE_ST_ATTACK;
    e->hit_mask = 0;
    e->buffered_attack = false;
    e->shot_fired = false;
    e->combo = chained ? e->combo + 1 : 1;
    play(e, clip, true);
    if (c->lunge != 0.0f) e->vel.x = (float)e->facing * c->lunge;
    if (c->rise != 0.0f) { e->vel.y = -c->rise; e->on_ground = false; }
    if (c->inv > e->invuln) e->invuln = c->inv;
}

static void end_state(GeEntity *e) {
    e->state = e->on_ground ? GE_ST_IDLE : GE_ST_AIR;
    e->state_timer = 0;
    e->combo = 0;
}

static int pick_attack(const GeEntity *e, const GeIntent *in) {
    const GeActorDef *d = e->def;
    if (in->move_y < -0.5f && d->clip_attack_up >= 0) return d->clip_attack_up;
    if (in->move_y > 0.5f && !e->on_ground && !d->flying && d->clip_attack_down >= 0) return d->clip_attack_down;
    if (!e->on_ground && !d->flying && d->clip_attack_air >= 0) return d->clip_attack_air;
    return d->clip_attack;
}

static void actor_free(GeWorld *w, int idx, const GeIntent *in) {
    GeEntity *e = &w->ents[idx];
    const GeActorDef *d = e->def;
    const bool fly = d->flying;

    /* steering */
    float mx = e->wall_lock > 0 ? 0.0f : in->move_x;
    float accel = (e->on_ground || fly) ? d->accel : d->air_accel;
    if (mx != 0.0f) {
        e->vel.x = approach(e->vel.x, mx * d->run_speed, accel);
        e->facing = mx > 0 ? 1 : -1;
    } else if (e->wall_lock <= 0) {
        e->vel.x = approach(e->vel.x, 0.0f, (e->on_ground || fly) ? d->friction : d->air_accel * 0.5f);
    }
    if (fly) e->vel.y = approach(e->vel.y, in->move_y * d->run_speed, accel);

    /* walls: slide when pushing into one, reset air moves */
    if (e->on_ground) { e->air_jumps_left = d->air_jumps; e->air_dashed = false; }
    int wall = (!e->on_ground && !fly && d->wall_slide_speed > 0.0f) ? ge_entity_wall_contact(w, e) : 0;
    e->wall_dir = wall;
    bool sliding = wall != 0 && in->move_x * (float)wall > 0.0f && e->vel.y > -0.5f && e->wall_lock <= 0;
    if (sliding) { e->air_jumps_left = d->air_jumps; e->air_dashed = false; e->facing = -wall; }

    /* down + jump drops through a one-way platform */
    bool dropped = false;
    if (in->move_y > 0.5f && in->jump_pressed && e->on_ground &&
        ge_tilemap_get(&w->map, (int)floorf(e->pos.x / GE_TILE), (int)floorf(e->pos.y / GE_TILE)) == GE_TILE_ONEWAY) {
        e->drop_timer = 10; e->jump_buf = 0; e->coyote = 0; dropped = true;
    }

    /* jumps: ground (with coyote time + buffer), wall, air */
    if (e->on_ground) e->coyote = d->coyote_ticks; else if (e->coyote > 0) e->coyote--;
    if (in->jump_pressed && !dropped) e->jump_buf = d->jump_buffer_ticks;
    else if (e->jump_buf > 0) e->jump_buf--;
    if (!fly && e->jump_buf > 0) {
        if (e->coyote > 0) {
            e->vel.y = -d->jump_vel;
            e->jump_buf = e->coyote = 0;
            e->on_ground = false;
            dust(w, e->pos.x, e->pos.y, 4);
        } else if (wall != 0 && in->jump_pressed) {
            e->vel.x = -(float)wall * d->wall_jump_vx;
            e->vel.y = -d->wall_jump_vy;
            e->facing = -wall;
            e->wall_lock = d->wall_jump_lock;
            e->jump_buf = 0;
            dust(w, e->pos.x + wall * d->body_w * 0.5f, e->pos.y - d->body_h * 0.5f, 5);
        } else if (in->jump_pressed && e->air_jumps_left > 0) {
            e->vel.y = -(d->air_jump_vel > 0 ? d->air_jump_vel : d->jump_vel);
            e->air_jumps_left--;
            e->jump_buf = 0;
            for (int i = 0; i < 8; i++) { /* a ring of silk below the feet */
                float a = (float)i * 0.785f;
                ge_world_emit(w, e->pos.x, e->pos.y, cosf(a) * 1.4f, sinf(a) * 0.5f + 0.4f, 0, 16, GE_ARGB(200, 230, 230, 255), 2, GE_P_ADD);
            }
        }
    }
    if (!in->jump_held && e->vel.y < -d->jump_cut_vel) e->vel.y = -d->jump_cut_vel; /* variable jump height */

    /* actions */
    int atk = in->attack_pressed ? pick_attack(e, in) : -1;
    if (atk >= 0) {
        start_attack(e, atk, false);
    } else if (in->skill_pressed && d->clip_skill >= 0 && e->meter >= d->skill_cost) {
        e->meter -= d->skill_cost;
        start_attack(e, d->clip_skill, false);
    } else if (in->tool_pressed && d->clip_tool >= 0 && e->ammo > 0) {
        e->ammo--;
        start_attack(e, d->clip_tool, false);
    } else if (in->dash_pressed && e->dash_cd <= 0 && d->dash_ticks > 0 &&
               (e->on_ground || fly || (d->air_dash && !e->air_dashed))) {
        if (!e->on_ground && !fly) e->air_dashed = true;
        e->state = GE_ST_DASH;
        e->state_timer = d->dash_ticks;
        play(e, d->clip_dash >= 0 ? d->clip_dash : d->clip_run, true);
    } else if (in->channel_held && e->on_ground && d->clip_channel >= 0 && e->meter >= d->channel_cost) {
        e->state = GE_ST_CHANNEL;
        e->state_timer = 0;
        play(e, d->clip_channel, true);
    }
    if (e->state == GE_ST_ATTACK || e->state == GE_ST_DASH || e->state == GE_ST_CHANNEL) return;

    gravity(e);
    if (sliding && e->vel.y > d->wall_slide_speed) e->vel.y = d->wall_slide_speed;
    if (!ge_entity_physics(w, idx)) return;

    if (fly) {
        e->state = (fabsf(e->vel.x) + fabsf(e->vel.y) > 0.3f) ? GE_ST_RUN : GE_ST_IDLE;
        play(e, e->state == GE_ST_RUN ? d->clip_run : d->clip_idle, false);
    } else if (e->on_ground) {
        e->state = fabsf(e->vel.x) > 0.3f ? GE_ST_RUN : GE_ST_IDLE;
        play(e, e->state == GE_ST_RUN ? d->clip_run : d->clip_idle, false);
    } else if (sliding && ge_entity_wall_contact(w, e) == wall) {
        e->state = GE_ST_WALL;
        play(e, d->clip_wall >= 0 ? d->clip_wall : d->clip_fall, false);
        if ((w->tick & 3) == 0) dust(w, e->pos.x + wall * d->body_w * 0.5f, e->pos.y - d->body_h, 1);
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
    if (c->shoot >= 0 && !e->shot_fired && e->anim.frame >= c->shoot && d->shot) {
        ge_world_shoot(w, e->team, idx, d->shot, e->pos.x + (float)e->facing * (d->body_w * 0.5f + 4.0f),
                       e->pos.y - d->body_h * 0.6f, e->facing);
        e->shot_fired = true;
    }
    if (d->flying) { e->vel.x = approach(e->vel.x, 0, d->friction); e->vel.y = approach(e->vel.y, 0, d->friction); }
    else if (e->on_ground) e->vel.x = approach(e->vel.x, 0.0f, d->friction * 0.5f);
    gravity(e); /* always (no-op for flyers): pressing into the floor is what keeps on_ground true */
    if (!ge_entity_physics(w, idx)) return;

    if (c->next >= 0 && e->buffered_attack && c->cancel_from >= 0 && (e->anim.frame >= c->cancel_from || e->anim.done)) {
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
    if (!ge_entity_physics(w, idx)) return;
    if (--e->state_timer <= 0) {
        e->dash_cd = d->dash_cooldown;
        e->vel.x = (float)e->facing * d->run_speed;
        end_state(e);
    }
}

static void actor_channel(GeWorld *w, int idx, const GeIntent *in) {
    GeEntity *e = &w->ents[idx];
    const GeActorDef *d = e->def;
    e->vel.x = approach(e->vel.x, 0.0f, d->friction);
    gravity(e);
    if (!ge_entity_physics(w, idx)) return;
    if (!in->channel_held || !e->on_ground) { end_state(e); return; }
    if ((e->state_timer & 3) == 0)
        ge_world_emit(w, e->pos.x + ge_rng_range(&w->rng, -8, 8), e->pos.y - 4, 0, -0.6f, 0, 26, GE_ARGB(220, 240, 230, 255), 2, GE_P_ADD);
    if (++e->state_timer >= d->channel_ticks) {
        e->meter -= d->channel_cost;
        ge_world_event(w, GE_EV_CHANNEL, idx, -1);
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
    if (e->wall_lock > 0) e->wall_lock--;
    if (d->ammo_max > 0 && e->ammo < d->ammo_max && ++e->ammo_timer >= d->ammo_recharge) { e->ammo++; e->ammo_timer = 0; }

    switch (e->state) {
    case GE_ST_DEAD:
        gravity(e);
        if (e->on_ground || d->flying) e->vel.x = approach(e->vel.x, 0.0f, 0.2f);
        if (d->flying) e->vel.y += 0.25f; /* flyers drop out of the sky */
        if (!ge_entity_physics(w, idx)) return;
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
        if (d->flying) { e->vel.x = approach(e->vel.x, 0, 0.1f); e->vel.y = approach(e->vel.y, 0, 0.1f); }
        if (!ge_entity_physics(w, idx)) return;
        if (--e->state_timer <= 0) end_state(e);
        break;
    case GE_ST_ATTACK:  actor_attack(w, idx, &in); break;
    case GE_ST_DASH:    actor_dash(w, idx); break;
    case GE_ST_CHANNEL: actor_channel(w, idx, &in); break;
    default:            actor_free(w, idx, &in); break;
    }
}

/* ---- hits -------------------------------------------------------------- */

static void pogo(GeWorld *w, int ai, float speed) {
    GeEntity *a = &w->ents[ai];
    a->vel.y = -speed;
    a->on_ground = false;
    a->air_jumps_left = a->def->air_jumps;
    a->air_dashed = false;
    ge_world_event(w, GE_EV_POGO, ai, -1);
}

void ge_world_apply_hit(GeWorld *w, int ai, int vi, const GeHit *h) {
    GeEntity *v = &w->ents[vi];

    v->hp -= h->damage;
    if (v->def->armored && v->hp > 0) {   /* super armour: flinch-free */
        v->flash = 5;
        v->hitstop = h->hitstop / 2;
        if (ai >= 0) {
            GeEntity *a = &w->ents[ai];
            a->hitstop = h->hitstop;
            if (h->pogo > 0.0f) pogo(w, ai, h->pogo);
            if (h->recoil > 0.0f) a->vel.x = -(float)h->dir * h->recoil * 1.5f;
            a->meter = a->meter + a->def->meter_per_hit > a->def->meter_max ? a->def->meter_max : a->meter + a->def->meter_per_hit;
        }
        ge_world_spark(w, h->x, h->y, h->dir, 6 + h->damage / 2, GE_ARGB(255, 255, 220, 110));
        ge_world_shake(w, 1.0f + (float)h->damage * 0.15f);
        ge_world_event(w, GE_EV_HIT, ai, vi);
        return;
    }
    v->vel.x = (float)h->dir * h->kb_x;
    v->vel.y = h->kb_y;
    if (h->kb_y < 0) v->on_ground = false;
    v->facing = -h->dir;
    v->hitstop = h->hitstop;
    v->flash = 5;
    v->invuln = v->def->invuln_ticks;
    v->buffered_attack = false;
    v->combo = 0;
    v->state = GE_ST_HURT;
    v->state_timer = h->stun;
    play(v, v->def->clip_hurt, true);

    if (ai >= 0) {
        GeEntity *a = &w->ents[ai];
        a->hitstop = h->hitstop;
        if (h->pogo > 0.0f) pogo(w, ai, h->pogo);
        if (h->recoil > 0.0f) a->vel.x = -(float)h->dir * h->recoil;
        a->meter += a->def->meter_per_hit;
        if (a->meter > a->def->meter_max) a->meter = a->def->meter_max;
    }

    if (v->hp <= 0) {
        v->hp = 0;
        v->state = GE_ST_DEAD;
        v->dead_timer = v->def->dead_ticks;
        v->vel.x = (float)h->dir * (h->kb_x + 1.5f);
        v->vel.y = h->kb_y < -3.0f ? h->kb_y : -3.0f;
        v->on_ground = false;
        play(v, v->def->clip_dead, true);
        ge_world_event(w, GE_EV_KILL, ai, vi);
    }

    ge_world_spark(w, h->x, h->y, h->dir, 8 + h->damage / 2, GE_ARGB(255, 255, 220, 110));
    ge_world_shake(w, 1.5f + (float)h->damage * 0.25f);
    ge_world_event(w, GE_EV_HIT, ai, vi);
}

static void overlap_centre(GeRect a, GeRect b, float *x, float *y) {
    float x0 = a.x > b.x ? a.x : b.x, x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    float y0 = a.y > b.y ? a.y : b.y, y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    *x = (x0 + x1) * 0.5f; *y = (y0 + y1) * 0.5f;
}

/* ---- shots ------------------------------------------------------------- */

static void kill_shot(GeWorld *w, GeShot *s) {
    s->active = false;
    for (int i = 0; i < 5; i++)
        ge_world_emit(w, s->pos.x, s->pos.y, ge_rng_range(&w->rng, -1, 1), ge_rng_range(&w->rng, -1, 0.5f), 0.08f, 12,
                      GE_ARGB(200, 255, 240, 200), 2, GE_P_ADD);
}

void ge_shots_update(GeWorld *w) {
    for (int i = 0; i < GE_MAX_SHOTS; i++) {
        GeShot *s = &w->shots[i];
        if (!s->active) continue;
        if (s->def->spr) ge_anim_update(&s->anim);
        if (--s->life <= 0) { kill_shot(w, s); continue; }
        if (s->def->grounded) {
            s->pos.x += s->vel.x;
            float half = s->def->h * 0.5f;
            float gy = ge_world_ground_y(w, s->pos.x, s->pos.y);
            if (gy < 0 || gy > s->pos.y + half + 20.0f) { kill_shot(w, s); continue; } /* ran off a ledge (snaps down small steps) */
            s->pos.y = gy - half;
        } else {
            s->vel.y += s->def->gravity;
            s->pos.x += s->vel.x;
            s->pos.y += s->vel.y;
        }
        int t = ge_tilemap_get(&w->map, (int)floorf(s->pos.x / GE_TILE), (int)floorf(s->pos.y / GE_TILE));
        if (s->def->solid_stops && t == GE_TILE_SOLID) { kill_shot(w, s); continue; }
        if (s->pos.y > (float)(w->map.h * GE_TILE) + 32.0f) s->active = false;
    }
}

/* ---- combat resolution (platformer rules) ------------------------------- */

void ge_combat_resolve(GeWorld *w) {
    /* melee */
    for (int ai = 0; ai < GE_MAX_ENTS; ai++) {
        GeEntity *a = &w->ents[ai];
        GeRect hit;
        if (!a->active || !ge_entity_hitbox(a, &hit)) continue;
        const GeClip *c = &a->anim.spr->clips[a->anim.clip];
        for (int vi = 0; vi < GE_MAX_ENTS; vi++) {
            GeEntity *v = &w->ents[vi];
            if (vi == ai || !v->active || v->team == a->team || v->state == GE_ST_DEAD || v->external) continue;
            if (a->hit_mask & (1ull << vi)) continue;              /* one hit per swing */
            GeRect hurt = ge_entity_hurtbox(v);
            if (!ge_rect_overlap(hit, hurt) || v->invuln > 0) continue;
            a->hit_mask |= 1ull << vi;
            GeHit h = { c->damage, c->kb_x, c->kb_y, c->hitstop, c->stun, v->pos.x >= a->pos.x ? 1 : -1, 0, 0, c->pogo, c->recoil };
            /* up/down slashes knock along their own axis */
            overlap_centre(hit, hurt, &h.x, &h.y);
            ge_world_apply_hit(w, ai, vi, &h);
            if (!a->active) break;
        }
        /* slashing an enemy shot destroys it -- and a down-slash bounces off it */
        for (int si = 0; si < GE_MAX_SHOTS; si++) {
            GeShot *s = &w->shots[si];
            if (!s->active || s->team == a->team || !s->def->breakable || !ge_rect_overlap(hit, ge_shot_box(s))) continue;
            kill_shot(w, s);
            ge_world_spark(w, s->pos.x, s->pos.y, a->facing, 6, GE_ARGB(255, 200, 230, 255));
            if (c->pogo > 0.0f) pogo(w, ai, c->pogo);
        }
        /* down-slash on spikes: bounce, Hollow Knight style */
        if (c->pogo > 0.0f && !(a->hit_mask & SPIKE_BIT) && ge_world_touches_tile(w, hit, GE_TILE_SPIKE)) {
            a->hit_mask |= SPIKE_BIT;
            pogo(w, ai, c->pogo);
            ge_world_spark(w, hit.x + hit.w * 0.5f, hit.y + hit.h, a->facing, 6, GE_ARGB(255, 210, 220, 255));
        }
    }

    /* projectiles */
    for (int si = 0; si < GE_MAX_SHOTS; si++) {
        GeShot *s = &w->shots[si];
        if (!s->active) continue;
        GeRect box = ge_shot_box(s);
        for (int vi = 0; vi < GE_MAX_ENTS && s->active; vi++) {
            GeEntity *v = &w->ents[vi];
            if (!v->active || v->team == s->team || v->state == GE_ST_DEAD || v->external || v->invuln > 0) continue;
            if (s->hit_mask & (1ull << vi)) continue;
            GeRect hurt = ge_entity_hurtbox(v);
            if (!ge_rect_overlap(box, hurt)) continue;
            s->hit_mask |= 1ull << vi;
            GeHit h = { s->def->damage, s->def->kb_x, s->def->kb_y, s->def->hitstop, s->def->stun, s->vel.x >= 0 ? 1 : -1, 0, 0, 0, 0 };
            overlap_centre(box, hurt, &h.x, &h.y);
            ge_world_apply_hit(w, -1, vi, &h);
            if (s->owner >= 0 && w->ents[s->owner].active) {
                GeEntity *o = &w->ents[s->owner];
                o->meter += o->def->meter_per_hit;
                if (o->meter > o->def->meter_max) o->meter = o->def->meter_max;
            }
            if (!s->def->pierce) kill_shot(w, s);
        }
    }

    /* body contact */
    for (int ai = 0; ai < GE_MAX_ENTS; ai++) {
        GeEntity *a = &w->ents[ai];
        if (!a->active || a->def->contact_damage <= 0 || a->state == GE_ST_DEAD || a->external) continue;
        GeRect ab = ge_entity_hurtbox(a);
        for (int vi = 0; vi < GE_MAX_ENTS; vi++) {
            GeEntity *v = &w->ents[vi];
            if (vi == ai || !v->active || v->team == a->team || v->state == GE_ST_DEAD || v->external || v->invuln > 0) continue;
            GeRect vb = ge_entity_hurtbox(v);
            if (!ge_rect_overlap(ab, vb)) continue;
            GeHit h = { a->def->contact_damage, a->def->contact_kb_x, a->def->contact_kb_y, 4, 16, v->pos.x >= a->pos.x ? 1 : -1, 0, 0, 0, 0 };
            overlap_centre(ab, vb, &h.x, &h.y);
            ge_world_apply_hit(w, -1, vi, &h);
        }
    }
}
