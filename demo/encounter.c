/* Encounters: every duelist has a pattern of their own, EarthBound style. It floods the screen when the duel
 * starts (a Pokemon-style diamond wipe, opening from where they stand) and stays as the duel's background. */

#include "demo.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    int kind, npal, cycle, cycle_from, cycle_split, scale;
    uint32_t pal[12];
    float amp, len, speed, vamp, vlen, vspeed, sx, sy;
    bool interleave;
} PatDef;

/* One per duelist. Rows: who, picture, colours, how it moves. All placeholder looks. */
static const PatDef DEFS[] = {
    { "GROBB", GE_PAT_CHECKER, 8, 8, 0, 0, 2,
      { GE_RGB(60, 44, 30), GE_RGB(86, 64, 40), GE_RGB(112, 84, 50), GE_RGB(86, 64, 40),
        GE_RGB(40, 70, 40), GE_RGB(60, 100, 50), GE_RGB(90, 130, 60), GE_RGB(60, 100, 50) },
      10.0f, 64.0f, 0.06f, 0, 1, 0, 0.3f, 0.0f, true },
    { "SILKE", GE_PAT_PLASMA, 8, 5, 0, 0, 2,
      { GE_RGB(30, 30, 70), GE_RGB(50, 60, 120), GE_RGB(80, 120, 180), GE_RGB(150, 210, 240),
        GE_RGB(230, 240, 255), GE_RGB(150, 210, 240), GE_RGB(80, 120, 180), GE_RGB(50, 60, 120) },
      12.0f, 80.0f, 0.04f, 6.0f, 90.0f, 0.03f, 0.0f, 0.15f, false },
    { "MOSSHIDE", GE_PAT_RINGS, 6, 4, 0, 0, 2,
      { GE_RGB(20, 40, 24), GE_RGB(30, 70, 36), GE_RGB(60, 110, 50), GE_RGB(130, 170, 70), GE_RGB(60, 110, 50), GE_RGB(30, 70, 36) },
      6.0f, 30.0f, 0.09f, 0, 1, 0, 0.0f, 0.0f, true },
    { "THE CHAMPION", GE_PAT_DIAMONDS, 8, 4, 0, 0, 2,
      { GE_RGB(70, 10, 20), GE_RGB(120, 20, 30), GE_RGB(190, 50, 40), GE_RGB(240, 150, 60),
        GE_RGB(255, 220, 120), GE_RGB(240, 150, 60), GE_RGB(190, 50, 40), GE_RGB(120, 20, 30) },
      4.0f, 40.0f, 0.10f, 0, 1, 0, 0.6f, 0.3f, false },
    /* the Paragon: a field of eyes, gold irises breathing outward, a dark tide around them. Slow and sure. */
    { "PARAGON", GE_PAT_EYES, 11, 9, 1, 6, 2,
      { GE_RGB(8, 4, 12),
        GE_RGB(255, 236, 160), GE_RGB(240, 196, 90), GE_RGB(190, 140, 50), GE_RGB(120, 80, 30), GE_RGB(190, 140, 50),
        GE_RGB(14, 10, 26), GE_RGB(26, 18, 44), GE_RGB(40, 28, 66), GE_RGB(26, 18, 44), GE_RGB(14, 10, 26) },
      3.0f, 120.0f, 0.02f, 3.0f, 150.0f, 0.015f, 0.0f, 0.0f, false },
};
#define NDEFS ((int)(sizeof DEFS / sizeof DEFS[0]))

static GePattern g_pats[NDEFS + 1];   /* the last one is the default */
static GeImage g_off;
static bool g_built;

static void build(void) {
    for (int i = 0; i < NDEFS; i++) {
        const PatDef *d = &DEFS[i];
        GePattern *p = &g_pats[i];
        uint32_t pal[12];
        float dim = 1.0f;   /* lower it to calm a pattern that sits behind fighters */
        for (int k = 0; k < d->npal; k++) {
            uint32_t c = d->pal[k];
            pal[k] = GE_RGB((int)((float)((c >> 16) & 255) * dim), (int)((float)((c >> 8) & 255) * dim), (int)((float)(c & 255) * dim));
        }
        ge_pattern_make(p, d->kind, pal, d->npal);
        p->cycle_ticks = d->cycle; p->cycle_from = d->cycle_from; p->cycle_split = d->cycle_split; p->scale = d->scale;
        p->wave_amp = d->amp; p->wave_len = d->len; p->wave_speed = d->speed;
        p->vwave_amp = d->vamp; p->vwave_len = d->vlen; p->vwave_speed = d->vspeed;
        p->scroll_x = d->sx; p->scroll_y = d->sy; p->interleave = d->interleave;
    }
    const uint32_t def[6] = { GE_RGB(30, 16, 50), GE_RGB(60, 30, 90), GE_RGB(110, 60, 150), GE_RGB(190, 120, 230), GE_RGB(110, 60, 150), GE_RGB(60, 30, 90) };
    ge_pattern_make(&g_pats[NDEFS], GE_PAT_RINGS, def, 6);
    g_off = ge_image_create(SCREEN_W, SCREEN_H);
    g_built = true;
}

GePattern *encounter_pattern(const char *name) {
    if (!g_built) build();
    for (int i = 0; i < NDEFS && name; i++) if (!strcmp(DEFS[i].name, name)) return &g_pats[i];
    return &g_pats[NDEFS];
}

void encounter_free(void) {
    if (g_built) ge_image_free(&g_off);
    g_built = false;
}

static void diamonds(GeImage *fb, float front, bool reverse, int cx, int cy);

/* t = ticks since the transition began (0..54). cx, cy: the duelist on screen, where the wipe opens. */
void encounter_draw_transition(GeImage *fb, GePattern *p, int t, int cx, int cy, uint32_t tick) {
    if (!g_built) build();
    if (t < 18) {                                     /* three quick flashes */
        if ((t / 3) & 1) ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(220, 255, 255, 255), 0);
        return;
    }
    ge_pattern_draw(p, &g_off, (int)tick);
    diamonds(fb, (float)(t - 18) / 30.0f, false, cx, cy);
}

/* The other half: at the start of the duel the pattern breaks up again, from the middle out, uncovering the
 * fight. t = 0..23. */
void encounter_draw_reveal(GeImage *fb, GePattern *p, int t, uint32_t tick) {
    if (!g_built) build();
    ge_pattern_draw(p, &g_off, (int)tick);
    diamonds(fb, (float)t / 22.0f, true, fb->w / 2, fb->h / 2);
}

/* progress 0..1. Forward: diamonds grow out from (cx, cy) until the pattern covers everything.
 * Reverse: the pattern starts whole and the diamonds shrink away, nearest (cx, cy) first. */
static void diamonds(GeImage *fb, float progress, bool reverse, int cx, int cy) {
    const int B = 20;
    int bx0 = cx / B, by0 = cy / B, cols = fb->w / B + 1, rows = fb->h / B + 1;
    int maxd = 0;
    for (int by = 0; by < rows; by++)
        for (int bx = 0; bx < cols; bx++) { int d = abs(bx - bx0) + abs(by - by0); if (d > maxd) maxd = d; }
    float front = progress * (float)(maxd + 3);   /* the wave of diamonds sweeps out */
    for (int by = 0; by < rows; by++)
        for (int bx = 0; bx < cols; bx++) {
            float d = (float)(abs(bx - bx0) + abs(by - by0));
            float r = reverse ? (d + 2.0f - front) / 2.0f : (front - d) / 2.0f;
            if (r <= 0.0f) continue;
            int half = r >= 1.0f ? B : (int)(r * (float)B);
            int mx = bx * B + B / 2, my = by * B + B / 2;
            for (int y = my - B / 2; y < my + B / 2; y++) {
                if (y < 0 || y >= fb->h) continue;
                int hw = half - abs(y - my);
                if (hw <= 0) continue;
                int x0 = mx - hw, x1 = mx + hw;
                if (x0 < bx * B) x0 = bx * B;
                if (x1 > bx * B + B) x1 = bx * B + B;
                if (x0 < 0) x0 = 0;
                if (x1 > fb->w) x1 = fb->w;
                if (x1 > x0) memcpy(fb->px + y * fb->w + x0, g_off.px + y * fb->w + x0, sizeof(uint32_t) * (size_t)(x1 - x0));
            }
        }
}
