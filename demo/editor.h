#ifndef EDITOR_H
#define EDITOR_H

/* The world editor: build rooms on the bottom screen with the stylus (the mouse on a PC), see them at full size
 * on the top screen, and play what you built with one press of START. Analogy: a model railway's layout table.
 * The bottom screen is the table seen from above, where you lay the track and set out the signals; the top
 * screen is a camera down at the trains' height; START runs a train over what you just laid, and START again
 * lifts you back to the table.
 *
 * What it changes is the world as data (world.h): the game rebuilds what it plays from that, and SAVE writes the
 * rooms you changed to mods/world.txt.
 *
 * The bottom screen, top to bottom:
 *   y 0-23     UNDO | ERASER | ROOM | SAVE | PLAY
 *   y 24-191   the map, 8 px a tile (40 x 21 tiles); the D-pad scrolls it
 *   y 192-239  four tabs of brushes (GROUND, PUZZLE, EXPLORE, FOES) and the brushes themselves; or, while a
 *              piece is selected, its settings
 * One rule for the stylus: what is under it when it touches down decides. A piece: a tap selects it, a drag
 * moves it. Anything else: the brush you picked paints (tiles) or places (pieces). */

#include "demo.h"
#include "puzzle.h"
#include "ge/ge_input.h"

typedef struct {
    World *wd;
    GeWorld *w;                  /* the game's world: its map shows the room being edited, its camera is the top screen's */
    const Sprites *spr;          /* creatures are shown with their own art */
    void (*show_room)(int room); /* make it the game's room, as built and at rest: tiles, backdrop, pieces, nobody in it */
    void (*log)(const char *line);   /* the play log */
    bool *world_broken;          /* world.txt didn't load: it is renamed before the first save, never written over */
    const char *world_problem;   /* ...and why (shown when the editor opens) */
    const char *world_file;      /* the file the game was started with (--world), else NULL: mods/world.txt */
} EditorHost;

typedef enum { ED_STAY, ED_PLAY } EditorAction;

/* Start editing `room`; the TEST FROM HERE marker goes in the cell (col, row). */
void editor_open(const EditorHost *h, int room, int col, int row);
EditorAction editor_update(const GeInput *in);
void editor_draw_top(GeImage *fb);                 /* over the room the game drew with the editor's camera */
bool editor_draw_bottom(GeImage *fb);              /* false: nothing changed since the last time */
void editor_marker(int *room, int *col, int *row); /* where PLAY puts the hero */
bool editor_room_edited(int room);                 /* changed since editing began: what the session knew about it is stale */
void editor_forget_edits(void);                    /* editing is over (STOP EDITING): the next session starts clean */
bool editor_unsaved(void);
void editor_autosave(void);                        /* mods/world.autosave.txt, if anything changed since the last save or autosave */

/* The bottom screen while a test play runs: BACK TO EDITING, RESTART ROOM, STOP EDITING, and the wires' lights. */
typedef enum { TP_NONE, TP_EDIT, TP_RESTART, TP_STOP } TestAction;
TestAction editor_test_touch(const GeInput *in);
bool editor_draw_test(GeImage *fb, const Puzzle *pz, bool force);   /* force: draw it even if nothing changed */

#endif
