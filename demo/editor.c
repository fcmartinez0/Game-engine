/* The world editor. See editor.h. */

#include "editor.h"
#include "ge/ge_font.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the bottom screen's layout ---------------------------------------------------------------- */

#define BW 320
#define TB_H 24                      /* the toolbar: five buttons of 64 */
#define TOOL_W 64
#define MAP_Y 24                     /* the map: 40 x 21 tiles of 8 px */
#define MAP_COLS 40
#define MAP_ROWS 21
#define MT 8
#define TABS_Y 192                   /* four tabs of 80 */
#define TAB_W 80
#define PAL_Y 216                    /* the brushes: cells of 32 x 24 */
#define PAL_W 32
#define SLOT_W 104                   /* a setting: [<] 24, its value, [>] 24 */
#define SLOT_H 24
#define ARROW_W 24

#define TAP_PX 4                     /* moved less than this: a tap, not a drag */
#define PICK_PX 12                   /* a piece this close to where the stylus lands is under it */
#define EDGE_PX 12                   /* dragging this close to the map's edge scrolls it, after EDGE_TICKS */
#define EDGE_TICKS 18
#define REPEAT_AFTER 24              /* holding a setting's arrow: it repeats after 0.4 s, 8 steps a second */
#define REPEAT_EVERY 8
#define UNDO_MAX 32
#define MAX_CREATURES 40             /* the world has room for 48 actors: the hero, these, and some to spare */

enum { TAB_GROUND, TAB_PUZZLE, TAB_EXPLORE, TAB_FOES, TAB_COUNT };
static const char *const TAB_NAMES[TAB_COUNT] = { "GROUND", "PUZZLE", "EXPLORE", "FOES" };
enum { TOOL_UNDO, TOOL_ERASER, TOOL_ROOM, TOOL_SAVE, TOOL_PLAY, TOOL_COUNT };
static const char *const TOOL_NAMES[TOOL_COUNT] = { "UNDO", "ERASER", "ROOM", "SAVE", "PLAY" };
enum { P_NONE, P_ROOM, P_KEYS };
enum { GV_GATE, GV_BRIDGE, GV_LOCKED };   /* Brush.variant for gates */

typedef struct {
    const char *name, *desc;   /* what the top screen says when it is picked */
    int tab;
    char tile;                 /* a tile brush paints this */
    int kind;                  /* a piece brush (tile 0) places this */
    int variant;
    bool locked;               /* shown, but made in world.txt: the code around them assumes the Gloamwood's */
} Brush;

static const Brush BRUSHES[] = {
    { "STONE", "ROCK. EVERY WALL CAN BE CLIMBED BY WALL-JUMPING", TAB_GROUND, 'S', 0, 0, false },
    { "EARTH", "GROUND: GRASS WHERE IT IS OPEN ABOVE, EARTH BELOW", TAB_GROUND, '#', 0, 0, false },
    { "STONE TOP", "STONE WITH A DRESSED TOP EDGE, EVEN UNDER A WALL", TAB_GROUND, 'T', 0, 0, false },
    { "PLANK", "STAND ON IT, JUMP UP THROUGH IT. DOWN+B DROPS THROUGH", TAB_GROUND, '=', 0, 0, false },
    { "SPIKES", "THEY HURT, AND SEND YOU BACK TO SAFE GROUND", TAB_GROUND, '^', 0, 0, false },
    { "CRACKED STONE", "BREAKS AFTER TWO HITS: SLASH, SPIN OR NEEDLE", TAB_GROUND, 'C', 0, 0, false },
    { "HIDDEN STONE", "LOOKS LIKE STONE, BREAKS LIKE CRACKED STONE: A SECRET", TAB_GROUND, 'H', 0, 0, false },
    { "LEVER", "HIT IT TO TURN ITS WIRE ON: GATES ON THAT WIRE OPEN", TAB_PUZZLE, 0, TH_LEVER, 0, false },
    { "PLATE", "ON WHILE THE HERO OR A CRATE STANDS ON IT", TAB_PUZZLE, 0, TH_PLATE, 0, false },
    { "GATE", "BARS THAT OPEN WHILE ITS WIRE IS ON", TAB_PUZZLE, 0, TH_GATE, GV_GATE, false },
    { "BRIDGE", "PLANKS THAT APPEAR WHILE ITS WIRE IS ON", TAB_PUZZLE, 0, TH_GATE, GV_BRIDGE, false },
    { "LOCKED GATE", "OPENS FOR GOOD WHEN YOU WALK INTO IT WITH A KEY", TAB_PUZZLE, 0, TH_GATE, GV_LOCKED, false },
    { "CRATE", "PUSH IT, KNOCK IT, DROP IT ON A PLATE TO HOLD IT DOWN", TAB_PUZZLE, 0, TH_CRATE, 0, false },
    { "KEY", "PICK IT UP, THEN WALK INTO A LOCKED GATE", TAB_PUZZLE, 0, TH_KEY, 0, false },
    { "DOOR", "UP GOES THROUGH. LINK IT TO A DOOR IN ANOTHER ROOM", TAB_EXPLORE, 0, TH_DOOR, 0, false },
    { "BENCH", "REST: MASKS BACK, AND YOU COME BACK HERE AFTER A FALL", TAB_EXPLORE, 0, TH_BENCH, 0, false },
    { "SIGN", "WORDS SHOWN WHEN YOU STAND BY IT", TAB_EXPLORE, 0, TH_SIGN, 0, false },
    { "RELIC", "FOUND ONCE: ONE MORE MASK, ONE MORE NEEDLE, OR SILK", TAB_EXPLORE, 0, TH_RELIC, 0, false },
    { "START", "WHERE YOU APPEAR WHEN YOU DON'T COME IN BY A DOOR", TAB_EXPLORE, 0, TH_START, 0, false },
    { "MARK", "A BOOKMARK FOR THE DEV MENU'S WARP", TAB_EXPLORE, 0, TH_MARK, 0, false },
    { "CRAWLER", "WALKS BACK AND FORTH ALONG ITS FLOOR", TAB_FOES, 0, TH_CRAWLER, 0, false },
    { "BAT", "FLIES, AND SWOOPS AT YOU", TAB_FOES, 0, TH_BAT, 0, false },
    { "SPITTER", "STAYS PUT AND SPITS AT YOU", TAB_FOES, 0, TH_SPITTER, 0, false },
    { "GRUNT", "CHASES YOU AND SWINGS", TAB_FOES, 0, TH_GRUNT, 0, false },
    { "DUELIST", "CHALLENGES YOU TO A DUEL WHEN IT SEES YOU", TAB_FOES, 0, TH_DUELIST, 0, false },
    { "BOSS", "THE THORNWARDEN. MADE IN WORLD.TXT, NOT HERE", TAB_FOES, 0, TH_BOSS, 0, true },
    { "ARENA", "THE BOSS'S ROOM. MADE IN WORLD.TXT, NOT HERE", TAB_FOES, 0, TH_ARENA, 0, true },
    { "HALL", "THE PARAGON'S HALL. MADE IN WORLD.TXT, NOT HERE", TAB_FOES, 0, TH_HALL, 0, true },
};
#define NBRUSHES ((int)(sizeof BRUSHES / sizeof BRUSHES[0]))

/* the colours of the editor's own screens */
#define INK      GE_RGB(20, 14, 30)
#define PANEL    GE_RGB(30, 20, 50)
#define PANEL2   GE_RGB(44, 30, 72)
#define LIT      GE_RGB(96, 64, 150)
#define TEXT     GE_RGB(226, 220, 240)
#define DIM      GE_RGB(150, 140, 180)
#define HI       GE_RGB(255, 236, 160)
#define BAD      GE_RGB(255, 120, 100)
#define GOOD     GE_RGB(140, 230, 160)
#define WARN     GE_RGB(255, 190, 110)

/* ---- state -------------------------------------------------------------------------------------- */

#define MARKER (-2)                  /* the TEST FROM HERE marker, as a piece you can pick up */
#define NOTHING (-1)

static struct {
    EditorHost h;
    bool ready;
    GeImage tiles8;                  /* the tileset at 8 px a tile, for the map */
    int room;
    int mx, my;                      /* the map's top-left tile */
    float cx, cy;                    /* the centre of the top screen's view, in world pixels */
    int tab, brush;                  /* the brush picked, or -1 */
    bool eraser;
    int sel;                         /* the selected piece, or -1 */
    int last_wire;                   /* the wire of the switch last placed or picked: a new gate joins it */
    int marker_room, marker_col, marker_row;
    bool edited[WORLD_MAX_ROOMS];
    bool unsaved, since_autosave, autosaved;
    bool bottom_dirty;
    int panel;
    char msg[80];
    int msg_timer;
    uint32_t msg_color;
    char warn[80];
    int confirm, confirm_timer;      /* a "tap again" waiting */
    int help;                        /* ticks left of the help card */
    bool help_seen;
    int pan_ticks;
    float pan_acc;
    uint32_t tick;
    int last_tap_x, last_tap_y;
    bool want_play;
    int kb_target, kb_thing, kb_len, kb_max;   /* the keyboard */
    char kb[72];
} E;

enum { G_NONE, G_TOOL, G_TAB, G_PALETTE, G_SLOT, G_PIECE, G_PAINT, G_BOX, G_PLACE, G_PAN, G_PANEL, G_SWALLOW };
static struct {
    int kind, item;
    bool down, moved, changed;
    int x0, y0, x, y;                /* where it touched down; where it is */
    int hx[3], hy[3];                /* the last samples, newest first: the very last one before a lift can jump */
    int ticks;
    int cell_c, cell_r;              /* painting: the last cell painted */
    int box_c, box_r;                /* box fill: where it began */
    int grab_dc, grab_dr;            /* moving a piece: its anchor cell, from the cell under the stylus */
    int mx0, my0;                    /* panning: the map's corner when it began */
    int edge, dir;
} G;

static int g_test_wires[10];          /* the test play's panel: the wires as last drawn */
static int g_test_frames;

static Room *R(void) { return &E.h.wd->rooms[E.room]; }
static Room *room_at(int i) { return &E.h.wd->rooms[i]; }

static void say(const char *s, uint32_t color) {
    snprintf(E.msg, sizeof E.msg, "%s", s);
    E.msg_timer = 200;
    E.msg_color = color;
}

static void elog(const char *fmt, ...) {
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (E.h.log) E.h.log(buf);
}

static void copy_text(char *dst, size_t n, const char *src) {   /* cut to fit */
    size_t k = 0;
    for (; src[k] && k + 1 < n; k++) dst[k] = src[k];
    dst[k] = 0;
}

static void upper(char *dst, size_t n, const char *src) {
    size_t k = 0;
    for (; src[k] && k + 1 < n; k++) dst[k] = (src[k] >= 'a' && src[k] <= 'z') ? (char)(src[k] - 32) : src[k];
    dst[k] = 0;
}

static bool confirmed(int what) {        /* the second tap within two seconds */
    if (E.confirm == what && E.confirm_timer > 0) { E.confirm = 0; return true; }
    E.confirm = what;
    E.confirm_timer = 120;
    return false;
}

static bool sized(int kind) { return kind == TH_GATE || kind == TH_ARENA || kind == TH_HALL; }
static bool creature(int kind) { return kind >= TH_CRAWLER && kind <= TH_BOSS; }
static bool locked_kind(int kind) { return kind == TH_BOSS || kind == TH_ARENA || kind == TH_HALL; }
static bool rock(char c) { return c == '#' || c == 'S' || c == 'T' || c == 'C' || c == 'H'; }
static bool is_input(int kind) { return kind == TH_LEVER || kind == TH_PLATE; }

static int brush_of(const Thing *t) {           /* the brush a piece was made with */
    int variant = t->kind != TH_GATE ? 0 : t->lock ? GV_LOCKED : t->invert ? GV_BRIDGE : GV_GATE;
    for (int i = 0; i < NBRUSHES; i++) if (!BRUSHES[i].tile && BRUSHES[i].kind == t->kind && BRUSHES[i].variant == variant) return i;
    return -1;
}

/* ---- the map's geometry -------------------------------------------------------------------------- */

static bool in_map(int x, int y) { return x >= 0 && x < BW && y >= MAP_Y && y < TABS_Y; }
static int map_col(int x) { return E.mx + (x < 0 ? -1 : x / MT); }
static int map_row(int y) { return E.my + (y < MAP_Y ? -1 : (y - MAP_Y) / MT); }
static int cell_x(int col) { return (col - E.mx) * MT; }
static int cell_y(int row) { return MAP_Y + (row - E.my) * MT; }

static void clamp_map(void) {
    int maxx = R()->w - MAP_COLS, maxy = R()->h - MAP_ROWS;
    E.mx = ge_clampi(E.mx, 0, maxx > 0 ? maxx : 0);
    E.my = ge_clampi(E.my, 0, maxy > 0 ? maxy : 0);
}

static void center_map(int col, int row) {
    E.mx = col - MAP_COLS / 2;
    E.my = row - MAP_ROWS / 2;
    clamp_map();
}

/* the top screen looks at (E.cx, E.cy), kept inside the room */
static void place_camera(void) {
    GeWorld *w = E.h.w;
    float x = E.cx - w->cam.vw * 0.5f, y = E.cy - w->cam.vh * 0.5f;
    float maxx = (float)(R()->w * GE_TILE) - w->cam.vw, maxy = (float)(R()->h * GE_TILE) - w->cam.vh;
    w->cam.x = x < 0 ? 0 : x > maxx ? maxx : x;
    w->cam.y = y < 0 ? 0 : y > maxy ? maxy : y;
}

/* a piece's cells: gates (and the arena, the hall) are w x h with their anchor at the bottom left; a door is
 * two tiles tall; everything else is the one cell it stands in */
static void thing_cells(const Thing *t, int *c0, int *r0, int *c1, int *r1) {
    *c0 = t->col; *r1 = t->row;
    *c1 = t->col + (sized(t->kind) ? t->w - 1 : 0);
    *r0 = t->row - (sized(t->kind) ? t->h - 1 : t->kind == TH_DOOR ? 1 : 0);
}

/* ---- the half-size tiles and the icons ----------------------------------------------------------- */

static void build_tiles8(void) {
    const GeImage *src = &E.h.w->tileset;
    int n = src->w / GE_TILE;
    E.tiles8 = ge_image_create(n * MT, MT);
    if (!E.tiles8.px) return;
    for (int t = 0; t < n; t++)
        for (int y = 0; y < MT; y++)
            for (int x = 0; x < MT; x++) {                      /* each pixel the average of four */
                unsigned a = 0, r = 0, gc = 0, b = 0;
                for (int k = 0; k < 4; k++) {
                    uint32_t p = src->px[(y * 2 + (k >> 1)) * src->w + t * GE_TILE + x * 2 + (k & 1)];
                    a += p >> 24; r += (p >> 16) & 255u; gc += (p >> 8) & 255u; b += p & 255u;
                }
                E.tiles8.px[y * E.tiles8.w + t * MT + x] = GE_ARGB(a / 4, r / 4, gc / 4, b / 4);
            }
}

static GeImage *g_fb;
static int g_ix, g_iy, g_is;
static void px(int x, int y, int w, int h, uint32_t c) { ge_fill_rect(g_fb, g_ix + x * g_is, g_iy + y * g_is, w * g_is, h * g_is, c, 0); }

static void figure(uint32_t body, uint32_t head) {       /* a little person */
    px(3, 0, 2, 2, head); px(2, 2, 4, 4, body); px(2, 6, 1, 2, body); px(5, 6, 1, 2, body);
}

/* A piece's picture in an 8 x 8 box (s = 1, the map) or 16 x 16 (s = 2, the brushes). */
static void icon(GeImage *fb, int kind, int variant, int ch, int x, int y, int s) {
    g_fb = fb; g_ix = x; g_iy = y; g_is = s;
    uint32_t wc = puzzle_wire_color(ch);
    switch (kind) {
    case TH_LEVER: px(1, 6, 6, 2, GE_RGB(110, 106, 130)); px(4, 3, 1, 3, GE_RGB(60, 56, 70)); px(5, 1, 2, 2, wc); break;
    case TH_PLATE: px(1, 5, 6, 1, GE_RGB(200, 196, 220)); px(0, 6, 8, 2, wc); break;
    case TH_GATE:
        if (variant == GV_BRIDGE) { px(0, 3, 8, 2, GE_RGB(150, 104, 60)); px(1, 3, 1, 2, wc); px(5, 3, 1, 2, wc); break; }
        px(0, 0, 8, 1, variant == GV_LOCKED ? GE_RGB(255, 214, 110) : wc);
        for (int k = 1; k < 8; k += 2) px(k, 1, 1, 7, GE_RGB(150, 146, 170));
        if (variant == GV_LOCKED) { px(2, 3, 4, 3, GE_RGB(255, 214, 110)); px(3, 4, 2, 1, INK); }
        break;
    case TH_CRATE: px(0, 0, 8, 8, GE_RGB(120, 80, 44)); px(1, 1, 6, 6, GE_RGB(170, 120, 70)); px(1, 3, 6, 1, GE_RGB(120, 80, 44)); break;
    case TH_KEY: px(0, 3, 3, 3, GE_RGB(255, 214, 110)); px(3, 4, 5, 1, GE_RGB(255, 214, 110)); px(6, 5, 1, 2, GE_RGB(255, 214, 110)); break;
    case TH_DOOR: px(1, 0, 6, 8, GE_RGB(110, 106, 130)); px(2, 1, 4, 7, GE_RGB(10, 6, 16)); break;
    case TH_BENCH: px(0, 4, 8, 2, GE_RGB(150, 104, 60)); px(1, 6, 1, 2, GE_RGB(110, 76, 44)); px(6, 6, 1, 2, GE_RGB(110, 76, 44)); break;
    case TH_SIGN: px(1, 1, 6, 5, GE_RGB(110, 106, 130)); px(2, 2, 4, 1, GE_RGB(200, 196, 220)); px(3, 6, 2, 2, GE_RGB(70, 66, 92)); break;
    case TH_RELIC: px(3, 1, 2, 6, GE_RGB(200, 230, 255)); px(1, 3, 6, 2, GE_RGB(200, 230, 255)); px(3, 3, 2, 2, GE_WHITE); break;
    case TH_START: px(1, 0, 1, 8, GE_WHITE); px(2, 0, 4, 3, GE_RGB(110, 230, 120)); break;
    case TH_MARK: px(2, 0, 4, 7, GE_RGB(230, 110, 220)); px(3, 6, 2, 2, INK); break;
    case TH_CRAWLER: px(1, 4, 6, 3, GE_RGB(120, 200, 90)); px(2, 3, 4, 1, GE_RGB(120, 200, 90)); px(5, 4, 1, 1, INK); break;
    case TH_BAT: px(3, 3, 2, 2, GE_RGB(170, 120, 220)); px(0, 2, 3, 2, GE_RGB(140, 96, 190)); px(5, 2, 3, 2, GE_RGB(140, 96, 190)); break;
    case TH_SPITTER: px(2, 2, 4, 6, GE_RGB(170, 170, 80)); px(3, 1, 2, 1, GE_RGB(170, 170, 80)); px(3, 3, 2, 1, INK); break;
    case TH_GRUNT: figure(GE_RGB(170, 120, 80), GE_RGB(220, 180, 140)); break;
    case TH_DUELIST: figure(variant ? GE_RGB(240, 196, 90) : GE_RGB(170, 110, 240), GE_RGB(230, 220, 250)); break;
    case TH_BOSS: figure(GE_RGB(200, 60, 50), GE_RGB(255, 170, 140)); break;
    case TH_ARENA: case TH_HALL:
        px(0, 0, 8, 1, GE_RGB(240, 196, 90)); px(0, 7, 8, 1, GE_RGB(240, 196, 90)); px(0, 0, 1, 8, GE_RGB(240, 196, 90)); px(7, 0, 1, 8, GE_RGB(240, 196, 90));
        break;
    default: break;
    }
}

static void padlock(GeImage *fb, int x, int y) {
    ge_fill_rect(fb, x, y + 3, 7, 5, GE_RGB(255, 214, 110), 0);
    ge_fill_rect(fb, x + 1, y, 5, 1, GE_RGB(255, 214, 110), 0);
    ge_fill_rect(fb, x + 1, y, 1, 3, GE_RGB(255, 214, 110), 0);
    ge_fill_rect(fb, x + 5, y, 1, 3, GE_RGB(255, 214, 110), 0);
    ge_fill_rect(fb, x + 3, y + 5, 1, 2, INK, 0);
}

static void rect_outline(GeImage *fb, int x, int y, int w, int h, uint32_t c, bool dashed) {
    for (int i = 0; i < w; i++) if (!dashed || (i & 2)) { ge_fill_rect(fb, x + i, y, 1, 1, c, 0); ge_fill_rect(fb, x + i, y + h - 1, 1, 1, c, 0); }
    for (int i = 0; i < h; i++) if (!dashed || (i & 2)) { ge_fill_rect(fb, x, y + i, 1, 1, c, 0); ge_fill_rect(fb, x + w - 1, y + i, 1, 1, c, 0); }
}

/* ---- undo: snapshots of rooms, one per room a step changes --------------------------------------- */

typedef struct {
    int step, room;
    bool absent;                     /* the step made this room: undoing it takes the room away */
    char name[24];
    int w, h, nthings;
    uint8_t backdrop;
    bool custom;
    Thing *things;
    char *tiles;
} Snap;
static Snap g_undo[UNDO_MAX];
static int g_nundo, g_step;

static void snap_free(Snap *s) { free(s->things); free(s->tiles); memset(s, 0, sizeof *s); }

static void begin_step(void) { g_step++; }

static Snap *snap_slot(int room) {
    for (int i = 0; i < g_nundo; i++) if (g_undo[i].step == g_step && g_undo[i].room == room) return NULL;   /* (once a step) */
    if (g_nundo == UNDO_MAX) {                       /* the oldest step goes, all of it */
        int old = g_undo[0].step, keep = 0;
        for (int i = 0; i < g_nundo; i++) { if (g_undo[i].step == old) snap_free(&g_undo[i]); else g_undo[keep++] = g_undo[i]; }
        for (int i = keep; i < g_nundo; i++) memset(&g_undo[i], 0, sizeof g_undo[i]);
        g_nundo = keep;
        if (g_nundo == UNDO_MAX) return NULL;
    }
    Snap *s = &g_undo[g_nundo];
    memset(s, 0, sizeof *s);
    s->step = g_step;
    s->room = room;
    return s;
}

/* remember a room as it is, before the step being made changes it */
static void snap_room(int room) {
    Snap *s = snap_slot(room);
    if (!s) return;
    const Room *r = room_at(room);
    memcpy(s->name, r->name, sizeof s->name);
    s->w = r->w; s->h = r->h; s->nthings = r->nthings; s->backdrop = r->backdrop; s->custom = r->custom;
    s->things = (Thing *)malloc(sizeof(Thing) * (size_t)(r->nthings > 0 ? r->nthings : 1));
    s->tiles = (char *)malloc((size_t)r->w * (size_t)r->h);
    if (!s->things || !s->tiles) { snap_free(s); return; }
    memcpy(s->things, r->things, sizeof(Thing) * (size_t)r->nthings);
    memcpy(s->tiles, r->tiles, (size_t)r->w * (size_t)r->h);
    g_nundo++;
}

static void snap_new_room(int room) {
    Snap *s = snap_slot(room);
    if (!s) return;
    s->absent = true;
    g_nundo++;
}

/* a step that changed nothing after all: forget it */
static void drop_step(void) { while (g_nundo > 0 && g_undo[g_nundo - 1].step == g_step) snap_free(&g_undo[--g_nundo]); }

static void undo_clear(void) { while (g_nundo > 0) snap_free(&g_undo[--g_nundo]); }

/* ---- edits ---------------------------------------------------------------------------------------- */

static void check_room(void);

static void mark_edited(int room) {
    E.edited[room] = true;
    room_at(room)->custom = true;
    E.unsaved = E.since_autosave = true;
    E.bottom_dirty = true;
    if (room == E.room) check_room();
}

/* the shown room as built, after a change to its pieces (the game rebuilds its tiles and pieces) */
static void refresh(void) {
    if (E.sel >= R()->nthings) E.sel = NOTHING;
    E.h.show_room(E.room);
    clamp_map();
    check_room();
    E.bottom_dirty = true;
}

static void switch_room(int r) {
    if (r < 0 || r >= E.h.wd->nrooms) return;
    E.room = r;
    E.sel = NOTHING;
    refresh();
    E.bottom_dirty = true;
}

static void undo(void) {
    if (g_nundo == 0) { say("NOTHING TO UNDO", DIM); return; }
    int step = g_undo[g_nundo - 1].step, other = -1;
    World *wd = E.h.wd;
    while (g_nundo > 0 && g_undo[g_nundo - 1].step == step) {
        Snap *s = &g_undo[--g_nundo];
        if (s->absent) {
            if (s->room == wd->nrooms - 1) { free(wd->rooms[s->room].tiles); memset(&wd->rooms[s->room], 0, sizeof(Room)); wd->nrooms--; }
        } else if (s->room < wd->nrooms) {
            Room *r = &wd->rooms[s->room];
            free(r->tiles);
            r->tiles = s->tiles;
            s->tiles = NULL;
            memcpy(r->name, s->name, sizeof r->name);
            r->w = s->w; r->h = s->h; r->backdrop = s->backdrop; r->nthings = s->nthings;
            memcpy(r->things, s->things, sizeof(Thing) * (size_t)s->nthings);
            mark_edited(s->room);
            r->custom = s->custom;
            other = s->room;
        }
        snap_free(s);
    }
    if (E.room >= wd->nrooms) E.room = other >= 0 ? other : 0;
    if (E.marker_room >= wd->nrooms) { E.marker_room = E.room; E.marker_col = 2; E.marker_row = R()->h - 2; }
    E.sel = NOTHING;
    refresh();
    say("UNDONE", GOOD);
    elog("editor: undo in %s", R()->name);
}

/* What stops a piece standing here, or NULL. `self` is the piece being moved (-1 for a new one). */
static const char *problem(const Room *r, const Thing *t, int self) {
    int c0, r0, c1, r1;
    thing_cells(t, &c0, &r0, &c1, &r1);
    if (t->kind == TH_DOOR) r0 = t->row;            /* (its frame may stand in front of rock) */
    if (c0 < 0 || r0 < 0 || c1 >= r->w || r1 >= r->h) return "OUTSIDE THE ROOM";
    if (t->kind != TH_ARENA && t->kind != TH_HALL)
        for (int y = r0; y <= r1; y++)
            for (int x = c0; x <= c1; x++) if (rock(room_tile(r, x, y))) return "INSIDE ROCK";
    for (int k = 0; k < r->nthings; k++) {
        if (k == self) continue;
        const Thing *o = &r->things[k];
        if (o->kind == TH_ARENA || o->kind == TH_HALL || t->kind == TH_ARENA || t->kind == TH_HALL) continue;   /* (areas, not things) */
        bool pair = (o->kind == TH_CRATE && t->kind == TH_PLATE) || (o->kind == TH_PLATE && t->kind == TH_CRATE);
        if (pair) continue;                          /* a crate can start on a plate */
        int oc0, or0, oc1, or1;
        thing_cells(o, &oc0, &or0, &oc1, &or1);
        if (o->kind == TH_DOOR) or0 = o->row;
        if (o->kind == TH_GATE || t->kind == TH_GATE) {
            if (c0 <= oc1 && oc0 <= c1 && r0 <= or1 && or0 <= r1) return "ON ANOTHER PIECE";
        } else if (o->col == t->col && o->row == t->row) {
            return "ON ANOTHER PIECE";
        }
    }
    return NULL;
}

static int count_creatures(const Room *r) { int n = 0; for (int i = 0; i < r->nthings; i++) n += creature(r->things[i].kind); return n; }

static int lowest_free_wire(const Room *r) {
    bool used[10] = { false };
    for (int i = 0; i < r->nthings; i++) if (r->things[i].ch > 0 && r->things[i].ch <= 9) used[r->things[i].ch] = true;
    for (int c = 1; c <= 9; c++) if (!used[c]) return c;
    return 1;
}

static void new_door_id(const Room *r, char *out, size_t n) {
    for (int k = 1; k < 100; k++) {
        snprintf(out, n, "door%d", k);
        bool taken = false;
        for (int i = 0; i < r->nthings && !taken; i++) taken = r->things[i].kind == TH_DOOR && !strcmp(r->things[i].name, out);
        if (!taken) return;
    }
}

static void keys_open(int target, int thing, const char *init, int max);
enum { KB_SIGN = 1, KB_NAME, KB_ROOM };

static int place(int bi, int col, int row) {
    const Brush *b = &BRUSHES[bi];
    Room *r = R();
    if (b->locked) { say(b->desc, WARN); return -1; }
    Thing t = thing_default(b->kind, col, row);
    switch (b->kind) {
    case TH_LEVER: case TH_PLATE: t.ch = (int8_t)lowest_free_wire(r); E.last_wire = t.ch; break;
    case TH_GATE:
        t.ch = (int8_t)(E.last_wire > 0 ? E.last_wire : 1);
        if (b->variant == GV_BRIDGE) { t.invert = true; t.plank = true; t.w = 3; t.h = 1; }
        if (b->variant == GV_LOCKED) { t.lock = true; t.ch = 0; }
        break;
    case TH_DOOR: new_door_id(r, t.name, sizeof t.name); t.target[0] = 0; break;
    case TH_DUELIST: t.style = STYLE_BRAWLER; t.level = 1; t.rounds = 1; break;
    default: break;
    }
    const char *why = problem(r, &t, -1);
    if (why) { say(why, BAD); return -1; }
    if (creature(t.kind) && count_creatures(r) >= MAX_CREATURES) { say("THIS ROOM HAS ITS 40 CREATURES", BAD); return -1; }
    if (r->nthings >= ROOM_MAX_THINGS) { say("THIS ROOM IS FULL (96 PIECES)", BAD); return -1; }
    begin_step();
    snap_room(E.room);
    int i = room_add_thing(r, t);
    mark_edited(E.room);
    refresh();
    char nm[24];
    upper(nm, sizeof nm, b->name);
    char m[80];
    snprintf(m, sizeof m, "%s PLACED. TAP IT TO CHANGE IT", nm);
    say(m, GOOD);
    elog("editor: placed %s at %d,%d in %s", thing_kind_name(t.kind), col, row, r->name);
    if (t.kind == TH_SIGN) keys_open(KB_SIGN, i, "", 63);    /* a sign needs its words: type them now */
    return i;
}

static void remove_piece(int i) {
    Room *r = R();
    if (i < 0 || i >= r->nthings) return;
    if (locked_kind(r->things[i].kind)) { say("MADE IN WORLD.TXT: IT CAN'T BE REMOVED HERE", WARN); return; }
    begin_step();
    snap_room(E.room);
    elog("editor: removed %s at %d,%d in %s", thing_kind_name(r->things[i].kind), r->things[i].col, r->things[i].row, r->name);
    room_remove_thing(r, i);
    E.sel = NOTHING;
    mark_edited(E.room);
    refresh();
    say("REMOVED (L UNDOES)", GOOD);
}

static void move_piece(int i, int col, int row) {
    Room *r = R();
    if (i == MARKER) {
        Thing t = thing_default(TH_START, col, row);
        if (col < 0 || row < 0 || col >= r->w || row >= r->h) { say("OUTSIDE THE ROOM", BAD); return; }
        (void)t;
        E.marker_room = E.room; E.marker_col = col; E.marker_row = row;
        say("PLAY STARTS HERE NOW", GOOD);
        E.bottom_dirty = true;
        return;
    }
    if (i < 0 || i >= r->nthings) return;
    if (locked_kind(r->things[i].kind)) { say("MADE IN WORLD.TXT: IT CAN'T BE MOVED HERE", WARN); return; }
    Thing t = r->things[i];
    if (t.col == col && t.row == row) return;
    t.col = (int16_t)col; t.row = (int16_t)row;
    const char *why = problem(r, &t, i);
    if (why) { say(why, BAD); return; }
    begin_step();
    snap_room(E.room);
    r->things[i].col = (int16_t)col; r->things[i].row = (int16_t)row;
    mark_edited(E.room);
    refresh();
    elog("editor: moved %s to %d,%d in %s", thing_kind_name(t.kind), col, row, r->name);
}

/* ---- painting -------------------------------------------------------------------------------------- */

static char brush_tile(void) { return E.eraser ? '.' : (E.brush >= 0 && BRUSHES[E.brush].tile) ? BRUSHES[E.brush].tile : 0; }

/* one cell, and the one below it (its top edge depends on this one), straight into the shown map */
static void paint(int col, int row, char t) {
    Room *r = R();
    if (!t || col < 0 || row < 0 || col >= r->w || row >= r->h || room_tile(r, col, row) == t) return;
    room_set_tile(r, col, row, t);
    GeTilemap *m = &E.h.w->map;
    if (m->w != r->w || m->h != r->h) return;
    for (int y = row; y <= row + 1 && y < m->h; y++) room_cell(r, col, y, &m->type[y * m->w + col], &m->vis[y * m->w + col]);
    G.changed = true;
}

static void paint_line(int c0, int r0, int c1, int r1, char t) {   /* the stylus moved faster than a cell a sample */
    int dc = abs(c1 - c0), dr = abs(r1 - r0), sc = c0 < c1 ? 1 : -1, sr = r0 < r1 ? 1 : -1, err = dc - dr;
    for (int n = 0; n < 1024; n++) {
        paint(c0, r0, t);
        if (c0 == c1 && r0 == r1) break;
        int e2 = 2 * err;
        if (e2 > -dr) { err -= dr; c0 += sc; }
        if (e2 < dc) { err += dc; r0 += sr; }
    }
}

static void end_stroke(void) {
    if (!G.changed) { drop_step(); return; }
    mark_edited(E.room);
    refresh();
    elog("editor: painted in %s", R()->name);
}

/* ---- the rest of a room: doors, its size, its name ------------------------------------------------ */

static void door_label(const Thing *t, char *out, size_t n) {
    int k;
    if (sscanf(t->name, "door%d", &k) == 1) snprintf(out, n, "DOOR %d", k);
    else upper(out, n, t->name);
}

/* "room:door" -> its room and the door's index there (-1 if it isn't there) */
static int find_door(const char *target, int *door) {
    char name[32];
    *door = -1;
    const char *colon = strchr(target, ':');
    if (!colon || colon == target || (size_t)(colon - target) >= sizeof name) return -1;
    memcpy(name, target, (size_t)(colon - target));
    name[colon - target] = 0;
    int r = world_find_room(E.h.wd, name);
    if (r < 0) return -1;
    for (int i = 0; i < room_at(r)->nthings; i++)
        if (room_at(r)->things[i].kind == TH_DOOR && !strcmp(room_at(r)->things[i].name, colon + 1)) *door = i;
    return *door >= 0 ? r : -1;
}

typedef struct { int room, door; } DoorRef;
static int link_options(DoorRef *out, int max) {   /* every door in the other rooms */
    int n = 0;
    for (int r = 0; r < E.h.wd->nrooms; r++) {
        if (r == E.room) continue;
        for (int i = 0; i < room_at(r)->nthings && n < max; i++)
            if (room_at(r)->things[i].kind == TH_DOOR) { out[n].room = r; out[n].door = i; n++; }
    }
    return n;
}

static void target_of(int room, int door, char *out, size_t n) {
    snprintf(out, n, "%s:%s", room_at(room)->name, room_at(room)->things[door].name);
}

/* Link a door of this room to (to_room, to_door), both ways; (-1, -1) unlinks it. A door one of them led to
 * before, that led back, is unlinked too, so no door is left pointing at a door that points elsewhere. */
static void set_link(int door, int to_room, int to_door) {
    Thing *t = &R()->things[door];
    char me[48];
    target_of(E.room, door, me, sizeof me);
    begin_step();
    snap_room(E.room);
    int od, orr = find_door(t->target, &od);
    if (orr >= 0 && !strcmp(room_at(orr)->things[od].target, me)) { snap_room(orr); room_at(orr)->things[od].target[0] = 0; mark_edited(orr); }
    t->target[0] = 0;
    if (to_room >= 0) {
        Thing *p = &room_at(to_room)->things[to_door];
        char them[48];
        target_of(to_room, to_door, them, sizeof them);
        int pd, pr = find_door(p->target, &pd);
        if (pr >= 0 && !(pr == E.room && pd == door) && !strcmp(room_at(pr)->things[pd].target, them)) {
            snap_room(pr); room_at(pr)->things[pd].target[0] = 0; mark_edited(pr);
        }
        snap_room(to_room);
        snprintf(p->target, sizeof p->target, "%s", me);
        snprintf(t->target, sizeof t->target, "%s", them);
        mark_edited(to_room);
    }
    mark_edited(E.room);
    elog("editor: door %s in %s now leads to %s", t->name, R()->name, t->target[0] ? t->target : "nowhere");
}

/* WIDER, NARROWER, TALLER, SHORTER: the room's inside grows or shrinks just inside its right wall or under its
 * ceiling, and the pieces beyond move with their cells (so does the marker) */
enum { RS_WIDER, RS_NARROWER, RS_TALLER, RS_SHORTER };
static void resize(int how) {
    Room *r = R();
    int at = (how == RS_WIDER) ? room_inner_col(r) + 1 : (how == RS_NARROWER) ? room_inner_col(r) : room_inner_row(r);
    if (how == RS_WIDER && at > r->w) at = r->w;
    if (how == RS_NARROWER || how == RS_SHORTER) {
        int n = how == RS_NARROWER ? room_things_in_col(r, at) : room_things_in_row(r, at);
        if (n > 0 && !confirmed(100 + how)) {
            char m[64];
            snprintf(m, sizeof m, "%d PIECE%s WILL GO - TAP AGAIN", n, n == 1 ? "" : "S");
            say(m, WARN);
            return;
        }
    }
    begin_step();
    snap_room(E.room);
    bool ok = how == RS_WIDER ? room_insert_col(r, at) : how == RS_NARROWER ? room_delete_col(r, at)
            : how == RS_TALLER ? room_insert_row(r, at) : room_delete_row(r, at);
    if (!ok) {
        drop_step();
        say(how == RS_WIDER ? "AS WIDE AS A ROOM CAN BE (512)" : how == RS_TALLER ? "AS TALL AS A ROOM CAN BE (128)"
            : how == RS_NARROWER ? "A ROOM IS AT LEAST A SCREEN WIDE (25)" : "A ROOM IS AT LEAST A SCREEN TALL (15)", WARN);
        return;
    }
    if (E.marker_room == E.room) {
        if (how == RS_WIDER && E.marker_col >= at) E.marker_col++;
        if (how == RS_NARROWER && E.marker_col > at) E.marker_col--;
        if (how == RS_TALLER && E.marker_row >= at) E.marker_row++;
        if (how == RS_SHORTER && E.marker_row > at) E.marker_row--;
        E.marker_col = ge_clampi(E.marker_col, 0, r->w - 1);
        E.marker_row = ge_clampi(E.marker_row, 0, r->h - 1);
    }
    mark_edited(E.room);
    refresh();
    char m[64];
    snprintf(m, sizeof m, "%s: %d X %d", how == RS_WIDER ? "WIDER" : how == RS_NARROWER ? "NARROWER" : how == RS_TALLER ? "TALLER" : "SHORTER", r->w, r->h);
    say(m, GOOD);
    elog("editor: %s is now %dx%d", r->name, r->w, r->h);
}

static bool builtin_room(const char *name, World *bi) {
    world_builtin(bi);
    return world_find_room(bi, name) >= 0;
}

static void new_room(void) {
    World *wd = E.h.wd;
    if (wd->nrooms >= WORLD_MAX_ROOMS) { say("AT MOST 16 ROOMS", BAD); return; }
    char name[24];
    for (int n = 1; n < 100; n++) { snprintf(name, sizeof name, "room%d", n); if (world_find_room(wd, name) < 0) break; }
    int from = E.room;
    /* the way back: a door where the marker is, if there is space for one */
    Thing back = thing_default(TH_DOOR, E.marker_col, E.marker_row);
    new_door_id(R(), back.name, sizeof back.name);
    bool linked = E.marker_room == from && !problem(R(), &back, -1) && R()->nthings < ROOM_MAX_THINGS;
    begin_step();
    if (linked) snap_room(from);
    int to = wd->nrooms;
    Room *r = world_add_room(wd, name, 40, 15, BD_CAVE);
    if (!r) { drop_step(); say("NOT ENOUGH MEMORY FOR A NEW ROOM", BAD); return; }
    snap_new_room(to);
    for (int y = 0; y < r->h; y++)
        for (int x = 0; x < r->w; x++) r->tiles[y * r->w + x] = (y <= 1 || y >= 12 || x == 0 || x == r->w - 1) ? 'S' : '.';
    room_add_thing(r, thing_default(TH_START, 3, 11));
    Thing door = thing_default(TH_DOOR, 5, 11);
    snprintf(door.name, sizeof door.name, "door1");
    if (linked) {
        snprintf(door.target, sizeof door.target, "%s:%s", room_at(from)->name, back.name);
        snprintf(back.target, sizeof back.target, "%s:door1", name);
        room_add_thing(room_at(from), back);
        mark_edited(from);
    }
    room_add_thing(r, door);
    mark_edited(to);
    switch_room(to);
    center_map(5, 11);
    E.cx = 5 * GE_TILE + 8.0f; E.cy = 11 * GE_TILE;
    char nm[24], m[80];
    upper(nm, sizeof nm, name);
    snprintf(m, sizeof m, linked ? "%s MADE. ITS DOOR LEADS BACK TO THE MARKER" : "%s MADE. LINK ITS DOOR TO COME BACK", nm);
    say(m, GOOD);
    elog("editor: new room %s%s", name, linked ? " (door linked back)" : "");
}

/* ---- warnings: one line on the top screen, about the room being edited ---------------------------- */

static void check_room(void) {
    const Room *r = R();
    int inputs[10] = { 0 }, gates[10] = { 0 }, doors = 0, ways_in = 0;
    E.warn[0] = 0;
    for (int i = 0; i < r->nthings; i++) {
        const Thing *t = &r->things[i];
        if (is_input(t->kind) && t->ch > 0 && t->ch <= 9) inputs[t->ch]++;
        if (t->kind == TH_GATE && !t->lock && t->ch > 0 && t->ch <= 9) gates[t->ch]++;
        if (t->kind == TH_DOOR) doors++;
        if (t->kind == TH_DOOR || t->kind == TH_START || t->kind == TH_BENCH) ways_in++;
    }
    int n = count_creatures(r);
    if (n >= MAX_CREATURES - 4) { snprintf(E.warn, sizeof E.warn, "CREATURES %d/%d", n, MAX_CREATURES); return; }
    if (ways_in == 0) { snprintf(E.warn, sizeof E.warn, "THIS ROOM HAS NO WAY IN: ADD A DOOR"); return; }
    for (int i = 0; i < r->nthings; i++) {
        const Thing *t = &r->things[i];
        char lab[24];
        if (t->kind == TH_DOOR) {
            int d;
            door_label(t, lab, sizeof lab);
            if (!t->target[0]) { snprintf(E.warn, sizeof E.warn, "%s ISN'T LINKED: TAP IT, THEN LEADS TO", lab); return; }
            if (find_door(t->target, &d) < 0) { snprintf(E.warn, sizeof E.warn, "%s LEADS TO A ROOM OR DOOR THAT ISN'T THERE", lab); return; }
            for (int k = 0; k < r->nthings; k++)
                if (r->things[k].kind == TH_BENCH && r->things[k].row == t->row && abs(r->things[k].col - t->col) < 2) {
                    snprintf(E.warn, sizeof E.warn, "%s IS TOO NEAR A BENCH (UP WOULD REST)", lab);
                    return;
                }
        }
        if (t->kind == TH_GATE && !t->lock && (t->ch <= 0 || inputs[t->ch] == 0)) {
            snprintf(E.warn, sizeof E.warn, "%s AT %d,%d HAS NO SWITCH ON WIRE %d", t->invert ? "BRIDGE" : "GATE", t->col, t->row, t->ch);
            return;
        }
    }
    for (int c = 1; c <= 9; c++) if (inputs[c] && !gates[c]) { snprintf(E.warn, sizeof E.warn, "WIRE %d HAS NO GATE OR BRIDGE", c); return; }
    (void)doors;
}

/* ---- saving --------------------------------------------------------------------------------------- */

static const char *mods_path(const char *file) {
    static char p[4][200];
    static int k;
    k = (k + 1) & 3;
    snprintf(p[k], sizeof p[k], "%s/%s", mods_dir(), file);
    return p[k];
}

static bool file_exists(const char *path) { FILE *f = fopen(path, "r"); if (f) fclose(f); return f != NULL; }

static int custom_rooms(void) { int n = 0; for (int i = 0; i < E.h.wd->nrooms; i++) n += E.h.wd->rooms[i].custom; return n; }

/* World.txt is written to world.tmp first, the old one kept as world.bak, then the new one renamed into place:
 * pulling the card (or the power) halfway through never leaves you without a world. */
static bool save(void) {
    const char *world = mods_path("world.txt"), *tmp = mods_path("world.tmp"), *bak = mods_path("world.bak");
    mods_make_dir();
    if (*E.h.world_broken) {                 /* never over the one that didn't load: it is kept beside it */
        const char *broken = mods_path("world_broken.txt");
        remove(broken);
        if (rename(world, broken) == 0) say("YOUR OLD WORLD.TXT DIDN'T LOAD: KEPT AS WORLD_BROKEN.TXT", WARN);
        *E.h.world_broken = false;
    }
    int n = custom_rooms();
    if (n == 0) {                            /* nothing of yours left: the built-in world, and no file */
        remove(bak);
        rename(world, bak);
        remove(mods_path("world.autosave.txt"));
        E.unsaved = E.since_autosave = false;
        say("SAVED: EVERY ROOM IS THE BUILT-IN ONE (OLD FILE: WORLD.BAK)", GOOD);
        elog("editor: saved (no rooms of your own: world.txt moved to world.bak)");
        return true;
    }
    if (world_save(E.h.wd, tmp, true) != 0) { remove(tmp); say("COULDN'T WRITE TO THE SD CARD", BAD); elog("editor: save failed (writing %s)", tmp); return false; }
    remove(bak);
    bool had = rename(world, bak) == 0;
    if (rename(tmp, world) != 0) {
        if (had) rename(bak, world);
        say("COULDN'T SAVE. THE OLD WORLD.TXT IS SAFE", BAD);
        elog("editor: save failed (renaming %s)", tmp);
        return false;
    }
    remove(mods_path("world.autosave.txt"));
    E.unsaved = E.since_autosave = false;
    char m[80];
    snprintf(m, sizeof m, "SAVED %d ROOM%s TO WORLD.TXT", n, n == 1 ? "" : "S");
    say(m, GOOD);
    elog("editor: saved %d rooms to %s", n, world);
    E.bottom_dirty = true;
    return true;
}

void editor_autosave(void) {
    if (!E.ready || !E.since_autosave) return;
    const char *path = mods_path("world.autosave.txt");
    mods_make_dir();
    if (custom_rooms() == 0) remove(path);
    else if (world_save(E.h.wd, path, true) != 0) { elog("editor: autosave failed (%s)", path); return; }
    E.since_autosave = false;
    E.autosaved = true;
    elog("editor: autosaved to %s", path);
}

static void restore_autosave(void) {
    const char *path = mods_path("world.autosave.txt");
    if (E.autosaved || !file_exists(path)) { say("NO AUTOSAVE FROM AN EARLIER SESSION", DIM); return; }
    if (!confirmed(200)) { say("PUT BACK THE AUTOSAVED ROOMS? UNDO WON'T REACH PAST IT. TAP AGAIN", WARN); return; }
    static World file;
    char err[100];
    int rc = world_load(&file, path, err, sizeof err);
    if (rc) { char m[80]; snprintf(m, sizeof m, "THE AUTOSAVE DIDN'T LOAD (LINE %d)", rc); say(m, BAD); return; }
    int names[WORLD_MAX_ROOMS], nn = file.nrooms;
    char rn[WORLD_MAX_ROOMS][24];
    for (int i = 0; i < nn; i++) { memcpy(rn[i], file.rooms[i].name, 24); names[i] = i; }
    int added = 0, replaced = world_overlay(E.h.wd, &file, &added);
    (void)names;
    for (int i = 0; i < nn; i++) { int r = world_find_room(E.h.wd, rn[i]); if (r >= 0) mark_edited(r); }
    undo_clear();
    E.autosaved = true;
    if (E.room >= E.h.wd->nrooms) E.room = 0;
    refresh();
    char m[80];
    snprintf(m, sizeof m, "AUTOSAVE BACK: %d ROOMS REPLACED, %d ADDED", replaced, added);
    say(m, GOOD);
    elog("editor: autosave restored (%d replaced, %d added)", replaced, added);
}

static void revert_room(void) {
    static World bi;
    Room *r = R();
    if (!builtin_room(r->name, &bi)) { world_free(&bi); say("THIS ROOM IS YOURS: THERE IS NO BUILT-IN ONE", DIM); return; }
    if (!confirmed(201)) { world_free(&bi); say("BACK TO THE BUILT-IN ROOM? TAP AGAIN (L UNDOES)", WARN); return; }
    const Room *src = &bi.rooms[world_find_room(&bi, r->name)];
    char *tiles = (char *)malloc((size_t)src->w * (size_t)src->h);
    if (!tiles) { world_free(&bi); return; }
    begin_step();
    snap_room(E.room);
    memcpy(tiles, src->tiles, (size_t)src->w * (size_t)src->h);
    free(r->tiles);
    r->tiles = tiles;
    r->w = src->w; r->h = src->h; r->backdrop = src->backdrop; r->nthings = src->nthings;
    memcpy(r->things, src->things, sizeof(Thing) * (size_t)src->nthings);
    mark_edited(E.room);
    r->custom = false;                       /* SAVE leaves it out of the file again */
    world_free(&bi);
    E.marker_col = ge_clampi(E.marker_col, 0, r->w - 1);
    E.marker_row = ge_clampi(E.marker_row, 0, r->h - 1);
    refresh();
    say("THE BUILT-IN ROOM IS BACK (SAVE TO KEEP IT SO)", GOOD);
    elog("editor: %s reverted to the built-in room", r->name);
}

/* ---- the keyboard (sign words, relic and room names) ----------------------------------------------- */

#define KEY_Y 120
#define KEY_W 32
#define KEY_H 24
static const char *const KEYROWS[4] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM,.?" };

static void keys_open(int target, int thing, const char *init, int max) {
    E.panel = P_KEYS;
    E.kb_target = target;
    E.kb_thing = thing;
    upper(E.kb, sizeof E.kb, init);
    E.kb_len = (int)strlen(E.kb);
    E.kb_max = max < (int)sizeof E.kb - 1 ? max : (int)sizeof E.kb - 1;
    if (E.kb_len > E.kb_max) { E.kb_len = E.kb_max; E.kb[E.kb_len] = 0; }
    E.bottom_dirty = true;
}

static void rename_room(const char *typed) {
    char name[24];
    size_t k = 0;
    for (const char *s = typed; *s && k + 1 < sizeof name; s++) {
        char c = *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        else if (c == ' ') c = '-';
        else if (!((c >= '0' && c <= '9') || c == '-')) continue;   /* only a-z, 0-9 and - */
        name[k++] = c;
    }
    name[k] = 0;
    Room *r = R();
    if (!world_name_ok(name)) { say("A ROOM'S NAME: LETTERS, NUMBERS AND -", BAD); return; }
    if (!strcmp(name, r->name)) return;
    if (world_find_room(E.h.wd, name) >= 0) { say("ANOTHER ROOM HAS THAT NAME", BAD); return; }
    char old[24], prefix[32];
    snprintf(old, sizeof old, "%s", r->name);
    snprintf(prefix, sizeof prefix, "%s:", old);
    begin_step();
    snap_room(E.room);
    snprintf(r->name, sizeof r->name, "%s", name);
    mark_edited(E.room);
    for (int i = 0; i < E.h.wd->nrooms; i++)       /* every door that led here still does */
        for (int k2 = 0; k2 < room_at(i)->nthings; k2++) {
            Thing *t = &room_at(i)->things[k2];
            if (t->kind != TH_DOOR || strncmp(t->target, prefix, strlen(prefix))) continue;
            char target[96];
            snprintf(target, sizeof target, "%s:%s", name, t->target + strlen(prefix));
            snap_room(i);
            copy_text(t->target, sizeof t->target, target);
            mark_edited(i);
        }
    char m[64];
    upper(m, sizeof m, name);
    say(m, GOOD);
    elog("editor: room %s renamed %s", old, name);
}

static void keys_commit(void) {
    E.panel = P_NONE;
    E.bottom_dirty = true;
    if (E.kb_target == KB_ROOM) { rename_room(E.kb); return; }
    Room *r = R();
    if (E.kb_thing < 0 || E.kb_thing >= r->nthings) return;
    Thing *t = &r->things[E.kb_thing];
    char *dst = E.kb_target == KB_SIGN ? t->text : t->name;
    size_t n = E.kb_target == KB_SIGN ? sizeof t->text : sizeof t->name;
    if (!strcmp(dst, E.kb)) return;
    begin_step();
    snap_room(E.room);
    copy_text(dst, n, E.kb);
    mark_edited(E.room);
    say("WRITTEN", GOOD);
    elog("editor: %s text set in %s", thing_kind_name(t->kind), r->name);
}

static void keys_type(char c) {
    if (E.kb_len >= E.kb_max) { say("THAT'S AS LONG AS IT CAN BE", WARN); return; }
    E.kb[E.kb_len++] = c;
    E.kb[E.kb_len] = 0;
}

static void keys_press(int x, int y) {
    E.bottom_dirty = true;
    if (y < TB_H) { if (x >= BW - 72) E.panel = P_NONE; return; }   /* CANCEL */
    if (y < KEY_Y) return;
    int row = (y - KEY_Y) / KEY_H, col = x / KEY_W;
    if (row < 4) { keys_type(KEYROWS[row][col]); return; }
    if (col == 0) keys_type('!');
    else if (col == 1) keys_type('-');
    else if (col == 2) keys_type(':');
    else if (col <= 5) keys_type(' ');
    else if (col <= 7) { if (E.kb_len > 0) E.kb[--E.kb_len] = 0; }
    else keys_commit();
}

static void draw_keys(GeImage *fb) {
    static const char *const LABEL[] = { "", "SIGN WORDS", "NAME", "ROOM NAME (LETTERS, NUMBERS, -)" };
    ge_clear(fb, PANEL);
    ge_fill_rect(fb, 0, 0, BW, TB_H, PANEL2, 0);
    ge_text(fb, 6, 8, LABEL[E.kb_target], HI, 1);
    ge_fill_rect(fb, BW - 72, 2, 70, 20, GE_RGB(70, 40, 60), 0);
    ge_text(fb, BW - 72 + (70 - ge_text_width("CANCEL", 1)) / 2, 8, "CANCEL", TEXT, 1);
    ge_fill_rect(fb, 4, 30, BW - 8, 70, INK, 0);
    char line[52];
    int y = 36;
    for (int at = 0; at <= E.kb_len && y < 96; at += 50, y += 12) {        /* 50 letters a line */
        snprintf(line, sizeof line, "%.50s", E.kb + at);
        ge_text(fb, 10, y, line, TEXT, 1);
        if (E.kb_len - at < 50 || (E.kb_len == at)) {                       /* the cursor, on the last line */
            if ((E.tick / 16) & 1) ge_fill_rect(fb, 10 + (E.kb_len - at) * 6, y, 5, 7, HI, 0);
            break;
        }
    }
    char count[24];
    snprintf(count, sizeof count, "%d/%d", E.kb_len, E.kb_max);
    ge_text(fb, BW - 8 - ge_text_width(count, 1), 104, count, DIM, 1);
    ge_text(fb, 6, 104, "B: CANCEL", DIM, 1);
    for (int row = 0; row < 5; row++)
        for (int col = 0; col < 10; col++) {
            int x = col * KEY_W, ky = KEY_Y + row * KEY_H;
            char k[8] = "";
            int w = KEY_W;
            if (row < 4) { k[0] = KEYROWS[row][col]; k[1] = 0; }
            else if (col == 0) snprintf(k, sizeof k, "!");
            else if (col == 1) snprintf(k, sizeof k, "-");
            else if (col == 2) snprintf(k, sizeof k, ":");
            else if (col == 3) { snprintf(k, sizeof k, "SPACE"); w = 3 * KEY_W; }
            else if (col == 6) { snprintf(k, sizeof k, "DEL"); w = 2 * KEY_W; }
            else if (col == 8) { snprintf(k, sizeof k, "OK"); w = 2 * KEY_W; }
            else continue;
            ge_fill_rect(fb, x + 1, ky + 1, w - 2, KEY_H - 2, col == 8 && row == 4 ? GE_RGB(50, 90, 60) : PANEL2, 0);
            ge_text(fb, x + (w - ge_text_width(k, 1)) / 2, ky + 9, k, TEXT, 1);
        }
}

/* ---- the ROOM panel ------------------------------------------------------------------------------ */

#define RP_LIST_Y 26
#define RP_ROW_H 21
#define RP_B1 118
#define RP_B2 146
#define RP_B3 174
#define RP_STRIP 202

static void room_panel_press(int x, int y) {
    E.bottom_dirty = true;
    if (y < TB_H) { if (x >= BW - 72) E.panel = P_NONE; return; }
    if (y >= RP_LIST_Y && y < RP_LIST_Y + 4 * RP_ROW_H) {
        int i = ((y - RP_LIST_Y) / RP_ROW_H) * 4 + x / 80;
        if (i < E.h.wd->nrooms) {
            switch_room(i);
            const Thing *at = NULL;
            for (int k = 0; k < R()->nthings && !at; k++) if (R()->things[k].kind == TH_START || R()->things[k].kind == TH_DOOR) at = &R()->things[k];
            center_map(at ? at->col : 0, at ? at->row : R()->h - 2);
            E.cx = (at ? at->col : 12) * GE_TILE + 8.0f; E.cy = (at ? at->row : R()->h - 2) * (float)GE_TILE;
            E.panel = P_NONE;
            char nm[24];
            upper(nm, sizeof nm, R()->name);
            say(nm, GOOD);
        }
        return;
    }
    int b = x / 80;
    if (y >= RP_B1 && y < RP_B1 + 24) resize(b);
    else if (y >= RP_B2 && y < RP_B2 + 24) {
        if (b == 0) {
            begin_step(); snap_room(E.room);
            R()->backdrop = (uint8_t)((R()->backdrop + 1) % BD_COUNT);
            mark_edited(E.room); refresh();
            say(R()->backdrop == BD_CAVE ? "BACKDROP: CAVE" : "BACKDROP: FOREST", GOOD);
        } else if (b == 1) {
            static World bi;
            bool built_in = builtin_room(R()->name, &bi);
            world_free(&bi);
            if (built_in) say("THE BUILT-IN ROOMS KEEP THEIR NAMES (DOORS FIND THEM BY IT)", WARN);
            else keys_open(KB_ROOM, -1, R()->name, 23);
        } else if (b == 2) new_room();
    } else if (y >= RP_B3 && y < RP_B3 + 24) {
        if (x < 160) revert_room(); else restore_autosave();
    } else if (y >= RP_STRIP) {                      /* the overview: jump there */
        const Room *r = R();
        int col = x * r->w / BW, row = (y - RP_STRIP) * r->h / (240 - RP_STRIP);
        center_map(col, row);
        E.cx = col * (float)GE_TILE; E.cy = row * (float)GE_TILE;
        E.panel = P_NONE;
    }
}

static void button(GeImage *fb, int x, int y, int w, int h, const char *label, uint32_t bg, uint32_t fg) {
    ge_fill_rect(fb, x + 1, y + 1, w - 2, h - 2, bg, 0);
    ge_text(fb, x + (w - ge_text_width(label, 1)) / 2, y + (h - 7) / 2, label, fg, 1);
}

static void draw_room_panel(GeImage *fb) {
    ge_clear(fb, PANEL);
    ge_fill_rect(fb, 0, 0, BW, TB_H, PANEL2, 0);
    char buf[80], nm[24];
    upper(nm, sizeof nm, R()->name);
    snprintf(buf, sizeof buf, "ROOMS. THIS ONE: %s, %d X %d", nm, R()->w, R()->h);
    ge_text(fb, 6, 8, buf, HI, 1);
    button(fb, BW - 72, 0, 72, TB_H, "CLOSE", GE_RGB(70, 40, 60), TEXT);
    for (int i = 0; i < E.h.wd->nrooms; i++) {
        int x = (i % 4) * 80, y = RP_LIST_Y + (i / 4) * RP_ROW_H;
        upper(nm, sizeof nm, E.h.wd->rooms[i].name);
        nm[12] = 0;
        button(fb, x, y, 80, RP_ROW_H, nm, i == E.room ? LIT : PANEL2, TEXT);
        if (E.h.wd->rooms[i].custom) ge_fill_circle(fb, x + 6, y + RP_ROW_H / 2, 2, GOOD, 0);   /* yours (saved to world.txt) */
    }
    static const char *const B1[4] = { "WIDER", "NARROWER", "TALLER", "SHORTER" };
    for (int b = 0; b < 4; b++) button(fb, b * 80, RP_B1, 80, 24, B1[b], PANEL2, TEXT);
    button(fb, 0, RP_B2, 80, 24, R()->backdrop == BD_CAVE ? "CAVE" : "FOREST", PANEL2, TEXT);
    button(fb, 80, RP_B2, 80, 24, "RENAME", PANEL2, TEXT);
    button(fb, 160, RP_B2, 80, 24, "NEW ROOM", PANEL2, GOOD);
    ge_text(fb, 248, RP_B2 + 3, "GREEN DOT:", DIM, 1);
    ge_text(fb, 248, RP_B2 + 13, "YOURS", DIM, 1);
    button(fb, 0, RP_B3, 160, 24, E.confirm == 201 ? "TAP AGAIN TO REVERT" : "BACK TO BUILT-IN", PANEL2, TEXT);
    bool can_restore = !E.autosaved && file_exists(mods_path("world.autosave.txt"));
    button(fb, 160, RP_B3, 160, 24, E.confirm == 200 ? "TAP AGAIN TO RESTORE" : "RESTORE AUTOSAVE", PANEL2, can_restore ? TEXT : DIM);
    /* the overview: the whole room squeezed into the strip, with the map's view on it */
    const Room *r = R();
    int sh = 240 - RP_STRIP;
    for (int y = 0; y < sh; y++)
        for (int x = 0; x < BW; x++) {
            char c = room_tile(r, x * r->w / BW, y * r->h / sh);
            uint32_t col = rock(c) ? GE_RGB(120, 112, 150) : c == '^' ? GE_RGB(200, 90, 90) : c == '=' ? GE_RGB(150, 104, 60) : GE_RGB(24, 20, 36);
            fb->px[(RP_STRIP + y) * fb->w + x] = col;
        }
    int fx = E.mx * BW / r->w, fw = MAP_COLS * BW / r->w, fy = RP_STRIP + E.my * sh / r->h, fh = MAP_ROWS * sh / r->h;
    rect_outline(fb, fx, fy, fw > BW - fx ? BW - fx : fw, fh > 240 - fy ? 240 - fy : fh, HI, false);
}

/* ---- a selected piece's settings --------------------------------------------------------------------- */

enum { S_NONE, S_WIRE, S_NEED, S_WIDTH, S_HEIGHT, S_GATE_KIND, S_MODE, S_TIME, S_GIVES, S_TEXT, S_NAME, S_LINK, S_GOTO,
       S_STYLE, S_LEVEL, S_ROUNDS, S_DELETE };
typedef struct { int id; const char *label; char value[40]; bool button; } Slot;
#define NSLOTS 6

static const char *const GATE_KINDS[] = { "GATE", "PLANK GATE", "BRIDGE", "STONE BRIDGE", "LOCKED" };
static int gate_kind(const Thing *t) { return t->lock ? 4 : t->invert ? (t->plank ? 2 : 3) : (t->plank ? 1 : 0); }
static const char *const LEVER_MODE_NAMES[] = { "STAYS ON", "FLIPS", "TIMED" };
static const char *const GIVE_NAMES[] = { "NOTHING", "A MASK", "A NEEDLE", "SILK" };
static const char *const LEVEL_NAMES[] = { "BEGINNER", "SKILLED", "EXPERT", "CHAMPION" };

static int slots_for(const Thing *t, Slot *s) {
    memset(s, 0, sizeof(Slot) * NSLOTS);
    int n = 0;
#define SLOT(i, l, ...) do { s[n].id = i; s[n].label = l; snprintf(s[n].value, sizeof s[n].value, __VA_ARGS__); n++; } while (0)
#define BUTTON(i, l) do { s[n].id = i; s[n].label = l; s[n].button = true; n++; } while (0)
    switch (t->kind) {
    case TH_LEVER:
        SLOT(S_WIRE, "WIRE", "%d", t->ch);
        SLOT(S_MODE, "LEVER", "%s", LEVER_MODE_NAMES[ge_clampi(t->mode, 0, 2)]);
        if (t->mode == LEVER_TIMED) SLOT(S_TIME, "STAYS ON FOR", "%d.%d S", t->time / 60, (t->time % 60) / 6);
        break;
    case TH_PLATE: SLOT(S_WIRE, "WIRE", "%d", t->ch); break;
    case TH_GATE:
        SLOT(S_GATE_KIND, "KIND", "%s", GATE_KINDS[gate_kind(t)]);
        if (!t->lock) {
            SLOT(S_WIRE, "WIRE", "%d", t->ch);
            SLOT(S_NEED, t->invert ? "APPEARS WHEN" : "OPENS WHEN", "%s", t->need == 0 ? "ALL ARE ON" : "ANY IS ON");
        }
        SLOT(S_WIDTH, "WIDTH", "%d", t->w);
        SLOT(S_HEIGHT, "HEIGHT", "%d", t->h);
        break;
    case TH_RELIC:
        SLOT(S_GIVES, "GIVES", "%s", GIVE_NAMES[ge_clampi(t->gives, 0, GIVE_COUNT - 1)]);
        BUTTON(S_NAME, "NAME IT");
        break;
    case TH_SIGN: BUTTON(S_TEXT, "WRITE ITS WORDS"); break;
    case TH_MARK: BUTTON(S_NAME, "NAME IT"); break;
    case TH_DOOR: {
        int d, r = find_door(t->target, &d);
        char nm[24], lab[24];
        if (r >= 0) { upper(nm, sizeof nm, room_at(r)->name); door_label(&room_at(r)->things[d], lab, sizeof lab); SLOT(S_LINK, "LEADS TO", "%.12s %.10s", nm, lab); }
        else SLOT(S_LINK, "LEADS TO", "%s", t->target[0] ? "(MISSING)" : "NOWHERE");
        if (r >= 0) BUTTON(S_GOTO, "GO THROUGH");
        break;
    }
    case TH_DUELIST:
        if (t->style == STYLE_PARAGON) break;      /* the final boss: as it is */
        SLOT(S_STYLE, "FIGHTS LIKE", "%s", t->style == STYLE_FENCER ? "A FENCER" : "A BRAWLER");
        SLOT(S_LEVEL, "SKILL", "%s", LEVEL_NAMES[ge_clampi(t->level, 0, 3)]);
        SLOT(S_ROUNDS, "ROUNDS TO WIN", "%d", t->rounds);
        break;
    default: break;
    }
    if (!locked_kind(t->kind) && !(t->kind == TH_DUELIST && t->style == STYLE_PARAGON)) { s[NSLOTS - 1].id = S_DELETE; s[NSLOTS - 1].label = "DELETE"; s[NSLOTS - 1].button = true; }
#undef SLOT
#undef BUTTON
    return n;
}

static int slot_at(int x, int y) {
    if (y < TABS_Y) return -1;
    int k = ((y - TABS_Y) / SLOT_H) * 3 + (x - 2) / (SLOT_W + 2);
    int sx = 2 + (k % 3) * (SLOT_W + 2);
    return x >= sx && x < sx + SLOT_W && k >= 0 && k < NSLOTS ? k : -1;
}

/* one press of a setting's arrow (dir -1 or +1) or button */
static void slot_apply(int id, int dir, bool first) {
    Room *r = R();
    if (E.sel < 0 || E.sel >= r->nthings) return;
    Thing *t = &r->things[E.sel];
    if (id == S_DELETE) { remove_piece(E.sel); return; }
    if (id == S_TEXT) { keys_open(KB_SIGN, E.sel, t->text, 63); return; }
    if (id == S_NAME) { keys_open(KB_NAME, E.sel, t->name, 23); return; }
    if (id == S_GOTO) {
        int d, to = find_door(t->target, &d);
        if (to < 0) return;
        switch_room(to);
        E.sel = d;
        center_map(room_at(to)->things[d].col, room_at(to)->things[d].row);
        E.cx = room_at(to)->things[d].col * GE_TILE + 8.0f; E.cy = room_at(to)->things[d].row * (float)GE_TILE;
        char nm[24];
        upper(nm, sizeof nm, R()->name);
        say(nm, GOOD);
        return;
    }
    if (id == S_LINK) {
        DoorRef opts[WORLD_MAX_ROOMS * 8];
        int n = link_options(opts, (int)(sizeof opts / sizeof opts[0])), cur = -1, d, to = find_door(t->target, &d);
        for (int k = 0; k < n; k++) if (opts[k].room == to && opts[k].door == d) cur = k;
        int next = cur + dir;                  /* -1 is NOWHERE */
        if (next < -1) next = n - 1;
        if (next >= n) next = -1;
        if (n == 0) { say("NO DOORS IN OTHER ROOMS YET (ROOM > NEW ROOM MAKES ONE)", WARN); return; }
        set_link(E.sel, next >= 0 ? opts[next].room : -1, next >= 0 ? opts[next].door : -1);
        refresh();
        return;
    }
    Thing before = *t;
    switch (id) {
    case S_WIRE: t->ch = (int8_t)((t->ch - 1 + dir + 9) % 9 + 1); if (is_input(t->kind)) E.last_wire = t->ch; break;
    case S_NEED: t->need = t->need == 0 ? 1 : 0; break;
    case S_WIDTH: t->w = (int16_t)ge_clampi(t->w + dir, 1, 8); break;
    case S_HEIGHT: t->h = (int16_t)ge_clampi(t->h + dir, 1, 8); break;
    case S_GATE_KIND: {
        int k = (gate_kind(t) + dir + 5) % 5;
        t->lock = k == 4; t->invert = k == 2 || k == 3; t->plank = k == 1 || k == 2;
        if (t->lock) t->ch = 0;
        else if (t->ch == 0) t->ch = (int8_t)(E.last_wire > 0 ? E.last_wire : 1);
        break;
    }
    case S_MODE: t->mode = (int8_t)((t->mode + dir + 3) % 3); if (t->mode == LEVER_TIMED && t->time <= 0) t->time = 180; break;
    case S_TIME: t->time = (int16_t)ge_clampi(t->time + dir * 30, 30, 600); break;    /* half-seconds: 0.5 to 10 */
    case S_GIVES: t->gives = (int8_t)((t->gives + dir + GIVE_COUNT) % GIVE_COUNT); break;
    case S_STYLE: t->style = t->style == STYLE_FENCER ? STYLE_BRAWLER : STYLE_FENCER; break;
    case S_LEVEL: t->level = (int8_t)ge_clampi(t->level + dir, 0, 3); break;
    case S_ROUNDS: t->rounds = (int8_t)ge_clampi(t->rounds + dir, 1, 3); break;
    default: return;
    }
    if (t->kind == TH_GATE) {                    /* it must still fit where it stands */
        const char *why = problem(r, t, E.sel);
        if (why) { *t = before; say(why, BAD); return; }
    }
    if (!memcmp(&before, t, sizeof before)) return;
    Thing after = *t;
    *t = before;                                  /* (the snapshot is of the room before the change) */
    if (first) { begin_step(); snap_room(E.room); }
    *t = after;
    mark_edited(E.room);
    refresh();
}

/* ---- picking pieces off the map ---------------------------------------------------------------------- */

static int dist_to_rect(int x, int y, int rx, int ry, int rw, int rh) {
    int dx = x < rx ? rx - x : x >= rx + rw ? x - (rx + rw - 1) : 0;
    int dy = y < ry ? ry - y : y >= ry + rh ? y - (ry + rh - 1) : 0;
    return dx > dy ? dx : dy;
}

/* the piece under the stylus: the nearest within PICK_PX; tapping the same spot again picks the next one there */
static int pick(int x, int y) {
    const Room *r = R();
    int cand[16], dist[16], n = 0;
    for (int i = -1; i < r->nthings && n < 16; i++) {
        int c0, r0, c1, r1;
        if (i < 0) {
            if (E.marker_room != E.room) continue;
            c0 = c1 = E.marker_col; r1 = E.marker_row; r0 = r1 - 1;
        } else {
            thing_cells(&r->things[i], &c0, &r0, &c1, &r1);
            if (r->things[i].kind == TH_ARENA || r->things[i].kind == TH_HALL) { r0 = r1; c1 = c0; }   /* (by their corner only) */
        }
        int d = dist_to_rect(x, y, cell_x(c0), cell_y(r0), (c1 - c0 + 1) * MT, (r1 - r0 + 1) * MT);
        if (d > PICK_PX) continue;
        int k = n++;
        while (k > 0 && dist[k - 1] > d) { cand[k] = cand[k - 1]; dist[k] = dist[k - 1]; k--; }
        cand[k] = i < 0 ? MARKER : i;
        dist[k] = d;
    }
    if (n == 0) return NOTHING;
    if (abs(x - E.last_tap_x) < TAP_PX && abs(y - E.last_tap_y) < TAP_PX)
        for (int k = 0; k < n; k++) if (cand[k] == E.sel) return cand[(k + 1) % n];
    return cand[0];
}

static void select_piece(int i) {
    E.sel = i;
    const Thing *t = &R()->things[i];
    if (is_input(t->kind) && t->ch > 0) E.last_wire = t->ch;
    if (locked_kind(t->kind)) say("MADE IN WORLD.TXT: LOOK, BUT IT STAYS AS IT IS", WARN);
    E.bottom_dirty = true;
}

/* ---- the stylus ------------------------------------------------------------------------------------------ */

static int palette_brushes(int tab, int *out) {
    int n = 0;
    for (int i = 0; i < NBRUSHES; i++) if (BRUSHES[i].tab == tab) out[n++] = i;
    return n;
}

static void tool_press(int t) {
    E.bottom_dirty = true;
    switch (t) {
    case TOOL_UNDO: undo(); break;
    case TOOL_ERASER: E.eraser = !E.eraser; if (E.eraser) { E.brush = -1; E.sel = NOTHING; } say(E.eraser ? "ERASER: TAP A PIECE OR RUB OUT TILES" : "ERASER PUT AWAY", DIM); break;
    case TOOL_ROOM: E.panel = P_ROOM; E.sel = NOTHING; break;
    case TOOL_SAVE: save(); break;
    case TOOL_PLAY: E.want_play = true; break;
    }
}

static void touch_down(int x, int y, const GeInput *in) {
    memset(&G, 0, sizeof G);
    G.down = true;
    G.x0 = G.x = x; G.y0 = G.y = y;
    for (int k = 0; k < 3; k++) { G.hx[k] = x; G.hy[k] = y; }
    E.bottom_dirty = true;
    E.help = 0;
    if (E.panel == P_ROOM) { G.kind = G_PANEL; room_panel_press(x, y); return; }
    if (E.panel == P_KEYS) { G.kind = G_PANEL; keys_press(x, y); return; }
    if (y < TB_H) { G.kind = G_TOOL; G.item = x / TOOL_W; tool_press(G.item); return; }
    if (y >= TABS_Y) {
        if (E.sel >= 0) {
            int k = slot_at(x, y);
            Slot s[NSLOTS];
            slots_for(&R()->things[E.sel], s);
            if (k < 0 || s[k].id == S_NONE) { E.sel = NOTHING; G.kind = G_SWALLOW; return; }   /* off the settings: done with it */
            G.kind = G_SLOT;
            G.item = s[k].id;
            int sx = 2 + (k % 3) * (SLOT_W + 2);
            G.dir = s[k].button ? 0 : x < sx + ARROW_W ? -1 : 1;
            slot_apply(G.item, G.dir ? G.dir : 1, true);
            return;
        }
        if (y < PAL_Y) { G.kind = G_TAB; E.tab = ge_clampi(x / TAB_W, 0, TAB_COUNT - 1); return; }
        int list[16], n = palette_brushes(E.tab, list), k = x / PAL_W;
        if (k >= n) return;
        G.kind = G_PALETTE;
        G.item = list[k];
        return;
    }
    int c = map_col(x), r = map_row(y);
    int p = pick(x, y);
    E.last_tap_x = x; E.last_tap_y = y;
    if (p != NOTHING) {
        G.kind = G_PIECE;
        G.item = p;
        int ac = p == MARKER ? E.marker_col : R()->things[p].col, ar = p == MARKER ? E.marker_row : R()->things[p].row;
        G.grab_dc = ac - c; G.grab_dr = ar - r;
        return;
    }
    if (E.sel >= 0) { E.sel = NOTHING; G.kind = G_SWALLOW; return; }   /* a tap off a selected piece just lets it go */
    char t = brush_tile();
    if (t) {
        begin_step();
        snap_room(E.room);
        if (in->down & GE_BTN_R) { G.kind = G_BOX; G.box_c = c; G.box_r = r; G.cell_c = c; G.cell_r = r; }
        else { G.kind = G_PAINT; G.cell_c = c; G.cell_r = r; paint(c, r, t); }
        return;
    }
    if (E.brush >= 0) { G.kind = G_PLACE; G.item = E.brush; return; }
    G.kind = G_PAN;                                   /* no brush: the map slides under the stylus */
    G.mx0 = E.mx; G.my0 = E.my;
}

static void touch_move(int x, int y) {
    G.x = x; G.y = y;
    G.ticks++;
    if (abs(x - G.x0) >= TAP_PX || abs(y - G.y0) >= TAP_PX) G.moved = true;
    E.bottom_dirty = true;
    if (G.kind == G_PAINT && in_map(x, y)) {
        int c = map_col(x), r = map_row(y);
        if (c != G.cell_c || r != G.cell_r) { paint_line(G.cell_c, G.cell_r, c, r, brush_tile()); G.cell_c = c; G.cell_r = r; }
    } else if (G.kind == G_BOX && in_map(x, y)) {
        G.cell_c = map_col(x); G.cell_r = map_row(y);
    } else if (G.kind == G_PAN) {
        E.mx = G.mx0 - (x - G.x0) / MT;
        E.my = G.my0 - (y - G.y0) / MT;
        clamp_map();
        E.cx = (E.mx + MAP_COLS / 2) * (float)GE_TILE; E.cy = (E.my + MAP_ROWS / 2) * (float)GE_TILE;
    }
}

static bool over_eraser(int x, int y) { return y < TB_H && x / TOOL_W == TOOL_ERASER; }

static void touch_up(void) {
    int x = G.hx[1], y = G.hy[1];                  /* the sample from 2 ticks before the lift: the last one can jump */
    if (!G.moved) { x = G.x0; y = G.y0; }
    switch (G.kind) {
    case G_PIECE:
        if (!G.moved) {
            if (G.item == MARKER) say("PLAY STARTS HERE. DRAG IT WHERE YOU WANT TO TEST", DIM);
            else if (E.eraser) remove_piece(G.item);
            else select_piece(G.item);
        } else if (over_eraser(x, y)) {
            if (G.item == MARKER) say("THE MARKER STAYS (IT ISN'T SAVED)", DIM);
            else remove_piece(G.item);
        } else if (in_map(x, y)) {
            move_piece(G.item, map_col(x) + G.grab_dc, map_row(y) + G.grab_dr);
        } else {
            say("PUT BACK", DIM);
        }
        break;
    case G_PALETTE:
        if (G.moved && in_map(x, y) && !BRUSHES[G.item].tile) { E.brush = G.item; E.eraser = false; place(G.item, map_col(x), map_row(y)); }
        else if (!G.moved || !in_map(x, y)) {
            if (BRUSHES[G.item].locked) say(BRUSHES[G.item].desc, WARN);
            else if (E.brush == G.item) E.brush = -1;
            else { E.brush = G.item; E.eraser = false; }
        } else { E.brush = G.item; E.eraser = false; }
        break;
    case G_PLACE:
        if (in_map(x, y)) place(G.item, map_col(x), map_row(y));
        break;
    case G_BOX: {
        int c0 = G.box_c < G.cell_c ? G.box_c : G.cell_c, c1 = G.box_c < G.cell_c ? G.cell_c : G.box_c;
        int r0 = G.box_r < G.cell_r ? G.box_r : G.cell_r, r1 = G.box_r < G.cell_r ? G.cell_r : G.box_r;
        for (int r = r0; r <= r1; r++) for (int c = c0; c <= c1; c++) paint(c, r, brush_tile());
        end_stroke();
        break;
    }
    case G_PAINT: end_stroke(); break;
    default: break;
    }
    G.kind = G_NONE;
    G.down = false;
    E.bottom_dirty = true;
}

/* dragging near the map's edge, it scrolls that way */
static void edge_scroll(void) {
    bool dragging = G.down && (G.kind == G_PAINT || G.kind == G_BOX || G.kind == G_PLACE || (G.kind == G_PIECE && G.moved) ||
                               (G.kind == G_PALETTE && G.moved));
    int dx = 0, dy = 0;
    if (dragging && in_map(G.x, G.y)) {
        if (G.x < EDGE_PX) dx = -1; else if (G.x >= BW - EDGE_PX) dx = 1;
        if (G.y < MAP_Y + EDGE_PX) dy = -1; else if (G.y >= TABS_Y - EDGE_PX) dy = 1;
    }
    if (!dx && !dy) { G.edge = 0; return; }
    if (++G.edge >= EDGE_TICKS && (G.edge - EDGE_TICKS) % 4 == 0) {
        E.mx += dx; E.my += dy;
        clamp_map();
        E.bottom_dirty = true;
        if (G.kind == G_PAINT) { int c = map_col(G.x), r = map_row(G.y); paint_line(G.cell_c, G.cell_r, c, r, brush_tile()); G.cell_c = c; G.cell_r = r; }
    }
}

/* ---- the update ------------------------------------------------------------------------------------------- */

void editor_open(const EditorHost *h, int room, int col, int row) {
    E.h = *h;
    if (!E.ready) {
        build_tiles8();
        E.ready = true;
        E.brush = -1;
        E.tab = TAB_PUZZLE;
        E.last_wire = 1;
    }
    E.room = ge_clampi(room, 0, E.h.wd->nrooms - 1);
    E.marker_room = E.room;
    E.marker_col = ge_clampi(col, 0, R()->w - 1);
    E.marker_row = ge_clampi(row, 0, R()->h - 1);
    E.panel = P_NONE;
    E.sel = NOTHING;
    E.want_play = false;
    memset(&G, 0, sizeof G);
    E.h.show_room(E.room);
    center_map(E.marker_col, E.marker_row);
    E.cx = E.marker_col * GE_TILE + 8.0f;
    E.cy = (float)(E.marker_row * GE_TILE);
    place_camera();
    if (!E.help_seen) { E.help = 600; E.help_seen = true; }
    if (!E.autosaved && file_exists(mods_path("world.autosave.txt"))) say("AN AUTOSAVE FROM BEFORE IS WAITING: ROOM > RESTORE AUTOSAVE", WARN);
    if (*E.h.world_broken && E.h.world_problem && E.h.world_problem[0]) say(E.h.world_problem, BAD);   /* (SAVE keeps that file aside) */
    check_room();
    E.bottom_dirty = true;
    elog("editor: opened in %s at %d,%d", R()->name, E.marker_col, E.marker_row);
}

EditorAction editor_update(const GeInput *in) {
    E.tick++;
    if (E.msg_timer > 0) E.msg_timer--;
    if (E.confirm_timer > 0 && --E.confirm_timer == 0) { E.confirm = 0; E.bottom_dirty = true; }
    if (E.help > 0) E.help--;
    if (E.panel == P_KEYS && (E.tick & 15) == 0) E.bottom_dirty = true;   /* the cursor blinks */
    if (E.sel >= 0 && (E.tick & 15) == 0) E.bottom_dirty = true;          /* so does the selection */

    if (in->pressed & GE_BTN_START) E.want_play = true;
    if (E.want_play) {
        E.want_play = false;
        g_test_frames = 0;
        E.msg_timer = 0;
        if (G.down && (G.kind == G_PAINT || G.kind == G_BOX)) end_stroke();
        memset(&G, 0, sizeof G);
        E.panel = P_NONE;
        E.sel = NOTHING;
        return ED_PLAY;
    }
    if (in->pressed & GE_BTN_B) {
        if (E.panel) E.panel = P_NONE;
        else if (E.sel >= 0) E.sel = NOTHING;
        else if (E.eraser || E.brush >= 0) { E.eraser = false; E.brush = -1; }
        E.bottom_dirty = true;
    }
    if ((in->pressed & GE_BTN_L) && E.panel != P_KEYS && !G.down) undo();

    /* the D-pad scrolls the map: 10 tiles a second, 40 once it has been held a second */
    int dx = ((in->down & GE_BTN_RIGHT) ? 1 : 0) - ((in->down & GE_BTN_LEFT) ? 1 : 0);
    int dy = ((in->down & GE_BTN_DOWN) ? 1 : 0) - ((in->down & GE_BTN_UP) ? 1 : 0);
    if ((dx || dy) && E.panel == P_NONE) {
        if (E.pan_ticks == 0) E.pan_acc = 1.0f;          /* the first press moves at once */
        E.pan_ticks++;
        E.pan_acc += (E.pan_ticks > 60 ? 40.0f : 10.0f) / 60.0f;
        while (E.pan_acc >= 1.0f) {
            E.pan_acc -= 1.0f;
            E.mx += dx; E.my += dy;
        }
        clamp_map();
        E.cx = (E.mx + MAP_COLS / 2) * (float)GE_TILE; E.cy = (E.my + MAP_ROWS / 2) * (float)GE_TILE;
        E.bottom_dirty = true;
    } else {
        E.pan_ticks = 0;
    }

    /* the stylus */
    if (in->touch) {
        G.hx[2] = G.hx[1]; G.hy[2] = G.hy[1]; G.hx[1] = G.hx[0]; G.hy[1] = G.hy[0];
        G.hx[0] = in->touch_x; G.hy[0] = in->touch_y;
    }
    if (in->touch_pressed) touch_down(in->touch_x, in->touch_y, in);
    else if (in->touch && G.down) touch_move(in->touch_x, in->touch_y);
    else if (!in->touch && G.down) touch_up();
    if (G.down && G.kind == G_SLOT && G.dir && G.ticks >= REPEAT_AFTER && (G.ticks - REPEAT_AFTER) % REPEAT_EVERY == 0) slot_apply(G.item, G.dir, false);
    edge_scroll();
    if (G.down && in_map(G.x, G.y) && (G.kind == G_PIECE || G.kind == G_PAINT || G.kind == G_BOX || G.kind == G_PLACE || G.kind == G_PALETTE)) {
        E.cx = (E.mx + G.x / (float)MT) * GE_TILE;      /* the top screen looks where the stylus is: a magnifier */
        E.cy = (E.my + (G.y - MAP_Y) / (float)MT) * GE_TILE;
    }
    place_camera();
    if (E.want_play) { E.want_play = false; memset(&G, 0, sizeof G); E.panel = P_NONE; E.sel = NOTHING; return ED_PLAY; }
    return ED_STAY;
}

void editor_marker(int *room, int *col, int *row) { *room = E.marker_room; *col = E.marker_col; *row = E.marker_row; }
bool editor_room_edited(int room) { return room >= 0 && room < WORLD_MAX_ROOMS && E.edited[room]; }
void editor_forget_edits(void) { memset(E.edited, 0, sizeof E.edited); }
bool editor_unsaved(void) { return E.unsaved; }

/* ---- drawing: the bottom screen ---------------------------------------------------------------------------- */

static void draw_toolbar(GeImage *fb) {
    ge_fill_rect(fb, 0, 0, BW, TB_H, PANEL, 0);
    for (int t = 0; t < TOOL_COUNT; t++) {
        bool on = (t == TOOL_ERASER && E.eraser) || (t == TOOL_ROOM && E.panel == P_ROOM);
        uint32_t bg = on ? LIT : t == TOOL_PLAY ? GE_RGB(40, 70, 50) : PANEL2;
        uint32_t fg = (t == TOOL_UNDO && g_nundo == 0) ? DIM : TEXT;
        button(fb, t * TOOL_W, 0, TOOL_W, TB_H, TOOL_NAMES[t], bg, fg);
    }
    if (E.unsaved) ge_fill_circle(fb, TOOL_SAVE * TOOL_W + 8, 7, 3, BAD, 0);   /* not saved yet */
}

static void draw_map(GeImage *fb) {
    const Room *r = R();
    const GeTilemap *m = &E.h.w->map;
    uint32_t sky = r->backdrop == BD_CAVE ? GE_RGB(26, 20, 40) : GE_RGB(28, 40, 62);
    ge_fill_rect(fb, 0, MAP_Y, BW, MAP_ROWS * MT, sky, 0);
    bool ok = m->w == r->w && m->h == r->h;
    for (int row = 0; row < MAP_ROWS; row++)
        for (int col = 0; col < MAP_COLS; col++) {
            int c = E.mx + col, rr = E.my + row, x = col * MT, y = MAP_Y + row * MT;
            if (c >= r->w || rr >= r->h) { ge_fill_rect(fb, x, y, MT, MT, GE_RGB(8, 6, 12), 0); continue; }   /* past the room's edge */
            if (!ok) continue;
            int i = rr * m->w + c;
            if (m->type[i] == GE_TILE_EMPTY && m->vis[i] == 0) {
                if (c % 5 == 0 && rr % 5 == 0) ge_fill_rect(fb, x, y, 1, 1, GE_RGB(70, 70, 100), 0);   /* a dot every 5 tiles */
                continue;
            }
            if (m->vis[i] < E.tiles8.w / MT) ge_blit(fb, &E.tiles8, (GeIRect){ m->vis[i] * MT, 0, MT, MT }, x, y, 0, GE_WHITE);
        }
    /* the pieces */
    int sel_wire = E.sel >= 0 && E.sel < r->nthings ? r->things[E.sel].ch : 0;
    for (int i = 0; i < r->nthings; i++) {
        const Thing *t = &r->things[i];
        int c0, r0, c1, r1;
        thing_cells(t, &c0, &r0, &c1, &r1);
        int x = cell_x(c0), y = cell_y(r0), w = (c1 - c0 + 1) * MT, h = (r1 - r0 + 1) * MT;
        if (x > BW || y > TABS_Y || x + w < 0 || y + h < MAP_Y) continue;
        if (t->kind == TH_GATE) {
            uint32_t c = t->lock ? GE_RGB(255, 214, 110) : puzzle_wire_color(t->ch);
            rect_outline(fb, x, y, w, h, c, t->invert);
            if (t->invert) icon(fb, TH_GATE, GV_BRIDGE, t->ch, x, y + h - MT, 1);
        } else if (t->kind == TH_ARENA || t->kind == TH_HALL) {
            rect_outline(fb, x, y, w, h, GE_RGB(160, 130, 70), true);
            padlock(fb, x + 2, y + h - 10);
        } else if (t->kind == TH_DOOR) {
            ge_fill_rect(fb, x + 1, y, MT - 2, 2 * MT, GE_RGB(110, 106, 130), 0);
            ge_fill_rect(fb, x + 2, y + 1, MT - 4, 2 * MT - 1, t->target[0] ? GE_RGB(10, 6, 16) : GE_RGB(90, 30, 30), 0);
        } else if (t->kind != TH_CRATE) {
            icon(fb, t->kind, t->kind == TH_DUELIST && t->style == STYLE_PARAGON, t->ch, x, y, 1);
        }
    }
    /* the selected piece's wire: lines from its switches to its gates */
    if (sel_wire > 0 && E.sel >= 0 && !(r->things[E.sel].kind == TH_GATE && r->things[E.sel].lock))
        for (int a = 0; a < r->nthings; a++) {
            const Thing *s = &r->things[a];
            if (!is_input(s->kind) || s->ch != sel_wire) continue;
            for (int b = 0; b < r->nthings; b++) {
                const Thing *gt = &r->things[b];
                if (gt->kind != TH_GATE || gt->lock || gt->ch != sel_wire) continue;
                ge_draw_line(fb, cell_x(s->col) + 4, cell_y(s->row) + 4, cell_x(gt->col) + gt->w * MT / 2, cell_y(gt->row - gt->h + 1) + gt->h * MT / 2, 1,
                             puzzle_wire_color(sel_wire));
            }
        }
    /* where the top screen is looking */
    const GeCamera *cam = &E.h.w->cam;
    int fx = (int)(cam->x / GE_TILE * MT) - E.mx * MT, fy = MAP_Y + (int)(cam->y / GE_TILE * MT) - E.my * MT;
    rect_outline(fb, fx, fy, (int)(cam->vw / GE_TILE * MT), (int)(cam->vh / GE_TILE * MT), GE_RGB(200, 200, 230), true);
    /* the TEST FROM HERE marker */
    if (E.marker_room == E.room && !(G.kind == G_PIECE && G.item == MARKER && G.moved)) {
        int x = cell_x(E.marker_col), y = cell_y(E.marker_row - 1);
        ge_fill_rect(fb, x + 2, y + 2, 4, 14, GE_WHITE, 0);
        ge_fill_rect(fb, x + 3, y, 2, 2, GE_WHITE, 0);
        ge_fill_rect(fb, x + 6, y + 2, 4, 3, GE_RGB(110, 230, 120), 0);
    }
    /* what the stylus is doing */
    if (G.down && G.kind == G_BOX) {
        int c0 = G.box_c < G.cell_c ? G.box_c : G.cell_c, r0 = G.box_r < G.cell_r ? G.box_r : G.cell_r;
        rect_outline(fb, cell_x(c0), cell_y(r0), (abs(G.cell_c - G.box_c) + 1) * MT, (abs(G.cell_r - G.box_r) + 1) * MT, HI, false);
    }
    if (G.down && G.moved && in_map(G.x, G.y) && (G.kind == G_PIECE || G.kind == G_PALETTE || G.kind == G_PLACE)) {
        int bi = G.kind == G_PIECE ? (G.item == MARKER ? -1 : brush_of(&r->things[G.item])) : G.item;
        int c = map_col(G.x) + (G.kind == G_PIECE ? G.grab_dc : 0), rr = map_row(G.y) + (G.kind == G_PIECE ? G.grab_dr : 0);
        if (bi >= 0 && !BRUSHES[bi].tile) {
            Thing t = G.kind == G_PIECE ? r->things[G.item] : thing_default(BRUSHES[bi].kind, c, rr);
            if (G.kind != G_PIECE && BRUSHES[bi].variant == GV_BRIDGE) { t.w = 3; t.h = 1; t.invert = true; }
            t.col = (int16_t)c; t.row = (int16_t)rr;
            bool bad = problem(r, &t, G.kind == G_PIECE ? G.item : -1) != NULL;
            int c0, r0, c1, r1;
            thing_cells(&t, &c0, &r0, &c1, &r1);
            icon(fb, t.kind, BRUSHES[bi].variant, t.ch, cell_x(c), cell_y(rr), 1);
            rect_outline(fb, cell_x(c0), cell_y(r0), (c1 - c0 + 1) * MT, (r1 - r0 + 1) * MT, bad ? BAD : GOOD, false);
        } else if (G.item == MARKER) {
            rect_outline(fb, cell_x(c), cell_y(rr - 1), MT, 2 * MT, GOOD, false);
        }
    }
    if (E.sel >= 0 && E.sel < r->nthings && ((E.tick / 16) & 1)) {                    /* the selected piece blinks */
        int c0, r0, c1, r1;
        thing_cells(&r->things[E.sel], &c0, &r0, &c1, &r1);
        rect_outline(fb, cell_x(c0) - 1, cell_y(r0) - 1, (c1 - c0 + 1) * MT + 2, (r1 - r0 + 1) * MT + 2, HI, false);
    }
}

static void draw_palette(GeImage *fb) {
    for (int t = 0; t < TAB_COUNT; t++) button(fb, t * TAB_W, TABS_Y, TAB_W, 24, TAB_NAMES[t], t == E.tab ? LIT : PANEL, t == E.tab ? HI : TEXT);
    ge_fill_rect(fb, 0, PAL_Y, BW, 24, INK, 0);
    int list[16], n = palette_brushes(E.tab, list);
    const GeImage *ts = &E.h.w->tileset;
    for (int k = 0; k < n; k++) {
        const Brush *b = &BRUSHES[list[k]];
        int x = k * PAL_W, y = PAL_Y;
        bool armed = list[k] == E.brush;
        ge_fill_rect(fb, x + 1, y + 1, PAL_W - 2, 22, armed ? LIT : PANEL2, 0);
        if (b->tile) {
            uint8_t type, vis;
            Room one = { .w = 1, .h = 1 };
            char c = b->tile;
            one.tiles = &c;
            room_cell(&one, 0, 0, &type, &vis);
            ge_blit(fb, ts, (GeIRect){ vis * GE_TILE, 0, GE_TILE, GE_TILE }, x + 8, y + 4, 0, GE_WHITE);
        } else {
            icon(fb, b->kind, b->variant, E.last_wire, x + 8, y + 4, 2);
            if (b->locked) padlock(fb, x + 22, y + 13);
        }
        if (armed) rect_outline(fb, x + 1, y + 1, PAL_W - 2, 22, HI, false);
    }
    /* the room, at the end of the row */
    char nm[24];
    upper(nm, sizeof nm, R()->name);
    nm[10] = 0;
    int x = n * PAL_W + 6;
    if (x < BW - 40) {
        ge_text(fb, x, PAL_Y + 4, nm, DIM, 1);
        char sz[24];
        snprintf(sz, sizeof sz, "%dX%d", R()->w, R()->h);
        ge_text(fb, x, PAL_Y + 14, sz, DIM, 1);
    }
}

static void draw_slots(GeImage *fb) {
    ge_fill_rect(fb, 0, TABS_Y, BW, 48, INK, 0);
    Slot s[NSLOTS];
    slots_for(&R()->things[E.sel], s);
    for (int k = 0; k < NSLOTS; k++) {
        int x = 2 + (k % 3) * (SLOT_W + 2), y = TABS_Y + (k / 3) * SLOT_H;
        if (s[k].id == S_NONE) continue;
        if (s[k].button) { button(fb, x, y, SLOT_W, SLOT_H, s[k].label, s[k].id == S_DELETE ? GE_RGB(90, 30, 40) : PANEL2, s[k].id == S_DELETE ? BAD : TEXT); continue; }
        ge_fill_rect(fb, x, y + 1, SLOT_W, SLOT_H - 2, PANEL, 0);
        button(fb, x, y, ARROW_W, SLOT_H, "<", PANEL2, TEXT);
        button(fb, x + SLOT_W - ARROW_W, y, ARROW_W, SLOT_H, ">", PANEL2, TEXT);
        int mid = x + SLOT_W / 2;
        ge_text(fb, mid - ge_text_width(s[k].label, 1) / 2, y + 3, s[k].label, DIM, 1);
        int vw = ge_text_width(s[k].value, 1);
        if (s[k].id == S_WIRE) {
            ge_fill_circle(fb, mid - vw / 2 - 6, y + 16, 3, puzzle_wire_color(R()->things[E.sel].ch), 0);
        }
        ge_text(fb, mid - vw / 2, y + 13, s[k].value, TEXT, 1);
    }
}

bool editor_draw_bottom(GeImage *fb) {
    if (!E.bottom_dirty && !G.down) return false;
    E.bottom_dirty = false;
    if (E.panel == P_ROOM) { draw_toolbar(fb); draw_room_panel(fb); return true; }
    if (E.panel == P_KEYS) { draw_keys(fb); return true; }
    draw_toolbar(fb);
    draw_map(fb);
    if (E.sel >= 0 && E.sel < R()->nthings) draw_slots(fb);
    else draw_palette(fb);
    return true;
}

/* ---- drawing: the top screen ------------------------------------------------------------------------------- */

static const GeSprite *creature_sprite(const Thing *t) {
    const Sprites *s = E.h.spr;
    switch (t->kind) {
    case TH_CRAWLER: return &s->crawler;
    case TH_BAT: return &s->bat;
    case TH_SPITTER: return &s->spitter;
    case TH_GRUNT: return &s->brawler;
    case TH_DUELIST: return t->style == STYLE_PARAGON ? &s->paragon : t->style == STYLE_FENCER ? &s->fencer : &s->brawler;
    case TH_BOSS: return &s->warden;
    default: return NULL;
    }
}

static void draw_idle(GeImage *fb, const GeSprite *sp, int x, int y, bool flip, uint32_t tint) {
    GeAnimator a;
    memset(&a, 0, sizeof a);
    a.spr = sp;
    a.clip = ge_sprite_find_clip(sp, "idle");
    if (a.clip < 0) a.clip = 0;
    if (sp->nclips > 0) ge_anim_draw(&a, fb, x, y, flip, 0, tint);
}

static void top_line(GeImage *fb, int y, const char *s, uint32_t c) {
    ge_fill_rect(fb, 0, y, fb->w, 12, GE_ARGB(210, 12, 8, 22), 0);
    ge_text(fb, (fb->w - ge_text_width(s, 1)) / 2, y + 3, s, c, 1);
}

void editor_draw_top(GeImage *fb) {
    const Room *r = R();
    int camx = (int)E.h.w->cam.x, camy = (int)E.h.w->cam.y;
    for (int i = 0; i < r->nthings; i++) {
        const Thing *t = &r->things[i];
        int x = t->col * GE_TILE - camx, y = t->row * GE_TILE - camy;
        if (x < -96 || x > fb->w + 96 || y < -96 || y > fb->h + 96) continue;
        const GeSprite *sp = creature_sprite(t);
        if (sp) draw_idle(fb, sp, x + 8, y + GE_TILE, true, GE_WHITE);
        if (t->kind == TH_GATE) {
            uint32_t c = t->lock ? GE_RGB(255, 214, 110) : puzzle_wire_color(t->ch);
            rect_outline(fb, x, (t->row - t->h + 1) * GE_TILE - camy, t->w * GE_TILE, t->h * GE_TILE, c, t->invert);
        } else if (t->kind == TH_DOOR || t->kind == TH_SIGN || t->kind == TH_RELIC || t->kind == TH_MARK || t->kind == TH_DUELIST) {
            char lab[64], nm[24], dl[24];
            if (t->kind == TH_DOOR) {
                int d, to = find_door(t->target, &d);
                door_label(t, dl, sizeof dl);
                if (to >= 0) { upper(nm, sizeof nm, room_at(to)->name); snprintf(lab, sizeof lab, "DOOR > %s", nm); }
                else snprintf(lab, sizeof lab, "%s > %s", dl, t->target[0] ? "MISSING" : "NOWHERE");
            } else if (t->kind == TH_SIGN) snprintf(lab, sizeof lab, "%.24s%s", t->text, strlen(t->text) > 24 ? ".." : "");
            else snprintf(lab, sizeof lab, "%s", t->name);
            int ly = t->kind == TH_DOOR ? y - GE_TILE - 10 : t->kind == TH_DUELIST ? y - 26 : y - 9;
            ge_text_outlined(fb, x + 8 - ge_text_width(lab, 1) / 2, ly, lab, t->kind == TH_DOOR && !t->target[0] ? BAD : TEXT, INK, 1);
        }
    }
    /* the marker: the hero, see-through */
    if (E.marker_room == E.room) {
        int x = E.marker_col * GE_TILE + 8 - camx, y = (E.marker_row + 1) * GE_TILE - camy;
        draw_idle(fb, &E.h.spr->hero, x, y, false, GE_ARGB(150, 255, 255, 255));
        ge_text_outlined(fb, x - ge_text_width("TEST FROM HERE", 1) / 2, y - 36, "TEST FROM HERE", GOOD, INK, 1);
    }
    if (E.sel >= 0 && E.sel < r->nthings && ((E.tick / 16) & 1)) {
        int c0, r0, c1, r1;
        thing_cells(&r->things[E.sel], &c0, &r0, &c1, &r1);
        rect_outline(fb, c0 * GE_TILE - camx - 1, r0 * GE_TILE - camy - 1, (c1 - c0 + 1) * GE_TILE + 2, (r1 - r0 + 1) * GE_TILE + 2, HI, false);
    }
    /* the stylus, magnified: a crosshair, and what you are carrying */
    if (G.down && in_map(G.x, G.y)) {
        int wx = (int)((E.mx + G.x / (float)MT) * GE_TILE) - camx, wy = (int)((E.my + (G.y - MAP_Y) / (float)MT) * GE_TILE) - camy;
        int c = map_col(G.x), rr = map_row(G.y);
        if (G.kind == G_PIECE && G.moved && G.item != MARKER) { c += G.grab_dc; rr += G.grab_dr; }
        int bi = G.kind == G_PIECE ? (G.item == MARKER ? -1 : brush_of(&r->things[G.item])) : (G.kind == G_PALETTE || G.kind == G_PLACE) ? G.item : -1;
        if (G.moved || G.kind == G_PLACE) {
            if (bi >= 0 && !BRUSHES[bi].tile) {
                Thing t = G.kind == G_PIECE ? r->things[G.item] : thing_default(BRUSHES[bi].kind, c, rr);
                if (G.kind != G_PIECE && BRUSHES[bi].variant == GV_BRIDGE) { t.w = 3; t.h = 1; t.invert = true; }
                t.col = (int16_t)c; t.row = (int16_t)rr;
                const char *why = problem(r, &t, G.kind == G_PIECE ? G.item : -1);
                int c0, r0, c1, r1;
                thing_cells(&t, &c0, &r0, &c1, &r1);
                ge_fill_rect(fb, c0 * GE_TILE - camx, r0 * GE_TILE - camy, (c1 - c0 + 1) * GE_TILE, (r1 - r0 + 1) * GE_TILE, why ? GE_ARGB(90, 255, 60, 60) : GE_ARGB(70, 120, 255, 140), 0);
                icon(fb, t.kind, BRUSHES[bi].variant, t.ch, c * GE_TILE - camx, rr * GE_TILE - camy, 2);
                if (why) ge_text_outlined(fb, c * GE_TILE - camx + 8 - ge_text_width(why, 1) / 2, r0 * GE_TILE - camy - 12, why, BAD, INK, 1);
            } else if (G.kind == G_PIECE && G.item == MARKER) {
                draw_idle(fb, &E.h.spr->hero, c * GE_TILE + 8 - camx, (rr + 1) * GE_TILE - camy, false, GE_ARGB(200, 255, 255, 255));
            }
        }
        if (G.kind == G_BOX) {
            int c0 = G.box_c < G.cell_c ? G.box_c : G.cell_c, r0 = G.box_r < G.cell_r ? G.box_r : G.cell_r;
            rect_outline(fb, c0 * GE_TILE - camx, r0 * GE_TILE - camy, (abs(G.cell_c - G.box_c) + 1) * GE_TILE, (abs(G.cell_r - G.box_r) + 1) * GE_TILE, HI, false);
        }
        ge_fill_rect(fb, wx - 8, wy, 17, 1, GE_ARGB(200, 255, 255, 255), 0);
        ge_fill_rect(fb, wx, wy - 8, 1, 17, GE_ARGB(200, 255, 255, 255), 0);
    }
    /* words: what is picked, at the top; a message or a warning, at the bottom */
    char head[96];
    if (E.sel >= 0 && E.sel < r->nthings) {
        int bi = brush_of(&r->things[E.sel]);
        snprintf(head, sizeof head, "%s: %s", bi >= 0 ? BRUSHES[bi].name : thing_kind_name(r->things[E.sel].kind), bi >= 0 ? BRUSHES[bi].desc : "");
    } else if (E.eraser) snprintf(head, sizeof head, "ERASER: TAP A PIECE TO REMOVE IT, OR RUB OUT TILES");
    else if (E.brush >= 0) snprintf(head, sizeof head, "%s: %s", BRUSHES[E.brush].name, BRUSHES[E.brush].desc);
    else snprintf(head, sizeof head, "EDITING %s. PICK A BRUSH BELOW THE MAP", r->name);
    char up[96];
    upper(up, sizeof up, head);
    top_line(fb, 0, up, HI);
    if (E.msg_timer > 0) top_line(fb, fb->h - 12, E.msg, E.msg_color);
    else if (E.warn[0]) top_line(fb, fb->h - 12, E.warn, WARN);
    else top_line(fb, fb->h - 12, "START: PLAY IT   L: UNDO   R: HOLD TO FILL A BOX   D-PAD: SCROLL", DIM);
    if (E.help > 0) {
        static const char *const HELP[] = {
            "THE WORLD EDITOR",
            "PICK A BRUSH UNDER THE MAP, THEN TAP OR DRAG ON THE MAP.",
            "TAP A PIECE TO CHANGE IT. DRAG IT TO MOVE IT.",
            "DROP IT ON ERASER TO REMOVE IT. L (Q ON A PC) UNDOES.",
            "START (ENTER ON A PC) PLAYS FROM THE MARKER, AND BACK.",
            "SAVE KEEPS YOUR ROOMS IN MODS/WORLD.TXT.",
        };
        int w = 0;
        for (int i = 0; i < 6; i++) { int tw = ge_text_width(HELP[i], 1); if (tw > w) w = tw; }
        int x = (fb->w - w) / 2 - 8, y = 66;
        ge_fill_rect(fb, x, y, w + 16, 6 * 13 + 10, GE_ARGB(230, 16, 10, 30), 0);
        rect_outline(fb, x, y, w + 16, 6 * 13 + 10, GE_RGB(190, 120, 255), false);
        for (int i = 0; i < 6; i++) ge_text(fb, x + 8, y + 7 + i * 13, HELP[i], i == 0 ? HI : TEXT, 1);
    }
}

/* ---- the bottom screen during a test play -------------------------------------------------------------------- */


TestAction editor_test_touch(const GeInput *in) {
    if (!in->touch_pressed) return TP_NONE;
    int x = in->touch_x, y = in->touch_y;
    g_test_frames = 0;
    if (x >= 10 && x < 310 && y >= 26 && y < 74) return TP_EDIT;
    if (y >= 84 && y < 114) {
        if (x < 160) return TP_RESTART;
        if (confirmed(300)) return TP_STOP;
        say("TAP AGAIN TO STOP EDITING AND KEEP PLAYING", WARN);
    }
    return TP_NONE;
}

bool editor_draw_test(GeImage *fb, const Puzzle *pz, bool force) {
    bool changed = force || memcmp(g_test_wires, pz->wire_on, sizeof g_test_wires) != 0;
    if (!changed && (g_test_frames++ % 30) != 0) return false;
    memcpy(g_test_wires, pz->wire_on, sizeof g_test_wires);
    ge_clear(fb, PANEL);
    ge_fill_rect(fb, 0, 0, BW, 20, PANEL2, 0);
    ge_text(fb, 6, 7, "TESTING YOUR ROOM: NOTHING HERE COUNTS", HI, 1);
    button(fb, 10, 26, 300, 48, "BACK TO EDITING (START)", LIT, HI);
    button(fb, 10, 84, 145, 30, "RESTART ROOM", PANEL2, TEXT);
    button(fb, 165, 84, 145, 30, E.confirm == 300 && E.confirm_timer > 0 ? "TAP AGAIN" : "STOP EDITING", PANEL2, TEXT);
    ge_text(fb, 10, 124, "WIRES (LIT WHILE ON):", DIM, 1);
    for (int k = 1; k <= 9; k++) {
        int x = 20 + (k - 1) * 34, y = 150;
        bool used = pz->wire_inputs[k] > 0, on = pz->wire_on[k] > 0;
        ge_fill_circle(fb, x, y, 9, INK, 0);
        ge_fill_circle(fb, x, y, 7, on ? puzzle_wire_color(k) : used ? GE_RGB(60, 52, 80) : PANEL, 0);
        char n[4];
        snprintf(n, sizeof n, "%d", k);
        ge_text(fb, x - 2, y + 14, n, used ? TEXT : DIM, 1);
    }
    if (E.msg_timer > 0) ge_text(fb, (BW - ge_text_width(E.msg, 1)) / 2, 196, E.msg, E.msg_color, 1);
    ge_text(fb, 10, 214, "STOP EDITING: GO ON PLAYING FROM HERE, FOR REAL", DIM, 1);
    ge_text(fb, 10, 226, "SELECT STILL OPENS THE DEV MENU", DIM, 1);
    if (E.confirm_timer > 0 && --E.confirm_timer == 0) E.confirm = 0;
    if (E.msg_timer > 0) E.msg_timer -= 30;
    return true;
}
