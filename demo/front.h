#ifndef FRONT_H
#define FRONT_H

/* The screens around the game: the title (after the opening picture), the pause menu (START), the story before
 * a new game, and the credits after the end. Analogy: the cover, the bookmark ribbon and the last page of a book;
 * main.c decides what each choice does, this file only shows them and reads the buttons. */

#include "ge/ge_gfx.h"
#include "ge/ge_input.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum { FI_CONTINUE, FI_NEW, FI_DIFFICULTY, FI_QUIT, FI_RESUME, FI_EDITOR, FI_TITLE, FI_COUNT } FrontItem;

typedef struct {
    uint8_t items[6];
    int n, sel;
    int confirm;              /* the row waiting for a second press (NEW GAME over a save), or -1 */
    int confirm_t;            /* ticks left to give it */
    int t;                    /* ticks open */
    bool dirty;               /* the bottom screen should be drawn again */
} FrontMenu;

void front_menu_open(FrontMenu *m, const FrontItem *items, int n);
/* UP/DOWN move, A/B/START choose, LEFT/RIGHT step a setting. Returns the item chosen (a row that needs confirming
 * only on its second press), or -1; *step gets -1 / +1 when LEFT / RIGHT change a setting row (the item comes back
 * too). `needs_confirm(item)` says which items ask twice. */
int  front_menu_input(FrontMenu *m, const GeInput *in, bool (*needs_confirm)(int item), int *step);
/* The same from a tap on the bottom screen (its buttons as front_draw_menu_bottom draws them). */
int  front_menu_touch(FrontMenu *m, int x, int y, bool (*needs_confirm)(int item));

/* labels: the difficulty's name; the screens draw over whatever is already in fb */
void front_draw_title(GeImage *fb, const FrontMenu *m, const char *difficulty, const char *note, uint32_t tick);   /* note: a word under the choices, or NULL */
void front_draw_pause(GeImage *fb, const FrontMenu *m, const char *difficulty, const char *where);
void front_draw_menu_bottom(GeImage *fb, const FrontMenu *m, const char *difficulty, bool title);

/* The note before a new game (in place of a story, for now): lines one after another. Done after FRONT_STORY_TICKS, or
 * when skipped. */
#define FRONT_STORY_TICKS 780
void front_draw_story(GeImage *fb, int t);

typedef struct {
    uint32_t play_ticks;
    int deaths, duels_won, kills, heals;
    int relics_found, relics;
    int paragon_level;        /* 0-100: how well it knows you */
} FrontStats;

/* The credits after the end: they roll up, then wait for a press. */
#define FRONT_CREDITS_ROLL 1500
void front_draw_credits(GeImage *fb, const FrontStats *s, int t);

#endif
