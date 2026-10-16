/* Engine tests: no window, no assets. Run with `ctest` or ./engine_tests */

#include "ge/ge_engine.h"
#include "ge/ge_font.h"
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
    "clip a3 once dmg=20 kb=4,-2 stop=6 stun=20\n frame 0 2\n frame 1 2 hit=2,-14,44,10\n frame 2 2\n"
    "clip up once dmg=5 kb=0,-3 stop=2 stun=8\n frame 0 2\n frame 1 2 hit=-8,-36,16,18\n frame 2 2\n"
    "clip down once dmg=5 kb=0,2 stop=0 stun=8 pogo=5.5\n frame 0 1\n frame 1 4 hit=-7,0,14,14\n frame 2 2\n"
    "clip tool once shoot=1\n frame 0 2\n frame 1 2\n frame 2 2\n"
    "clip skill once dmg=15 kb=3,-2 stop=4 stun=12\n frame 0 2\n frame 1 4 hit=-20,-24,40,24\n frame 2 2\n"
    "clip heal loop\n frame 0 5\n"
    "clip recoil once dmg=1 kb=1,0 stop=0 stun=4 recoil=2.5\n frame 0 1\n frame 1 2 hit=2,-14,44,10\n frame 2 1\n";

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
    d.clip_dash = d.clip_wall = d.clip_attack_air = d.clip_attack_up = d.clip_attack_down = -1;
    d.clip_skill = d.clip_tool = d.clip_channel = -1;
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
    CHECK(s.nclips == 12);
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

/* .layers files (written by tools/asset-prep's backdrop builder) through ge_world_load_layers. */
static void test_layers_file(void) {
    puts("layers file");
    GeWorld w;
    ge_world_init(&w, 16, 4, 1);
    int line = -1;
    CHECK(ge_world_load_layers(&w, GE_TEST_DATA_DIR "/backdrop/tiny.layers", &line) == GE_LAYERS_OK && line == 0);
    CHECK(w.nlayers == 2);
    const GeParallaxLayer *a = &w.layers[0], *b = &w.layers[1];
    CHECK(a->img.w == 8 && a->img.h == 4 && a->opaque && !a->foreground && a->alpha == 255);
    CHECK_NEAR(a->factor_x, 0.5f);
    CHECK(b->img.w == 4 && !b->opaque && b->foreground && b->additive && b->optional && b->alpha == 128 && b->y == 3);
    CHECK_NEAR(b->factor_x, 1.5f);
    CHECK_NEAR(b->auto_x, 0.25f);

    /* parallax maths: camera at x=10, factor 0.5 -> the layer has scrolled 5px, so screen x=0 shows
     * column 5 of the 8px image, and it wraps (screen x=3 shows column 0). */
    GeImage fb = ge_image_create(16, 4);
    w.cam.x = 10;
    w.layers[1].optional = true; w.low_detail = true;        /* hide the front layer for this check */
    ge_world_draw(&w, &fb);
    CHECK(fb.px[0] == GE_RGB(150, 0, 0) && fb.px[3] == GE_RGB(0, 0, 0) && fb.px[4] == GE_RGB(30, 0, 0));
    ge_image_free(&fb);
    ge_world_free(&w);

    /* errors leave the world exactly as it was */
    ge_world_init(&w, 16, 4, 1);
    CHECK(ge_world_load_layers(&w, "no/such/file.layers", &line) == GE_LAYERS_EOPEN);
    CHECK(ge_world_parse_layers(&w, "layer cols.pam factor=0.5\nlayer cols.pam speed=2\n", GE_TEST_DATA_DIR "/backdrop", &line) == GE_LAYERS_ESYNTAX);
    CHECK(line == 2 && w.nlayers == 0);
    CHECK(ge_world_parse_layers(&w, "layer cols.pam factor=abc\n", GE_TEST_DATA_DIR "/backdrop", &line) == GE_LAYERS_ESYNTAX);
    CHECK(ge_world_parse_layers(&w, "layer cols.pam alpha=0\n", GE_TEST_DATA_DIR "/backdrop", &line) == GE_LAYERS_ESYNTAX);
    CHECK(ge_world_parse_layers(&w, "# comment\n\nlayer missing.pam\n", GE_TEST_DATA_DIR "/backdrop", &line) == GE_LAYERS_EIMAGE);
    CHECK(line == 3 && w.nlayers == 0);
    char many[1024] = "";
    for (int i = 0; i < GE_MAX_LAYERS + 1; i++) strcat(many, "layer cols.pam\n");
    CHECK(ge_world_parse_layers(&w, many, GE_TEST_DATA_DIR "/backdrop", &line) == GE_LAYERS_EFULL);
    CHECK(line == GE_MAX_LAYERS + 1 && w.nlayers == 0);
    CHECK(ge_world_parse_layers(&w, "layer cols.pam\n", GE_TEST_DATA_DIR "/backdrop", &line) == GE_LAYERS_OK);
    CHECK(w.nlayers == 1 && w.layers[0].factor_x == 1.0f && w.layers[0].y == 0);   /* defaults */
    ge_world_free(&w);
}

static int g_ev[8];
static void count_all(void *u, GeEventType ev, int a, int b) { (void)u; (void)a; (void)b; g_ev[ev]++; }
static void press(GeWorld *w, int e, GeIntent in) { w->intents[e] = in; run(w, 1); memset(&w->intents[e], 0, sizeof w->intents[e]); }

static void test_moveset(void) {
    puts("overworld moveset");
    GeSprite s = make_sprite();
    GeActorDef d = make_def(&s);
    d.air_jumps = 1; d.air_jump_vel = 6;
    d.wall_slide_speed = 1; d.wall_jump_vx = 3; d.wall_jump_vy = 6; d.wall_jump_lock = 8;
    d.dash_speed = 4; d.dash_ticks = 8; d.air_dash = true;
    d.clip_attack_up = ge_sprite_find_clip(&s, "up");
    d.clip_attack_down = ge_sprite_find_clip(&s, "down");
    GeWorld w;

    /* double jump: exactly one extra jump in the air, refilled on landing */
    make_world(&w);
    int a = ge_world_spawn(&w, &d, &s, 0, 100, 160, 100);
    run(&w, 3);
    press(&w, a, (GeIntent){ .jump_pressed = true, .jump_held = true });
    for (int i = 0; i < 25; i++) { w.intents[a].jump_held = true; run(&w, 1); }
    CHECK(!w.ents[a].on_ground && w.ents[a].vel.y > 0);           /* now falling */
    press(&w, a, (GeIntent){ .jump_pressed = true, .jump_held = true });
    CHECK(w.ents[a].vel.y < -5.0f && w.ents[a].air_jumps_left == 0);
    for (int i = 0; i < 25; i++) { w.intents[a].jump_held = true; run(&w, 1); }
    press(&w, a, (GeIntent){ .jump_pressed = true, .jump_held = true });
    CHECK(w.ents[a].vel.y > 0);                                   /* no third jump */
    run(&w, 90);
    CHECK(w.ents[a].on_ground && w.ents[a].air_jumps_left == 1);
    ge_world_free(&w);

    /* wall slide + wall jump */
    make_world(&w);
    for (int y = 0; y < 10; y++) w.map.type[y * 40 + 20] = GE_TILE_SOLID;
    a = ge_world_spawn(&w, &d, &s, 0, 320 - 5, 60, 100);
    w.ents[a].air_jumps_left = 0;
    for (int i = 0; i < 12; i++) { w.intents[a].move_x = 1; run(&w, 1); }
    CHECK(w.ents[a].state == GE_ST_WALL && w.ents[a].vel.y <= 1.0f + 0.001f);
    CHECK(w.ents[a].air_jumps_left == 1 && w.ents[a].facing == -1); /* refilled, facing away from the wall */
    press(&w, a, (GeIntent){ .move_x = 1, .jump_pressed = true, .jump_held = true });
    CHECK(w.ents[a].vel.x < -2.0f && w.ents[a].vel.y < -5.0f && w.ents[a].wall_lock > 0);
    ge_world_free(&w);

    /* air dash: once per airtime */
    make_world(&w);
    a = ge_world_spawn(&w, &d, &s, 0, 100, 30, 100);
    press(&w, a, (GeIntent){ .dash_pressed = true });
    CHECK(w.ents[a].state == GE_ST_DASH);
    run(&w, 9);
    CHECK(!w.ents[a].on_ground && w.ents[a].state != GE_ST_DASH);
    press(&w, a, (GeIntent){ .dash_pressed = true });
    CHECK(w.ents[a].state != GE_ST_DASH);                         /* already used */
    run(&w, 90);
    press(&w, a, (GeIntent){ .dash_pressed = true });
    CHECK(w.ents[a].state == GE_ST_DASH);                         /* back on the ground: available again */
    ge_world_free(&w);

    /* down-slash pogo off spikes, without touching them */
    make_world(&w);
    memset(g_ev, 0, sizeof g_ev);
    w.on_event = count_all;
    for (int x = 10; x < 15; x++) w.map.type[9 * 40 + x] = GE_TILE_SPIKE;
    a = ge_world_spawn(&w, &d, &s, 0, 12 * 16 + 8, 128, 100);
    w.ents[a].state = GE_ST_AIR;
    press(&w, a, (GeIntent){ .move_y = 1, .attack_pressed = true });
    float minvy = 0;
    for (int i = 0; i < 8; i++) { run(&w, 1); if (w.ents[a].vel.y < minvy) minvy = w.ents[a].vel.y; }
    CHECK(minvy < -4.5f && g_ev[GE_EV_POGO] == 1 && g_ev[GE_EV_HAZARD] == 0);
    /* ...and touching spikes raises a hazard event */
    int b = ge_world_spawn(&w, &d, &s, 1, 13 * 16 + 8, 160, 10);
    run(&w, 1);
    CHECK(g_ev[GE_EV_HAZARD] >= 1);
    w.ents[b].active = false;
    ge_world_free(&w);

    /* pogo off an enemy's head */
    make_world(&w);
    memset(g_ev, 0, sizeof g_ev);
    w.on_event = count_all;
    int vic = ge_world_spawn(&w, &d, &s, 1, 200, 160, 50);
    a = ge_world_spawn(&w, &d, &s, 0, 200, 132, 100);
    w.ents[a].state = GE_ST_AIR;
    press(&w, a, (GeIntent){ .move_y = 1, .attack_pressed = true });
    minvy = 0;
    for (int i = 0; i < 8; i++) { run(&w, 1); if (w.ents[a].vel.y < minvy) minvy = w.ents[a].vel.y; }
    CHECK(w.ents[vic].hp == 45 && minvy < -4.5f && g_ev[GE_EV_POGO] == 1);
    /* up-slash reaches above the head */
    ge_world_free(&w);
    make_world(&w);
    vic = ge_world_spawn(&w, &d, &s, 1, 200, 130, 50);
    w.ents[vic].def = &d;
    a = ge_world_spawn(&w, &d, &s, 0, 200, 160, 100);
    GeActorDef flyer = d; flyer.flying = true;
    w.ents[vic].def = &flyer;                                     /* hovering just above */
    run(&w, 2);
    press(&w, a, (GeIntent){ .move_y = -1, .attack_pressed = true });
    run(&w, 6);
    CHECK(w.ents[vic].hp == 45);
    ge_world_free(&w);

    /* flyers ignore gravity and steer with move_y */
    make_world(&w);
    a = ge_world_spawn(&w, &flyer, &s, 1, 100, 80, 10);
    run(&w, 30);
    CHECK_NEAR(w.ents[a].pos.y, 80.0f);
    for (int i = 0; i < 20; i++) { w.intents[a].move_y = 1; run(&w, 1); }
    CHECK(w.ents[a].pos.y > 90.0f);
    ge_world_free(&w);

    /* recoil pushes the attacker back on a hit */
    GeActorDef rd = d; rd.clip_attack = ge_sprite_find_clip(&s, "recoil");
    make_world(&w);
    a = ge_world_spawn(&w, &rd, &s, 0, 100, 160, 100);
    vic = ge_world_spawn(&w, &d, &s, 1, 112, 160, 50);
    run(&w, 3);
    press(&w, a, (GeIntent){ .attack_pressed = true });
    run(&w, 10);
    CHECK(w.ents[a].pos.x < 99.0f && w.ents[vic].hp == 49);
    ge_world_free(&w);
    ge_sprite_free(&s);
}

static void test_contact_shots_meter(void) {
    puts("contact damage / shots / meter / channel / skill");
    GeSprite s = make_sprite();
    GeActorDef pd = make_def(&s), ed = make_def(&s);
    pd.invuln_ticks = 30;
    ed.contact_damage = 7; ed.contact_kb_x = 3; ed.contact_kb_y = -2;
    GeWorld w;

    make_world(&w);
    int p = ge_world_spawn(&w, &pd, &s, 0, 100, 160, 100);
    ge_world_spawn(&w, &ed, &s, 1, 104, 160, 10);
    run(&w, 2);
    CHECK(w.ents[p].hp == 93 && w.ents[p].invuln > 0);
    run(&w, 10);
    CHECK(w.ents[p].hp == 93);                                    /* i-frames: no repeat */
    ge_world_free(&w);

    /* tool: fires on the shoot= frame, costs ammo, hits, recharges */
    GeShotDef shot = { NULL, 0, 4, 0, 0, 6, 6, 4, 1, 0, 0, 6, 0, 0, 60, false, true, true, false };
    GeActorDef td = make_def(&s);
    td.shot = &shot; td.clip_tool = ge_sprite_find_clip(&s, "tool"); td.ammo_max = 2; td.ammo_recharge = 50;
    make_world(&w);
    memset(g_ev, 0, sizeof g_ev);
    w.on_event = count_all;
    p = ge_world_spawn(&w, &td, &s, 0, 100, 160, 100);
    int e = ge_world_spawn(&w, &ed, &s, 1, 170, 160, 10);
    w.ents[e].def = &pd;                                          /* no contact damage for this one */
    run(&w, 2);
    press(&w, p, (GeIntent){ .tool_pressed = true });
    CHECK(w.ents[p].ammo == 1);
    run(&w, 25);
    CHECK(w.ents[e].hp == 6 && g_ev[GE_EV_SHOOT] == 1);
    press(&w, p, (GeIntent){ .tool_pressed = true }); run(&w, 8);
    press(&w, p, (GeIntent){ .tool_pressed = true }); run(&w, 8);
    CHECK(g_ev[GE_EV_SHOOT] == 2 && w.ents[p].ammo == 0);         /* third press: empty */
    run(&w, 50);
    CHECK(w.ents[p].ammo == 1);
    /* shots die on walls */
    for (int y = 0; y < 10; y++) w.map.type[y * 40 + 9] = GE_TILE_SOLID;
    w.ents[p].facing = -1;
    press(&w, p, (GeIntent){ .tool_pressed = true });
    run(&w, 30);
    int live = 0;
    for (int i = 0; i < GE_MAX_SHOTS; i++) live += w.shots[i].active;
    CHECK(live == 0);
    ge_world_free(&w);

    /* a slash destroys a breakable enemy shot */
    make_world(&w);
    p = ge_world_spawn(&w, &pd, &s, 0, 100, 160, 100);
    run(&w, 2);
    GeShotDef slow = shot; slow.speed = 0.5f;
    int si = ge_world_shoot(&w, 1, -1, &slow, 130, 150, -1);
    press(&w, p, (GeIntent){ .attack_pressed = true });
    run(&w, 6);
    CHECK(!w.shots[si].active && w.ents[p].hp == 100);
    ge_world_free(&w);

    /* meter: earned by hits, spent by skills and channels */
    GeActorDef md = make_def(&s);
    md.meter_max = 30; md.meter_per_hit = 4;
    md.clip_skill = ge_sprite_find_clip(&s, "skill"); md.skill_cost = 15;
    md.clip_channel = ge_sprite_find_clip(&s, "heal"); md.channel_cost = 10; md.channel_ticks = 30;
    make_world(&w);
    memset(g_ev, 0, sizeof g_ev);
    w.on_event = count_all;
    p = ge_world_spawn(&w, &md, &s, 0, 100, 160, 100);
    int v = ge_world_spawn(&w, &pd, &s, 1, 112, 160, 100);
    run(&w, 2);
    press(&w, p, (GeIntent){ .attack_pressed = true });
    run(&w, 10);
    CHECK(w.ents[p].meter == 4);
    w.ents[v].active = false;
    run(&w, 20);
    press(&w, p, (GeIntent){ .skill_pressed = true });
    CHECK(w.ents[p].state != GE_ST_ATTACK);                       /* 4 < 15: refused */
    w.ents[p].meter = 20;
    press(&w, p, (GeIntent){ .skill_pressed = true });
    CHECK(w.ents[p].state == GE_ST_ATTACK && w.ents[p].meter == 5);
    run(&w, 20);
    w.ents[p].meter = 20;
    for (int i = 0; i < 10; i++) { w.intents[p].channel_held = true; run(&w, 1); }
    CHECK(w.ents[p].state == GE_ST_CHANNEL);
    w.intents[p].channel_held = false; run(&w, 2);
    CHECK(w.ents[p].state != GE_ST_CHANNEL && g_ev[GE_EV_CHANNEL] == 0 && w.ents[p].meter == 20); /* let go: cancelled */
    for (int i = 0; i < 40; i++) { w.intents[p].channel_held = true; run(&w, 1); }
    CHECK(g_ev[GE_EV_CHANNEL] == 1 && w.ents[p].meter <= 10);
    ge_world_free(&w);
    ge_sprite_free(&s);
}

static void test_armor_grounded_zoom_font(void) {
    puts("armour / floor shots / zoom / font");
    GeSprite s = make_sprite();
    GeActorDef d = make_def(&s), boss = make_def(&s);
    boss.armored = true;
    GeWorld w;
    make_world(&w);
    int a = ge_world_spawn(&w, &d, &s, 0, 100, 160, 100);
    int b = ge_world_spawn(&w, &boss, &s, 1, 112, 160, 100);
    run(&w, 3);
    press(&w, a, (GeIntent){ .attack_pressed = true });
    run(&w, 6);
    CHECK(w.ents[b].hp == 90 && w.ents[b].state != GE_ST_HURT && fabsf(w.ents[b].vel.x) < 0.01f); /* damaged, not flinched */
    ge_world_free(&w);

    /* a grounded shot rides the floor and dies at the ledge */
    make_world(&w);
    for (int y = 10; y < 12; y++) w.map.type[y * 40 + 20] = GE_TILE_EMPTY;   /* a hole at column 20 */
    GeShotDef wave = { .speed = 3, .w = 12, .h = 14, .damage = 1, .life = 200, .grounded = true, .pierce = true };
    int si = ge_world_shoot(&w, 1, -1, &wave, 200, 140, 1);
    run(&w, 5);
    CHECK(w.shots[si].active && fabsf(w.shots[si].pos.y - (160.0f - 7.0f)) < 0.01f);
    run(&w, 60);
    CHECK(!w.shots[si].active);
    ge_world_free(&w);

    /* zoom: entities are drawn magnified around the camera */
    GeSprite red;
    memset(&red, 0, sizeof red);
    red.sheet = ge_image_create(16, 16);
    for (int i = 0; i < 256; i++) red.sheet.px[i] = GE_RGB(255, 0, 0);
    ge_sprite_parse(&red, "cell 16 16\norigin 8 15\nclip idle loop\n frame 0 10\n");
    GeActorDef rd = make_def(&red);
    rd.clip_idle = rd.clip_run = rd.clip_jump = rd.clip_fall = 0;
    make_world(&w);
    w.zoom = 2;
    int e = ge_world_spawn(&w, &rd, &red, 1, 100, 100, 1);
    w.ents[e].external = true;                                 /* hold it still */
    GeImage fb = ge_image_create(400, 240);
    ge_world_draw(&w, &fb);
    CHECK(fb.px[180 * 400 + 200] == GE_RGB(255, 0, 0));       /* (100-8..100+8, 85..100) x2 covers (200, 180) */
    CHECK(fb.px[180 * 400 + 100] != GE_RGB(255, 0, 0));       /* where it would sit unzoomed: nothing */
    ge_image_free(&fb);
    ge_world_free(&w);
    ge_sprite_free(&red);

    CHECK(ge_text_width("AB", 1) == 11 && ge_text_width("AB", 2) == 22 && ge_text_width("", 1) == 0);
    GeImage t = ge_image_create(20, 10);
    ge_text(&t, 0, 0, "I", GE_WHITE, 1);
    CHECK(t.px[0 * 20 + 2] == GE_WHITE && t.px[3 * 20 + 2] == GE_WHITE && t.px[3 * 20 + 0] == 0);
    ge_image_free(&t);
    ge_sprite_free(&s);
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
    w.intents[a].move_y = 1; w.intents[a].jump_pressed = true;
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
    test_layers_file();
    test_moveset();
    test_contact_shots_meter();
    test_armor_grounded_zoom_font();
    test_gfx();
    test_tilemap();
    test_physics();
    test_combat();
    test_determinism();
    test_loop();
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
