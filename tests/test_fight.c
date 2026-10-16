/* Fighting-game mode tests, driven through real inputs. */

#include "ge/ge_fight.h"

#include <stdio.h>
#include <string.h>

static int g_checks, g_failed;
#define CHECK(cond) do { g_checks++; if (!(cond)) { g_failed++; printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char *CLIPS =
    "cell 16 16\norigin 8 15\n"
    "clip idle loop\n frame 0 10\n"
    "clip walk loop\n frame 0 10\n"
    "clip crouch loop\n frame 1 10\n"
    "clip jump once\n frame 0 10\n"
    "clip fall once\n frame 0 10\n"
    "clip hurt once\n frame 2 99\n"
    "clip hurt_air once\n frame 2 99\n"
    "clip block once\n frame 3 99\n"
    "clip block_low once\n frame 3 99\n"
    "clip down once\n frame 2 99\n"
    "clip ko once\n frame 2 99\n"
    "clip win once\n frame 0 99\n"
    "clip throw once\n frame 0 30\n"
    "clip thrown once\n frame 2 99\n"
    "clip dash once\n frame 0 99\n"
    /* normals: startup / active / recovery; cancel from the active frame */
    "clip l once dmg=4 kb=1,0 stop=4 stun=14 bstun=10 cancel=1\n frame 0 3\n frame 1 2 hit=2,-20,36,12\n frame 0 8\n"
    "clip m once dmg=7 kb=1.5,0 stop=5 stun=18 bstun=12 cancel=1\n frame 0 5\n frame 1 3 hit=2,-20,38,12\n frame 0 12\n"
    "clip h once dmg=11 kb=3,-4 stop=7 stun=24 bstun=14\n frame 0 7\n frame 1 3 hit=2,-22,40,14\n frame 0 16\n"
    "clip low once dmg=3 kb=1,0 stop=3 stun=12 guard=low cancel=1\n frame 1 3\n frame 1 2 hit=2,-6,34,6\n frame 1 8\n"
    "clip ovh once dmg=6 kb=1,0 stop=4 stun=16 guard=high\n frame 0 12\n frame 1 3 hit=2,-24,34,14\n frame 0 10\n"
    "clip jh once dmg=8 kb=1,0 stop=5 stun=18 guard=high\n frame 0 3\n frame 1 6 hit=-4,-10,30,16\n frame 0 4\n"
    "clip fb once shoot=1\n frame 0 6\n frame 0 20\n"
    "clip dp once dmg=12 kb=2,-5 stop=6 stun=30 inv=8 rise=6\n frame 0 2\n frame 1 8 hit=-4,-36,26,34\n frame 0 20\n"
    "clip sup once dmg=30 kb=4,-5 stop=10 stun=40 inv=20\n frame 0 4\n frame 1 10 hit=-20,-30,60,30\n frame 0 20\n";

static GeSprite spr;
static GeShotDef fireball = { .speed = 3.0f, .w = 10, .h = 10, .damage = 6, .kb_x = 1.5f, .hitstop = 4, .stun = 16, .blockstun = 12, .chip = 2, .life = 200 };

static void make_def(GeFighterDef *d) {
    memset(d, 0, sizeof *d);
    d->body.gravity = 0.45f; d->body.max_fall = 9; d->body.jump_vel = 7.5f; d->body.run_speed = 1.6f;
    d->body.body_w = 14; d->body.body_h = 28; d->body.dead_ticks = 9999;
    d->walk_back = 1.2f; d->jump_vx = 2.0f; d->dash_speed = 4.0f; d->dash_ticks = 10;
    d->crouch_h = 18; d->max_hp = 100; d->throw_range = 34; d->throw_damage = 12;
#define C(n) ge_sprite_find_clip(&spr, n)
    d->clip_idle = C("idle"); d->clip_walk_f = d->clip_walk_b = C("walk"); d->clip_crouch = C("crouch");
    d->clip_jump = C("jump"); d->clip_fall = C("fall"); d->clip_hurt = C("hurt"); d->clip_hurt_air = C("hurt_air");
    d->clip_block = C("block"); d->clip_block_low = C("block_low"); d->clip_down = C("down"); d->clip_ko = C("ko");
    d->clip_win = C("win"); d->clip_throw = C("throw"); d->clip_thrown = C("thrown"); d->clip_dash = d->clip_backdash = C("dash");
#define MV(i, c, lv, cost, sh, role) { .input = i, .clip = c, .level = lv, .meter_cost = cost, .shot = sh, .ai = role }
    GeMove moves[] = {
        MV("S", "sup", 4, GE_FIGHT_METER, NULL, GE_AI_SUPER),
        MV("623H", "dp", 3, 0, NULL, GE_AI_ANTIAIR),
        MV("236L", "fb", 3, 0, &fireball, GE_AI_ZONE),
        MV("LM", "throw", 0, 0, NULL, GE_AI_THROW),
        MV("6M", "ovh", 1, 0, NULL, GE_AI_OVERHEAD),
        MV("2L", "low", 0, 0, NULL, GE_AI_LOW),
        MV("j.H", "jh", 2, 0, NULL, GE_AI_AIR),
        MV("5L", "l", 0, 0, NULL, GE_AI_POKE),
        MV("5M", "m", 1, 0, NULL, GE_AI_COMBO),
        MV("5H", "h", 2, 0, NULL, GE_AI_COMBO),
    };
    d->nmoves = (int)(sizeof moves / sizeof moves[0]);
    memcpy(d->moves, moves, sizeof moves);
}

static GeFighterDef D0, D1;
static GeFight F;
static const GeFightInput NONE = { 0, 0, 0, 0, -1 };

static bool F_live;
static void fight_new(uint32_t seed) {
    if (F_live) ge_fight_free(&F);
    ge_fight_init(&F, &D0, &spr, &D1, &spr, NULL, seed);
    F_live = true;
}
static void fresh(void) {
    fight_new(7);
    F.rounds_to_win = 1;
    for (int i = 0; i < 200 && F.phase != GE_FP_FIGHT; i++) { GeFightInput in[2] = { NONE, NONE }; ge_fight_update(&F, in); }
}
static GeEntity *E(int s) { return &F.world.ents[F.f[s].ent]; }
static void step(GeFightInput a, GeFightInput b) { GeFightInput in[2] = { a, b }; ge_fight_update(&F, in); }
static void idle(int n) { for (int i = 0; i < n; i++) step(NONE, NONE); }
static GeFightInput dirs(int x, int y) { GeFightInput in = NONE; in.x = x; in.y = y; return in; }
static GeFightInput btn(int x, int y, uint8_t b) { GeFightInput in = dirs(x, y); in.pressed = in.held = b; return in; }
static void close_in(float gap) {
    E(0)->pos.x = 288 - gap * 0.5f; E(1)->pos.x = 288 + gap * 0.5f;
    E(0)->facing = 1; E(1)->facing = -1;
}
static int move_named(int s, const char *clip) {
    if (F.f[s].state != GE_FS_MOVE) return 0;
    return !strcmp(spr.clips[F.f[s].def->moves[F.f[s].move].clip_idx].name, clip);
}
static void until_neutral(int s) { for (int i = 0; i < 200 && F.f[s].state != GE_FS_STAND && F.f[s].state != GE_FS_CROUCH; i++) idle(1); }

static void test_notation(void) {
    puts("move notation");
    CHECK(D0.moves[2].nmotion == 3 && D0.moves[2].motion[0] == 2 && D0.moves[2].motion[2] == 6 && D0.moves[2].buttons == GE_FB_L);
    CHECK(D0.moves[3].is_throw && D0.moves[3].buttons == (GE_FB_L | GE_FB_M));
    CHECK(D0.moves[6].stance == 4 /* air */ && !D0.moves[0].is_throw);
    GeFighterDef bad;
    make_def(&bad);
    bad.moves[0].clip = "nope";
    CHECK(ge_fighter_def_finish(&bad, &spr) == -1);
    make_def(&bad);
    bad.moves[4].input = "6Q";
    CHECK(ge_fighter_def_finish(&bad, &spr) == -5);
}

static void test_motions(void) {
    puts("motion inputs (relative to facing)");
    fresh();
    step(dirs(0, 1), NONE); step(dirs(1, 1), NONE); step(btn(1, 0, GE_FB_L), NONE);
    CHECK(move_named(0, "fb"));                                  /* 236L = fireball */
    idle(8);                                                     /* 6-tick startup, then it fires */
    int shots = 0;
    for (int i = 0; i < GE_MAX_SHOTS; i++) shots += F.world.shots[i].active;
    CHECK(shots == 1);
    until_neutral(0);
    /* too slow: the motion has gone stale, so it's just a standing light */
    step(dirs(0, 1), NONE); step(dirs(1, 1), NONE); idle(20); step(btn(1, 0, GE_FB_L), NONE);
    CHECK(move_named(0, "l"));
    /* the second player faces left, so "forward" is pressing left */
    fresh();
    step(NONE, dirs(0, 1)); step(NONE, dirs(-1, 1)); step(NONE, btn(-1, 0, GE_FB_L));
    CHECK(move_named(1, "fb"));
    /* 623 */
    fresh();
    step(dirs(1, 0), NONE); step(dirs(0, 1), NONE); step(dirs(1, 1), NONE); step(btn(1, 1, GE_FB_H), NONE);
    CHECK(move_named(0, "dp") && E(0)->invuln > 0);
    /* forward dash: 6, 5, 6 */
    fresh();
    step(dirs(1, 0), NONE); step(NONE, NONE); step(dirs(1, 0), NONE);
    CHECK(F.f[0].state == GE_FS_DASH);
    /* command normal beats the plain one when forward is held */
    fresh();
    step(btn(1, 0, GE_FB_M), NONE);
    CHECK(move_named(0, "ovh"));
    fresh();
    step(btn(0, 0, GE_FB_M), NONE);
    CHECK(move_named(0, "m"));
}

static void attack_vs(GeFightInput atk, GeFightInput def, int ticks) {
    step(atk, def);
    for (int i = 0; i < ticks; i++) step(NONE, def);
}

static void test_blocking(void) {
    puts("blocking high / low / mid, chip");
    /* mid: blocked standing; P2 is to the right, so "back" is +1 */
    fresh(); close_in(30);
    attack_vs(btn(0, 0, GE_FB_L), dirs(1, 0), 8);
    CHECK(F.f[1].state == GE_FS_BLOCKSTUN && E(1)->hp == 100);
    /* low vs a standing block: hits */
    fresh(); close_in(30);
    attack_vs(btn(0, 1, GE_FB_L), dirs(1, 0), 8);
    CHECK(F.f[1].state == GE_FS_HITSTUN && E(1)->hp < 100);
    /* low vs a crouching block: blocked */
    fresh(); close_in(30);
    attack_vs(btn(0, 1, GE_FB_L), dirs(1, 1), 8);
    CHECK(F.f[1].state == GE_FS_BLOCKSTUN && E(1)->hp == 100);
    /* overhead vs a crouching block: hits */
    fresh(); close_in(30);
    attack_vs(btn(1, 0, GE_FB_M), dirs(1, 1), 18);
    CHECK(F.f[1].state == GE_FS_HITSTUN && E(1)->hp < 100);
    /* overhead vs a standing block: blocked (start close: holding back walks away during the startup) */
    fresh(); close_in(18);
    attack_vs(btn(1, 0, GE_FB_M), dirs(1, 0), 18);
    CHECK(F.f[1].state == GE_FS_BLOCKSTUN);
    /* not holding back: hit */
    fresh(); close_in(30);
    attack_vs(btn(0, 0, GE_FB_L), NONE, 8);
    CHECK(F.f[1].state == GE_FS_HITSTUN && E(1)->hp == 96);
    /* projectile chip damage on block */
    fresh(); close_in(120);
    step(dirs(0, 1), dirs(1, 0)); step(dirs(1, 1), dirs(1, 0)); step(btn(1, 0, GE_FB_L), dirs(1, 0));
    for (int i = 0; i < 150; i++) step(NONE, dirs(1, 0));
    CHECK(E(1)->hp == 98);
}

static void test_throws_cancels_juggles(void) {
    puts("throws, cancels, juggles, super");
    fresh(); close_in(26);
    step(btn(0, 0, GE_FB_L | GE_FB_M), NONE);
    CHECK(F.f[1].state == GE_FS_THROWN);
    idle(40);
    CHECK(E(1)->hp == 88);
    for (int i = 0; i < 80 && F.f[1].state != GE_FS_DOWN; i++) idle(1);
    CHECK(F.f[1].state == GE_FS_DOWN);                           /* throws knock down */
    /* out of range: L+M falls through to a standing light */
    fresh(); close_in(80);
    step(btn(0, 0, GE_FB_L | GE_FB_M), NONE);
    CHECK(move_named(0, "l") && F.f[1].state != GE_FS_THROWN);

    /* cancel 5L into 5M, but only when 5L made contact */
    fresh(); close_in(30);
    step(btn(0, 0, GE_FB_L), NONE);
    for (int i = 0; i < 5 && !F.f[0].contact; i++) idle(1);
    CHECK(F.f[0].contact);
    for (int i = 0; i < 6; i++) idle(1);                         /* hit-stop */
    step(btn(0, 0, GE_FB_M), NONE);
    CHECK(move_named(0, "m"));
    fresh(); close_in(200);
    step(btn(0, 0, GE_FB_L), NONE); idle(4);
    step(btn(0, 0, GE_FB_M), NONE);
    CHECK(move_named(0, "l"));                                   /* whiffed: no cancel */
    /* light -> medium -> heavy -> special combo counts hits */
    fresh(); close_in(28);
    int t;
    step(btn(0, 0, GE_FB_L), NONE);
    for (t = 0; t < 10; t++) idle(1);
    step(btn(0, 0, GE_FB_M), NONE);
    for (t = 0; t < 14; t++) idle(1);
    step(btn(0, 0, GE_FB_H), NONE);
    for (t = 0; t < 40; t++) idle(1);
    CHECK(F.combo_count[0] == 3);

    /* juggle limit: an airborne victim can only be hit so many times */
    fresh(); close_in(28);
    F.f[1].air_hit = true; F.f[1].juggle = GE_FIGHT_JUGGLE_MAX;
    E(1)->on_ground = false; E(1)->pos.y = 190; F.f[1].state = GE_FS_HITSTUN; F.f[1].timer = 60;
    step(btn(0, 0, GE_FB_L), NONE); idle(6);
    CHECK(E(1)->hp == 100);

    /* super: needs a full meter, empties it, freezes the screen */
    fresh(); close_in(40);
    step(btn(0, 0, GE_FB_S), NONE);
    CHECK(F.f[0].state != GE_FS_MOVE);
    until_neutral(0);
    F.f[0].meter = GE_FIGHT_METER;
    step(btn(0, 0, GE_FB_S), NONE);
    CHECK(move_named(0, "sup") && F.f[0].meter == 0 && F.super_freeze > 0);
    idle(80);
    CHECK(E(1)->hp <= 70);
    /* a rising anti-air is invulnerable at the start */
    fresh(); close_in(30);
    step(dirs(1, 0), NONE); step(dirs(0, 1), NONE); step(dirs(1, 1), NONE);
    step(btn(1, 1, GE_FB_H), btn(0, 0, GE_FB_L));
    idle(6);
    CHECK(E(0)->hp == 100);
}

static void test_rounds(void) {
    puts("rounds, K.O., time over");
    fresh(); close_in(30);
    E(1)->hp = 3;
    attack_vs(btn(0, 0, GE_FB_L), NONE, 8);
    for (int i = 0; i < 600 && !F.over; i++) idle(1);
    CHECK(F.over && F.winner == 0 && F.f[0].wins == 1);
    /* time over: more health wins */
    fresh();
    E(0)->hp = 40; F.clock = 3;
    for (int i = 0; i < 600 && !F.over; i++) idle(1);
    CHECK(F.over && F.winner == 1);
    /* best of three */
    fresh(); F.rounds_to_win = 2;
    E(1)->hp = 1; close_in(30); attack_vs(btn(0, 0, GE_FB_L), NONE, 8);
    for (int i = 0; i < 400 && F.phase != GE_FP_INTRO; i++) idle(1);
    CHECK(F.round == 2 && !F.over && E(1)->hp == 100 && F.f[0].wins == 1);
}

static int cpu_match(uint32_t seed, int *ticks, int *hits) {
    fight_new(seed);
    *ticks = 0; *hits = 0;
    int last0 = 100, last1 = 100;
    while (!F.over && *ticks < 40000) {
        GeFightInput in[2];
        ge_fight_cpu(&F, 0, 2, &in[0]);
        ge_fight_cpu(&F, 1, 3, &in[1]);
        ge_fight_update(&F, in);
        if (E(0)->hp < last0 || E(1)->hp < last1) (*hits)++;
        last0 = E(0)->hp; last1 = E(1)->hp;
        if (F.phase == GE_FP_INTRO) { last0 = E(0)->hp; last1 = E(1)->hp; }
        (*ticks)++;
    }
    return F.winner;
}

static void test_cpu(void) {
    puts("CPU vs CPU");
    int t1, h1, t2, h2;
    int w1 = cpu_match(42, &t1, &h1), w2 = cpu_match(42, &t2, &h2);
    printf("  match: winner=%d ticks=%d damaging hits=%d\n", w1, t1, h1);
    CHECK(F.over && t1 < 40000);
    CHECK(h1 > 10);                                              /* they actually fight */
    CHECK(w1 == w2 && t1 == t2 && h1 == h2);                     /* deterministic */
}

int main(void) {
    memset(&spr, 0, sizeof spr);
    spr.sheet = ge_image_create(64, 16);
    if (ge_sprite_parse(&spr, CLIPS) != 0) { puts("clip parse failed"); return 1; }
    make_def(&D0); make_def(&D1);
    if (ge_fighter_def_finish(&D0, &spr) || ge_fighter_def_finish(&D1, &spr)) { puts("def finish failed"); return 1; }
    test_notation();
    test_motions();
    test_blocking();
    test_throws_cancels_juggles();
    test_rounds();
    test_cpu();
    if (F_live) ge_fight_free(&F);
    ge_sprite_free(&spr);
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
