/* "Gloamwood": one Silksong-style slice, left to right.
 *
 *   0  bench, crawlers        26 spike pit (pogo the bat)    44 spitter on a ledge
 *   51 wall-jump shaft up to a plateau -- duelist 1 waits on top
 *   87 duelist 2              92 spike floor with planks     105 bench
 *   121 duelist 3             132 second spike pit + bats    146 the champion (best of 3)
 *   154-175 boss arena: the gate shuts behind you; the far door (176) opens when the boss falls
 *   178-211 the Paragon's hall: a bench at 182, the Paragon waits at 200
 */

#include "demo.h"

#include <stddef.h>

#define AUTO 255

const Spawn level_spawns[] = {
    { EN_CRAWLER, 13, GROUND_ROW, 0, 0, 0, NULL },
    { EN_CRAWLER, 21, GROUND_ROW, 0, 0, 0, NULL },
    { EN_BAT, 29, 7, 0, 0, 0, NULL },
    { EN_GRUNT, 40, GROUND_ROW, 0, 0, 0, NULL },
    { EN_SPITTER, 46, 10, 0, 0, 0, NULL },
    { EN_DUELIST, 63, 4, DUEL_BRAWLER, 1, 1, "GROBB" },
    { EN_CRAWLER, 76, GROUND_ROW, 0, 0, 0, NULL },
    { EN_BAT, 82, 8, 0, 0, 0, NULL },
    { EN_DUELIST, 87, GROUND_ROW, DUEL_FENCER, 1, 1, "SILKE" },
    { EN_GRUNT, 110, GROUND_ROW, 0, 0, 0, NULL },
    { EN_CRAWLER, 116, GROUND_ROW, 0, 0, 0, NULL },
    { EN_DUELIST, 121, GROUND_ROW, DUEL_BRAWLER, 2, 1, "MOSSHIDE" },
    { EN_CRAWLER, 127, GROUND_ROW, 0, 0, 0, NULL },
    { EN_SPITTER, 130, GROUND_ROW, 0, 0, 0, NULL },
    { EN_BAT, 134, 7, 0, 0, 0, NULL },
    { EN_BAT, 137, 8, 0, 0, 0, NULL },
    { EN_DUELIST, 146, GROUND_ROW, DUEL_FENCER, 3, 2, "THE CHAMPION" },
    { EN_BOSS, 168, GROUND_ROW, 0, 0, 0, "THORNWARDEN" },
    { EN_DUELIST, 200, GROUND_ROW, DUEL_PARAGON, 4, 1, "PARAGON" },
};
const int level_nspawns = (int)(sizeof level_spawns / sizeof level_spawns[0]);
const int level_benches[] = { 4, 105, 182 };
const int level_nbenches = 3;

static void fill(GeTilemap *m, int x0, int x1, int y0, int y1, uint8_t t, uint8_t vis) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            m->type[y * m->w + x] = t;
            m->vis[y * m->w + x] = vis;
        }
}

void level_set_gate(GeWorld *w, bool closed) {
    fill(&w->map, GATE_COL, GATE_COL, 9, 11, closed ? GE_TILE_SOLID : GE_TILE_EMPTY, TV_STONE);
}

void level_set_exit(GeWorld *w, bool closed) {
    fill(&w->map, EXIT_COL, EXIT_COL, 9, 11, closed ? GE_TILE_SOLID : GE_TILE_EMPTY, TV_STONE);
}

void level_build(GeWorld *w) {
    GeTilemap *m = &w->map;
    ge_tilemap_create(m, LEVEL_W, LEVEL_H);

    fill(m, 0, LEVEL_W - 1, GROUND_ROW, LEVEL_H - 1, GE_TILE_SOLID, AUTO);
    /* spike pit A, with a plank to land on */
    fill(m, 26, 31, GROUND_ROW, GROUND_ROW, GE_TILE_EMPTY, 0);
    fill(m, 26, 31, 13, 13, GE_TILE_SPIKE, TV_SPIKE);
    fill(m, 28, 29, 9, 9, GE_TILE_ONEWAY, TV_PLANK);
    /* the spitter's ledge */
    fill(m, 44, 47, 10, 11, GE_TILE_SOLID, TV_STONE);
    /* wall-jump shaft: a hanging wall on the left, the plateau's cliff on the right */
    fill(m, 51, 52, 0, 8, GE_TILE_SOLID, TV_STONE);
    fill(m, 56, 69, 4, 11, GE_TILE_SOLID, TV_STONE);
    /* spike floor with planks */
    fill(m, 92, 99, 11, 11, GE_TILE_SPIKE, TV_SPIKE);
    fill(m, 94, 95, 9, 9, GE_TILE_ONEWAY, TV_PLANK);
    fill(m, 97, 98, 9, 9, GE_TILE_ONEWAY, TV_PLANK);
    /* spike pit B, bridged only by bats (pogo) and one plank */
    fill(m, 132, 138, GROUND_ROW, GROUND_ROW, GE_TILE_EMPTY, 0);
    fill(m, 132, 138, 13, 13, GE_TILE_SPIKE, TV_SPIKE);
    fill(m, 135, 135, 10, 10, GE_TILE_ONEWAY, TV_PLANK);
    /* boss arena: a doorway in, a wall at the far end */
    fill(m, GATE_COL - 1, GATE_COL, 0, 8, GE_TILE_SOLID, TV_STONE);
    fill(m, EXIT_COL, EXIT_COL + 1, 0, 8, GE_TILE_SOLID, TV_STONE);
    level_set_exit(w, true);
    /* the Paragon's hall: a long low stone room, with a ceiling and a wall at the end */
    fill(m, HALL_COL0, LEVEL_W - 1, 0, 5, GE_TILE_SOLID, TV_STONE);
    fill(m, LEVEL_W - 2, LEVEL_W - 1, 0, GROUND_ROW - 1, GE_TILE_SOLID, TV_STONE);

    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            int i = y * m->w + x;
            bool open_above = y == 0 || m->type[i - m->w] != GE_TILE_SOLID;
            if (m->type[i] != GE_TILE_SOLID) continue;
            if (m->vis[i] == AUTO) m->vis[i] = open_above ? TV_GRASS : ((x * 7 + y * 13) % 5 == 0 ? TV_DIRT2 : TV_DIRT);
            else if (m->vis[i] == TV_STONE && open_above) m->vis[i] = TV_STONE_TOP;
        }
    /* the hall's floor is dressed stone, not grass */
    for (int x = HALL_COL0; x < LEVEL_W; x++) {
        m->vis[GROUND_ROW * m->w + x] = TV_STONE_TOP;
        for (int y = GROUND_ROW + 1; y < m->h; y++) m->vis[y * m->w + x] = TV_STONE;
    }
}
