#ifndef DEMO_H
#define DEMO_H

#include "ge/ge_fight.h"
#include "ge/ge_world.h"

#define SCREEN_W 400
#define SCREEN_H 240

/* ---- art (art.c, backdrop.c) --------------------------------------------
 * The demo ships no image files: every sheet is drawn in code at start-up.
 * Swap any of them for ge_sprite_load("x.pam", "x.anim") to use real art. */

/* Humanoid sheet layout (56x32 cells, 8 per row), shared by the hero, the
 * duelists and the boss, so their clip texts can use the same cell numbers.
 * The body stands at x = 20 so the sword has 35 pixels of room in front. */
#define HUM_CELL_W 56
#define HUM_CELL_H 32
#define HUM_BODY_X 20
enum {
    H_IDLE0, H_IDLE1, H_RUN0, H_RUN1, H_RUN2, H_RUN3, H_JUMP, H_FALL,
    H_S1A, H_S1B, H_S1C, H_S2A, H_S2B, H_S2C, H_UPA, H_UPB,
    H_UPC, H_DNA, H_DNB, H_DNC, H_WALL, H_DASH, H_HURT, H_DEAD,
    H_THA, H_THB, H_THC, H_SP0, H_SP1, H_SP2, H_SP3, H_HEAL0,
    H_HEAL1, H_CROUCH, H_BLOCK, H_BLOCKLO, H_JABA, H_JABB, H_HVA, H_HVB,
    H_HVC, H_HVD, H_KICK, H_CSA, H_CSB, H_SWA, H_SWB, H_RISE,
    H_GRAB, H_WIN, H_HURTAIR, H_JH, H_TRA, H_TRB, H_TRC, H_JK,
    H_COUNT
};

/* projectile sheet (16x16 cells, origin at the centre) */
enum { FX_NEEDLE0, FX_NEEDLE1, FX_GLOB0, FX_GLOB1, FX_ROCK, FX_WAVE0, FX_WAVE1, FX_ORB0, FX_ORB1, FX_SPEAR, FX_DUST };

/* tileset cells */
enum { TV_GRASS = 0, TV_DIRT = 1, TV_PLANK = 2, TV_DIRT2 = 3, TV_SPIKE = 4, TV_STONE = 5, TV_STONE_TOP = 6,
       TV_CRACK = 7, TV_GATE = 8, TV_CRATE = 9, TV_LOCK = 10, TV_BLANK = 11 /* see-through: a crate moving in */, TV_COUNT = 12 };
#define TV_FIRST_PUZZLE TV_CRACK    /* cells from here on are drawn after the others, from their own random numbers */

typedef struct {
    GeSprite hero, paragon, brawler, fencer, warden, crawler, bat, spitter, fx;
} Sprites;

void art_make_sheets(Sprites *s);     /* images only; chars_init() parses the clips */
void art_free(Sprites *s);
void art_make_tileset(GeImage *tiles);
void art_puzzle_tiles(GeImage *tiles);   /* the cells from TV_FIRST_PUZZLE on (called by art_make_tileset) */
void art_add_layers(GeWorld *w);        /* the forest */
void art_add_cave_layers(GeWorld *w);   /* the caves under it */
void art_draw_bench(GeImage *fb, int x, int y);   /* x = centre, y = floor */

/* ---- characters (chars.c): clips, platformer defs, fighter defs ---------- */
typedef struct {
    GeActorDef hero, crawler, bat, spitter, grunt, duelist, warden;
    GeShotDef needle, glob, spear, shockwave;                   /* overworld shots */
    GeShotDef f_needle, f_orb, f_rock, f_dust;                  /* fighting-game shots */
    GeFighterDef f_hero, f_brawler, f_fencer, f_paragon;
} Chars;

int chars_init(Chars *c, Sprites *s);  /* 0, or prints what failed */
int chars_parse(Sprites *s);           /* the two halves of chars_init: built-in clips, */
int chars_build(Chars *c, Sprites *s);  /* then the defs (clip lookups by name)          */

/* ---- the Paragon (paragon.c) ---------------------------------------------
 * The final boss: a fighter that has been watching the whole journey. See docs/PARAGON.md. */
#include "ge/ge_model.h"
#include "ge/ge_paragon.h"

enum { GIFT_GROBB = 1, GIFT_SILKE = 2, GIFT_MOSSHIDE = 4, GIFT_CHAMPION = 8, GIFT_THORNWARDEN = 16 };
#define PARAGON_CLOCK_TICKS (75 * 60)           /* the finisher fires after this long, whatever happens */

/* The Paragon's fighter def: the hero's move list, undying, plus what it took from each duelist beaten. */
void paragon_build_def(const Chars *c, Sprites *s, unsigned gifts, GeFighterDef *out);
void paragon_forget_base(void);
int  paragon_learn_level(const GePlayerModel *m, unsigned gifts);
/* Cross-session memory: a text file next to the game. */
void paragon_set_profile_path(const char *path);
const char *paragon_profile_path(void);
void paragon_profile_load(GePlayerModel *m);
void paragon_profile_save(const GePlayerModel *m);
/* the final room's back wall: columns col .. col+w-1, its ceiling's underside at row top, its floor at row floor */
void paragon_draw_hall(GeImage *fb, int camx, int camy, uint32_t tick, int col, int w, int top, int floor);
void paragon_draw_report(GeImage *fb, const char facts[][72], int nfacts, const GePlayerModel *m, int learn, unsigned gifts, int t);

/* ---- encounters (encounter.c): each duelist's EarthBound-style pattern ---- */
GePattern *encounter_pattern(const char *duelist_name);   /* a default one for unknown names */
void encounter_draw_transition(GeImage *fb, GePattern *p, int t, int cx, int cy, uint32_t tick);
void encounter_draw_reveal(GeImage *fb, GePattern *p, int t, uint32_t tick);
void encounter_free(void);

/* ---- the mods folder (mods.c): art, clips and move lists from files ------ */
#define MOD_REPORT_LINES 10
typedef struct { char lines[MOD_REPORT_LINES][72]; int nlines, loaded, errors; } ModReport;
const char *mods_dir(void);
void mods_apply_sprites(Sprites *s, ModReport *r);     /* after chars_parse, before chars_build */
void mods_apply_moves(Chars *c, Sprites *s, ModReport *r); /* after chars_build */
int  mods_export(Sprites *s, Chars *c);                /* built-in files into mods/_export/; returns files written */

/* ---- the world (world.h / world.c): rooms, tiles and things as data ---- */
#include "world.h"
#define GROUND_ROW 12               /* the Gloamwood room's ground row (the --autoplay bot's landmarks use it) */
enum { EN_CRAWLER, EN_BAT, EN_SPITTER, EN_GRUNT, EN_DUELIST, EN_BOSS };   /* entity kinds in the overworld */

#endif
