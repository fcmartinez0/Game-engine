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
            m->type[y * w + x] = c == '#' ? GE_TILE_SOLID : c == '=' ? GE_TILE_ONEWAY : GE_TILE_EMPTY;
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
    ge_image_free(&w->tileset);
    for (int i = 0; i < w->nlayers; i++) ge_image_free(&w->layers[i].img);
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
        e->anim.spr = spr;
        e->anim.clip = def->clip_idle;
        return i;
    }
    return -1;
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

int ge_world_particle_count(const GeWorld *w) {
    int n = 0;
    for (int i = 0; i < GE_MAX_PARTS; i++) n += w->parts[i].life > 0;
    return n;
}

void ge_world_shake(GeWorld *w, float amount) {
    if (amount > w->cam.shake) w->cam.shake = amount;
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
    float maxx = (float)(w->map.w * GE_TILE - c->vw), maxy = (float)(w->map.h * GE_TILE - c->vh);
    c->x = ge_clampf(c->x, 0.0f, maxx > 0 ? maxx : 0.0f);
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
        if (w->ents[i].active) ge_actor_update(w, i);
    ge_combat_resolve(w);
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

static void draw_tiles(const GeWorld *w, GeImage *fb, int camx, int camy) {
    const GeTilemap *m = &w->map;
    int tx0 = (int)floorf((float)camx / GE_TILE), tx1 = (camx + fb->w) / GE_TILE + 1;
    int ty0 = (int)floorf((float)camy / GE_TILE), ty1 = (camy + fb->h) / GE_TILE + 1;
    for (int ty = ge_clampi(ty0, 0, m->h); ty < ge_clampi(ty1 + 1, 0, m->h); ty++)
        for (int tx = ge_clampi(tx0, 0, m->w); tx < ge_clampi(tx1 + 1, 0, m->w); tx++) {
            if (m->type[ty * m->w + tx] == GE_TILE_EMPTY) continue;
            GeIRect sr = { m->vis[ty * m->w + tx] * GE_TILE, 0, GE_TILE, GE_TILE };
            ge_blit(fb, &w->tileset, sr, tx * GE_TILE - camx, ty * GE_TILE - camy, 0, GE_WHITE);
        }
}

static void draw_entity(const GeWorld *w, GeImage *fb, const GeEntity *e, int camx, int camy) {
    /* lanes: a few pixels of vertical offset and a cool shade sell the depth */
    int lane_dy = e->lane * 5;
    uint32_t tint = e->lane < 0 ? GE_RGB(165, 172, 205) : GE_WHITE;
    int sx = (int)floorf(e->pos.x) - camx, sy = (int)floorf(e->pos.y) - camy + lane_dy;

    /* soft contact shadow, shrinking as the entity leaves the ground */
    float gy = ge_world_ground_y(w, e->pos.x, e->pos.y - 1.0f);
    if (gy >= 0.0f && e->state != GE_ST_DEAD) {
        float height = gy - e->pos.y;
        if (height < 0) height = 0;
        int hw = (int)(e->def->body_w * 0.8f - height * 0.12f);
        if (hw >= 2) {
            int gsy = (int)gy - camy + lane_dy;
            ge_fill_rect(fb, sx - hw / 2 + 1, gsy - 1, hw - 2, 1, GE_ARGB(70, 0, 0, 0), 0);
            ge_fill_rect(fb, sx - hw / 2, gsy, hw, 1, GE_ARGB(90, 0, 0, 0), 0);
            ge_fill_rect(fb, sx - hw / 2 + 1, gsy + 1, hw - 2, 1, GE_ARGB(70, 0, 0, 0), 0);
        }
    }

    if (e->state == GE_ST_DEAD && e->dead_timer < 30 && (e->dead_timer & 3) < 2) return;
    if (e->invuln > 0 && e->state != GE_ST_HURT && ((e->invuln / 3) & 1)) return;

    int flags = 0;
    if (e->flash > 0) { flags |= GE_FLASH; tint = GE_WHITE; }
    ge_anim_draw(&e->anim, fb, sx, sy, e->facing < 0, flags, tint);
}

static void draw_particles(const GeWorld *w, GeImage *fb, int camx, int camy) {
    for (int i = 0; i < GE_MAX_PARTS; i++) {
        const GeParticle *p = &w->parts[i];
        if (p->life <= 0) continue;
        int a = (int)(p->color >> 24) * p->life / p->max_life;
        if (p->flags & GE_P_TWINKLE) {
            float t = 0.5f + 0.5f * sinf((float)w->tick * 0.12f + p->x * 0.37f);
            a = (int)((float)a * (0.25f + 0.75f * t));
        }
        uint32_t c = ((uint32_t)a << 24) | (p->color & 0x00FFFFFFu);
        ge_fill_rect(fb, (int)p->x - camx, (int)p->y - camy, p->size, p->size, c, (p->flags & GE_P_ADD) ? GE_ADDITIVE : 0);
    }
}

void ge_world_draw(GeWorld *w, GeImage *fb) {
    int camx = (int)floorf(w->cam.x + w->cam.shake_x);
    int camy = (int)floorf(w->cam.y + w->cam.shake_y);

    ge_clear(fb, GE_RGB(8, 6, 16));
    for (int i = 0; i < w->nlayers; i++)
        if (!w->layers[i].foreground && !(w->low_detail && w->layers[i].optional))
            draw_layer(w, fb, &w->layers[i], camx, camy);

    draw_tiles(w, fb, camx, camy);

    /* back to front; within a lane enemies first so the player is never hidden */
    for (int lane = GE_LANE_BACK; lane <= GE_LANE_FRONT; lane++)
        for (int team = 1; team >= 0; team--)
            for (int i = 0; i < GE_MAX_ENTS; i++) {
                const GeEntity *e = &w->ents[i];
                if (e->active && e->lane == lane && (e->team != 0) == (team != 0))
                    draw_entity(w, fb, e, camx, camy);
            }

    draw_particles(w, fb, camx, camy);

    for (int i = 0; i < w->nlayers; i++)
        if (w->layers[i].foreground && !(w->low_detail && w->layers[i].optional))
            draw_layer(w, fb, &w->layers[i], camx, camy);

    if (w->vignette > 0 && !w->low_detail) ge_vignette(fb, w->vignette, 150);
}
