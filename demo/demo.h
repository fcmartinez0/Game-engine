#ifndef DEMO_H
#define DEMO_H

#include "ge/ge_fight.h"
#include "ge/ge_world.h"

#define SCREEN_W 400
#define SCREEN_H 240

/* ---- art (art.c, backdrop.c) --------------------------------------------
 * The demo ships no image files: every sheet is drawn in code at start-up.
 * Swap any of them for ge_sprite_load("x.pam", "x.anim") to use real art. */

/* Humanoid sheet layout (32x32 cells, 8 per row), shared by the hero, the
 * duelists and the boss, so their clip texts can use the same cell numbers. */
enum {
    H_IDLE0, H_IDLE1, H_RUN0, H_RUN1, H_RUN2, H_RUN3, H_JUMP, H_FALL,
    H_S1A, H_S1B, H_S1C, H_S2A, H_S2B, H_S2C, H_UPA, H_UPB,
    H_UPC, H_DNA, H_DNB, H_DNC, H_WALL, H_DASH, H_HURT, H_DEAD,
    H_THA, H_THB, H_THC, H_SP0, H_SP1, H_SP2, H_SP3, H_HEAL0,
    H_HEAL1, H_CROUCH, H_BLOCK, H_BLOCKLO, H_JABA, H_JABB, H_HVA, H_HVB,
    H_HVC, H_HVD, H_KICK, H_CSA, H_CSB, H_SWA, H_SWB, H_RISE,
    H_GRAB, H_WIN, H_HURTAIR, H_JH,
    H_COUNT
};

/* projectile sheet (16x16 cells, origin at the centre) */
enum { FX_NEEDLE0, FX_NEEDLE1, FX_GLOB0, FX_GLOB1, FX_ROCK, FX_WAVE0, FX_WAVE1, FX_ORB0, FX_ORB1, FX_SPEAR, FX_DUST };

/* tileset cells */
enum { TV_GRASS = 0, TV_DIRT = 1, TV_PLANK = 2, TV_DIRT2 = 3, TV_SPIKE = 4, TV_STONE = 5, TV_STONE_TOP = 6, TV_COUNT = 7 };

typedef struct {
    GeSprite hero, brawler, fencer, warden, crawler, bat, spitter, fx;
} Sprites;

void art_make_sheets(Sprites *s);     /* images only; chars_init() parses the clips */
void art_free(Sprites *s);
void art_make_tileset(GeImage *tiles);
void art_add_layers(GeWorld *w);
void art_draw_bench(GeImage *fb, int x, int y);   /* x = centre, y = floor */

/* ---- characters (chars.c): clips, platformer defs, fighter defs ---------- */
typedef struct {
    GeActorDef hero, crawler, bat, spitter, grunt, duelist, warden;
    GeShotDef needle, glob, spear, shockwave;                   /* overworld shots */
    GeShotDef f_needle, f_orb, f_rock, f_dust;                  /* fighting-game shots */
    GeFighterDef f_hero, f_brawler, f_fencer;
} Chars;

int chars_init(Chars *c, Sprites *s);  /* 0, or prints what failed */

/* ---- level (level.c) ---------------------------------------------------- */
#define LEVEL_W 178
#define LEVEL_H 15
#define GROUND_ROW 12
#define BOSS_COL0 154               /* boss arena: columns BOSS_COL0..BOSS_COL1 */
#define BOSS_COL1 175
#define GATE_COL 153

enum { EN_CRAWLER, EN_BAT, EN_SPITTER, EN_GRUNT, EN_DUELIST, EN_BOSS };
enum { DUEL_BRAWLER, DUEL_FENCER };

typedef struct {
    int kind, col, row;
    int variant;                    /* duelists: DUEL_* */
    int cpu_level, rounds;          /* duelists: CPU strength 0-3, rounds to win */
    const char *name;
} Spawn;

extern const Spawn level_spawns[];
extern const int level_nspawns;
extern const int level_benches[];  /* columns */
extern const int level_nbenches;

void level_build(GeWorld *w);
void level_set_gate(GeWorld *w, bool closed);

#endif
