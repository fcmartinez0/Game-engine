/* The saved game as a text file (save.h). Example:
 *
 *   fransdemo-save 1
 *   bench gloamwood 105 11
 *   relics 1 0
 *   keys 0
 *   stats 12 3 2 1 4 54000          (enemies defeated, deaths, duels won, lost, heals, ticks played)
 *   ending 0
 *   room gloamwood 47 0,4,4,5,...   (its pieces then, by kind, in order)
 *   beaten 7 12 19                  (pieces beaten, by number)
 *   done 3 5                        (levers pulled, relics and keys taken, locks opened)
 *   broken 48:9 48:10               (stone broken, column:row)
 *   crate 7 10:11                   (crate 7 rests on a plate at 10,11)
 *   endroom
 *   end
 *
 * The last line, "end", says the file is whole. */

#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- writing ------------------------------------------------------------------ */

static void put_list_start(FILE *f, const char *key, int *count) { if (*count == 0) fprintf(f, "%s", key); }
static void put_list_end(FILE *f, int *count) { if (*count > 0) fputc('\n', f); *count = 0; }

int save_write(const SaveGame *s, const char *path) {
    char tmp[256], bak[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    snprintf(bak, sizeof bak, "%s.bak", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    static char buf[16384];                    /* one write to the card */
    setvbuf(f, buf, _IOFBF, sizeof buf);
    fprintf(f, "fransdemo-save %d\n", SAVE_VERSION);
    fprintf(f, "# Fran's demo: your progress. The game writes this at benches and when you leave. Delete it to start over.\n");
    fprintf(f, "bench %s %d %d\n", s->bench_room[0] ? s->bench_room : "gloamwood", s->bench_col, s->bench_row);
    fprintf(f, "relics %d %d\n", s->bonus_masks, s->bonus_ammo);
    fprintf(f, "keys %d\n", s->keys);
    fprintf(f, "stats %d %d %d %d %d %u\n", s->kills, s->deaths, s->duels_won, s->duels_lost, s->heals, (unsigned)s->play_ticks);
    fprintf(f, "ending %d\n", s->ending_seen ? 1 : 0);
    for (int r = 0; r < s->nrooms && r < WORLD_MAX_ROOMS; r++) {
        const SaveRoom *sr = &s->rooms[r];
        int n = sr->nthings < 0 ? 0 : sr->nthings > ROOM_MAX_THINGS ? ROOM_MAX_THINGS : sr->nthings, c = 0;
        fprintf(f, "room %s %d", sr->name, n);
        for (int i = 0; i < n; i++) fprintf(f, "%c%d", i ? ',' : ' ', sr->kinds[i]);
        fputc('\n', f);
        for (int i = 0; i < n; i++) if (sr->beaten[i]) { put_list_start(f, "beaten", &c); fprintf(f, " %d", i); if (++c == 32) put_list_end(f, &c); }
        put_list_end(f, &c);
        for (int i = 0; i < n; i++) if (sr->progress.done[i]) { put_list_start(f, "done", &c); fprintf(f, " %d", i); if (++c == 32) put_list_end(f, &c); }
        put_list_end(f, &c);
        int nb = sr->progress.nbroken < 0 ? 0 : sr->progress.nbroken > ROOM_MAX_BROKEN ? ROOM_MAX_BROKEN : sr->progress.nbroken;
        for (int i = 0; i < nb; i++) {
            put_list_start(f, "broken", &c);
            fprintf(f, " %d:%d", sr->progress.broken[i][0], sr->progress.broken[i][1]);
            if (++c == 24) put_list_end(f, &c);
        }
        put_list_end(f, &c);
        for (int i = 0; i < n; i++)
            if (sr->progress.crate_homed[i]) fprintf(f, "crate %d %d:%d\n", i, sr->progress.crate_home[i][0], sr->progress.crate_home[i][1]);
        fprintf(f, "endroom\n");
    }
    fprintf(f, "end\n");
    if (fclose(f) != 0) { remove(tmp); return -2; }
    remove(bak);                               /* (rename won't replace a file on every system) */
    rename(path, bak);                         /* the last good save stays, just in case */
    if (rename(tmp, path) != 0) return -3;
    return 0;
}

/* ---- reading ------------------------------------------------------------------ */

/* the next word of a line: separated by spaces, tabs or commas */
static bool word(char **p, char *out, size_t n) {
    char *s = *p;
    while (*s == ' ' || *s == '\t' || *s == ',') s++;
    if (!*s) { *p = s; return false; }
    size_t k = 0;
    while (*s && *s != ' ' && *s != '\t' && *s != ',') { if (k + 1 < n) out[k++] = *s; s++; }
    out[k] = 0;
    *p = s;
    return true;
}
/* a whole number (and nothing else) */
static bool number(const char *s, long lo, long hi, long *v) {
    char *end;
    long x = strtol(s, &end, 10);
    if (end == s || *end || x < lo || x > hi) return false;
    *v = x;
    return true;
}
static bool pair(const char *s, int *a, int *b) {
    const char *colon = strchr(s, ':');
    if (!colon) return false;
    char left[16];
    size_t n = (size_t)(colon - s);
    if (n == 0 || n >= sizeof left) return false;
    memcpy(left, s, n); left[n] = 0;
    long x, y;
    if (!number(left, -32768, 32767, &x) || !number(colon + 1, -32768, 32767, &y)) return false;
    *a = (int)x; *b = (int)y;
    return true;
}

static int read_one(SaveGame *s, const char *path, char *err, int errlen) {
    memset(s, 0, sizeof *s);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, (size_t)errlen, "no save"); return -1; }
    static char line[1024];
    int ln = 0, rc = 0;
    bool whole = false;
    SaveRoom *room = NULL;
#define BAD(...) do { snprintf(err, (size_t)errlen, __VA_ARGS__); rc = ln > 0 ? ln : 1; goto done; } while (0)
    while (fgets(line, sizeof line, f)) {
        ln++;
        size_t n = strlen(line);
        if (n == sizeof line - 1 && line[n - 1] != '\n') BAD("a line too long");
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
        char *p = line, w[32], a[64];
        if (ln == 1) {
            long v;
            if (!word(&p, w, sizeof w) || strcmp(w, "fransdemo-save") || !word(&p, a, sizeof a) || !number(a, 1, 1000000, &v)) BAD("not a save file");
            if (v > SAVE_VERSION) BAD("made by a newer version of the game");
            continue;
        }
        if (whole) BAD("more after the end");
        if (!word(&p, w, sizeof w) || w[0] == '#') continue;
        long v[6];
        if (!strcmp(w, "bench")) {
            char c[16], r[16];
            if (!word(&p, a, sizeof a) || !world_name_ok(a) || !word(&p, c, sizeof c) || !word(&p, r, sizeof r) ||
                !number(c, 0, ROOM_MAX_W, &v[0]) || !number(r, 0, ROOM_MAX_H, &v[1])) BAD("expected: bench <room> <column> <row>");
            snprintf(s->bench_room, sizeof s->bench_room, "%.23s", a);
            s->bench_col = (int)v[0]; s->bench_row = (int)v[1];
        } else if (!strcmp(w, "relics") || !strcmp(w, "keys") || !strcmp(w, "ending") || !strcmp(w, "stats")) {
            int want = !strcmp(w, "relics") ? 2 : !strcmp(w, "stats") ? 6 : 1;
            for (int k = 0; k < want; k++)
                if (!word(&p, a, sizeof a) || !number(a, 0, k == 5 ? 0x7fffffffL : 1000000L, &v[k])) BAD("%s: expected %d numbers", w, want);
            if (!strcmp(w, "relics")) { s->bonus_masks = (int)v[0]; s->bonus_ammo = (int)v[1]; }
            else if (!strcmp(w, "keys")) s->keys = (int)v[0];
            else if (!strcmp(w, "ending")) s->ending_seen = v[0] != 0;
            else { s->kills = (int)v[0]; s->deaths = (int)v[1]; s->duels_won = (int)v[2]; s->duels_lost = (int)v[3]; s->heals = (int)v[4]; s->play_ticks = (uint32_t)v[5]; }
        } else if (!strcmp(w, "room")) {
            if (room) BAD("a room inside a room");
            if (s->nrooms >= WORLD_MAX_ROOMS) BAD("too many rooms");
            char c[16];
            if (!word(&p, a, sizeof a) || !world_name_ok(a) || !word(&p, c, sizeof c) || !number(c, 0, ROOM_MAX_THINGS, &v[0]))
                BAD("expected: room <name> <pieces> <kinds...>");
            if (save_find_room(s, a)) BAD("room %s twice", a);
            room = &s->rooms[s->nrooms++];
            snprintf(room->name, sizeof room->name, "%.23s", a);
            room->nthings = (int)v[0];
            for (int i = 0; i < room->nthings; i++) {
                if (!word(&p, c, sizeof c) || !number(c, 0, TH_COUNT - 1, &v[1])) BAD("room %s: %d kinds expected", room->name, room->nthings);
                room->kinds[i] = (uint8_t)v[1];
            }
            if (word(&p, c, sizeof c)) BAD("room %s: more kinds than its %d pieces", room->name, room->nthings);
        } else if (!strcmp(w, "beaten") || !strcmp(w, "done")) {
            if (!room) BAD("%s outside a room", w);
            bool *set = !strcmp(w, "beaten") ? room->beaten : room->progress.done;
            while (word(&p, a, sizeof a)) {
                if (!number(a, 0, room->nthings - 1, &v[0])) BAD("%s: '%s' is not one of the room's %d pieces", w, a, room->nthings);
                set[v[0]] = true;
            }
        } else if (!strcmp(w, "broken")) {
            if (!room) BAD("broken outside a room");
            int x, y;
            while (word(&p, a, sizeof a)) {
                if (!pair(a, &x, &y) || x < 0 || y < 0 || x >= ROOM_MAX_W || y >= ROOM_MAX_H) BAD("broken: '%s' is not column:row", a);
                if (room->progress.nbroken >= ROOM_MAX_BROKEN) BAD("too much broken stone");
                room->progress.broken[room->progress.nbroken][0] = (int16_t)x;
                room->progress.broken[room->progress.nbroken][1] = (int16_t)y;
                room->progress.nbroken++;
            }
        } else if (!strcmp(w, "crate")) {
            if (!room) BAD("crate outside a room");
            char c[16];
            int x, y;
            if (!word(&p, c, sizeof c) || !number(c, 0, room->nthings - 1, &v[0]) || !word(&p, a, sizeof a) || !pair(a, &x, &y) ||
                x < 0 || y < 0 || x >= ROOM_MAX_W || y >= ROOM_MAX_H) BAD("expected: crate <piece> <column>:<row>");
            room->progress.crate_homed[v[0]] = true;
            room->progress.crate_home[v[0]][0] = (int16_t)x;
            room->progress.crate_home[v[0]][1] = (int16_t)y;
        } else if (!strcmp(w, "endroom")) {
            if (!room) BAD("endroom without a room");
            room = NULL;
        } else if (!strcmp(w, "end")) {
            if (room) BAD("the file ends inside room %s", room->name);
            whole = true;
        }
        /* anything else: from a later version, skipped */
    }
    if (ln == 0) BAD("the file is empty");
    if (!whole) { ln++; BAD("the save is cut off"); }
done:
#undef BAD
    fclose(f);
    if (rc) memset(s, 0, sizeof *s);
    return rc;
}

int save_read(SaveGame *s, const char *path, char *err, int errlen) {
    int rc = read_one(s, path, err, errlen);
    if (rc == 0) return 0;
    char other[256], dummy[96];
    static const char *const tails[] = { ".tmp", ".bak" };   /* a newer whole save cut off before its swap, then the one before */
    for (int i = 0; i < 2; i++) {
        snprintf(other, sizeof other, "%s%s", path, tails[i]);
        if (read_one(s, other, dummy, sizeof dummy) == 0) return 0;
    }
    return rc;
}

bool save_exists(const char *path) {
    static SaveGame probe;
    char err[96];
    return save_read(&probe, path, err, sizeof err) == 0;
}

void save_remove(const char *path) {
    char other[256];
    remove(path);
    snprintf(other, sizeof other, "%s.bak", path); remove(other);
    snprintf(other, sizeof other, "%s.tmp", path); remove(other);
}

const SaveRoom *save_find_room(const SaveGame *s, const char *name) {
    for (int i = 0; i < s->nrooms; i++) if (!strcmp(s->rooms[i].name, name)) return &s->rooms[i];
    return NULL;
}
