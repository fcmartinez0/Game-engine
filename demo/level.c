#include "demo.h"

const int level_enemy_cols[] = { 22, 41, 55, 66, 82, 94, 104, 118, 130, 146 };
const int level_enemy_count = (int)(sizeof level_enemy_cols / sizeof level_enemy_cols[0]);

static void fill(GeTilemap *m, int x0, int x1, int y0, int y1, uint8_t t) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) m->type[y * m->w + x] = t;
}

void level_build(GeWorld *w) {
    GeTilemap *m = &w->map;
    ge_tilemap_create(m, LEVEL_W, LEVEL_H);

    fill(m, 0, LEVEL_W - 1, 12, 14, GE_TILE_SOLID);               /* ground */
    fill(m, 34, 36, 12, 14, GE_TILE_EMPTY);                       /* three pits */
    fill(m, 72, 74, 12, 14, GE_TILE_EMPTY);
    fill(m, 110, 112, 12, 14, GE_TILE_EMPTY);
    fill(m, 50, 53, 10, 11, GE_TILE_SOLID);                       /* step */
    fill(m, 88, 91, 10, 11, GE_TILE_SOLID);                       /* staircase */
    fill(m, 92, 93, 9, 11, GE_TILE_SOLID);
    fill(m, 18, 23, 9, 9, GE_TILE_ONEWAY);                        /* one-way platforms: jump up through, press down+jump to drop */
    fill(m, 33, 37, 9, 9, GE_TILE_ONEWAY);
    fill(m, 60, 66, 9, 9, GE_TILE_ONEWAY);
    fill(m, 62, 64, 6, 6, GE_TILE_ONEWAY);
    fill(m, 71, 75, 9, 9, GE_TILE_ONEWAY);
    fill(m, 100, 104, 9, 9, GE_TILE_ONEWAY);
    fill(m, 106, 109, 7, 7, GE_TILE_ONEWAY);
    fill(m, 125, 131, 9, 9, GE_TILE_ONEWAY);

    /* pick a visual tile for each collision tile */
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            int t = m->type[y * m->w + x];
            if (t == GE_TILE_ONEWAY) m->vis[y * m->w + x] = 2;
            else if (t == GE_TILE_SOLID) {
                bool open_above = y == 0 || m->type[(y - 1) * m->w + x] != GE_TILE_SOLID;
                m->vis[y * m->w + x] = open_above ? 0 : ((x * 7 + y * 13) % 5 == 0 ? 3 : 1);
            }
        }
}
