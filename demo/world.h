#ifndef WORLD_H
#define WORLD_H

/* The world as data: rooms made of tiles and "things" (benches, enemies, duelists, levers, gates, crates,
 * doors...). The game builds what it plays from this, the editor changes it, and it is saved as a text
 * file you can also read and edit by hand (mods/world.txt). Analogy: a stage set described on paper,
 * every prop with its place and its settings, that the stage crew can rebuild exactly any night. */

#include "ge/ge_world.h"

#include <stdbool.h>
#include <stdint.h>

#define WORLD_MAX_ROOMS 16
#define ROOM_MAX_THINGS 96
#define ROOM_MAX_W 512
#define ROOM_MAX_H 128
#define ROOM_MIN_W 25              /* a screen's width in tiles */
#define ROOM_MIN_H 15              /* a screen's height */

/* Tiles, one character per cell:
 *   .  empty            #  ground (grass on top, earth below)   S  stone (a dressed top edge where open above)
 *   T  stone, always the top edge (a floor under a wall)          =  plank: stand on it, jump up through it
 *   ^  spikes           C  cracked stone: breaks after two hits (slash any way, spin, needle)
 *   H  hidden stone: looks like S, breaks like C (a real secret) */
#define TILE_CHARS ".#ST=^CH"

typedef enum {
    TH_START,       /* where the hero appears when the room is entered other than by a door */
    TH_BENCH,       /* rest: health back, respawn point, the room's enemies return */
    TH_DOOR,        /* UP to go through: name = this door's id, target = "room:door" */
    TH_SIGN,        /* text shown when you stand near it */
    TH_CRAWLER, TH_BAT, TH_SPITTER, TH_GRUNT,
    TH_DUELIST,     /* challenges you to a duel: name, style (brawler/fencer/paragon), level, rounds */
    TH_BOSS,        /* Avery The Cat; fought inside an arena */
    TH_ARENA,       /* the boss room: w tiles wide from col; its gate is the column before it, its far door the one after */
    TH_HALL,        /* the Paragon's hall: indoors, w tiles wide from col, h tiles high above row */
    TH_LEVER,       /* input: hit it to switch its wire on (mode: once / toggle / timed) */
    TH_PLATE,       /* input: on while the hero's feet or a crate are on it */
    TH_GATE,        /* output: w x h solid tiles, gone while its wire is on (invert: a bridge that appears instead;
                     * lock: opens for a key instead of a wire; plank: its cells are planks you can jump up through) */
    TH_CRATE,       /* push it by walking into it, or knock it with a slash or a needle; it falls; it holds plates
                     * down; it rests on spikes; it goes back to its place when you come back into the room */
    TH_KEY,         /* pick it up, then walk into a locked gate */
    TH_RELIC,       /* a collectible, found once: maybe a mask, a needle or a silk segment */
    TH_MARK,        /* a bookmark for the dev menu's warps (name); not seen in play */
    TH_COUNT
} ThingKind;

enum { LEVER_ONCE, LEVER_TOGGLE, LEVER_TIMED };                    /* Thing.mode for levers */
enum { GIVE_NONE, GIVE_MASK, GIVE_NEEDLE, GIVE_SILK, GIVE_COUNT };  /* Thing.gives for relics */

enum { STYLE_BRAWLER, STYLE_FENCER, STYLE_PARAGON };
typedef enum { BD_FOREST, BD_CAVE, BD_COUNT } Backdrop;

typedef struct {
    uint8_t kind;
    int16_t col, row;          /* the cell it stands in (its feet are on the bottom edge of the cell) */
    int16_t w, h;              /* gates, locks, arenas, halls */
    int8_t ch;                 /* inputs and gates: wire (channel) 1-9, 0 = none */
    int8_t need;               /* gate: how many inputs on its wire must be on: 1 = any, 0 = all of them */
    bool invert;               /* gate: solid while its wire is on (a bridge) */
    bool lock;                 /* gate: opens (for good) when you walk into it carrying a key */
    bool plank;                /* gate: its cells are planks, not bars */
    int8_t mode;               /* lever: LEVER_* */
    int16_t time;              /* lever: ticks a timed lever stays on */
    int8_t gives;              /* relic: GIVE_* */
    int8_t style, level, rounds;   /* duelist */
    char name[24];             /* duelist / boss / relic / mark name, door id */
    char target[48];           /* door: "room:door" */
    char text[64];             /* sign */
} Thing;

typedef struct {
    char name[24];             /* a-z, 0-9, - and _ (doors name rooms in "room:door") */
    int w, h;
    uint8_t backdrop;          /* Backdrop */
    bool custom;               /* from world.txt or changed in the editor: SAVE writes it (not saved itself) */
    char *tiles;               /* w * h characters, row by row */
    Thing things[ROOM_MAX_THINGS];
    int nthings;
} Room;

typedef struct {
    Room rooms[WORLD_MAX_ROOMS];
    int nrooms;
} World;

const char *thing_kind_name(int kind);       /* "lever", "gate", ... as in the file */
int  thing_kind_from_name(const char *s);    /* -1 if unknown */
Thing thing_default(int kind, int col, int row);   /* a new thing with sensible settings */

void world_free(World *w);
Room *world_add_room(World *w, const char *name, int width, int height, int backdrop);   /* empty: just a floor */
int  world_find_room(const World *w, const char *name);                                 /* -1 if none */
void room_resize(Room *r, int width, int height);   /* keeps what fits, new cells empty (the bottom row ground) */
/* Grow or shrink a room's space, the editor's WIDER/NARROWER/TALLER/SHORTER: a column (or row) is added as a copy
 * of the one beside it, or taken out, and the pieces beyond it move with their cells (those in a column or row that
 * goes are removed). False if the room would be too big or too small. */
bool room_insert_col(Room *r, int at);              /* a copy of column at-1 goes in at `at` */
bool room_delete_col(Room *r, int at);
bool room_insert_row(Room *r, int at);              /* a copy of row `at` goes in at `at`: things at or below move down */
bool room_delete_row(Room *r, int at);
int  room_inner_col(const Room *r);                 /* the rightmost column that isn't rock from top to bottom */
int  room_inner_row(const Room *r);                 /* the top row that isn't rock from side to side */
int  room_things_in_col(const Room *r, int col);    /* pieces that a deleted column would take with it */
int  room_things_in_row(const Room *r, int row);
char room_tile(const Room *r, int col, int row);    /* '.' outside */
void room_set_tile(Room *r, int col, int row, char t);
int  room_find(const Room *r, int kind, int from);  /* index of the next thing of a kind at or after `from`, -1 if none */
int  room_add_thing(Room *r, Thing t);              /* index, -1 if the room is full */
void room_remove_thing(Room *r, int i);

/* The tilemap the game plays: collision types plus a picture per cell (TV_* in demo.h). */
void room_build_tilemap(const Room *r, GeTilemap *m);
/* one cell as room_build_tilemap makes it (to restore a cell a gate or a crate leaves, or one just painted) */
void room_cell(const Room *r, int col, int row, uint8_t *type, uint8_t *vis);
bool world_name_ok(const char *s);                  /* a-z 0-9 - _, 1 to 23 characters */
int  arena_gate_col(const Thing *arena);             /* the column before it */
int  arena_exit_col(const Thing *arena);             /* the column after it */

/* The world that ships in the game: the Gloamwood and the rest, made in code. */
void world_builtin(World *w);

/* The text format (see world.c for a full example). Load returns 0, or a line number > 0 with a message in
 * err; on failure *w is left empty. */
int  world_save(const World *w, const char *path, bool only_custom);   /* 0, or < 0 if it couldn't be written */
/* Lay the rooms of src over dst: a room of the same name is replaced, a new one added (marked custom). src's rooms
 * are moved, not copied (src is left empty). Returns how many were replaced; *added gets how many were added. */
int  world_overlay(World *dst, World *src, int *added);
int  world_load(World *w, const char *path, char *err, int errlen);
bool world_equal(const World *a, const World *b);   /* for tests: same rooms, tiles and things */

#endif
