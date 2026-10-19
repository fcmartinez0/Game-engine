/* The creatures' minds (demo/ai.c), played in small hand-made rooms with the game's own hero, art and physics.
 * Each test builds a room, puts a creature and a motionless, unhurtable hero in it, runs the world tick by tick
 * and looks at where the creature goes and what it does. */

#include "../demo/demo.h"
#include "../demo/ai.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static Sprites spr;
static Chars ch;
static GeWorld w;
static int hero_i;

#define RW 40
#define RH 15

/* rows of the room: # ground  ^ spikes  = plank  . empty. Row 12 is the floor in an unchanged room. */
static void room(const char *const *rows, int n) {
    ge_world_free(&w);
    ge_world_init(&w, SCREEN_W, SCREEN_H, 11);
    ge_tilemap_create(&w.map, RW, RH);
    for (int y = 0; y < RH; y++)
        for (int x = 0; x < RW; x++) {
            char c = y < n ? rows[y][x] : '.';
            w.map.type[y * RW + x] = c == '#' ? GE_TILE_SOLID : c == '^' ? GE_TILE_SPIKE : c == '=' ? GE_TILE_ONEWAY : GE_TILE_EMPTY;
        }
}

/* a blank room with a floor at row 12 and walls at the edges; callers carve into it */
static char g_rows[RH][RW + 1];
static const char *g_ptrs[RH];
static void blank(void) {
    for (int y = 0; y < RH; y++) {
        for (int x = 0; x < RW; x++) g_rows[y][x] = y >= 12 ? '#' : '.';
        g_rows[y][RW] = 0;
        g_ptrs[y] = g_rows[y];
    }
}
static void put(int x0, int x1, int y0, int y1, char c) { for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) g_rows[y][x] = c; }
static void build(void) { room(g_ptrs, RH); }

static GeEntity *E(int i) { return &w.ents[i]; }

/* a motionless hero standing with their feet in the cell (col, row) */
static void place_hero(int col, int row) {
    hero_i = ge_world_spawn(&w, &ch.hero, &spr.hero, 0, col * GE_TILE + 8.0f, (row + 1.0f) * GE_TILE, 99);
    w.follow = hero_i;
}

static int creature(int kind, int col, int row) {
    const GeActorDef *d = kind == EN_CRAWLER ? &ch.crawler : kind == EN_BAT ? &ch.bat : kind == EN_SPITTER ? &ch.spitter : &ch.grunt;
    const GeSprite *sp = kind == EN_CRAWLER ? &spr.crawler : kind == EN_BAT ? &spr.bat : kind == EN_SPITTER ? &spr.spitter : &spr.brawler;
    float x = col * GE_TILE + 8.0f, y = (row + 1.0f) * GE_TILE;
    int i = ge_world_spawn(&w, d, sp, 1, x, y, 100);
    GeEntity *e = E(i);
    e->kind = kind; e->facing = -1; e->ai_x0 = x; e->ai_y0 = y;
    ai_reset(i);
    return i;
}

/* (a creature spawns facing left, like the game's: tests that want it looking at the hero turn it) */
static int creature_facing(int kind, int col, int row, int facing) { int i = creature(kind, col, row); E(i)->facing = facing; return i; }

static int g_hostile_shots, g_prev_shots;
static void step(int *creatures, int n) {
    GeEntity *h = E(hero_i);
    h->invuln = 5;                                          /* the hero can't be hurt or pushed around */
    h->hp = h->max_hp;
    memset(&w.intents[hero_i], 0, sizeof w.intents[hero_i]);
    for (int k = 0; k < n; k++) {
        GeEntity *e = E(creatures[k]);
        if (!e->active || e->state == GE_ST_DEAD) continue;
        memset(&w.intents[creatures[k]], 0, sizeof(GeIntent));
        ai_think(&w, creatures[k], h->active ? h : NULL, &w.intents[creatures[k]]);
    }
    ge_world_update(&w);
    int shots = 0;
    for (int i = 0; i < GE_MAX_SHOTS; i++) shots += w.shots[i].active && w.shots[i].team == 1;
    if (shots > g_prev_shots) g_hostile_shots += shots - g_prev_shots;
    g_prev_shots = shots;
}
static void run(int *c, int n, int ticks) { for (int i = 0; i < ticks; i++) step(c, n); }
static bool in_mode(int idx, const char *name) { return !strcmp(ai_mode_name(idx, 0), name); }

/* run until a mode shows up (false if it never does within `ticks`) */
static bool wait_mode(int idx, const char *name, int ticks) {
    for (int i = 0; i < ticks; i++) { step(&idx, 1); if (in_mode(idx, name)) return true; }
    return false;
}

static void settle(int *c, int n) { run(c, n, 30); }       /* land, and let them look once */

/* ---- eyes ---------------------------------------------------------------------------------------------- */

static void test_line_of_sight(void) {
    puts("sight: a clear line, a wall between, a wall behind");
    blank();
    put(15, 15, 9, 11, '#');
    build();
    CHECK(ai_line_clear(&w.map, 40, 180, 200, 180));          /* open floor level, left of the wall */
    CHECK(!ai_line_clear(&w.map, 40, 180, 400, 180));         /* the wall at column 15 is between */
    CHECK(ai_line_clear(&w.map, 260, 180, 400, 180));         /* both on the far side of it */
    CHECK(ai_line_clear(&w.map, 40, 100, 400, 100));          /* above the wall */
}

/* ---- the spitter --------------------------------------------------------------------------------------- */

static void test_spitter(void) {
    puts("the spitter: spits at what it can see, never through a wall, never from too far");
    int c[1];
    blank(); build();
    place_hero(14, 11);
    c[0] = creature(EN_SPITTER, 8, 11);
    g_hostile_shots = g_prev_shots = 0;
    settle(c, 1);
    run(c, 1, 900);
    CHECK(g_hostile_shots >= 3);                              /* in sight, 6 columns away: it keeps spitting */
    CHECK(E(c[0])->facing == 1);                              /* and faces you */

    blank(); put(11, 11, 8, 11, '#'); build();                /* a wall between them */
    place_hero(14, 11);
    c[0] = creature(EN_SPITTER, 8, 11);
    g_hostile_shots = g_prev_shots = 0;
    settle(c, 1);
    run(c, 1, 900);
    CHECK(g_hostile_shots == 0);

    blank(); build();
    place_hero(36, 11);                                       /* 28 columns: far out of range */
    c[0] = creature(EN_SPITTER, 8, 11);
    g_hostile_shots = g_prev_shots = 0;
    settle(c, 1);
    run(c, 1, 600);
    CHECK(g_hostile_shots == 0);

    /* not a metronome: the gaps between its spits aren't all the same */
    blank(); build();
    place_hero(14, 11);
    c[0] = creature(EN_SPITTER, 8, 11);
    int at[8], n = 0;
    g_hostile_shots = g_prev_shots = 0;
    for (int t = 0; t < 1500 && n < 8; t++) { int before = g_hostile_shots; step(c, 1); if (g_hostile_shots > before) at[n++] = t; }
    bool varied = false;
    for (int i = 2; i < n; i++) if (abs((at[i] - at[i - 1]) - (at[1] - at[0])) > 6) varied = true;
    CHECK(n >= 5 && varied);
}

/* ---- the grunt ------------------------------------------------------------------------------------------ */

static void test_grunt_hops_a_step(void) {
    puts("the grunt: hops a one-tile step to get at you");
    int c[1];
    blank(); put(13, 13, 11, 11, '#'); build();               /* a step one tile high */
    place_hero(18, 11);
    c[0] = creature_facing(EN_GRUNT, 9, 11, 1);
    settle(c, 1);
    float best = 0;
    for (int t = 0; t < 500; t++) { step(c, 1); if (E(c[0])->pos.x > best) best = E(c[0])->pos.x; }
    CHECK(best > 15 * GE_TILE);                               /* well past the step */
}

static void test_grunt_waits_at_a_wall(void) {
    puts("the grunt: a two-tile pillar is more than it can hop: it waits at the foot of it");
    int c[1];
    blank(); put(14, 14, 10, 11, '#'); build();               /* a pillar, with you standing on it */
    place_hero(14, 9);
    c[0] = creature_facing(EN_GRUNT, 8, 11, 1);
    settle(c, 1);
    float best = 0;
    for (int t = 0; t < 600; t++) { step(c, 1); if (E(c[0])->pos.x > best) best = E(c[0])->pos.x; }
    CHECK(best < 14 * GE_TILE);                               /* never past the pillar */
    CHECK(best > 11 * GE_TILE);                               /* but it came for you: it is at the foot of it */
    CHECK(E(c[0])->pos.y > 11.5f * GE_TILE);                  /* on the floor, not on the pillar */
}

static void test_grunt_drops_and_doesnt_jump_off_cliffs(void) {
    puts("the grunt: follows you down a short drop, but not off a tall cliff or into a pit");
    int c[1];
    blank(); put(3, 9, 10, 11, '#'); build();                 /* a ledge two tiles up: a short drop */
    place_hero(16, 11);
    c[0] = creature(EN_GRUNT, 6, 9);
    settle(c, 1);
    run(c, 1, 500);
    CHECK(E(c[0])->pos.y > 11.5f * GE_TILE && E(c[0])->pos.x > 9 * GE_TILE);   /* down on the floor, on its way to you */

    blank(); put(3, 9, 6, 11, '#'); build();                  /* a ledge six tiles up */
    place_hero(16, 11);
    c[0] = creature(EN_GRUNT, 6, 5);
    settle(c, 1);
    float lowest = 0;
    for (int t = 0; t < 500; t++) { step(c, 1); if (E(c[0])->pos.y > lowest) lowest = E(c[0])->pos.y; }
    CHECK(lowest < 6.5f * GE_TILE);                           /* stayed on the ledge's top */

    blank(); put(12, 15, 12, 14, '.'); put(12, 15, 14, 14, '^'); build();   /* a pit with spikes */
    place_hero(20, 11);
    c[0] = creature(EN_GRUNT, 7, 11);
    settle(c, 1);
    float far = 0;
    for (int t = 0; t < 600; t++) { step(c, 1); if (E(c[0])->pos.x > far) far = E(c[0])->pos.x; }
    CHECK(far < 12 * GE_TILE);                                /* stopped at the pit's edge */
    CHECK(E(c[0])->active && E(c[0])->pos.y < 12.5f * GE_TILE);
}

static void test_grunt_gives_up(void) {
    puts("the grunt: goes to where it last saw you, looks around, and gives up");
    int c[1];
    blank(); put(20, 20, 8, 11, '#'); build();                /* a wall to hide behind */
    place_hero(15, 11);
    c[0] = creature_facing(EN_GRUNT, 10, 11, 1);
    CHECK(wait_mode(c[0], "chase", 120));                     /* it notices you */
    E(hero_i)->pos.x = 24 * GE_TILE;                          /* you slip behind the wall */
    bool searched = false, patrolled = false;
    for (int t = 0; t < 700 && !patrolled; t++) {
        step(c, 1);
        searched |= in_mode(c[0], "search");
        patrolled = searched && in_mode(c[0], "patrol");
    }
    CHECK(searched);
    CHECK(patrolled);
    CHECK(E(c[0])->pos.x < 20 * GE_TILE);                     /* it never got through the wall */
}

static void test_grunt_hit_from_afar_knows_where(void) {
    puts("the grunt: a creature you hit knows where you are");
    int c[1];
    blank(); put(20, 20, 8, 11, '#'); build();
    place_hero(26, 11);                                       /* behind a wall: it can't see you */
    c[0] = creature(EN_GRUNT, 10, 11);
    run(c, 1, 60);
    CHECK(in_mode(c[0], "patrol"));
    ai_provoke(c[0], E(hero_i));
    CHECK(in_mode(c[0], "chase"));
}

static void test_grunt_swings_and_backs_off(void) {
    puts("the grunt: swings when in reach, then steps back (it doesn't just stand and trade blows)");
    int c[1];
    blank(); build();
    place_hero(12, 11);
    c[0] = creature_facing(EN_GRUNT, 9, 11, 1);
    bool struck = false, backed = false;
    for (int t = 0; t < 600 && !backed; t++) {
        step(c, 1);
        struck |= in_mode(c[0], "strike");
        backed = struck && in_mode(c[0], "backoff");
    }
    CHECK(struck);
    CHECK(backed);
}

/* ---- the crawler ------------------------------------------------------------------------------------------ */

static void test_crawler(void) {
    puts("the crawler: charges what is in front of it, ignores what is behind a wall, stops at a ledge");
    int c[1];
    blank(); build();
    place_hero(12, 11);
    c[0] = creature(EN_CRAWLER, 8, 11);                       /* facing left, away from you: you are 4 columns behind it */
    run(c, 1, 60);
    CHECK(in_mode(c[0], "patrol"));                           /* it has no eyes in the back of its head */

    blank(); build();
    place_hero(12, 11);
    c[0] = creature_facing(EN_CRAWLER, 9, 11, 1);             /* facing you, 3 columns off */
    CHECK(wait_mode(c[0], "alert", 60));                      /* a beat to turn and look (the tell)... */
    CHECK(wait_mode(c[0], "chase", 60));                      /* ...then it charges */
    run(c, 1, 20);
    CHECK(fabsf(E(c[0])->vel.x) > 0.6f);                      /* faster than its 0.45 pace */

    blank(); put(11, 11, 8, 11, '#'); build();
    place_hero(14, 11);
    c[0] = creature_facing(EN_CRAWLER, 8, 11, 1);
    settle(c, 1);
    bool noticed = false;
    for (int t = 0; t < 400; t++) { step(c, 1); noticed |= in_mode(c[0], "chase") || in_mode(c[0], "alert"); }
    CHECK(!noticed);                                          /* a wall between: it paces, unaware */

    blank(); put(14, 17, 12, 14, '.'); put(14, 17, 14, 14, '^'); build();
    place_hero(22, 11);
    c[0] = creature_facing(EN_CRAWLER, 7, 11, 1);
    settle(c, 1);
    float far = 0;
    for (int t = 0; t < 600; t++) { step(c, 1); if (E(c[0])->pos.x > far) far = E(c[0])->pos.x; }
    CHECK(far < 14 * GE_TILE);                                /* it charged to the edge and stopped */
}

static void test_crawler_gives_up(void) {
    puts("the crawler: gives up a chase when it can't see you for a while");
    int c[1];
    blank(); put(20, 20, 8, 11, '#'); build();
    place_hero(14, 11);
    c[0] = creature_facing(EN_CRAWLER, 10, 11, 1);
    CHECK(wait_mode(c[0], "chase", 120));
    E(hero_i)->pos.x = 26 * GE_TILE;
    CHECK(wait_mode(c[0], "patrol", 600));
}

/* ---- the bat ----------------------------------------------------------------------------------------------- */

static void test_bat(void) {
    puts("the bat: draws back first (a tell), then dives, then goes home; never dives through a wall");
    int c[1];
    blank(); build();
    place_hero(14, 11);
    c[0] = creature(EN_BAT, 10, 7);
    float home_x = E(c[0])->ai_x0, home_y = E(c[0])->ai_y0;
    bool windup = false, dive = false, retreat = false, home_again = false;
    int windup_ticks = 0;
    for (int t = 0; t < 900 && !home_again; t++) {
        step(c, 1);
        if (in_mode(c[0], "windup")) { windup = true; windup_ticks++; }
        if (in_mode(c[0], "dive")) { CHECK(windup); dive = true; }
        if (in_mode(c[0], "retreat")) { CHECK(dive); retreat = true; }
        home_again = retreat && in_mode(c[0], "patrol");
    }
    CHECK(windup && dive && retreat && home_again);
    CHECK(windup_ticks >= 15);                                /* a tell you have time to read: a quarter of a second */
    CHECK(fabsf(E(c[0])->pos.x - home_x) < 50.0f && fabsf(E(c[0])->pos.y - home_y) < 40.0f);

    blank(); put(12, 12, 4, 11, '#'); build();                /* a wall between its perch and you */
    place_hero(15, 11);
    c[0] = creature(EN_BAT, 9, 7);
    bool saw_dive = false;
    for (int t = 0; t < 600; t++) { step(c, 1); saw_dive |= in_mode(c[0], "windup") || in_mode(c[0], "dive"); }
    CHECK(!saw_dive);
    CHECK(E(c[0])->pos.x < 12 * GE_TILE);                     /* still on its side of it */
}

/* ---- the same inputs give the same game ---------------------------------------------------------------------- */

static float run_everything(void) {
    blank(); put(13, 13, 11, 11, '#'); put(25, 25, 9, 11, '#'); build();
    place_hero(30, 11);
    int c[4];
    c[0] = creature(EN_GRUNT, 9, 11);
    c[1] = creature(EN_CRAWLER, 18, 11);
    c[2] = creature(EN_BAT, 21, 6);
    c[3] = creature(EN_SPITTER, 33, 11);
    run(c, 4, 900);
    float h = 0;
    for (int k = 0; k < 4; k++) h = h * 1.0001f + E(c[k])->pos.x * 3.0f + E(c[k])->pos.y;
    return h + (float)g_hostile_shots;
}

static void test_determinism(void) {
    puts("the same room, played twice, gives the same game");
    g_hostile_shots = g_prev_shots = 0;
    float a = run_everything();
    g_hostile_shots = g_prev_shots = 0;
    float b = run_everything();
    CHECK(a == b);
}

int main(void) {
    art_make_sheets(&spr);
    if (chars_init(&ch, &spr) != 0) { puts("characters failed to build"); return 1; }
    test_line_of_sight();
    test_spitter();
    test_grunt_hops_a_step();
    test_grunt_waits_at_a_wall();
    test_grunt_drops_and_doesnt_jump_off_cliffs();
    test_grunt_gives_up();
    test_grunt_hit_from_afar_knows_where();
    test_grunt_swings_and_backs_off();
    test_crawler();
    test_crawler_gives_up();
    test_bat();
    test_determinism();
    ge_world_free(&w);
    art_free(&spr);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
