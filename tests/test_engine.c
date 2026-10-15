/* Engine tests: no window, no assets. Run with `ctest` or ./engine_tests */

#include "ge/ge_engine.h"
#include "ge/ge_world.h"

#include <stdio.h>
#include <string.h>

static int g_checks, g_failed;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b) CHECK(fabsf((a) - (b)) < 0.01f)

static const char *CLIPS =
    "cell 16 16\n"
    "origin 8 15\n"
    "clip idle loop\n frame 0 2\n frame 1 3\n"
    "clip hurt once\n frame 2 99\n"
    "clip dead once\n frame 3 99\n"
    "clip a1 once dmg=10 kb=2,-1 stop=3 stun=10 cancel=2 next=a2\n frame 0 2\n frame 1 2 hit=2,-14,44,10\n frame 2 2\n"
    "clip a2 once dmg=10 kb=2,-1 stop=3 stun=10 cancel=2 next=a3\n frame 0 2\n frame 1 2 hit=2,-14,44,10\n frame 2 2\n"
    "clip a3 once dmg=20 kb=4,-2 stop=6 stun=20\n frame 0 2\n frame 1 2 hit=2,-14,44,10\n frame 2 2\n";

static GeSprite make_sprite(void) {
    GeSprite s;
    memset(&s, 0, sizeof s);
    s.sheet = ge_image_create(64, 32);
    int rc = ge_sprite_parse(&s, CLIPS);
    if (rc != 0) printf("  sprite parse failed: %d\n", rc);
    return s;
}

static GeActorDef make_def(const GeSprite *s) {
    GeActorDef d;
    memset(&d, 0, sizeof d);
    d.run_speed = 1.5f; d.accel = 0.5f; d.friction = 0.5f; d.air_accel = 0.2f;
    d.gravity = 0.4f; d.max_fall = 8; d.jump_vel = 6.5f; d.jump_cut_vel = 2;
    d.coyote_ticks = 5; d.jump_buffer_ticks = 5;
    d.dead_ticks = 20; d.body_w = 10; d.body_h = 20;
    d.clip_idle = d.clip_run = d.clip_jump = d.clip_fall = ge_sprite_find_clip(s, "idle");
    d.clip_hurt = ge_sprite_find_clip(s, "hurt");
    d.clip_dead = ge_sprite_find_clip(s, "dead");
    d.clip_dash = -1;
    d.clip_attack = ge_sprite_find_clip(s, "a1");
    return d;
}

/* flat ground on rows 10..11 of a 40x12 map */
static void make_world(GeWorld *w) {
    ge_world_init(w, 400, 240, 99);
    ge_tilemap_create(&w->map, 40, 12);
    for (int y = 10; y < 12; y++)
        for (int x = 0; x < 40; x++) w->map.type[y * 40 + x] = GE_TILE_SOLID;
}

static void run(GeWorld *w, int ticks) { for (int i = 0; i < ticks; i++) ge_world_update(w); }

static void test_parser_and_anim(void) {
    puts("parser/animator");
    GeSprite s = make_sprite();
    CHECK(s.nclips == 6);
    int a1 = ge_sprite_find_clip(&s, "a1");
    CHECK(a1 >= 0 && s.clips[a1].is_attack && s.clips[a1].damage == 10);
    CHECK(s.clips[a1].frames[1].has_hit && !s.clips[a1].frames[0].has_hit);
    CHECK(s.clips[a1].next == ge_sprite_find_clip(&s, "a2"));
    CHECK(s.clips[a1].cancel_from == 2 && s.clips[a1].hitstop == 3);
    CHECK_NEAR(s.clips[a1].kb_y, -1.0f);

    GeSprite bad;
    memset(&bad, 0, sizeof bad);
    CHECK(ge_sprite_parse(&bad, "clip x loop\n frame 0 0\n") < 0);        /* zero-length frame */
    CHECK(ge_sprite_parse(&bad, "clip x sideways\n") < 0);               /* bad loop mode */
    CHECK(ge_sprite_parse(&bad, "clip x once next=nope\n frame 0 1\n") < 0); /* dangling next */

    GeAnimator an = { &s, 0, 0, 0, false };       /* idle: 2 ticks, then 3 ticks, looping */
    ge_anim_update(&an); CHECK(an.frame == 0);
    ge_anim_update(&an); CHECK(an.frame == 1);
    ge_anim_update(&an); ge_anim_update(&an); CHECK(an.frame == 1);
    ge_anim_update(&an); CHECK(an.frame == 0);   /* looped */
    GeAnimator once = { &s, a1, 0, 0, false };
    for (int i = 0; i < 6; i++) ge_anim_update(&once);
    CHECK(once.done && once.frame == 2);
    ge_sprite_free(&s);
}

/* Files exported by tools/asset-prep must load through the engine's own loader. */
static void test_asset_prep_roundtrip(void) {
    puts("asset-prep export -> ge_sprite_load");
    GeSprite s;
    CHECK(ge_sprite_load(&s, GE_TEST_DATA_DIR "/hero.pam", GE_TEST_DATA_DIR "/hero.anim") == 0);
    CHECK(s.sheet.w == 128 && s.sheet.h == 32 && s.cell_w == 32 && s.origin_x == 16 && s.origin_y == 31 && s.cols == 4);
    CHECK(s.nclips == 3);
    int a1 = ge_sprite_find_clip(&s, "attack1");
    CHECK(a1 >= 0 && s.clips[a1].is_attack && s.clips[a1].damage == 8 && s.clips[a1].hitstop == 4 && s.clips[a1].stun == 16);
    CHECK(s.clips[a1].cancel_from == 1 && s.clips[a1].next == ge_sprite_find_clip(&s, "attack2"));
    CHECK_NEAR(s.clips[a1].lunge, 1.6f);
    CHECK(s.clips[a1].frames[1].has_hit && s.clips[a1].frames[1].hit.w == 12 && s.clips[a1].frames[1].hit.y == -25);
    CHECK(s.sheet.px[8 + 6 * 128] == GE_RGB(220, 60, 60));   /* red block in cell 0 survived the PAM round trip */
    ge_sprite_free(&s);
    CHECK(ge_sprite_load(&s, "does/not/exist.pam", "x.anim") == -1);
    CHECK(ge_sprite_load(&s, GE_TEST_DATA_DIR "/hero.pam", "does/not/exist.anim") == -2);
}

static void test_gfx(void) {
    puts("gfx");
    GeImage a = ge_image_create(4, 2), b = ge_image_create(4, 2);
    a.px[0] = GE_RGB(255, 0, 0); a.px[1] = GE_RGB(0, 255, 0);
    GeIRect all = { 0, 0, 4, 2 };
    ge_blit(&b, &a, all, 0, 0, GE_FLIP_X, GE_WHITE);
    CHECK(b.px[3] == GE_RGB(255, 0, 0) && b.px[2] == GE_RGB(0, 255, 0));
    ge_blit(&b, &a, all, 100, 100, 0, GE_WHITE);   /* fully off-screen: must not crash */
    ge_clear(&b, GE_RGB(0, 0, 0));
    ge_fill_rect(&b, 0, 0, 4, 2, GE_ARGB(128, 255, 255, 255), 0);
    CHECK((b.px[0] & 255) > 120 && (b.px[0] & 255) < 136);   /* 50% blend */
    ge_fill_rect(&b, -5, -5, 20, 20, GE_RGB(1, 2, 3), 0);    /* clipped */
    CHECK(b.px[7] == GE_RGB(1, 2, 3));
    CHECK(ge_image_save_png(&b, "ge_test_out.png") == 0);
    ge_image_free(&a); ge_image_free(&b);
}

static void test_tilemap(void) {
    puts("tilemap");
    const char *rows[] = { "....", ".=..", "####" };
    GeTilemap m;
    CHECK(ge_tilemap_from_ascii(&m, rows, 3) == 0);
    CHECK(m.w == 4 && m.h == 3);
    CHECK(ge_tilemap_get(&m, 1, 1) == GE_TILE_ONEWAY);
    CHECK(ge_tilemap_get(&m, 2, 2) == GE_TILE_SOLID);
    CHECK(ge_tilemap_get(&m, -1, 0) == GE_TILE_SOLID);  /* side walls */
    CHECK(ge_tilemap_get(&m, 0, -1) == GE_TILE_EMPTY);  /* open sky */
    ge_tilemap_free(&m);
    const char *ragged[] = { "...", ".." };
    CHECK(ge_tilemap_from_ascii(&m, ragged, 2) != 0);
}

static void test_physics(void) {
    puts("physics");
    GeSprite s = make_sprite();
    GeActorDef d = make_def(&s);
    GeWorld w;
    make_world(&w);

    int a = ge_world_spawn(&w, &d, &s, 0, 100, 40, 10);
    run(&w, 120);
    CHECK_NEAR(w.ents[a].pos.y, 160.0f);          /* rests exactly on the floor (row 10) */
    CHECK(w.ents[a].on_ground);

    /* jump: variable height. Held jump rises higher than a tapped one. */
    float tap_peak = 1e9f, hold_peak = 1e9f;
    w.intents[a].jump_pressed = true; w.intents[a].jump_held = true;
    for (int i = 0; i < 60; i++) {
        ge_world_update(&w);
        w.intents[a].jump_pressed = false;
        if (w.ents[a].pos.y < hold_peak) hold_peak = w.ents[a].pos.y;
    }
    run(&w, 60);
    w.intents[a].jump_pressed = true; w.intents[a].jump_held = false;
    for (int i = 0; i < 60; i++) {
        ge_world_update(&w);
        w.intents[a].jump_pressed = false;
        if (w.ents[a].pos.y < tap_peak) tap_peak = w.ents[a].pos.y;
    }
    CHECK(hold_peak < tap_peak - 10.0f);
    CHECK(160.0f - hold_peak > 40.0f);

    /* run into the right-hand world edge: stops flush against the wall */
    memset(&w.intents[a], 0, sizeof w.intents[a]);
    w.ents[a].pos.x = 600;
    w.intents[a].move_x = 1;
    run(&w, 120);
    CHECK_NEAR(w.ents[a].pos.x, 640.0f - 5.0f);

    /* one-way platform at row 7: jump up through it, land on it, drop back down */
    for (int x = 10; x < 20; x++) w.map.type[7 * 40 + x] = GE_TILE_ONEWAY;
    w.ents[a].pos = (GeVec2){ 15 * 16, 160 };
    w.ents[a].vel = (GeVec2){ 0, 0 };
    memset(&w.intents[a], 0, sizeof w.intents[a]);
    w.intents[a].jump_pressed = true; w.intents[a].jump_held = true;
    float top = 1e9f;
    for (int i = 0; i < 90; i++) {
        ge_world_update(&w);
        w.intents[a].jump_pressed = false;
        top = w.ents[a].pos.y < top ? w.ents[a].pos.y : top;
    }
    CHECK_NEAR(w.ents[a].pos.y, 7 * 16.0f);        /* passed through from below, landed on top */
    memset(&w.intents[a], 0, sizeof w.intents[a]);
    w.intents[a].drop_held = true; w.intents[a].jump_pressed = true;
    run(&w, 1);
    w.intents[a].jump_pressed = false;
    run(&w, 60);
    CHECK_NEAR(w.ents[a].pos.y, 160.0f);           /* dropped through to the ground */

    /* falling out of the world fires an event and removes the entity */
    w.map.type[10 * 40 + 30] = w.map.type[11 * 40 + 30] = GE_TILE_EMPTY;
    int fell_events = 0;
    struct { int *n; } ctx = { &fell_events };
    w.user = &ctx;
    w.on_event = NULL;
    w.ents[a].pos = (GeVec2){ 30 * 16 + 8, 100 };
    run(&w, 200);
    CHECK(!w.ents[a].active);

    ge_world_free(&w);
    ge_sprite_free(&s);
    (void)fell_events;
}

static int g_hits, g_kills;
static void count_events(void *u, GeEventType ev, int a, int b) {
    (void)u; (void)a; (void)b;
    if (ev == GE_EV_HIT) g_hits++;
    if (ev == GE_EV_KILL) g_kills++;
}

static void test_combat(void) {
    puts("combat");
    GeSprite s = make_sprite();
    GeActorDef d = make_def(&s);
    GeWorld w;
    make_world(&w);
    w.on_event = count_events;
    g_hits = g_kills = 0;

    int atk = ge_world_spawn(&w, &d, &s, 0, 100, 160, 100);
    int vic = ge_world_spawn(&w, &d, &s, 1, 112, 160, 25);   /* inside the hitbox */
    int ally = ge_world_spawn(&w, &d, &s, 0, 110, 160, 50);
    run(&w, 5);

    w.intents[atk].attack_pressed = true;
    run(&w, 1);
    w.intents[atk].attack_pressed = false;
    int guard0 = 0;
    while (w.ents[vic].hp == 25 && guard0++ < 30) run(&w, 1);
    CHECK(w.ents[vic].state == GE_ST_HURT && w.ents[vic].vel.x > 0 && w.ents[vic].vel.y < 0); /* knocked away + up */
    CHECK(w.ents[vic].facing == -1);             /* turned to face the attacker */
    run(&w, 20);
    CHECK(w.ents[vic].hp == 15);                 /* hit exactly once despite 2+ active ticks and hitstop */
    CHECK(w.ents[ally].hp == 50);                /* same team is never hurt */
    CHECK(g_hits == 1);

    /* hit-stop: the victim froze for 3 ticks right after the hit */
    ge_world_free(&w);
    make_world(&w);
    w.on_event = count_events;
    atk = ge_world_spawn(&w, &d, &s, 0, 100, 160, 100);
    vic = ge_world_spawn(&w, &d, &s, 1, 112, 160, 100);
    run(&w, 3);
    w.intents[atk].attack_pressed = true; run(&w, 1);
    w.intents[atk].attack_pressed = false;
    int guard = 0;
    while (w.ents[vic].hp == 100 && guard++ < 30) run(&w, 1);
    CHECK(w.ents[vic].hitstop > 0 && w.ents[atk].hitstop > 0);

    /* a different depth lane dodges the swing entirely */
    ge_world_free(&w);
    make_world(&w);
    w.on_event = count_events;
    g_hits = 0;
    atk = ge_world_spawn(&w, &d, &s, 0, 100, 160, 100);
    vic = ge_world_spawn(&w, &d, &s, 1, 112, 160, 100);
    w.ents[vic].lane = GE_LANE_BACK;
    run(&w, 3);
    w.intents[atk].attack_pressed = true; run(&w, 1);
    w.intents[atk].attack_pressed = false; run(&w, 30);
    CHECK(w.ents[vic].hp == 100 && g_hits == 0);

    /* combo chain: pressing attack again inside the cancel window advances a1 -> a2 -> a3 */
    ge_world_free(&w);
    make_world(&w);
    atk = ge_world_spawn(&w, &d, &s, 0, 100, 160, 100);
    vic = ge_world_spawn(&w, &d, &s, 1, 112, 160, 500);
    run(&w, 3);
    int maxcombo = 0;
    for (int t = 0; t < 80; t++) {
        w.intents[atk].attack_pressed = (t % 4 == 0);
        run(&w, 1);
        if (w.ents[atk].combo > maxcombo) maxcombo = w.ents[atk].combo;
    }
    CHECK(maxcombo == 3);
    CHECK(w.ents[vic].hp <= 500 - 10 - 10 - 20);   /* all three hits landed (more if it restarted) */

    /* killing blow: DEAD state, KILL event, corpse removed after dead_ticks */
    ge_world_free(&w);
    make_world(&w);
    w.on_event = count_events;
    g_kills = 0;
    atk = ge_world_spawn(&w, &d, &s, 0, 100, 160, 100);
    vic = ge_world_spawn(&w, &d, &s, 1, 112, 160, 5);
    run(&w, 3);
    w.intents[atk].attack_pressed = true; run(&w, 1);
    w.intents[atk].attack_pressed = false; run(&w, 12);
    CHECK(w.ents[vic].state == GE_ST_DEAD && g_kills == 1);
    run(&w, 60);
    CHECK(!w.ents[vic].active);

    ge_world_free(&w);
    ge_sprite_free(&s);
}

/* Same inputs, same seed => identical frames (needed for replays and for sane debugging). */
static uint32_t scripted_run(void) {
    GeSprite s = make_sprite();
    GeActorDef d = make_def(&s);
    GeWorld w;
    make_world(&w);
    int a = ge_world_spawn(&w, &d, &s, 0, 100, 160, 100);
    ge_world_spawn(&w, &d, &s, 1, 140, 160, 100);
    w.follow = a;
    GeImage fb = ge_image_create(400, 240);
    for (int t = 0; t < 300; t++) {
        w.intents[a].move_x = (t / 40) % 2 ? -1.0f : 1.0f;
        w.intents[a].attack_pressed = t % 17 == 0;
        w.intents[a].jump_pressed = t % 53 == 0;
        w.intents[a].jump_held = t % 53 < 20;
        ge_world_update(&w);
    }
    ge_world_draw(&w, &fb);
    uint32_t sum = ge_image_checksum(&fb);
    ge_image_free(&fb);
    ge_world_free(&w);
    ge_sprite_free(&s);
    return sum;
}

static void test_determinism(void) {
    puts("determinism");
    uint32_t a = scripted_run(), b = scripted_run();
    CHECK(a == b);
    CHECK(a != 0);
}

/* The fixed-timestep loop through the headless backend. */
static int g_updates, g_draws;
static int  loop_init(void *u) { (void)u; return 0; }
static void loop_update(void *u, const GeInput *in) { (void)u; (void)in; g_updates++; }
static void loop_draw(void *u, GeImage *fb) { (void)u; (void)fb; g_draws++; }
#include "ge/ge_platform_null.h"
static void test_loop(void) {
    puts("engine loop");
    ge_null_configure(120, 0, NULL, NULL, NULL);
    GeGame game = { NULL, loop_init, loop_update, loop_draw, NULL };
    GePlatformConfig cfg = { "t", 400, 240, 1 };
    CHECK(ge_run(&game, &cfg) == 0);
    CHECK(g_draws == 120);
    CHECK(g_updates == 120);                       /* exactly one 1/60 s step per presented frame */
}

int main(void) {
    test_parser_and_anim();
    test_asset_prep_roundtrip();
    test_gfx();
    test_tilemap();
    test_physics();
    test_combat();
    test_determinism();
    test_loop();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
