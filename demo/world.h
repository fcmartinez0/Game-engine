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
 *   ^  spikes           C  cracked stone: breaks when slashed or hit by a needle */
#define TILE_CHARS ".#ST=^C"

typedef enum {
    TH_START,       /* where the hero appears when the room is entered other than by a door */
    TH_BENCH,       /* rest: health back, respawn point, the room's enemies return */
    TH_DOOR,        /* UP to go through: name = this door's id, target = "room:door" */
    TH_SIGN,        /* text shown when you stand near it */
    TH_CRAWLER, TH_BAT, TH_SPITTER, TH_GRUNT,
    TH_DUELIST,     /* challenges you to a duel: name, style (brawler/fencer/paragon), level, rounds */
    TH_BOSS,        /* Thornwarden; fought inside an arena */
    TH_ARENA,       /* the boss room: w tiles wide from col; a = the gate's column, b = the far door's; h = door height */
    TH_HALL,        /* the Paragon's hall: indoors, w tiles wide from col, h tiles high above row */
    TH_LEVER,       /* input: hit it to switch its channel on (time > 0: it springs back after that many ticks) */
    TH_PLATE,       /* input: on while the hero, a crate or an enemy stands on it */
    TH_GATE,        /* output: w x h solid tiles, gone while its channel is on (invert: the other way round) */
    TH_CRATE,       /* push it by walking into it; it falls; it holds plates down; it rests on spikes */
    TH_KEY,         /* pick it up... */
    TH_LOCK,        /* ...and walk into this w x h door to open it */
    TH_RELIC,       /* a collectible: found once, counted */
    TH_COUNT
} ThingKind;

enum { STYLE_BRAWLER, STYLE_FENCER, STYLE_PARAGON };
typedef enum { BD_FOREST, BD_CAVE, BD_COUNT } Backdrop;

typedef struct {
    uint8_t kind;
    int16_t col, row;          /* the cell it stands in (its feet are on the bottom edge of the cell) */
    int16_t w, h;              /* gates, locks, arenas, halls */
    int8_t ch;                 /* inputs and gates: channel 1-9 (0 = none) */
    int8_t need;               /* gate: how many inputs on its channel must be on (default 1) */
    bool invert;               /* gate: solid while the channel is on */
    int16_t time;              /* lever: ticks it stays on (0 = it toggles) */
    int8_t style, level, rounds;   /* duelist */
    int16_t a, b;              /* arena: gate column, far-door column */
    char name[24];             /* duelist / boss / relic name, door id */
    char target[32];           /* door: "room:door" */
    char text[64];             /* sign */
} Thing;

typedef struct {
    char name[24];
    int w, h;
    uint8_t backdrop;          /* Backdrop */
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
char room_tile(const Room *r, int col, int row);    /* '.' outside */
void room_set_tile(Room *r, int col, int row, char t);
int  room_find(const Room *r, int kind, int from);  /* index of the next thing of a kind at or after `from`, -1 if none */
int  room_add_thing(Room *r, Thing t);              /* index, -1 if the room is full */
void room_remove_thing(Room *r, int i);

/* The tilemap the game plays: collision types plus a picture per cell (TV_* in demo.h). */
void room_build_tilemap(const Room *r, GeTilemap *m);

/* The world that ships in the game: the Gloamwood and the rest, made in code. */
void world_builtin(World *w);

/* The text format (see world.c for a full example). Load returns 0, or a line number > 0 with a message in
 * err; on failure *w is left empty. */
int  world_save(const World *w, const char *path);
int  world_load(World *w, const char *path, char *err, int errlen);
bool world_equal(const World *a, const World *b);   /* for tests: same rooms, tiles and things */

#endif
