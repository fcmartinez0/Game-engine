#include "ge/ge_world.h"

#include <stdlib.h>
#include <string.h>

/* ---- tilemap ----------------------------------------------------------- */

int ge_tilemap_create(GeTilemap *m, int w, int h) {
    m->w = w; m->h = h;
    m->type = (uint8_t *)calloc((size_t)w * h, 1);
    m->vis = (uint8_t *)calloc((size_t)w * h, 1);
    if (!m->type || !m->vis) { ge_tilemap_free(m); return -1; }
    return 0;
}

void ge_tilemap_free(GeTilemap *m) {
    free(m->type); free(m->vis);
    memset(m, 0, sizeof *m);
}

int ge_tilemap_from_ascii(GeTilemap *m, const char *const *rows, int nrows) {
    if (nrows <= 0) return -1;
    int w = (int)strlen(rows[0]);
    for (int y = 0; y < nrows; y++)
        if ((int)strlen(rows[y]) != w) return -2;
    if (ge_tilemap_create(m, w, nrows) != 0) return -1;
    for (int y = 0; y < nrows; y++)
        for (int x = 0; x < w; x++) {
            char c = rows[y][x];
            m->type[y * w + x] = c == '#' ? GE_TILE_SOLID : c == '=' ? GE_TILE_ONEWAY : c == '^' ? GE_TILE_SPIKE : GE_TILE_EMPTY;
        }
    return 0;
}

/* ---- world lifecycle --------------------------------------------------- */

void ge_world_init(GeWorld *w, int view_w, int view_h, uint32_t seed) {
    memset(w, 0, sizeof *w);
    w->cam.vw = view_w; w->cam.vh = view_h;
    w->cam.lookahead = 36.0f;
    w->cam.smooth = 0.12f;
    w->follow = -1;
    ge_rng_seed(&w->rng, seed);
}

void ge_world_free(GeWorld *w) {
    ge_tilemap_free(&w->map);
    if (!w->tileset_borrowed) ge_image_free(&w->tileset);
    for (int i = 0; i < w->nlayers; i++)
        if (!w->layers[i].borrowed) ge_image_free(&w->layers[i].img);
    w->nlayers = 0;
}

int ge_world_add_layer(GeWorld *w, GeParallaxLayer layer) {
    if (w->nlayers >= GE_MAX_LAYERS) return -1;
    if (layer.alpha == 0) layer.alpha = 255;
    w->layers[w->nlayers] = layer;
    return w->nlayers++;
}

int ge_world_spawn(GeWorld *w, const GeActorDef *def, const GeSprite *spr, int team, float x, float y, int hp) {
    for (int i = 0; i < GE_MAX_ENTS; i++) {
        GeEntity *e = &w->ents[i];
        if (e->active) continue;
        memset(e, 0, sizeof *e);
        memset(&w->intents[i], 0, sizeof w->intents[i]);
        e->active = true;
        e->team = team;
        e->pos = (GeVec2){ x, y };
        e->facing = 1;
        e->hp = e->max_hp = hp;
        e->def = def;
        e->ammo = def->ammo_max;
        e->air_jumps_left = def->air_jumps;
        e->anim.spr = spr;
        e->anim.clip = def->clip_idle;
        return i;
    }
    return -1;
}

void ge_world_event(GeWorld *w, GeEventType ev, int a, int b) {
    if (w->on_event) w->on_event(w->user, ev, a, b);
}

/* ---- particles --------------------------------------------------------- */

void ge_world_emit(GeWorld *w, float x, float y, float vx, float vy, float g, int life, uint32_t color, int size, int flags) {
    for (int i = 0; i < GE_MAX_PARTS; i++) {
        GeParticle *p = &w->parts[i];
        if (p->life > 0) continue;
        *p = (GeParticle){ x, y, vx, vy, g, life, life, color, (uint8_t)size, (uint8_t)flags };
        return;
    }
}

void ge_world_spark(GeWorld *w, float x, float y, int dir, int count, uint32_t color) {
    ge_world_emit(w, x - 4, y - 4, 0, 0, 0, 4, GE_ARGB(255, 255, 255, 230), 8, GE_P_ADD); /* impact flash */
    for (int i = 0; i < count; i++) {
        float ang = ge_rng_range(&w->rng, -2.6f, 2.6f) + (dir >= 0 ? 0.0f : 3.14159f);
        float sp = ge_rng_range(&w->rng, 1.0f, 3.2f);
        ge_world_emit(w, x, y, cosf(ang) * sp, sinf(ang) * sp - 0.6f, 0.12f,
                      10 + (int)ge_rng_range(&w->rng, 0, 8), color, 2, GE_P_ADD);
    }
}

int ge_world_particle_count(const GeWorld *w) {
    int n = 0;
    for (int i = 0; i < GE_MAX_PARTS; i++) n += w->parts[i].life > 0;
    return n;
}

void ge_world_shake(GeWorld *w, float amount) {
    if (amount > w->cam.shake) w->cam.shake = amount;
}

/* ---- shots ------------------------------------------------------------- */

int ge_world_shoot(GeWorld *w, int team, int owner, const GeShotDef *def, float x, float y, int facing) {
    for (int i = 0; i < GE_MAX_SHOTS; i++) {
        GeShot *s = &w->shots[i];
        if (s->active) continue;
        memset(s, 0, sizeof *s);
        s->active = true;
        s->team = team; s->owner = owner; s->def = def;
        s->pos = (GeVec2){ x, y };
        s->vel = (GeVec2){ (float)facing * def->speed, def->vy };
        s->facing = facing;
        s->life = def->life;
        s->anim.spr = def->spr;
        s->anim.clip = def->clip;
        ge_world_event(w, GE_EV_SHOOT, owner, i);
        return i;
    }
    return -1;
}

GeRect ge_shot_box(const GeShot *s) {
    return (GeRect){ s->pos.x - s->def->w * 0.5f, s->pos.y - s->def->h * 0.5f, s->def->w, s->def->h };
}

/* ---- queries ----------------------------------------------------------- */

float ge_world_ground_y(const GeWorld *w, float x, float y) {
    int tx = (int)floorf(x / GE_TILE), ty = (int)floorf(y / GE_TILE);
    if (ty < 0) ty = 0;
    for (int r = ty; r < w->map.h; r++) {
        int t = ge_tilemap_get(&w->map, tx, r);
        if (t == GE_TILE_SOLID || t == GE_TILE_ONEWAY) return (float)(r * GE_TILE);
    }
    return -1.0f;
}

bool ge_world_touches_tile(const GeWorld *w, GeRect r, int tile_type) {
    int tx0 = (int)floorf(r.x / GE_TILE), tx1 = (int)floorf((r.x + r.w - 0.01f) / GE_TILE);
    int ty0 = (int)floorf(r.y / GE_TILE), ty1 = (int)floorf((r.y + r.h - 0.01f) / GE_TILE);
    for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++)
            if (tx >= 0 && tx < w->map.w && ge_tilemap_get(&w->map, tx, ty) == tile_type) return true;
    return false;
}

GeRect ge_entity_hurtbox(const GeEntity *e) {
    return (GeRect){ e->pos.x - e->def->body_w * 0.5f, e->pos.y - e->def->body_h, e->def->body_w, e->def->body_h };
}

bool ge_entity_hitbox(const GeEntity *e, GeRect *out) {
    if (e->state != GE_ST_ATTACK) return false;
    const GeClip *c = &e->anim.spr->clips[e->anim.clip];
    const GeAnimFrame *f = &c->frames[e->anim.frame];
    if (!c->is_attack || !f->has_hit || e->anim.done) return false;
    GeRect r = f->hit;
    if (e->facing < 0) r.x = -(r.x + r.w);
    r.x += e->pos.x; r.y += e->pos.y;
    *out = r;
    return true;
}

/* ---- update ------------------------------------------------------------ */

static void update_particles(GeWorld *w) {
    for (int i = 0; i < GE_MAX_PARTS; i++) {
        GeParticle *p = &w->parts[i];
        if (p->life <= 0) continue;
        p->life--;
        if (p->flags & GE_P_WANDER) {
            p->vx = ge_clampf(p->vx + ge_rng_range(&w->rng, -0.02f, 0.02f), -0.4f, 0.4f);
            p->vy = ge_clampf(p->vy + ge_rng_range(&w->rng, -0.02f, 0.02f), -0.3f, 0.3f);
        }
        p->x += p->vx; p->y += p->vy; p->vy += p->g;
    }
}

static void update_camera(GeWorld *w) {
    GeCamera *c = &w->cam;
    if (w->follow >= 0 && w->ents[w->follow].active) {
        const GeEntity *f = &w->ents[w->follow];
        float tx = f->pos.x + (float)f->facing * c->lookahead - c->vw * 0.5f;
        float ty = f->pos.y - c->vh * 0.65f;
        c->x += (tx - c->x) * c->smooth;
        c->y += (ty - c->y) * c->smooth;
    }
    float minx = 0.0f, maxx = (float)(w->map.w * GE_TILE - c->vw), maxy = (float)(w->map.h * GE_TILE - c->vh);
    if (c->locked) { minx = c->lock_x0; maxx = c->lock_x1 - c->vw; }
    if (maxx < minx) maxx = minx;
    c->x = ge_clampf(c->x, minx, maxx);
    c->y = ge_clampf(c->y, 0.0f, maxy > 0 ? maxy : 0.0f);
    if (c->shake > 0.3f) {
        c->shake_x = ge_rng_range(&w->rng, -1.0f, 1.0f) * c->shake;
        c->shake_y = ge_rng_range(&w->rng, -1.0f, 1.0f) * c->shake;
        c->shake *= 0.85f;
    } else {
        c->shake = c->shake_x = c->shake_y = 0.0f;
    }
}

void ge_world_update(GeWorld *w) {
    w->tick++;
    for (int i = 0; i < GE_MAX_ENTS; i++)
        if (w->ents[i].active && !w->ents[i].external) ge_actor_update(w, i);
    ge_shots_update(w);
    if (!w->custom_combat) ge_combat_resolve(w);
    update_particles(w);
    update_camera(w);
}

/* ---- drawing ----------------------------------------------------------- */

static void draw_layer(const GeWorld *w, GeImage *fb, const GeParallaxLayer *l, int camx, int camy) {
    int sx = (int)(camx * l->factor_x + l->auto_x * (float)w->tick);
    int dy = l->y - (int)(camy * l->factor_y);
    uint32_t tint = l->alpha == 255 ? GE_WHITE : GE_ARGB(l->alpha, 255, 255, 255);
    ge_blit_wrap_x(fb, &l->img, sx, dy, (l->additive ? GE_ADDITIVE : 0) | (l->opaque ? GE_OPAQUE : 0), tint);
}

static void draw_tiles(const GeWorld *w, GeImage *fb, int camx, int camy, int z) {
    const GeTilemap *m = &w->map;
    if (!w->tileset.px) return;
    int tx0 = (int)floorf((float)camx / GE_TILE), tx1 = (camx + fb->w / z) / GE_TILE + 1;
    int ty0 = (int)floorf((float)camy / GE_TILE), ty1 = (camy + fb->h / z) / GE_TILE + 1;
    for (int ty = ge_clampi(ty0, 0, m->h); ty < ge_clampi(ty1 + 1, 0, m->h); ty++)
        for (int tx = ge_clampi(tx0, 0, m->w); tx < ge_clampi(tx1 + 1, 0, m->w); tx++) {
            if (m->type[ty * m->w + tx] == GE_TILE_EMPTY) continue;
            GeIRect sr = { m->vis[ty * m->w + tx] * GE_TILE, 0, GE_TILE, GE_TILE };
            ge_blit_scaled(fb, &w->tileset, sr, (tx * GE_TILE - camx) * z, (ty * GE_TILE - camy) * z, z, 0, GE_WHITE);
        }
}

static void draw_entity(const GeWorld *w, GeImage *fb, const GeEntity *e, int camx, int camy, int z) {
    int sx = ((int)floorf(e->pos.x) - camx) * z, sy = ((int)floorf(e->pos.y) - camy) * z;

    /* soft contact shadow, shrinking as the entity leaves the ground */
    float gy = ge_world_ground_y(w, e->pos.x, e->pos.y - 1.0f);
    if (gy >= 0.0f && e->state != GE_ST_DEAD && !e->def->flying) {
        float height = gy - e->pos.y;
        if (height < 0) height = 0;
        int hw = (int)(e->def->body_w * 0.8f - height * 0.12f) * z;
        if (hw >= 2) {
            int gsy = ((int)gy - camy) * z;
            ge_fill_rect(fb, sx - hw / 2 + z, gsy - z, hw - 2 * z, z, GE_ARGB(70, 0, 0, 0), 0);
            ge_fill_rect(fb, sx - hw / 2, gsy, hw, z, GE_ARGB(90, 0, 0, 0), 0);
            ge_fill_rect(fb, sx - hw / 2 + z, gsy + z, hw - 2 * z, z, GE_ARGB(70, 0, 0, 0), 0);
        }
    }

    if (e->state == GE_ST_DEAD && e->dead_timer < 30 && (e->dead_timer & 3) < 2) return;
    if (!e->external && e->invuln > 0 && e->state != GE_ST_HURT && e->state != GE_ST_DASH &&
        e->state != GE_ST_ATTACK && ((e->invuln / 3) & 1)) return;

    ge_anim_draw_scaled(&e->anim, fb, sx, sy, e->facing < 0, e->flash > 0 ? GE_FLASH : 0, GE_WHITE, z);
}

static void draw_shots(const GeWorld *w, GeImage *fb, int camx, int camy, int z) {
    for (int i = 0; i < GE_MAX_SHOTS; i++) {
        const GeShot *s = &w->shots[i];
        if (!s->active || !s->def->spr) continue;
        ge_anim_draw_scaled(&s->anim, fb, ((int)floorf(s->pos.x) - camx) * z, ((int)floorf(s->pos.y) - camy) * z, s->facing < 0, 0, GE_WHITE, z);
    }
}

static void draw_particles(const GeWorld *w, GeImage *fb, int camx, int camy, int z) {
    for (int i = 0; i < GE_MAX_PARTS; i++) {
        const GeParticle *p = &w->parts[i];
        if (p->life <= 0) continue;
        int a = (int)(p->color >> 24) * p->life / p->max_life;
        if (p->flags & GE_P_TWINKLE) {
            float t = 0.5f + 0.5f * sinf((float)w->tick * 0.12f + p->x * 0.37f);
            a = (int)((float)a * (0.25f + 0.75f * t));
        }
        uint32_t c = ((uint32_t)a << 24) | (p->color & 0x00FFFFFFu);
        ge_fill_rect(fb, ((int)p->x - camx) * z, ((int)p->y - camy) * z, p->size * z, p->size * z, c, (p->flags & GE_P_ADD) ? GE_ADDITIVE : 0);
    }
}

void ge_world_draw(GeWorld *w, GeImage *fb) {
    int camx = (int)floorf(w->cam.x + w->cam.shake_x);
    int camy = (int)floorf(w->cam.y + w->cam.shake_y);
    int z = w->zoom > 1 ? w->zoom : 1;
    int lcamy = z > 1 ? 0 : camy;   /* a zoomed view keeps the backdrop framed as authored */

    ge_clear(fb, GE_RGB(8, 6, 16));
    for (int i = 0; i < w->nlayers; i++)
        if (!w->layers[i].foreground && !(w->low_detail && w->layers[i].optional))
            draw_layer(w, fb, &w->layers[i], camx * z, lcamy);

    draw_tiles(w, fb, camx, camy, z);
    if (w->draw_props) w->draw_props(w->user, fb, camx, camy);

    /* enemies first so the player is never hidden behind them */
    for (int team = 1; team >= 0; team--)
        for (int i = 0; i < GE_MAX_ENTS; i++) {
            const GeEntity *e = &w->ents[i];
            if (e->active && (e->team != 0) == (team != 0)) draw_entity(w, fb, e, camx, camy, z);
        }

    draw_shots(w, fb, camx, camy, z);
    draw_particles(w, fb, camx, camy, z);

    for (int i = 0; i < w->nlayers; i++)
        if (w->layers[i].foreground && !(w->low_detail && w->layers[i].optional))
            draw_layer(w, fb, &w->layers[i], camx * z, lcamy);

    if (w->vignette > 0 && !w->low_detail) ge_vignette(fb, w->vignette, 150);
}
