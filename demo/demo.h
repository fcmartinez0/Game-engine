#ifndef DEMO_H
#define DEMO_H

#include "ge/ge_world.h"

/* Procedural art: the demo ships no image files, it draws its own sprite
 * sheets at start-up. Replace these with ge_image_load_pam() + your own clip
 * text to use real art. */
int  art_make_player(GeSprite *s);
int  art_make_goblin(GeSprite *s);
void art_make_tileset(GeImage *tiles);
void art_add_layers(GeWorld *w);

/* Level layout. */
#define LEVEL_W 160
#define LEVEL_H 15
void level_build(GeWorld *w);
extern const int level_enemy_cols[];
extern const int level_enemy_count;

#endif
