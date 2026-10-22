/* The Paragon, the demo's final boss: how its fighter def is assembled from what it has watched and taken,
 * where its memory is kept, and the end-of-demo report. The brain itself lives in the engine
 * (ge_paragon.c, ge_model.c): this file is the game-specific part. */

#include "demo.h"
#include "ge/ge_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the def ------------------------------------------------------------- */

static int popcount(unsigned v) { int n = 0; for (; v; v >>= 1) n += (int)(v & 1u); return n; }

static int base_dmg[GE_SPRITE_MAX_CLIPS];
static bool have_base;

void paragon_forget_base(void) { have_base = false; }   /* its clips were reloaded (mods) */

void paragon_build_def(const Chars *c, Sprites *s, unsigned gifts, GeFighterDef *out) {
    GeSprite *spr = &s->paragon;
    if (!have_base) {
        for (int i = 0; i < spr->nclips; i++) base_dmg[i] = spr->clips[i].damage;
        have_base = true;
    }
    float dmg = (gifts & GIFT_MOSSHIDE) ? 1.2f : 1.0f;                 /* Mosshide: hits harder */
    for (int i = 0; i < spr->nclips; i++) spr->clips[i].damage = (int)((float)base_dmg[i] * dmg + 0.5f);

    *out = c->f_paragon;
    out->undying = true;
    out->clip_finisher = ge_sprite_find_clip(spr, "f_finisher");
    out->max_hp = 130;
    if (gifts & GIFT_GROBB) out->max_hp += 25;                          /* Grobb: more health, and a rock to throw */
    if (gifts & GIFT_MOSSHIDE) out->max_hp += 35;                       /* Mosshide: much more */
    if (gifts & GIFT_SILKE) {                                           /* Silke: faster feet, and her dust */
        out->body.run_speed *= 1.12f; out->walk_back *= 1.12f; out->dash_speed *= 1.15f;
    }
    int n = out->nmoves;
    if (gifts & GIFT_GROBB) out->moves[n++] = (GeMove){ .input = "236S", .clip = "f_shoot", .level = 3, .shot = &c->f_rock, .ai = GE_AI_ZONE };
    if (gifts & GIFT_SILKE) out->moves[n++] = (GeMove){ .input = "214S", .clip = "f_shoot", .level = 3, .shot = &c->f_dust, .ai = GE_AI_ZONE };
    if (gifts & GIFT_AVERY) out->moves[n++] = (GeMove){ .input = "623S", .clip = "f_charge", .level = 3, .ai = GE_AI_RUSH };
    out->nmoves = n;
    ge_fighter_def_finish(out, spr);
}

int paragon_learn_level(const GePlayerModel *m, unsigned gifts) {
    float conf = ge_model_confidence(m);
    int v = 8 + 12 * popcount(gifts) + (int)(m->duels > 10 ? 30 : m->duels * 3) + (int)(conf * 20.0f) + (int)m->finals * 8;
    return v > 100 ? 100 : v;
}

/* ---- memory -------------------------------------------------------------- */

static const char *g_path_override;

void paragon_set_profile_path(const char *path) { g_path_override = path; }

#ifdef __3DS__
static const char *const PATHS[] = { "sdmc:/3ds/gloamwood/profile.txt", "sdmc:/gloamwood_profile.txt" };
#else
static const char *const PATHS[] = { "gloamwood_profile.txt" };
#endif

const char *paragon_profile_path(void) { return g_path_override ? g_path_override : PATHS[0]; }

void paragon_profile_load(GePlayerModel *m) {
    if (g_path_override) { ge_model_load(m, g_path_override); return; }
    for (unsigned i = 0; i < sizeof PATHS / sizeof PATHS[0]; i++) if (ge_model_load(m, PATHS[i]) == 0) return;
}

void paragon_profile_save(const GePlayerModel *m) {
    if (g_path_override) { ge_model_save(m, g_path_override); return; }
    for (unsigned i = 0; i < sizeof PATHS / sizeof PATHS[0]; i++) if (ge_model_save(m, PATHS[i]) == 0) return;
}

/* ---- the report ---------------------------------------------------------- */

void paragon_draw_report(GeImage *fb, const char facts[][72], int nfacts, const GePlayerModel *m, int learn, unsigned gifts, int t) {
    const uint32_t ink = GE_RGB(20, 14, 30), gold = GE_RGB(255, 214, 120), dim = GE_RGB(150, 140, 180);
    int a = t < 30 ? t * 255 / 30 : 255;
    ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(a, 8, 4, 16), 0);
    if (t < 30) return;
    const char *title = "END OF DEMO";
    ge_text_outlined(fb, (fb->w - ge_text_width(title, 3)) / 2, 16, title, gold, ink, 3);
    const char *sub = "WHAT THE PARAGON LEARNED ABOUT YOU";
    ge_text_outlined(fb, (fb->w - ge_text_width(sub, 1)) / 2, 50, sub, GE_RGB(230, 220, 250), ink, 1);
    ge_fill_rect(fb, 40, 62, fb->w - 80, 1, GE_RGB(190, 120, 255), 0);
    int shown = (t - 30) / 20;                                   /* the lines appear one at a time */
    for (int i = 0; i < nfacts && i < shown; i++) ge_text_outlined(fb, 44, 72 + i * 15, facts[i], GE_RGB(255, 236, 190), ink, 1);
    if (nfacts == 0 && shown > 0) ge_text_outlined(fb, 44, 72, "NOT ENOUGH SEEN YET. IT WILL KEEP WATCHING.", GE_RGB(255, 236, 190), ink, 1);
    char buf[96];
    snprintf(buf, sizeof buf, "DUELS %u   PARAGON KNOWS %d%% OF YOU   TECHNIQUES TAKEN %d", (unsigned)m->duels, (int)(ge_model_confidence(m) * 100.0f), popcount(gifts));
    ge_text(fb, (fb->w - ge_text_width(buf, 1)) / 2, 178, buf, dim, 1);
    snprintf(buf, sizeof buf, "ITS LEVEL: %d / 100", learn);
    ge_text(fb, (fb->w - ge_text_width(buf, 1)) / 2, 191, buf, dim, 1);
    if (t > 120 && ((t / 24) & 1) == 0) {
        const char *go = "PRESS A TO WAKE AT THE BENCH";
        ge_text_outlined(fb, (fb->w - ge_text_width(go, 1)) / 2, 214, go, GE_WHITE, ink, 1);
    }
}

/* ---- the hall -------------------------------------------------------------
 * "The Still Hall": the last room of the demo, built for the Paragon. Long, low and symmetrical, cold stone,
 * pillars with gold capitals, narrow windows that look out onto its own pattern (the field of eyes), and
 * behind the place where it waits, one great almond-shaped window: an eye, watching the door you came in by.
 * Everything here is placeholder art; the layout and the motif are the design. */

static void almond(GeImage *fb, GePattern *pat, int cx, int cy, int w, int h, uint32_t frame, uint32_t tick) {
    for (int dy = -h - 2; dy <= h + 2; dy++) {
        float t = 1.0f - (float)abs(dy) / (float)(h + 2);
        if (t <= 0.0f) continue;
        int fw = (int)((float)(w + 3) * sqrtf(t));
        ge_fill_rect(fb, cx - fw, cy + dy, fw * 2, 1, frame, 0);
        float ti = 1.0f - (float)abs(dy) / (float)h;
        if (ti <= 0.0f) continue;
        int iw = (int)((float)w * sqrtf(ti));
        if (pat) ge_pattern_draw_rect(pat, fb, cx - iw, cy + dy, iw * 2, 1, (int)tick);
    }
}

void paragon_draw_hall(GeImage *fb, int camx, int camy, uint32_t tick, int col, int width, int top_row, int floor_row) {
    /* everything is placed from the hall's first column, so the hall can be moved in the editor */
    int x0 = col * GE_TILE - camx, x1 = (col + width) * GE_TILE - camx;
    if (x1 < 0 || x0 > fb->w) return;
    int top = top_row * GE_TILE - camy, floor = floor_row * GE_TILE - camy, h = floor - top;
    const uint32_t stone = GE_RGB(30, 26, 42), joint = GE_RGB(20, 17, 30), pillar = GE_RGB(62, 56, 80), gold = GE_RGB(200, 160, 80);
    GePattern *pat = encounter_pattern("PARAGON");
    ge_fill_rect(fb, x0, top, x1 - x0, h, stone, 0);
    for (int y = top + 11; y < floor; y += 12) ge_fill_rect(fb, x0, y, x1 - x0, 1, joint, 0);         /* courses */
    for (int row = 0, y = top; y < floor; row++, y += 12)
        for (int x = x0 + (row & 1) * 12; x < x1; x += 24) ge_fill_rect(fb, x, y, 1, 11, joint, 0);  /* staggered joints */
    /* narrow windows onto the field of eyes */
    static const int windows[] = { 6, 14, 30 };
    for (int i = 0; i < 3; i++) {
        int wx = (col + windows[i]) * GE_TILE + 8 - camx;
        if (wx < -20 || wx > fb->w + 20) continue;
        ge_fill_rect(fb, wx - 9, top + 14, 18, 52, gold, 0);
        ge_pattern_draw_rect(pat, fb, wx - 7, top + 18, 14, 46, (int)tick);
        for (int k = 0; k < 7; k++) ge_fill_rect(fb, wx - 7 + k, top + 14 + (k < 4 ? 4 - k : k - 3), 14 - 2 * k, 1, gold, 0);  /* pointed arch */
        ge_fill_rect(fb, wx - 1, top + 18, 2, 46, gold, 0);                                              /* mullion */
    }
    /* the great eye behind its place */
    int ex = (col + 22) * GE_TILE + 8 - camx;
    if (ex > -60 && ex < fb->w + 60) {
        almond(fb, pat, ex, top + 40, 40, 22, gold, tick);
        ge_fill_rect(fb, ex - 2, top + 22, 4, 37, GE_RGB(8, 4, 12), 0);   /* the slit, like its own mask */
        ge_fill_rect(fb, ex - 1, top + 26, 2, 29, GE_RGB(255, 210, 80), 0);
    }
    /* pillars, with candles at their feet */
    static const int pillars[] = { 2, 10, 18, 26, 33 };
    for (int i = 0; i < 5; i++) {
        int px = (col + pillars[i]) * GE_TILE + 8 - camx;
        if (px < -12 || px > fb->w + 12) continue;
        ge_fill_rect(fb, px - 6, top, 12, h, pillar, 0);
        ge_fill_rect(fb, px - 6, top, 2, h, GE_RGB(84, 76, 104), 0);
        ge_fill_rect(fb, px + 4, top, 2, h, GE_RGB(44, 40, 60), 0);
        ge_fill_rect(fb, px - 8, top, 16, 4, gold, 0);
        ge_fill_rect(fb, px - 8, floor - 5, 16, 5, GE_RGB(52, 46, 70), 0);
        ge_fill_rect(fb, px - 8, floor - 6, 16, 1, gold, 0);
        int flick = (int)((tick / 5 + (uint32_t)i * 3) % 3);
        ge_fill_rect(fb, px + 10, floor - 9, 2, 6, GE_RGB(230, 225, 210), 0);
        ge_fill_rect(fb, px + 10, floor - 12 - flick, 2, 3 + flick, flick ? GE_RGB(255, 200, 90) : GE_RGB(255, 240, 170), 0);
    }
    /* a gold threshold set in the floor where it stands */
    int tx = (col + 18) * GE_TILE - camx;
    ge_fill_rect(fb, tx, floor + 1, 9 * GE_TILE, 2, gold, 0);
    for (int k = 0; k <= 9; k++) ge_fill_rect(fb, tx + k * GE_TILE, floor + 1, 1, 5, gold, 0);
}
