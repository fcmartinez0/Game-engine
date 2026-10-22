/* The puzzle pieces, played by the real hero (the game's own art, clips and moves) in small rooms built
 * for each test: press buttons, step the world, look at the cells. Analogy: a test bench for the house's
 * wiring, with a real hand flipping the real switches. */

#include "../demo/demo.h"
#include "../demo/puzzle.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static Sprites spr;
static Chars ch;
static World wd;
static GeWorld w;
static Puzzle pz;
static RoomProgress prog;
static int keys, hero_i;
static GeInput in;

static void on_event(void *user, GeEventType ev, int a, int b) {
    (void)user;
    if (ev == GE_EV_SHOT_TILE) puzzle_shot_tile(&pz, a, b);
}

/* A room 30 x 15 of stone: a floor on row 12 (things stand in row 11), side walls. */
static Room *new_room(void) {
    world_free(&wd);
    Room *r = world_add_room(&wd, "test", 30, 15, BD_CAVE);
    for (int y = 0; y < r->h; y++)
        for (int x = 0; x < r->w; x++) room_set_tile(r, x, y, (y >= 12 || x == 0 || x == r->w - 1) ? 'S' : '.');
    return r;
}

static GeEntity *hero(void) { return &w.ents[hero_i]; }

/* the room as the game would start it: tilemap, the hero standing in a cell, the pieces */
static void enter_at(Room *r, int hero_col, int hero_row, bool keep_progress) {
    if (!keep_progress) { memset(&prog, 0, sizeof prog); keys = 0; }
    ge_world_free(&w);
    ge_world_init(&w, SCREEN_W, SCREEN_H, 7);
    w.on_event = on_event;
    room_build_tilemap(r, &w.map);
    hero_i = ge_world_spawn(&w, &ch.hero, &spr.hero, 0, hero_col * GE_TILE + 8.0f, (hero_row + 1.0f) * GE_TILE, 6);
    w.follow = hero_i;
    puzzle_enter(&pz, r, &prog, &w, hero_i, &keys);
    memset(&in, 0, sizeof in);
}
static void enter(Room *r, int hero_col, bool keep_progress) { enter_at(r, hero_col, 11, keep_progress); }

/* one tick with these buttons held (the overworld's layout: B jump, Y slash, X needle, R dash, A spin) */
static void step(uint32_t buttons) {
    ge_input_update(&in, buttons);
    GeIntent *it = &w.intents[hero_i];
    memset(it, 0, sizeof *it);
    it->move_x = ((buttons & GE_BTN_RIGHT) ? 1.0f : 0.0f) - ((buttons & GE_BTN_LEFT) ? 1.0f : 0.0f);
    it->move_y = ((buttons & GE_BTN_DOWN) ? 1.0f : 0.0f) - ((buttons & GE_BTN_UP) ? 1.0f : 0.0f);
    it->jump_pressed = (in.pressed & GE_BTN_B) != 0;
    it->jump_held = (buttons & GE_BTN_B) != 0;
    it->attack_pressed = (in.pressed & GE_BTN_Y) != 0;
    it->tool_pressed = (in.pressed & GE_BTN_X) != 0;
    it->skill_pressed = (in.pressed & GE_BTN_A) != 0;
    it->dash_pressed = (in.pressed & GE_BTN_R) != 0;
    puzzle_pre_update(&pz, it);
    ge_world_update(&w);
    puzzle_update(&pz, &in);
}
static void wait(int n) { for (int i = 0; i < n; i++) step(0); }
static void hold(uint32_t b, int n) { for (int i = 0; i < n; i++) step(b); }
static void tap(uint32_t b) { step(b); step(0); }

static int cell(int col, int row) { return ge_tilemap_get(&w.map, col, row); }
static bool gate_shut(int col, int top, int bottom) {
    for (int y = top; y <= bottom; y++) if (cell(col, y) != GE_TILE_SOLID) return false;
    return true;
}
static bool gate_open(int col, int top, int bottom) {
    for (int y = top; y <= bottom; y++) if (cell(col, y) != GE_TILE_EMPTY) return false;
    return true;
}
static int add(Room *r, int kind, int col, int row) { return room_add_thing(r, thing_default(kind, col, row)); }
static int count_events(PuzzleEventKind k) { int n = 0; for (int i = 0; i < pz.nev; i++) n += pz.ev[i].kind == k; return n; }

static void test_lever_opens_gate(void) {
    puts("a lever beside a gate: one slash opens it, and it stays open");
    Room *r = new_room();
    add(r, TH_LEVER, 6, 11);                       /* ONCE, wire 1 */
    add(r, TH_GATE, 9, 11);                        /* 1 x 3, wire 1: rows 9-11 */
    enter(r, 5, false);
    wait(10);
    CHECK(gate_shut(9, 9, 11));
    tap(GE_BTN_Y);
    wait(20);
    CHECK(gate_open(9, 9, 11));
    CHECK(prog.done[0]);
    wait(200);
    CHECK(gate_open(9, 9, 11));
    /* back in the room: still open, no animation */
    room_build_tilemap(r, &w.map);
    puzzle_enter(&pz, r, &prog, &w, hero_i, &keys);
    CHECK(gate_open(9, 9, 11));
}

static void test_lever_not_through_wall(void) {
    puts("a lever behind a wall can't be slashed through it");
    Room *r = new_room();
    room_set_tile(r, 7, 11, 'S'); room_set_tile(r, 7, 10, 'S');   /* a thin wall between the hero and the lever */
    add(r, TH_LEVER, 8, 11);
    add(r, TH_GATE, 12, 11);
    enter(r, 6, false);
    wait(10);
    hold(GE_BTN_RIGHT, 6);
    tap(GE_BTN_Y);
    wait(30);
    CHECK(!pz.p[0].on);
    CHECK(gate_shut(12, 9, 11));
}

static void test_toggle(void) {
    puts("a toggle lever: a two-hit combo counts once; two separate hits switch it back");
    Room *r = new_room();
    int lv = add(r, TH_LEVER, 6, 11);
    r->things[lv].mode = LEVER_TOGGLE;
    add(r, TH_GATE, 9, 11);
    enter(r, 5, false);
    wait(10);
    tap(GE_BTN_Y); wait(5); tap(GE_BTN_Y);         /* Y, Y: one swing chain */
    wait(20);
    CHECK(pz.p[lv].on);
    CHECK(gate_open(9, 9, 11));
    wait(30);
    tap(GE_BTN_Y);
    wait(40);
    CHECK(!pz.p[lv].on);
    CHECK(gate_shut(9, 9, 11));
    CHECK(!prog.done[lv]);                         /* toggles are not remembered */
}

static void test_timed(void) {
    puts("a timed lever: off when its time runs out; hitting it again starts the time over");
    Room *r = new_room();
    int lv = add(r, TH_LEVER, 6, 11);
    r->things[lv].mode = LEVER_TIMED;
    r->things[lv].time = 120;
    add(r, TH_GATE, 9, 11);
    enter(r, 5, false);
    wait(10);
    tap(GE_BTN_Y);
    wait(60);
    CHECK(pz.p[lv].on);
    CHECK(gate_open(9, 9, 11));
    tap(GE_BTN_Y);                                 /* 62 ticks in: the time starts over */
    wait(100);
    CHECK(pz.p[lv].on);                            /* it would have run out by now */
    wait(40);
    CHECK(!pz.p[lv].on);
    wait(40);
    CHECK(gate_shut(9, 9, 11));
}

static void test_gate_waits(void) {
    puts("a gate never shuts on the hero: it waits, then shuts once they are out");
    Room *r = new_room();
    int lv = add(r, TH_LEVER, 6, 11);
    r->things[lv].mode = LEVER_TIMED;
    r->things[lv].time = 60;
    add(r, TH_GATE, 9, 11);
    enter(r, 5, false);
    wait(10);
    tap(GE_BTN_Y);
    wait(10);
    CHECK(gate_open(9, 9, 11));
    hold(GE_BTN_RIGHT, 20);                        /* walk into the gateway and stop there */
    for (int i = 0; i < 40 && fabsf(hero()->pos.x - (9 * GE_TILE + 8)) > 2.0f; i++) step(hero()->pos.x < 9 * GE_TILE + 8 ? GE_BTN_RIGHT : GE_BTN_LEFT);
    wait(150);                                     /* the lever ran out long ago */
    CHECK(!pz.p[lv].on);
    int col = (int)floorf(hero()->pos.x / GE_TILE);
    CHECK(col == 9);
    CHECK(cell(9, 11) == GE_TILE_EMPTY && cell(9, 10) == GE_TILE_EMPTY);   /* not over the hero */
    CHECK(hero()->active && hero()->state != GE_ST_DEAD);
    hold(GE_BTN_RIGHT, 30);                        /* out the far side */
    wait(60);
    CHECK(gate_shut(9, 9, 11));
}

static void test_crate_plate(void) {
    puts("a crate pushed onto a plate holds the gate open, and is still there when you come back");
    Room *r = new_room();
    int crate = add(r, TH_CRATE, 6, 11);
    add(r, TH_PLATE, 8, 11);
    add(r, TH_GATE, 14, 11);
    enter(r, 3, false);
    wait(10);
    CHECK(gate_shut(14, 9, 11));
    for (int i = 0; i < 300 && !(pz.p[crate].col == 8 && pz.p[crate].dx == 0); i++) step(GE_BTN_RIGHT);   /* push it two cells: */
    hold(GE_BTN_RIGHT, 6);                                                                                 /* the plate catches it */
    CHECK(pz.p[crate].col == 8 && pz.p[crate].dx == 0);
    wait(30);                                                                                              /* and let go */
    CHECK(pz.p[crate].col == 8);
    CHECK(gate_open(14, 9, 11));
    CHECK(prog.crate_homed[crate]);
    /* leave and come back */
    enter(r, 3, true);
    wait(20);
    CHECK(pz.p[crate].col == 8 && pz.p[crate].row == 11);
    CHECK(cell(8, 11) == GE_TILE_SOLID);
    CHECK(gate_open(14, 9, 11));
}

static void test_crate_falls(void) {
    puts("a crate pushed off a ledge falls onto a plate below");
    Room *r = new_room();
    for (int x = 1; x <= 7; x++) room_set_tile(r, x, 8, 'S');    /* a ledge */
    int crate = add(r, TH_CRATE, 6, 7);
    add(r, TH_PLATE, 8, 11);
    add(r, TH_GATE, 14, 11);
    enter(r, 3, false);
    hero()->pos.y = 8.0f * GE_TILE;               /* up on the ledge */
    wait(10);
    hold(GE_BTN_RIGHT, 60);
    wait(60);
    CHECK(pz.p[crate].col == 8 && pz.p[crate].row == 11);
    CHECK(gate_open(14, 9, 11));
}

static void test_crack(void) {
    puts("cracked stone: the first hit cracks it, the second breaks it; a needle breaks it too");
    Room *r = new_room();
    for (int y = 9; y <= 11; y++) room_set_tile(r, 8, y, 'C');
    enter(r, 6, false);
    wait(10);
    hold(GE_BTN_RIGHT, 4);
    tap(GE_BTN_Y);
    wait(30);
    CHECK(cell(8, 11) == GE_TILE_SOLID);
    CHECK(w.map.vis[11 * w.map.w + 8] == TV_CRACK);
    tap(GE_BTN_Y);
    wait(30);
    CHECK(cell(8, 11) == GE_TILE_EMPTY && cell(8, 10) == GE_TILE_EMPTY && cell(8, 9) == GE_TILE_EMPTY);
    CHECK(prog.nbroken == 3);
    CHECK(cell(8, 8) == GE_TILE_EMPTY);
    /* broken for the session */
    enter(r, 6, true);
    CHECK(cell(8, 10) == GE_TILE_EMPTY);
    /* a needle, twice */
    Room *r2 = new_room();
    for (int y = 9; y <= 11; y++) room_set_tile(r2, 20, y, 'C');
    enter(r2, 6, false);
    wait(10);
    tap(GE_BTN_X);
    wait(60);
    CHECK(cell(20, 10) == GE_TILE_SOLID);
    tap(GE_BTN_X);
    wait(60);
    CHECK(cell(20, 10) == GE_TILE_EMPTY);
}

static void test_crack_redress(void) {
    puts("stone under broken stone gets its top edge");
    Room *r = new_room();
    room_set_tile(r, 8, 11, 'C');
    room_set_tile(r, 8, 10, 'C');
    room_set_tile(r, 9, 11, 'S');               /* a block whose top is under the crack's side: unaffected */
    enter(r, 6, false);
    CHECK(w.map.vis[12 * w.map.w + 8] == TV_STONE);   /* covered by the crack */
    wait(10);
    hold(GE_BTN_RIGHT, 4);
    tap(GE_BTN_Y); wait(30); tap(GE_BTN_Y); wait(30);
    CHECK(cell(8, 11) == GE_TILE_EMPTY);
    CHECK(w.map.vis[12 * w.map.w + 8] == TV_STONE_TOP);
    enter(r, 6, true);                           /* and when the room is entered again */
    CHECK(w.map.vis[12 * w.map.w + 8] == TV_STONE_TOP);
}

static void test_needle_off_the_edge(void) {
    puts("a needle leaving the room breaks nothing");
    Room *r = new_room();
    room_set_tile(r, r->w - 1, 11, 'C');          /* breakable stone at the edge would be the wrong thing to break */
    room_set_tile(r, r->w - 1, 10, '.');
    room_set_tile(r, r->w - 1, 9, '.');
    for (int y = 0; y < 9; y++) room_set_tile(r, r->w - 1, y, '.');
    room_set_tile(r, r->w - 1, 11, '.');
    enter(r, r->w - 4, false);
    wait(10);
    int solid_before = 0, solid_after = 0;
    for (int i = 0; i < w.map.w * w.map.h; i++) solid_before += w.map.type[i] == GE_TILE_SOLID;
    tap(GE_BTN_X);
    wait(60);
    for (int i = 0; i < w.map.w * w.map.h; i++) solid_after += w.map.type[i] == GE_TILE_SOLID;
    CHECK(solid_before == solid_after);
    CHECK(pz.nshot_cells == 0);
}

static void test_key_lock(void) {
    puts("a key opens a locked gate; without one it stays shut");
    Room *r = new_room();
    int lock = add(r, TH_GATE, 12, 11);
    r->things[lock].lock = true; r->things[lock].ch = 0;
    int key = add(r, TH_KEY, 8, 11);
    enter(r, 3, false);
    wait(10);
    CHECK(gate_shut(12, 9, 11));
    bool got = false;
    for (int i = 0; i < 120 && !got; i++) { step(GE_BTN_RIGHT); got = count_events(PZ_KEY) > 0 || got; }
    CHECK(prog.done[key]);
    for (int i = 0; i < 120 && !prog.done[lock]; i++) step(GE_BTN_RIGHT);
    CHECK(prog.done[lock]);
    CHECK(keys == 0);
    wait(20);
    CHECK(gate_open(12, 9, 11));
    /* no key: walking into it does nothing */
    Room *r2 = new_room();
    int lock2 = add(r2, TH_GATE, 12, 11);
    r2->things[lock2].lock = true; r2->things[lock2].ch = 0;
    enter(r2, 3, false);
    hold(GE_BTN_RIGHT, 150);
    CHECK(gate_shut(12, 9, 11));
    CHECK(hero()->pos.x < 12 * GE_TILE);
}

static void test_bridge(void) {
    puts("a bridge (an inverted plank gate) appears when its lever is on");
    Room *r = new_room();
    for (int x = 10; x <= 14; x++) { room_set_tile(r, x, 12, '.'); room_set_tile(r, x, 13, '.'); room_set_tile(r, x, 14, '^'); }
    int br = add(r, TH_GATE, 10, 12);
    r->things[br].w = 5; r->things[br].h = 1; r->things[br].invert = true; r->things[br].plank = true;
    add(r, TH_LEVER, 6, 11);
    enter(r, 5, false);
    wait(10);
    CHECK(cell(12, 12) == GE_TILE_EMPTY);
    tap(GE_BTN_Y);
    wait(20);
    CHECK(cell(12, 12) == GE_TILE_ONEWAY);
    hold(GE_BTN_RIGHT, 160);                       /* walk across */
    CHECK(hero()->pos.x > 15 * GE_TILE);
    CHECK(hero()->pos.y <= 12.0f * GE_TILE + 0.5f);
}

static void test_door_not_on_up_slash(void) {
    puts("UP at a door goes through; UP+Y (an up-slash) does not");
    Room *r = new_room();
    add(r, TH_DOOR, 5, 11);
    enter(r, 5, false);
    wait(20);
    step(GE_BTN_UP | GE_BTN_Y);
    CHECK(count_events(PZ_DOOR) == 0);
    wait(30);
    step(GE_BTN_UP);
    CHECK(count_events(PZ_DOOR) == 1);
}

static void test_gate_arrow(void) {
    puts("a gate that opens out of view gets an arrow pointing at it");
    Room *r = new_room();
    room_resize(r, 80, 15);
    for (int x = 0; x < r->w; x++) for (int y = 12; y < 15; y++) room_set_tile(r, x, y, 'S');
    add(r, TH_LEVER, 6, 11);
    add(r, TH_GATE, 70, 11);
    add(r, TH_GATE, 9, 11);
    enter(r, 5, false);
    w.cam.x = 0; w.cam.y = 0;
    wait(10);
    tap(GE_BTN_Y);
    wait(2);
    CHECK(pz.p[1].alert > 0);                      /* far away */
    CHECK(pz.p[2].alert == 0);                     /* right here */
    static GeImage fb;
    if (!fb.px) fb = ge_image_create(SCREEN_W, SCREEN_H);
    ge_clear(&fb, 0);
    for (int i = 0; i < 16; i++) { puzzle_draw_hints(&pz, &fb, 0, 0, (uint32_t)i); }
    int lit = 0;
    for (int y = 0; y < SCREEN_H; y++) for (int x = SCREEN_W - 30; x < SCREEN_W; x++) lit += (fb.px[y * fb.w + x] & 0xffffff) != 0;
    CHECK(lit > 0);                                /* at the right edge */
    ge_image_free(&fb);
}

/* ---- the sample rooms, played through ------------------------------------------- */

static Room *builtin_room(const char *name) {
    world_builtin(&wd);
    int i = world_find_room(&wd, name);
    return i >= 0 ? &wd.rooms[i] : NULL;
}
static int nth(const Room *r, int kind, int n) {
    int i = room_find(r, kind, 0);
    while (i >= 0 && n-- > 0) i = room_find(r, kind, i + 1);
    return i;
}
/* walk until the hero stands within 2 px of a column's centre (false if it takes over 10 s) */
static bool walk_to(int col) {
    for (int i = 0; i < 600; i++) {
        float dx = col * GE_TILE + 8.0f - hero()->pos.x;
        if (fabsf(dx) < 2.0f && hero()->on_ground) { wait(4); return true; }
        step(dx > 0 ? GE_BTN_RIGHT : GE_BTN_LEFT);
    }
    printf("  (stuck at x %.0f, y %.0f on the way to column %d)\n", hero()->pos.x, hero()->pos.y, col);
    return false;
}
static void up_slash(void) { step(GE_BTN_UP | GE_BTN_Y); step(GE_BTN_UP); wait(24); }

static void test_hollow_route(void) {
    puts("the Hollow, played through: each beat can be done, in order");
    Room *r = builtin_room("hollow");
    CHECK(r != NULL);
    if (!r) return;
    enter_at(r, 3, 11, false);
    wait(10);
    CHECK(walk_to(5));
    tap(GE_BTN_Y); wait(20);
    CHECK(gate_open(8, 9, 11));
    CHECK(walk_to(12));                            /* under the hanging lever */
    up_slash();
    CHECK(gate_open(16, 9, 11));
    CHECK(walk_to(17));
    tap(GE_BTN_X); wait(50);                       /* the needle, across the pit */
    CHECK(cell(22, 12) == GE_TILE_ONEWAY);
    CHECK(walk_to(30));
    tap(GE_BTN_Y);                                 /* the timed lever */
    int timed = nth(r, TH_LEVER, 3);
    CHECK(pz.p[timed].on);
    CHECK(walk_to(33));
    hold(GE_BTN_RIGHT | GE_BTN_B, 14);             /* over the spikes */
    hold(GE_BTN_RIGHT, 30);
    CHECK(walk_to(43));
    CHECK(pz.p[timed].on);                         /* made it with time to spare */
    CHECK(hero()->pos.x > 42 * GE_TILE);
    CHECK(walk_to(47));
    tap(GE_BTN_Y); wait(24); tap(GE_BTN_Y); wait(24);
    CHECK(cell(48, 10) == GE_TILE_EMPTY);
    CHECK(walk_to(49));
    CHECK(prog.done[nth(r, TH_RELIC, 0)]);
}

static void test_deep_route(void) {
    puts("the Deep, played through: crate, chimney, key, secret, lock, the way back");
    Room *r = builtin_room("deep");
    CHECK(r != NULL);
    if (!r) return;
    enter_at(r, 3, 4, false);
    wait(10);
    int crate = nth(r, TH_CRATE, 0);
    for (int i = 0; i < 400 && !(pz.p[crate].row == 11 && pz.p[crate].dy == 0); i++) step(GE_BTN_RIGHT);   /* off the edge */
    wait(40);
    CHECK(pz.p[crate].col == 10 && pz.p[crate].row == 11);
    CHECK(gate_open(18, 9, 11));
    CHECK(walk_to(24));                            /* under the chimney */
    hold(GE_BTN_B, 14); step(0); hold(GE_BTN_B, 18); wait(40);   /* jump, jump again: up onto its plank */
    CHECK(prog.done[nth(r, TH_KEY, 0)]);
    CHECK(hero()->on_ground && fabsf(hero()->pos.y - 7.0f * GE_TILE) < 0.5f);
    up_slash(); up_slash();                        /* the hidden stone above */
    CHECK(cell(24, 4) == GE_TILE_EMPTY);
    hold(GE_BTN_B, 16); wait(40);
    CHECK(prog.done[nth(r, TH_RELIC, 0)]);
    step(GE_BTN_DOWN | GE_BTN_B); hold(GE_BTN_DOWN, 4); wait(40);   /* down through the plank */
    CHECK(hero()->pos.y > 11.0f * GE_TILE);
    CHECK(walk_to(28));                            /* the key opens the lock on the way */
    CHECK(prog.done[nth(r, TH_GATE, 1)] && keys == 0);
    CHECK(walk_to(30));
    wait(60);                                      /* down the drop */
    CHECK(hero()->pos.y > 20.0f * GE_TILE);
    CHECK(walk_to(23));
    tap(GE_BTN_Y); wait(30);                       /* facing left: the lever */
    CHECK(gate_open(8, 18, 20));
    CHECK(walk_to(4));
    step(GE_BTN_UP);
    CHECK(count_events(PZ_DOOR) == 1 && pz.door_near == nth(r, TH_DOOR, 1));
}

int main(void) {
    art_make_sheets(&spr);
    if (chars_init(&ch, &spr) != 0) { puts("characters failed to build"); return 1; }
    test_lever_opens_gate();
    test_lever_not_through_wall();
    test_toggle();
    test_timed();
    test_gate_waits();
    test_crate_plate();
    test_crate_falls();
    test_crack();
    test_crack_redress();
    test_needle_off_the_edge();
    test_key_lock();
    test_bridge();
    test_door_not_on_up_slash();
    test_gate_arrow();
    test_hollow_route();
    test_deep_route();
    world_free(&wd);
    ge_world_free(&w);
    art_free(&spr);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
