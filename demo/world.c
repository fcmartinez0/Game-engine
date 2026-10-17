/* The world as data: rooms, their tiles and their things, and the text file they are saved in.
 *
 * A world file (mods/world.txt) looks like this; lines starting with # are notes:
 *
 *   gloamwood-world 1
 *   room hollow 40 16 cave
 *   tiles
 *   SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS
 *   S......................................S
 *   ...                       (one line per row, one character per cell: see TILE_CHARS in world.h)
 *   ########################################
 *   end
 *   thing door 2 14 id=in to=gloamwood:hollow
 *   thing lever 10 14 ch=1 mode=timed time=2.5
 *   thing gate 14 14 w=1 h=3 ch=1 need=any
 *   thing sign 5 14 text="HIT THE LEVER WITH A SLASH"
 *   thing duelist 30 14 name="GROBB" style=brawler level=0 rounds=1
 *   endroom
 *
 * Each thing is "thing <kind> <column> <row>" and then its settings as key=value (quotes for text with
 * spaces). The column and row are the cell a thing stands in; things bigger than a cell (gates, the arena,
 * the hall) grow right and up from it. Unknown keys are ignored, so a file from a newer build still loads.
 * A room in world.txt replaces the built-in room of the same name; other rooms are added. */

#include "demo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const KIND_NAMES[TH_COUNT] = {
    "start", "bench", "door", "sign", "crawler", "bat", "spitter", "grunt", "duelist", "boss", "arena", "hall",
    "lever", "plate", "gate", "crate", "key", "relic", "mark",
};
static const char *const LEVER_MODES[] = { "once", "toggle", "timed" };
static const char *const GIVES[GIVE_COUNT] = { "none", "mask", "needle", "silk" };
static const char *const STYLE_NAMES[] = { "brawler", "fencer", "paragon" };
static const char *const BACKDROP_NAMES[BD_COUNT] = { "forest", "cave" };

const char *thing_kind_name(int kind) { return kind >= 0 && kind < TH_COUNT ? KIND_NAMES[kind] : "?"; }

int thing_kind_from_name(const char *s) {
    for (int i = 0; i < TH_COUNT; i++) if (!strcmp(s, KIND_NAMES[i])) return i;
    if (!strcmp(s, "lock")) return TH_GATE;    /* older name: a gate that opens with a key (the loader sets lock) */
    return -1;
}

Thing thing_default(int kind, int col, int row) {
    Thing t;
    memset(&t, 0, sizeof t);
    t.kind = (uint8_t)kind;
    t.col = (int16_t)col; t.row = (int16_t)row;
    t.w = 1; t.h = 1;
    t.need = 1;
    t.rounds = 1;
    switch (kind) {
    case TH_LEVER: t.ch = 1; t.time = 180; break;
    case TH_PLATE: t.ch = 1; break;
    case TH_GATE:  t.ch = 1; t.h = 3; break;
    case TH_ARENA: t.w = 22; t.h = 3; break;
    case TH_HALL:  t.w = 34; t.h = 6; break;
    case TH_DUELIST: snprintf(t.name, sizeof t.name, "DUELIST"); t.level = 1; break;
    case TH_BOSS:  snprintf(t.name, sizeof t.name, "THORNWARDEN"); break;
    case TH_RELIC: snprintf(t.name, sizeof t.name, "RELIC"); break;
    case TH_DOOR:  snprintf(t.name, sizeof t.name, "door"); break;
    case TH_MARK:  snprintf(t.name, sizeof t.name, "MARK"); break;
    case TH_SIGN:  snprintf(t.text, sizeof t.text, "..."); break;
    default: break;
    }
    return t;
}

int arena_gate_col(const Thing *a) { return a->col - 1; }
int arena_exit_col(const Thing *a) { return a->col + a->w; }

bool world_name_ok(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > 23) return false;
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '_')) return false;
    return true;
}

/* ---- rooms --------------------------------------------------------------- */

void world_free(World *w) {
    for (int i = 0; i < w->nrooms; i++) free(w->rooms[i].tiles);
    memset(w, 0, sizeof *w);
}

Room *world_add_room(World *w, const char *name, int width, int height, int backdrop) {
    if (w->nrooms >= WORLD_MAX_ROOMS) return NULL;
    width = ge_clampi(width, ROOM_MIN_W, ROOM_MAX_W);
    height = ge_clampi(height, ROOM_MIN_H, ROOM_MAX_H);
    Room *r = &w->rooms[w->nrooms];
    memset(r, 0, sizeof *r);
    r->tiles = (char *)malloc((size_t)width * (size_t)height);
    if (!r->tiles) return NULL;
    w->nrooms++;
    snprintf(r->name, sizeof r->name, "%s", name);
    r->w = width; r->h = height;
    r->backdrop = (uint8_t)ge_clampi(backdrop, 0, BD_COUNT - 1);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) r->tiles[y * width + x] = y == height - 1 ? '#' : '.';
    return r;
}

int world_find_room(const World *w, const char *name) {
    for (int i = 0; i < w->nrooms; i++) if (!strcmp(w->rooms[i].name, name)) return i;
    return -1;
}

void room_resize(Room *r, int width, int height) {
    width = ge_clampi(width, ROOM_MIN_W, ROOM_MAX_W);
    height = ge_clampi(height, ROOM_MIN_H, ROOM_MAX_H);
    char *t = (char *)malloc((size_t)width * (size_t)height);
    if (!t) return;
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++) {
            char c = x < r->w && y < r->h ? r->tiles[y * r->w + x] : '.';
            if (y >= r->h && y == height - 1) c = '#';   /* a new bottom row is ground */
            t[y * width + x] = c;
        }
    free(r->tiles);
    r->tiles = t;
    r->w = width; r->h = height;
    for (int i = r->nthings - 1; i >= 0; i--)      /* things left outside are dropped */
        if (r->things[i].col >= width || r->things[i].row >= height) room_remove_thing(r, i);
}

char room_tile(const Room *r, int col, int row) {
    if (col < 0 || row < 0 || col >= r->w || row >= r->h) return '.';
    return r->tiles[row * r->w + col];
}

void room_set_tile(Room *r, int col, int row, char t) {
    if (col < 0 || row < 0 || col >= r->w || row >= r->h || !t || !strchr(TILE_CHARS, t)) return;
    r->tiles[row * r->w + col] = t;
}

int room_find(const Room *r, int kind, int from) {
    for (int i = from < 0 ? 0 : from; i < r->nthings; i++) if (r->things[i].kind == kind) return i;
    return -1;
}

int room_add_thing(Room *r, Thing t) {
    if (r->nthings >= ROOM_MAX_THINGS) return -1;
    r->things[r->nthings] = t;
    return r->nthings++;
}

void room_remove_thing(Room *r, int i) {
    if (i < 0 || i >= r->nthings) return;
    memmove(&r->things[i], &r->things[i + 1], sizeof(Thing) * (size_t)(r->nthings - i - 1));
    r->nthings--;
}

/* ---- the tilemap ---------------------------------------------------------- */

#define AUTO 255

static void char_cell(char c, uint8_t *type, uint8_t *vis) {
    *type = GE_TILE_EMPTY; *vis = 0;
    switch (c) {
    case '#': *type = GE_TILE_SOLID; *vis = AUTO; break;
    case 'S': *type = GE_TILE_SOLID; *vis = TV_STONE; break;
    case 'T': *type = GE_TILE_SOLID; *vis = TV_STONE_TOP; break;
    case '=': *type = GE_TILE_ONEWAY; *vis = TV_PLANK; break;
    case '^': *type = GE_TILE_SPIKE; *vis = TV_SPIKE; break;
    case 'C': *type = GE_TILE_SOLID; *vis = TV_CRACK; break;
    case 'H': *type = GE_TILE_SOLID; *vis = TV_STONE; break;   /* hidden: looks like stone (gets the top edge too) */
    default: break;
    }
}

void room_cell(const Room *r, int col, int row, uint8_t *type, uint8_t *vis) {
    char_cell(room_tile(r, col, row), type, vis);
    if (*type != GE_TILE_SOLID) return;
    uint8_t above, av;
    char_cell(room_tile(r, col, row - 1), &above, &av);
    bool open_above = row == 0 || above != GE_TILE_SOLID;
    char c = room_tile(r, col, row);
    if (*vis == AUTO) *vis = open_above ? TV_GRASS : ((col * 7 + row * 13) % 5 == 0 ? TV_DIRT2 : TV_DIRT);
    else if (*vis == TV_STONE && open_above && (c == 'S' || c == 'H')) *vis = TV_STONE_TOP;
}

void room_build_tilemap(const Room *r, GeTilemap *m) {
    ge_tilemap_free(m);
    ge_tilemap_create(m, r->w, r->h);
    for (int i = 0; i < r->w * r->h; i++) char_cell(r->tiles[i], &m->type[i], &m->vis[i]);
    /* ground: grass where it is open to the sky, earth below; stone gets its dressed top edge the same way */
    for (int y = 0; y < m->h; y++)
        for (int x = 0; x < m->w; x++) {
            int i = y * m->w + x;
            if (m->type[i] != GE_TILE_SOLID) continue;
            bool open_above = y == 0 || m->type[i - m->w] != GE_TILE_SOLID;
            if (m->vis[i] == AUTO) m->vis[i] = open_above ? TV_GRASS : ((x * 7 + y * 13) % 5 == 0 ? TV_DIRT2 : TV_DIRT);
            else if (m->vis[i] == TV_STONE && open_above && (r->tiles[i] == 'S' || r->tiles[i] == 'H')) m->vis[i] = TV_STONE_TOP;
        }
}

/* ---- the built-in world ------------------------------------------------------ */

static void fill(Room *r, int x0, int x1, int y0, int y1, char t) {
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) room_set_tile(r, x, y, t);
}

static void add(Room *r, int kind, int col, int row) { room_add_thing(r, thing_default(kind, col, row)); }

static void add_duelist(Room *r, int col, int row, int style, int level, int rounds, const char *name) {
    Thing t = thing_default(TH_DUELIST, col, row);
    t.style = (int8_t)style; t.level = (int8_t)level; t.rounds = (int8_t)rounds;
    snprintf(t.name, sizeof t.name, "%s", name);
    room_add_thing(r, t);
}

/* "Gloamwood": one Silksong-style slice, left to right.
 *   0  bench, crawlers        26 spike pit (pogo the bat)    44 spitter on a ledge
 *   51 wall-jump shaft up to a plateau -- duelist 1 waits on top
 *   87 duelist 2              92 spike floor with planks     105 bench
 *   121 duelist 3             132 second spike pit + bats    146 the champion (best of 3)
 *   154-175 boss arena: the gate shuts behind you; the far door (176) opens when the boss falls
 *   178-211 the Paragon's hall: a bench at 182, the Paragon waits at 200 */
static void build_gloamwood(World *w) {
    const int W = 214, H = 15, G = 12;   /* G: the ground row */
    Room *r = world_add_room(w, "gloamwood", W, H, BD_FOREST);
    if (!r) return;
    fill(r, 0, W - 1, 0, H - 1, '.');
    fill(r, 0, W - 1, G, H - 1, '#');
    fill(r, 26, 31, G, G, '.');            /* spike pit A, with a plank to land on */
    fill(r, 26, 31, 13, 13, '^');
    fill(r, 28, 29, 9, 9, '=');
    fill(r, 44, 47, 10, 11, 'S');          /* the spitter's ledge */
    fill(r, 51, 52, 0, 8, 'S');            /* wall-jump shaft: a hanging wall on the left, the plateau's cliff on the right */
    fill(r, 56, 69, 4, 11, 'S');
    fill(r, 92, 99, 11, 11, '^');          /* spike floor with planks */
    fill(r, 94, 95, 9, 9, '=');
    fill(r, 97, 98, 9, 9, '=');
    fill(r, 132, 138, G, G, '.');          /* spike pit B, bridged only by bats (pogo) and one plank */
    fill(r, 132, 138, 13, 13, '^');
    fill(r, 135, 135, 10, 10, '=');
    fill(r, 152, 153, 0, 8, 'S');          /* boss arena: a doorway in, a wall at the far end */
    fill(r, 176, 177, 0, 8, 'S');
    fill(r, 176, 176, 9, 11, 'S');         /* the far door, shut until the boss falls */
    fill(r, 178, W - 1, 0, 5, 'S');        /* the Paragon's hall: a long low stone room, with a ceiling and a wall at the end */
    fill(r, W - 2, W - 1, 0, G - 1, 'S');
    fill(r, 178, W - 1, G, G, 'T');        /* its floor is dressed stone, not grass */
    fill(r, 178, W - 1, G + 1, H - 1, 'S');

    add(r, TH_START, 5, G - 1);
    add(r, TH_CRAWLER, 13, G - 1);
    add(r, TH_CRAWLER, 21, G - 1);
    add(r, TH_BAT, 29, 6);
    add(r, TH_GRUNT, 40, G - 1);
    add(r, TH_SPITTER, 46, 9);
    add_duelist(r, 63, 3, STYLE_BRAWLER, 0, 1, "GROBB");
    add(r, TH_CRAWLER, 76, G - 1);
    add(r, TH_BAT, 82, 7);
    add_duelist(r, 87, G - 1, STYLE_FENCER, 1, 1, "SILKE");
    add(r, TH_GRUNT, 110, G - 1);
    add(r, TH_CRAWLER, 116, G - 1);
    add_duelist(r, 121, G - 1, STYLE_BRAWLER, 2, 1, "MOSSHIDE");
    add(r, TH_CRAWLER, 127, G - 1);
    add(r, TH_SPITTER, 130, G - 1);
    add(r, TH_BAT, 134, 6);
    add(r, TH_BAT, 137, 7);
    add_duelist(r, 146, G - 1, STYLE_FENCER, 3, 2, "THE CHAMPION");
    add(r, TH_BOSS, 168, G - 1);
    add_duelist(r, 200, G - 1, STYLE_PARAGON, 4, 1, "PARAGON");
    add(r, TH_BENCH, 4, G - 1);
    add(r, TH_BENCH, 105, G - 1);
    add(r, TH_BENCH, 182, G - 1);
    Thing arena = thing_default(TH_ARENA, 154, G - 1);   /* its gate is column 153, its far door 176 */
    arena.w = 22; arena.h = 3;
    room_add_thing(r, arena);
    Thing hall = thing_default(TH_HALL, 178, G - 1);
    hall.w = 34; hall.h = 6;
    room_add_thing(r, hall);
    Thing mark = thing_default(TH_MARK, 49, G - 1);
    snprintf(mark.name, sizeof mark.name, "WALL SHAFT");
    room_add_thing(r, mark);
}

void world_builtin(World *w) {
    world_free(w);
    build_gloamwood(w);
}

/* ---- the file ---------------------------------------------------------------- */

static void put_str(FILE *f, const char *key, const char *s) {
    fprintf(f, " %s=\"", key);
    for (; *s; s++) if (*s != '"' && *s != '\n') fputc(*s, f);
    fputc('"', f);
}

int world_save(const World *w, const char *path, bool only_custom) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    static char buf[32768];                      /* one write to the card, not hundreds */
    setvbuf(f, buf, _IOFBF, sizeof buf);
    fprintf(f, "gloamwood-world 1\n");
    fprintf(f, "# Made with the world editor (dev menu > EDIT WORLD). Tiles: . empty  # ground  S stone  T stone top\n");
    fprintf(f, "# = plank  ^ spikes  C cracked stone  H hidden stone. Things: thing <kind> <column> <row> settings...\n");
    fprintf(f, "# The game rewrites this whole file when you SAVE in the editor: notes you add by hand are not kept.\n");
    fprintf(f, "# A room here replaces the built-in room of the same name; the built-in rooms not here stay as they are.\n");
    for (int i = 0; i < w->nrooms; i++) {
        const Room *r = &w->rooms[i];
        if (only_custom && !r->custom) continue;
        fprintf(f, "\nroom %s %d %d %s\ntiles\n", r->name, r->w, r->h, BACKDROP_NAMES[r->backdrop % BD_COUNT]);
        for (int y = 0; y < r->h; y++) { fwrite(r->tiles + (size_t)y * r->w, 1, (size_t)r->w, f); fputc('\n', f); }
        fprintf(f, "end\n");
        for (int k = 0; k < r->nthings; k++) {
            const Thing *t = &r->things[k];
            fprintf(f, "thing %s %d %d", thing_kind_name(t->kind), t->col, t->row);
            switch (t->kind) {
            case TH_LEVER:
                fprintf(f, " ch=%d mode=%s", t->ch, LEVER_MODES[ge_clampi(t->mode, 0, 2)]);
                if (t->mode == LEVER_TIMED) fprintf(f, " time=%d.%d", t->time / 60, (t->time % 60) / 6);   /* seconds */
                break;
            case TH_PLATE: fprintf(f, " ch=%d", t->ch); break;
            case TH_GATE:
                fprintf(f, " w=%d h=%d ch=%d", t->w, t->h, t->ch);
                if (t->need == 0) fprintf(f, " need=all"); else if (t->need == 1) fprintf(f, " need=any"); else fprintf(f, " need=%d", t->need);
                if (t->invert) fprintf(f, " invert=1");
                if (t->lock) fprintf(f, " lock=1");
                if (t->plank) fprintf(f, " plank=1");
                break;
            case TH_ARENA: fprintf(f, " w=%d h=%d", t->w, t->h); break;
            case TH_HALL:  fprintf(f, " w=%d h=%d", t->w, t->h); break;
            case TH_DUELIST:
                put_str(f, "name", t->name);
                fprintf(f, " style=%s level=%d rounds=%d", STYLE_NAMES[ge_clampi(t->style, 0, 2)], t->level, t->rounds);
                break;
            case TH_BOSS: case TH_MARK: put_str(f, "name", t->name); break;
            case TH_RELIC: put_str(f, "name", t->name); if (t->gives) fprintf(f, " gives=%s", GIVES[ge_clampi(t->gives, 0, GIVE_COUNT - 1)]); break;
            case TH_DOOR: put_str(f, "id", t->name); put_str(f, "to", t->target); break;
            case TH_SIGN: put_str(f, "text", t->text); break;
            default: break;
            }
            fputc('\n', f);
        }
        fprintf(f, "endroom\n");
    }
    return fclose(f) == 0 ? 0 : -2;
}

/* next token: a word, a key=value, or key="quoted text"; returns false at the end of the line */
static bool next_token(char **p, char *key, size_t klen, char *val, size_t vlen) {
    char *s = *p;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || *s == '\n' || *s == '\r' || *s == '#') return false;
    size_t k = 0;
    while (*s && *s != ' ' && *s != '\t' && *s != '=' && *s != '\n' && *s != '\r') { if (k + 1 < klen) key[k++] = *s; s++; }
    key[k] = 0;
    val[0] = 0;
    if (*s == '=') {
        s++;
        size_t v = 0;
        if (*s == '"') {
            s++;
            while (*s && *s != '"' && *s != '\n') { if (v + 1 < vlen) val[v++] = *s; s++; }
            if (*s == '"') s++;
        } else {
            while (*s && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') { if (v + 1 < vlen) val[v++] = *s; s++; }
        }
        val[v] = 0;
    }
    *p = s;
    return true;
}

static void copy_str(char *dst, size_t n, const char *src) {   /* cut to fit */
    size_t k = 0;
    for (; src[k] && k + 1 < n; k++) dst[k] = src[k];
    dst[k] = 0;
}

static int parse_named(const char *s, const char *const *names, int n) {
    for (int i = 0; i < n; i++) if (!strcmp(s, names[i])) return i;
    return atoi(s);
}

static int parse_style(const char *s) {
    for (int i = 0; i < 3; i++) if (!strcmp(s, STYLE_NAMES[i])) return i;
    return atoi(s);
}

int world_load(World *w, const char *path, char *err, int errlen) {
    world_free(w);
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, (size_t)errlen, "can't open %s", path); return -1; }
    static char line[ROOM_MAX_W + 64];
    int ln = 0, rc = 0;
    Room *r = NULL;
    int tile_row = -1;                      /* >= 0 while reading a tiles block */
#define FAIL(...) do { snprintf(err, (size_t)errlen, __VA_ARGS__); rc = ln > 0 ? ln : 1; goto done; } while (0)
    if (!fgets(line, sizeof line, f)) { ln = 1; FAIL("the file is empty"); }
    char *first = line;
    if ((unsigned char)first[0] == 0xEF && (unsigned char)first[1] == 0xBB && (unsigned char)first[2] == 0xBF) first += 3;   /* Notepad's BOM */
    if (strncmp(first, "gloamwood-world", 15) != 0) { ln = 1; FAIL("not a world file (the first line should be 'gloamwood-world 1')"); }
    ln = 1;
    while (fgets(line, sizeof line, f)) {
        ln++;
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) line[--n] = 0;
        if (tile_row >= 0) {
            if (!strcmp(line, "end")) {
                if (tile_row != r->h) FAIL("room %s has %d tile rows, its size says %d", r->name, tile_row, r->h);
                tile_row = -1;
                continue;
            }
            if (tile_row >= r->h) FAIL("room %s: more tile rows than its height (%d)", r->name, r->h);
            if ((int)n != r->w) FAIL("room %s, tile row %d: %d characters, its width is %d", r->name, tile_row, (int)n, r->w);
            for (int x = 0; x < r->w; x++) {
                if (!strchr(TILE_CHARS, line[x]) || !line[x]) FAIL("room %s, tile row %d: '%c' is not a tile (use %s)", r->name, tile_row, line[x], TILE_CHARS);
                r->tiles[tile_row * r->w + x] = line[x];
            }
            tile_row++;
            continue;
        }
        char *p = line, word[32], dummy[4];
        if (!next_token(&p, word, sizeof word, dummy, sizeof dummy)) continue;   /* blank or a note */
        if (!strcmp(word, "room")) {
            char name[32] = "", sw[16] = "", sh[16] = "", bd[16] = "forest", v[4];
            if (!next_token(&p, name, sizeof name, v, sizeof v) || !next_token(&p, sw, sizeof sw, v, sizeof v) || !next_token(&p, sh, sizeof sh, v, sizeof v))
                FAIL("expected: room <name> <width> <height> [forest|cave]");
            next_token(&p, bd, sizeof bd, v, sizeof v);
            int width = atoi(sw), height = atoi(sh), backdrop = !strcmp(bd, "cave") ? BD_CAVE : BD_FOREST;
            if (!world_name_ok(name)) FAIL("room name '%s': use a-z, 0-9, - and _ (no spaces), at most 23", name);
            if (width < ROOM_MIN_W || width > ROOM_MAX_W || height < ROOM_MIN_H || height > ROOM_MAX_H)
                FAIL("room %s: size %dx%d (from %dx%d to %dx%d)", name, width, height, ROOM_MIN_W, ROOM_MIN_H, ROOM_MAX_W, ROOM_MAX_H);
            if (world_find_room(w, name) >= 0) FAIL("two rooms called %s", name);
            r = world_add_room(w, name, width, height, backdrop);
            if (!r) FAIL("too many rooms (at most %d)", WORLD_MAX_ROOMS);
        } else if (!strcmp(word, "tiles")) {
            if (!r) FAIL("'tiles' before any 'room'");
            tile_row = 0;
        } else if (!strcmp(word, "endroom")) {
            r = NULL;
        } else if (!strcmp(word, "thing")) {
            if (!r) FAIL("'thing' outside a room");
            char kind[24] = "", sc[16] = "", sr[16] = "", v[4];
            if (!next_token(&p, kind, sizeof kind, v, sizeof v) || !next_token(&p, sc, sizeof sc, v, sizeof v) || !next_token(&p, sr, sizeof sr, v, sizeof v))
                FAIL("expected: thing <kind> <column> <row> ...");
            int k = thing_kind_from_name(kind);
            if (k < 0) FAIL("'%s' is not a kind of thing", kind);
            int col = atoi(sc), row = atoi(sr);
            if (col < 0 || row < 0 || col >= r->w || row >= r->h) FAIL("%s at %d,%d is outside room %s (%dx%d)", kind, col, row, r->name, r->w, r->h);
            Thing t = thing_default(k, col, row);
            if (!strcmp(kind, "lock")) { t.lock = true; t.ch = 0; }
            char key[24], val[72];
            while (next_token(&p, key, sizeof key, val, sizeof val)) {
                int iv = atoi(val);
                if (!strcmp(key, "w")) t.w = (int16_t)ge_clampi(iv, 1, ROOM_MAX_W);
                else if (!strcmp(key, "h")) t.h = (int16_t)ge_clampi(iv, 1, ROOM_MAX_H);
                else if (!strcmp(key, "ch")) t.ch = (int8_t)ge_clampi(iv, 0, 9);
                else if (!strcmp(key, "need")) t.need = (int8_t)(!strcmp(val, "all") ? 0 : !strcmp(val, "any") ? 1 : ge_clampi(iv, 0, 9));
                else if (!strcmp(key, "invert")) t.invert = iv != 0;
                else if (!strcmp(key, "lock")) t.lock = iv != 0;
                else if (!strcmp(key, "plank")) t.plank = iv != 0;
                else if (!strcmp(key, "mode")) t.mode = (int8_t)ge_clampi(parse_named(val, LEVER_MODES, 3), 0, 2);
                else if (!strcmp(key, "gives")) t.gives = (int8_t)ge_clampi(parse_named(val, GIVES, GIVE_COUNT), 0, GIVE_COUNT - 1);
                else if (!strcmp(key, "time")) t.time = (int16_t)ge_clampi((int)(atof(val) * 60.0 + 0.5), 6, 30000);   /* seconds */
                else if (!strcmp(key, "style")) t.style = (int8_t)ge_clampi(parse_style(val), 0, 2);
                else if (!strcmp(key, "level")) t.level = (int8_t)ge_clampi(iv, -1, 4);
                else if (!strcmp(key, "rounds")) t.rounds = (int8_t)ge_clampi(iv, 1, 3);
                else if (!strcmp(key, "name") || !strcmp(key, "id")) copy_str(t.name, sizeof t.name, val);
                else if (!strcmp(key, "to")) copy_str(t.target, sizeof t.target, val);
                else if (!strcmp(key, "text")) copy_str(t.text, sizeof t.text, val);
            }
            if (t.kind == TH_DOOR && t.name[0] && !world_name_ok(t.name)) FAIL("door id '%s': use a-z, 0-9, - and _", t.name);
            if (room_add_thing(r, t) < 0) FAIL("room %s has too many things (at most %d)", r->name, ROOM_MAX_THINGS);
        } else if (strcmp(word, "gloamwood-world") != 0) {
            FAIL("'%s' is not something a world file says", word);
        }
    }
    if (tile_row >= 0) FAIL("the file ends inside the tiles of room %s", r->name);
    if (w->nrooms == 0) { ln = 1; FAIL("no rooms in the file"); }
done:
#undef FAIL
    fclose(f);
    if (rc) world_free(w);
    return rc;
}

bool world_equal(const World *a, const World *b) {
    if (a->nrooms != b->nrooms) return false;
    for (int i = 0; i < a->nrooms; i++) {
        const Room *x = &a->rooms[i], *y = &b->rooms[i];
        if (strcmp(x->name, y->name) || x->w != y->w || x->h != y->h || x->backdrop != y->backdrop || x->nthings != y->nthings) return false;
        if (memcmp(x->tiles, y->tiles, (size_t)x->w * (size_t)x->h)) return false;
        for (int k = 0; k < x->nthings; k++) {
            const Thing *s = &x->things[k], *t = &y->things[k];
            if (s->kind != t->kind || s->col != t->col || s->row != t->row || strcmp(s->name, t->name) || strcmp(s->target, t->target) || strcmp(s->text, t->text))
                return false;
            switch (s->kind) {
            case TH_LEVER: if (s->ch != t->ch || s->mode != t->mode || (s->mode == LEVER_TIMED && s->time != t->time)) return false; break;
            case TH_PLATE: if (s->ch != t->ch) return false; break;
            case TH_GATE:
                if (s->w != t->w || s->h != t->h || s->ch != t->ch || s->need != t->need || s->invert != t->invert || s->lock != t->lock || s->plank != t->plank)
                    return false;
                break;
            case TH_RELIC: if (s->gives != t->gives) return false; break;
            case TH_HALL: if (s->w != t->w || s->h != t->h) return false; break;
            case TH_ARENA: if (s->w != t->w || s->h != t->h) return false; break;
            case TH_DUELIST: if (s->style != t->style || s->level != t->level || s->rounds != t->rounds) return false; break;
            default: break;
            }
        }
    }
    return true;
}
