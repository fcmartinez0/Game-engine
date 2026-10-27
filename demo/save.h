#ifndef SAVE_H
#define SAVE_H

/* The saved game: what a player has done, so CONTINUE on the title screen picks up where they left off. It is
 * written at every bench, after every duel won, boss felled or relic found, and when the game is left; you come
 * back at the last bench you rested at, with everything you did kept. Analogy: a bookmark that also remembers
 * which pages you folded down. A plain text file like the world and the Paragon's memory (save.txt). */

#include "world.h"
#include "puzzle.h"

#include <stdbool.h>
#include <stdint.h>

#define SAVE_VERSION 1

typedef struct {
    char name[24];
    int nthings;                       /* the room's pieces when it was saved, and their kinds: a room changed */
    uint8_t kinds[ROOM_MAX_THINGS];    /* since then (a different world.txt) can't be matched up, and starts fresh */
    bool beaten[ROOM_MAX_THINGS];      /* creatures, duelists, the boss */
    RoomProgress progress;             /* levers pulled, relics and keys taken, locks opened, stone broken, crates */
} SaveRoom;

typedef struct {
    char bench_room[24];               /* where you come back */
    int bench_col, bench_row;
    int bonus_masks, bonus_ammo, keys;
    int kills, deaths, duels_won, duels_lost, heals;
    uint32_t play_ticks;               /* time played, at 60 a second */
    bool ending_seen;                  /* the credits have rolled once */
    int nrooms;
    SaveRoom rooms[WORLD_MAX_ROOMS];
} SaveGame;

/* 0, or < 0 if it couldn't be written. Writes path.tmp, then swaps it in (the old one is kept as path.bak),
 * so a save cut off half way (the power gone) never leaves a broken file behind. */
int  save_write(const SaveGame *s, const char *path);
/* 0, -1 if there is no save, or the line number of something it couldn't read (err says what). When path
 * is missing but path.bak or path.tmp is whole, that one is read. */
int  save_read(SaveGame *s, const char *path, char *err, int errlen);
bool save_exists(const char *path);
void save_remove(const char *path);   /* NEW GAME: the save, its .bak and any .tmp */
const SaveRoom *save_find_room(const SaveGame *s, const char *name);

#endif
