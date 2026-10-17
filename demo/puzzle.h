#ifndef PUZZLE_H
#define PUZZLE_H

/* The puzzle pieces of the room being played, while it is played: levers and plates switch wires, gates
 * follow their wire, crates get pushed and fall, cracked stone breaks, keys open locked gates, relics are
 * found. Analogy: the house's wiring and furniture. The world file is the floor plan; this is the lights
 * actually going on when you flip a switch, and the sofa where you last shoved it.
 *
 * Gates, crates and stone are cells of the tilemap, so the hero stands on them, wall-jumps off them and
 * is stopped by them with no extra collision code; this module only changes the cells. */

#include "world.h"
#include "ge/ge_input.h"

/* What the session remembers about a room: once-levers pulled, keys and relics taken, locks opened, broken
 * stone, and crates left on plates. Toggle and timed levers start over each time you come into the room, and
 * so do crates, at their place as built or on the last plate they came to rest on: a crate shoved into a
 * corner can never lock a room for good, and a gate you solved with a crate stays solved. (Not saved between
 * sessions yet.) */
#define ROOM_MAX_BROKEN 256
typedef struct {
    bool done[ROOM_MAX_THINGS];
    int16_t broken[ROOM_MAX_BROKEN][2];
    int nbroken;
    bool crate_homed[ROOM_MAX_THINGS];         /* this crate came to rest on a plate: it starts there now */
    int16_t crate_home[ROOM_MAX_THINGS][2];
} RoomProgress;

typedef struct {
    bool on;                  /* levers and plates: this input is on */
    int timer;                /* timed lever: ticks left */
    int lockout;              /* levers, crates: ticks before the next hit counts (one swing = one hit) */
    int open_rows;            /* gates: rows open, from the bottom (a portcullis: it rises, and comes down top first) */
    int step;                 /* gates: ticks to the next row */
    int16_t col, row;         /* crates: the cell it is in */
    int push;                 /* crates: ticks the hero has been pushing against it */
    int dx, dy, px, speed;    /* crates: moving one cell: direction, pixels so far, pixels a tick */
    bool want;                /* gates: what its wire said last tick (open or shut) */
    int alert;                /* gates: ticks left of the arrow pointing at it, after it changed out of view */
} Piece;

/* What happened this tick, for the game to show, log and reward. */
typedef enum { PZ_LEVER, PZ_KEY, PZ_UNLOCK, PZ_RELIC, PZ_CRACK, PZ_SECRET, PZ_DOOR } PuzzleEventKind;
typedef struct { PuzzleEventKind kind; int thing; } PuzzleEvent;

#define PZ_MAX_CRACKS 32
typedef struct {
    Room *room;
    RoomProgress *prog;
    GeWorld *w;
    int hero;
    int *keys;                /* keys carried (the whole session's, shared by every room) */
    Piece p[ROOM_MAX_THINGS];
    struct { int16_t col, row; int8_t hits; int8_t lockout; } cracks[PZ_MAX_CRACKS];   /* stone hit once */
    int ncracks;
    int wire_on[10], wire_inputs[10];   /* per wire: inputs on, inputs in all */
    int sign_near, door_near;           /* the sign / door the hero is standing at, or -1 */
    int shot_cells[8], nshot_cells;     /* needles stopped against cells this tick, handled in puzzle_update */
    PuzzleEvent ev[16];
    int nev;
} Puzzle;

/* Set up the room's pieces as authored, plus what the session remembers (prog). The tilemap must be
 * freshly built from the room (room_build_tilemap). */
void puzzle_enter(Puzzle *pz, Room *r, RoomProgress *prog, GeWorld *w, int hero, int *keys);
/* Before the world update: a hero pushing a crate walks at the crate's pace. */
void puzzle_pre_update(Puzzle *pz, GeIntent *hero_intent);
/* After the world update: hits, plates, wires, gates, crates, pickups, doors. `in` is the player's input. */
void puzzle_update(Puzzle *pz, const GeInput *in);
/* A shot stopped against a solid cell (the engine's GE_EV_SHOT_TILE). */
void puzzle_shot_tile(Puzzle *pz, int owner, int cell);
void puzzle_draw(const Puzzle *pz, GeImage *fb, int camx, int camy, uint32_t tick);
/* over everything (with the HUD): an arrow at the screen's edge, in its wire's colour, toward a gate that just
 * opened or shut out of view, so a lever whose gate is far away still says what it did */
void puzzle_draw_hints(const Puzzle *pz, GeImage *fb, int camx, int camy, uint32_t tick);
/* the colour of a wire, for the editor too */
uint32_t puzzle_wire_color(int ch);
/* is this cell part of a puzzle piece (a gate, a crate)? Safe-ground checks skip those: they can vanish */
bool puzzle_owns_cell(const Puzzle *pz, int col, int row);

#endif
