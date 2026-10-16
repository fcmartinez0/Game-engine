/* The Paragon, the demo's final boss: how its fighter def is assembled from what it has watched and taken,
 * where its memory is kept, and the end-of-demo report. The brain itself lives in the engine
 * (ge_paragon.c, ge_model.c): this file is the game-specific part. */

#include "demo.h"
#include "ge/ge_font.h"

#include <stdio.h>
#include <string.h>

/* ---- the def ------------------------------------------------------------- */

static int popcount(unsigned v) { int n = 0; for (; v; v >>= 1) n += (int)(v & 1u); return n; }

void paragon_build_def(const Chars *c, Sprites *s, unsigned gifts, GeFighterDef *out) {
    static int base_dmg[GE_SPRITE_MAX_CLIPS];
    static bool have_base;
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
    if (gifts & GIFT_THORNWARDEN) out->moves[n++] = (GeMove){ .input = "623S", .clip = "f_charge", .level = 3, .ai = GE_AI_RUSH };
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
