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
        if (!w->layers[i].borrowed) { ge_image_free(&w->layers[i].img); ge_spans_free(&w->layers[i].spans); }
    w->nlayers = 0;
}

int ge_world_add_layer(GeWorld *w, GeParallaxLayer layer) {
    if (w->nlayers >= GE_MAX_LAYERS) return -1;
    if (layer.alpha == 0) layer.alpha = 255;
    if (!layer.spans.spans && !layer.borrowed) {        /* compile once; borrowed layers arrive compiled */
        if (layer.alpha != 255) { ge_image_scale_alpha(&layer.img, layer.alpha); layer.alpha = 255; }
        ge_spans_build(&layer.spans, &layer.img);
    }
    layer.translucent = false;                          /* any partly see-through pixels? (the compositor's fast path needs none) */
    if (layer.spans.spans)
        for (uint32_t k = 0; k < layer.spans.row_start[layer.spans.rows]; k++) if (!layer.spans.spans[k].opaque) { layer.translucent = true; break; }
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
    int clip = w->background_clip_x > 0 && !l->foreground ? w->background_clip_x : fb->w;
    if (l->spans.spans) { ge_spans_draw_wrap_x_clip(fb, &l->img, &l->spans, sx, dy, l->additive ? GE_ADDITIVE : 0, clip); return; }
    uint32_t tint = l->alpha == 255 ? GE_WHITE : GE_ARGB(l->alpha, 255, 255, 255);
    ge_blit_wrap_x(fb, &l->img, sx, dy, (l->additive ? GE_ADDITIVE : 0) | (l->opaque ? GE_OPAQUE : 0), tint);
}

static void tile_opacity(GeWorld *w) {
    if (!w->tile_opaque_ready) {
        int cells = w->tileset.w / GE_TILE > 256 ? 256 : w->tileset.w / GE_TILE;
        for (int c = 0; c < cells; c++) {
            bool opaque = true;
            for (int y = 0; y < GE_TILE && opaque; y++)
                for (int x = 0; x < GE_TILE; x++)
                    if ((w->tileset.px[y * w->tileset.w + c * GE_TILE + x] >> 24) != 255) { opaque = false; break; }
            w->tile_opaque[c] = opaque;
        }
        w->tile_opaque_ready = true;
    }
}

static void draw_tiles(GeWorld *w, GeImage *fb, int camx, int camy, int z) {
    const GeTilemap *m = &w->map;
    if (!w->tileset.px) return;
    tile_opacity(w);
    int tx0 = (int)floorf((float)camx / GE_TILE), tx1 = (camx + fb->w / z) / GE_TILE + 1;
    int ty0 = (int)floorf((float)camy / GE_TILE), ty1 = (camy + fb->h / z) / GE_TILE + 1;
    for (int ty = ge_clampi(ty0, 0, m->h); ty < ge_clampi(ty1 + 1, 0, m->h); ty++)
        for (int tx = ge_clampi(tx0, 0, m->w); tx < ge_clampi(tx1 + 1, 0, m->w); tx++) {
            if (m->type[ty * m->w + tx] == GE_TILE_EMPTY) continue;
            int cell = m->vis[ty * m->w + tx];
            GeIRect sr = { cell * GE_TILE, 0, GE_TILE, GE_TILE };
            ge_blit_scaled(fb, &w->tileset, sr, (tx * GE_TILE - camx) * z, (ty * GE_TILE - camy) * z, z,
                           w->tile_opaque[cell] ? GE_OPAQUE : 0, GE_WHITE);
        }
}


/* ---- the background compositor ----------------------------------------------------------------------
 * Painting the parallax layers back to front writes most screen pixels two or three times (sky, then
 * mountains over it, then hills, then trees), and on the original 3DS (16 KB of cache, no L2) that memory
 * traffic is most of the frame. So the background is composed one screen row at a time, nearest layer
 * first: each row keeps a short sorted list of covered intervals, a farther layer only copies the parts of
 * its opaque spans that are still uncovered, and pixels that something drawn later will cover anyway
 * (opaque tiles, the foreground layers' opaque spans, the game's art beyond background_clip_x) are never
 * painted at all. Translucent and additive spans are then applied back to front, each only where the pixel
 * belongs to a farther layer, which gives exactly the same result as painting everything in order.
 * Analogy: hanging pictures on a wall from the front row back, and never painting wall that a frame hides. */

#define COV_MAX 96
#define OWN_FRONT 32767      /* covered by something drawn later this frame */
#define OWN_CLEAR (-1)       /* nothing there: the clear colour */
#define MAX_BG 16

typedef struct { int16_t a, b, owner; } Cov;
typedef struct { const GeParallaxLayer *l; int off, dy, flags; bool add; } BgLayer;

/* Add [a, b) for `owner`, filling only the parts not covered yet; `copy` (may be NULL) is called for each
 * newly covered piece. Keeps the list sorted, merges touching pieces of the same owner. Returns false if
 * the row has run out of room (the caller then paints that row the simple way). */
typedef void (*CovFn)(void *ctx, int a, int b);

/* Cover [a, b) for `owner`: each still-uncovered piece is handed to `copy` (may be NULL) and added to the
 * row's sorted list. `any` merges touching pieces whatever their owner (fine when no translucent pass needs
 * to know who owns a pixel), which keeps the list to a handful of entries. *hint remembers where the last
 * call ended, so a layer's spans (which come left to right) don't rescan the row from the start. Returns
 * false if the row runs out of room; the caller then paints that row the simple way. */
static bool cov_fill(Cov *c, int *n, int *hint, int a, int b, int owner, bool any, CovFn copy, void *ctx) {
    int i = *hint > *n ? *n : *hint;
    while (i > 0 && c[i - 1].b > a) i--;            /* everything before i ends at or before a */
    int pos = a;
    while (pos < b) {
        while (i < *n && c[i].b <= pos) i++;
        int next = i < *n ? c[i].a : b;
        if (next > pos) {                           /* a gap: [pos, ge) */
            int ge = next < b ? next : b;
            if (copy) copy(ctx, pos, ge);
            bool left = i > 0 && c[i - 1].b == pos && (any || c[i - 1].owner == owner);
            bool right = i < *n && c[i].a == ge && (any || c[i].owner == owner);
            if (left && right) {
                c[i - 1].b = c[i].b;
                memmove(&c[i], &c[i + 1], sizeof(Cov) * (size_t)(*n - i - 1));
                (*n)--;
                i--;
            } else if (left) {
                c[i - 1].b = (int16_t)ge;
                i--;
            } else if (right) {
                c[i].a = (int16_t)pos;
            } else {
                if (*n >= COV_MAX) return false;
                memmove(&c[i + 1], &c[i], sizeof(Cov) * (size_t)(*n - i));
                c[i] = (Cov){ (int16_t)pos, (int16_t)ge, (int16_t)owner };
                (*n)++;
            }
            pos = ge;
        } else {                                    /* already covered from pos: jump past it */
            pos = c[i].b;
            i++;
        }
    }
    *hint = i > 0 ? i - 1 : 0;
    return true;
}

typedef struct { uint32_t *drow; const uint32_t *srow; int base; } CopyCtx;
static void copy_piece(void *ctx, int a, int b) {
    CopyCtx *k = (CopyCtx *)ctx;
    memcpy(k->drow + a, k->srow + (a - k->base), sizeof(uint32_t) * (size_t)(b - a));
}
static void clear_piece(void *ctx, int a, int b) {
    uint32_t *drow = (uint32_t *)ctx;
    for (int x = a; x < b; x++) drow[x] = GE_RGB(8, 6, 16);
}

/* one row the old way: every layer back to front (used if a row ever has too many pieces to track) */
static void row_simple(GeImage *fb, int y, const BgLayer *L, int nl, int cw) {
    uint32_t *drow = fb->px + (size_t)y * fb->w;
    for (int x = 0; x < cw; x++) drow[x] = GE_RGB(8, 6, 16);
    for (int i = 0; i < nl; i++) {
        const GeParallaxLayer *l = L[i].l;
        int ly = y - L[i].dy;
        if (ly < 0 || ly >= l->img.h) continue;
        const uint32_t *srow = l->img.px + (size_t)ly * l->img.w;
        for (uint32_t k = l->spans.row_start[ly]; k < l->spans.row_start[ly + 1]; k++) {
            const GeSpan *g = &l->spans.spans[k];
            for (int base = -L[i].off; base < cw; base += l->img.w) {
                int a = base + g->x, b = a + g->len;
                if (b <= 0 || a >= cw) continue;
                int from = a < 0 ? -a : 0;
                if (a < 0) a = 0;
                if (b > cw) b = cw;
                if (g->opaque && !L[i].add) memcpy(drow + a, srow + g->x + from, sizeof(uint32_t) * (size_t)(b - a));
                else ge_blend_row(drow + a, srow + g->x + from, b - a, L[i].flags);
            }
        }
    }
}

/* Returns false when the layers can't be composed this way (a layer without compiled spans): the caller
 * then paints them back to front as before. */
static bool compose_owned(GeWorld *w, GeImage *fb, int camx, int camy, int z, bool covers) {
    BgLayer L[MAX_BG], F[MAX_BG];
    int nl = 0, nf = 0;
    int lcamy = z > 1 ? 0 : camy;
    for (int i = 0; i < w->nlayers; i++) {
        const GeParallaxLayer *l = &w->layers[i];
        if (w->low_detail && l->optional) continue;
        if (!l->spans.spans || l->img.w <= 0) { if (!l->foreground) return false; continue; }
        int sx = (int)((camx * z) * l->factor_x + l->auto_x * (float)w->tick);
        int off = sx % l->img.w;
        if (off < 0) off += l->img.w;
        BgLayer b = { l, off, l->y - (int)(lcamy * l->factor_y), l->additive ? GE_ADDITIVE : 0, l->additive };
        if (!l->foreground) { if (nl >= MAX_BG) return false; L[nl++] = b; }
        else if (!w->backdrop_pattern && !w->foreground_off && !l->additive && nf < MAX_BG) {
            /* a foreground layer worth pre-covering: few spans per row (grass blades would cost more than they save) */
            uint32_t spans = l->spans.row_start[l->img.h] - l->spans.row_start[0];
            if (spans <= (uint32_t)l->img.h * 16u) F[nf++] = b;
        }
    }
    int cw = w->background_clip_x > 0 && w->background_clip_x < fb->w ? w->background_clip_x : fb->w;
    /* without translucent background spans nobody needs to know which layer owns a pixel */
    bool any = true;
    for (int i = 0; i < nl; i++) if (L[i].l->additive || L[i].l->translucent) any = false;
    const GeTilemap *m = &w->map;
    bool tiles = w->tileset.px && m->type;
    if (tiles) tile_opacity(w);
    int tx0 = (int)floorf((float)camx / GE_TILE), tx1 = (camx + fb->w / z) / GE_TILE + 1;
    tx0 = ge_clampi(tx0, 0, m->w); tx1 = ge_clampi(tx1 + 1, 0, m->w);

    for (int y = 0; y < fb->h; y++) {
        Cov c[COV_MAX];
        int n = 0;
        bool ok = true;
        uint32_t *drow = fb->px + (size_t)y * fb->w;
        /* 1. what will be covered later anyway */
        int hint = 0;
        if (cw < fb->w) ok = cov_fill(c, &n, &hint, cw, fb->w, OWN_FRONT, any, NULL, NULL);
        if (tiles) {
            int wy = camy + y / z, ty = wy >= 0 ? wy / GE_TILE : -1;
            if (ty >= 0 && ty < m->h) {
                int run_a = 0, run_b = -1;
                for (int tx = tx0; tx < tx1 && ok; tx++) {
                    int t = ty * m->w + tx;
                    if (m->type[t] == GE_TILE_EMPTY || !w->tile_opaque[m->vis[t]]) continue;
                    int a = (tx * GE_TILE - camx) * z, b = a + GE_TILE * z;
                    if (a < 0) a = 0;
                    if (b > fb->w) b = fb->w;
                    if (a >= b) continue;
                    if (a == run_b) { run_b = b; continue; }      /* neighbouring tiles: one piece */
                    if (run_b > run_a) ok = cov_fill(c, &n, &hint, run_a, run_b, OWN_FRONT, any, NULL, NULL);
                    run_a = a; run_b = b;
                }
                if (ok && run_b > run_a) ok = cov_fill(c, &n, &hint, run_a, run_b, OWN_FRONT, any, NULL, NULL);
            }
        }
        for (int f = 0; f < nf && ok; f++) {
            const GeParallaxLayer *l = F[f].l;
            int ly = y - F[f].dy;
            if (ly < 0 || ly >= l->img.h) continue;
            hint = 0;
            for (int base = -F[f].off; base < fb->w && ok; base += l->img.w)
                for (uint32_t k = l->spans.row_start[ly]; k < l->spans.row_start[ly + 1] && ok; k++) {
                    const GeSpan *g = &l->spans.spans[k];
                    if (!g->opaque) continue;
                    int a = base + g->x, b = a + g->len;
                    if (a < 0) a = 0;
                    if (b > fb->w) b = fb->w;
                    if (a < b) ok = cov_fill(c, &n, &hint, a, b, OWN_FRONT, any, NULL, NULL);
                }
        }
        /* 2. opaque spans, nearest layer first */
        for (int i = nl - 1; i >= 0 && ok; i--) {
            const GeParallaxLayer *l = L[i].l;
            if (L[i].add) continue;
            int ly = y - L[i].dy;
            if (ly < 0 || ly >= l->img.h) continue;
            CopyCtx k = { drow, l->img.px + (size_t)ly * l->img.w, 0 };
            hint = 0;
            for (int base = -L[i].off; base < fb->w && ok; base += l->img.w) {   /* left to right: the hint stays useful */
                k.base = base;
                for (uint32_t s = l->spans.row_start[ly]; s < l->spans.row_start[ly + 1] && ok; s++) {
                    const GeSpan *g = &l->spans.spans[s];
                    if (!g->opaque) continue;
                    int a = base + g->x, b = a + g->len;
                    if (b <= 0) continue;
                    if (a >= fb->w) break;
                    if (a < 0) a = 0;
                    if (b > fb->w) b = fb->w;
                    ok = cov_fill(c, &n, &hint, a, b, i, any, copy_piece, &k);
                }
            }
        }
        /* 3. the gaps get the clear colour (when the sky covers everything there are none) */
        hint = 0;
        if (ok) ok = cov_fill(c, &n, &hint, 0, fb->w, OWN_CLEAR, any, clear_piece, drow);
        if (!ok) { row_simple(fb, y, L, nl, cw); continue; }
        /* 4. translucent and additive spans, farthest first, only over pixels owned by a farther layer */
        for (int i = 0; i < nl && !any; i++) {
            const GeParallaxLayer *l = L[i].l;
            int ly = y - L[i].dy;
            if (ly < 0 || ly >= l->img.h) continue;
            const uint32_t *srow = l->img.px + (size_t)ly * l->img.w;
            for (uint32_t s = l->spans.row_start[ly]; s < l->spans.row_start[ly + 1]; s++) {
                const GeSpan *g = &l->spans.spans[s];
                if (g->opaque && !L[i].add) continue;
                for (int base = -L[i].off; base < fb->w; base += l->img.w) {
                    int a = base + g->x, b = a + g->len;
                    if (a < 0) a = 0;
                    if (b > fb->w) b = fb->w;
                    for (int j = 0; j < n && a < b; j++) {
                        if (c[j].b <= a || c[j].owner >= i) continue;
                        if (c[j].a >= b) break;
                        int pa = c[j].a > a ? c[j].a : a, pb = c[j].b < b ? c[j].b : b;
                        ge_blend_row(drow + pa, srow + (pa - base), pb - pa, L[i].flags);
                    }
                }
            }
        }
    }
    (void)covers;
    return true;
}


/* ---- the fast path: no translucent background spans (the usual case, and always in low detail) -------
 * Nobody needs to know which layer owns a pixel, so a row's coverage is a bitmask, one bit per screen
 * pixel (13 words for 400 pixels), and each layer row's opaque pixels are a bitmask too (built with its
 * spans). "What of this layer still shows" is then (layer & ~covered), 32 pixels per operation however
 * busy the art is, and only the runs of set bits in that are copied. Analogy: a stencil held over the
 * picture instead of a written list of which bits of wall are already painted. */

#define NW_MAX 32   /* screens up to 1024 pixels wide */

static inline int bit_ctz(uint32_t v) {   /* index of the lowest set bit; v != 0 */
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctz(v);
#else
    int n = 0;
    while (!(v & 1u)) { v >>= 1; n++; }
    return n;
#endif
}

static void set_bits(uint32_t *m, int a, int b) {   /* bits [a, b) */
    while (a < b) {
        int k = a >> 5, s = a & 31, n = b - a < 32 - s ? b - a : 32 - s;
        m[k] |= (n == 32 ? ~0u : ((1u << n) - 1u)) << s;
        a += n;
    }
}

/* The screen's view of one wrapping layer row: screen pixel x shows image pixel (x + off) % img_w
 * (0 <= off < img_w, img_w >= 32). Words the cover already fills are skipped: nothing there can show. */
static void row_window(const uint32_t *m, int img_w, int off, int nw, const uint32_t *cov, uint32_t *out) {
    int p = off;
    for (int k = 0; k < nw; k++, p += 32) {
        if (p >= img_w) p -= img_w;
        if (cov[k] == ~0u) { out[k] = 0; continue; }
        int wi = p >> 5, s = p & 31;
        uint32_t v = s ? (m[wi] >> s) | (m[wi + 1] << (32 - s)) : m[wi];   /* bits past img_w read as 0 */
        if (img_w - p < 32) v |= m[0] << (img_w - p);                       /* the row starts over mid-word */
        out[k] = v;
    }
}

/* screen pixels [a, b) from the wrapping layer row (src != NULL), or the clear colour */
static void put_run(uint32_t *drow, const uint32_t *src, int img_w, int off, int a, int b) {
    if (!src) { for (int x = a; x < b; x++) drow[x] = GE_RGB(8, 6, 16); return; }
    int ix = a + off;
    while (ix >= img_w) ix -= img_w;
    while (a < b) {
        int n = b - a < img_w - ix ? b - a : img_w - ix;
        memcpy(drow + a, src + ix, sizeof(uint32_t) * (size_t)n);
        a += n;
        ix = 0;
    }
}

/* put_run for every run of set bits in need[0..nw) */
static void put_runs(const uint32_t *need, int nw, uint32_t *drow, const uint32_t *src, int img_w, int off) {
    int start = -1;
    for (int k = 0; k < nw; k++) {
        uint32_t v = need[k];
        int base = k << 5;
        if (start >= 0) {                              /* a run carried over from the last word */
            if (v == ~0u) continue;
            int e = bit_ctz(~v);
            put_run(drow, src, img_w, off, start, base + e);
            start = -1;
            v &= ~0u << e;
        }
        while (v) {
            int s = bit_ctz(v);
            uint32_t rest = ~v & (~0u << s);
            if (!rest) { start = base + s; break; }  /* runs on into the next word */
            int e = bit_ctz(rest);
            put_run(drow, src, img_w, off, base + s, base + e);
            v &= ~0u << e;
        }
    }
    if (start >= 0) put_run(drow, src, img_w, off, start, nw << 5);
}

static bool row_full(const uint32_t *cov, int nw) {
    for (int k = 0; k < nw; k++) if (cov[k] != ~0u) return false;
    return true;
}

static bool compose_background(GeWorld *w, GeImage *fb, int camx, int camy, int z, bool covers) {
    BgLayer L[MAX_BG], F[MAX_BG];
    int nl = 0, nf = 0;
    int lcamy = z > 1 ? 0 : camy;
    int W = fb->w, nw = (W + 31) >> 5;
    if (nw > NW_MAX) return false;
    for (int i = 0; i < w->nlayers; i++) {
        const GeParallaxLayer *l = &w->layers[i];
        if (w->low_detail && l->optional) continue;
        if (!l->spans.spans || l->img.w <= 0) { if (!l->foreground) return false; continue; }
        if (!l->foreground && (l->additive || l->translucent)) return compose_owned(w, fb, camx, camy, z, covers);
        if (!l->spans.mask || l->img.w < 32) { if (!l->foreground) return false; continue; }
        int sx = (int)((camx * z) * l->factor_x + l->auto_x * (float)w->tick);
        int off = sx % l->img.w;
        if (off < 0) off += l->img.w;
        BgLayer b = { l, off, l->y - (int)(lcamy * l->factor_y), 0, false };
        if (!l->foreground) { if (nl >= MAX_BG) return false; L[nl++] = b; }
        else if (!w->backdrop_pattern && !w->foreground_off && !l->additive && nf < MAX_BG) F[nf++] = b;
    }
    int cw = w->background_clip_x > 0 && w->background_clip_x < W ? w->background_clip_x : W;

    /* what something drawn later covers anyway, the same on every row: past the clip column and past the
     * screen's right edge (the bits of the last word beyond W) */
    uint32_t base_cov[NW_MAX] = { 0 };
    set_bits(base_cov, cw, nw << 5);

    /* opaque tiles, one mask per visible tile row */
    const GeTilemap *m = &w->map;
    bool tiles = w->tileset.px && m->type;
    enum { TROWS = 32 };
    uint32_t tmask[TROWS][NW_MAX];
    bool thas[TROWS] = { false };
    int ty0 = 0;
    if (tiles) {
        tile_opacity(w);
        int tx0 = ge_clampi((int)floorf((float)camx / GE_TILE), 0, m->w), tx1 = ge_clampi((camx + W / z) / GE_TILE + 2, 0, m->w);
        ty0 = camy >= 0 ? camy / GE_TILE : -((-camy + GE_TILE - 1) / GE_TILE);
        int rows = (fb->h / z) / GE_TILE + 2;
        for (int r = 0; r < TROWS && r < rows; r++) {
            int ty = ty0 + r;
            if (ty < 0 || ty >= m->h) continue;
            memset(tmask[r], 0, sizeof(uint32_t) * (size_t)nw);
            for (int tx = tx0; tx < tx1; tx++) {
                int t = ty * m->w + tx;
                if (m->type[t] == GE_TILE_EMPTY || !w->tile_opaque[m->vis[t]]) continue;
                int a = (tx * GE_TILE - camx) * z, b = a + GE_TILE * z;
                if (a < 0) a = 0;
                if (b > W) b = W;
                if (a < b) { set_bits(tmask[r], a, b); thas[r] = true; }
            }
        }
    }

    uint32_t cov[NW_MAX], lm[NW_MAX], need[NW_MAX];
    for (int y = 0; y < fb->h; y++) {
        uint32_t *drow = fb->px + (size_t)y * W;
        memcpy(cov, base_cov, sizeof(uint32_t) * (size_t)nw);
        /* 1. what something drawn later covers anyway */
        if (tiles) {
            int wy = camy + y / z, r = (wy >= 0 ? wy / GE_TILE : -((-wy + GE_TILE - 1) / GE_TILE)) - ty0;
            if (r >= 0 && r < TROWS && thas[r])
                for (int k = 0; k < nw; k++) cov[k] |= tmask[r][k];
        }
        for (int f = 0; f < nf; f++) {
            const GeSpans *sp = &F[f].l->spans;
            int ly = y - F[f].dy;
            if (ly < 0 || ly >= F[f].l->img.h || sp->row_kind[ly] == GE_ROW_NONE) continue;
            row_window(sp->mask + (size_t)ly * (size_t)sp->mask_words, F[f].l->img.w, F[f].off, nw, cov, lm);
            for (int k = 0; k < nw; k++) cov[k] |= lm[k];
        }
        if (row_full(cov, nw)) continue;                   /* e.g. ground, all tiles: no background at all */
        /* 2. opaque pixels, nearest layer first: put what still shows, then add it to the cover */
        bool full = false;
        for (int i = nl - 1; i >= 0; i--) {
            const GeParallaxLayer *l = L[i].l;
            const GeSpans *sp = &l->spans;
            int ly = y - L[i].dy;
            if (ly < 0 || ly >= l->img.h || sp->row_kind[ly] == GE_ROW_NONE) continue;
            const uint32_t *srow = l->img.px + (size_t)ly * (size_t)l->img.w;
            if (sp->row_kind[ly] == GE_ROW_ALL) {          /* solid all the way across: everything left shows */
                for (int k = 0; k < nw; k++) need[k] = ~cov[k];
                put_runs(need, nw, drow, srow, l->img.w, L[i].off);
                full = true;
                break;
            }
            row_window(sp->mask + (size_t)ly * (size_t)sp->mask_words, l->img.w, L[i].off, nw, cov, lm);
            for (int k = 0; k < nw; k++) need[k] = lm[k] & ~cov[k];
            put_runs(need, nw, drow, srow, l->img.w, L[i].off);
            if (i == 0 && covers) { full = true; break; }   /* nothing farther and no gaps to clear: done */
            for (int k = 0; k < nw; k++) cov[k] |= lm[k];
            if (row_full(cov, nw)) { full = true; break; }   /* farther layers can't show on this row */
        }
        /* 3. whatever is left gets the clear colour (with a full-screen sky there is nothing left) */
        if (!full && (nl == 0 || !covers)) {
            for (int k = 0; k < nw; k++) need[k] = ~cov[k];
            put_runs(need, nw, drow, NULL, 0, 0);
        }
    }
    return true;
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

    /* the first layer usually paints the whole screen (the sky): clearing first would be wasted work */
    const GeParallaxLayer *l0 = w->nlayers > 0 ? &w->layers[0] : NULL;
    bool covers = l0 && l0->opaque && !l0->additive && l0->img.w >= fb->w && l0->y <= 0 && l0->y + l0->img.h >= fb->h &&
                  l0->factor_y == 0.0f && !(w->low_detail && l0->optional);
    if (w->background_off) {
        /* nothing: tiles and props cover every pixel */
    } else if (w->backdrop_pattern) {
        ge_pattern_draw(w->backdrop_pattern, fb, (int)w->tick);
    } else if (w->reference_background || !compose_background(w, fb, camx, camy, z, covers)) {
        if (!covers) ge_clear(fb, GE_RGB(8, 6, 16));
        else if (w->background_clip_x > 0 && l0 && !l0->spans.spans) ge_clear(fb, GE_RGB(8, 6, 16));
        for (int i = 0; i < w->nlayers; i++)
            if (!w->layers[i].foreground && !(w->low_detail && w->layers[i].optional))
                draw_layer(w, fb, &w->layers[i], camx * z, lcamy);
    }

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

    for (int i = 0; i < w->nlayers && !w->backdrop_pattern && !w->foreground_off; i++)
        if (w->layers[i].foreground && !(w->low_detail && w->layers[i].optional))
            draw_layer(w, fb, &w->layers[i], camx * z, lcamy);

    if (w->vignette > 0 && !w->low_detail) ge_vignette(fb, w->vignette, 150);
}
