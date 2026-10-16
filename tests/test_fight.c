/* Fighting-game mode tests, driven through real inputs. */

#include "fight_fixture.h"

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

static void test_shoulder_buttons(void) {
    puts("K button, block button, dash button");
    CHECK(D0.moves[10].buttons == GE_FB_K && D0.moves[10].stance == 1);
    fresh(); close_in(30);
    step(btn(0, 0, GE_FB_K), NONE);
    CHECK(move_named(0, "m") && F.f[0].move == 10);
    /* block button: guards a mid without holding back, even while pressing forward */
    GeFightInput guard = NONE; guard.held = GE_FB_BLOCK;
    fresh(); close_in(30);
    attack_vs(btn(0, 0, GE_FB_L), guard, 8);
    CHECK(F.f[1].state == GE_FS_BLOCKSTUN && E(1)->hp == 100);
    GeFightInput guard_fwd = dirs(-1, 0); guard_fwd.held = GE_FB_BLOCK;
    fresh(); close_in(30);
    float x0 = E(1)->pos.x;
    attack_vs(btn(0, 0, GE_FB_L), guard_fwd, 8);
    CHECK(F.f[1].state == GE_FS_BLOCKSTUN && E(1)->pos.x >= x0 - 0.01f);   /* blocking roots you */
    /* the guard button stops every height: a low and an overhead too (holding back still needs the right one) */
    fresh(); close_in(30);
    attack_vs(btn(0, 1, GE_FB_L), guard, 8);
    CHECK(F.f[1].state == GE_FS_BLOCKSTUN && F.f[1].crouch_block && E(1)->hp == 100);
    fresh(); close_in(18);
    attack_vs(btn(1, 0, GE_FB_M), guard, 18);
    CHECK(F.f[1].state == GE_FS_BLOCKSTUN && E(1)->hp == 100);
    fresh(); close_in(30);
    attack_vs(btn(0, 1, GE_FB_L), dirs(1, 0), 8);
    CHECK(F.f[1].state == GE_FS_HITSTUN);                          /* holding back, standing: the low gets in */
    /* ...but a guard is thrown */
    fresh(); close_in(20);
    step(btn(0, 0, GE_FB_L | GE_FB_M), guard);
    CHECK(F.f[1].state == GE_FS_THROWN);
    /* holding back while an attack is coming shows the guard pose and stops the walk-back */
    fresh(); close_in(60);
    float gx = E(1)->pos.x;
    step(btn(0, 0, GE_FB_H), dirs(1, 0)); step(NONE, dirs(1, 0)); step(NONE, dirs(1, 0));
    CHECK(F.f[1].guarding && E(1)->pos.x <= gx + 0.01f);
    fresh(); close_in(160);
    gx = E(1)->pos.x;
    step(NONE, dirs(1, 0)); step(NONE, dirs(1, 0));
    CHECK(!F.f[1].guarding && E(1)->pos.x > gx);                  /* nothing coming: it walks back as usual */
    /* dash button: forward from neutral, backwards while holding back */
    GeFightInput dash = NONE; dash.pressed = GE_FB_DASH;
    fresh(); close_in(120);
    x0 = E(0)->pos.x;
    step(dash, NONE); step(NONE, NONE); step(NONE, NONE);
    CHECK(F.f[0].state == GE_FS_DASH && E(0)->pos.x > x0 + 4.0f);
    GeFightInput back_dash = dirs(-1, 0); back_dash.pressed = GE_FB_DASH;
    fresh(); close_in(120);
    x0 = E(0)->pos.x;
    step(back_dash, NONE); step(dirs(-1, 0), NONE); step(dirs(-1, 0), NONE);
    CHECK(F.f[0].state == GE_FS_DASH && E(0)->pos.x < x0 - 4.0f);
}

/* the forgiving guard (easy / normal in the demo): L also blocks in your own move's recovery and in the air */
static void test_forgiving_guard(void) {
    puts("forgiving guard");
    for (int forgiving = 0; forgiving <= 1; forgiving++) {
        fresh(); close_in(80);                                         /* far enough that P2's heavy whiffs */
        F.forgiving_guard = forgiving;
        GeFightInput h = btn(0, 0, GE_FB_H); h.held |= GE_FB_BLOCK;
        step(NONE, h);                                                 /* P2 swings a heavy, holding L */
        GeFightInput hold = NONE; hold.held = GE_FB_BLOCK;
        int n = 0;
        while (n++ < 60 && !(F.f[1].state == GE_FS_MOVE && ge_fight_frames_until_free(&F, 1) < 12)) step(NONE, hold);
        CHECK(F.f[1].state == GE_FS_MOVE);                             /* in its recovery now */
        close_in(30);
        attack_vs(btn(0, 0, GE_FB_L), hold, 6);
        if (forgiving) CHECK(F.f[1].state == GE_FS_BLOCKSTUN && E(1)->hp == 100);
        else CHECK(F.f[1].state == GE_FS_HITSTUN);
    }
    /* in the air: an anti-air into a jumper who holds L */
    for (int forgiving = 0; forgiving <= 1; forgiving++) {
        fresh(); close_in(24);
        F.forgiving_guard = forgiving;
        GeFightInput up = dirs(0, -1); up.held = GE_FB_BLOCK;
        GeFightInput hold = NONE; hold.held = GE_FB_BLOCK;
        step(NONE, up);
        for (int i = 0; i < 4; i++) step(NONE, hold);
        step(dirs(1, 0), hold); step(dirs(0, 1), hold); step(dirs(1, 1), hold);   /* 6 2 3 + H = rising anti-air */
        step(btn(1, 1, GE_FB_H), hold);
        bool blocked = false, hit = false;
        for (int i = 0; i < 20; i++) { step(NONE, hold); blocked |= F.f[1].state == GE_FS_BLOCKSTUN; hit |= F.f[1].state == GE_FS_HITSTUN; }
        if (forgiving) CHECK(blocked && !hit && E(1)->hp == 100);
        else CHECK(hit);
    }
}

static void test_buffer_and_tech(void) {
    puts("input buffer, throw break window");
    /* a light pressed during the last ticks of a move comes out the moment the move ends */
    fresh(); close_in(120);
    step(btn(0, 0, GE_FB_M), NONE);
    int n = 0;
    while (F.f[0].state == GE_FS_MOVE && ge_fight_frames_until_free(&F, 0) > 4 && n++ < 100) step(NONE, NONE);
    step(btn(0, 0, GE_FB_L), NONE);                                 /* too early, by the old rules */
    for (int i = 0; i < 8 && !move_named(0, "l"); i++) step(NONE, NONE);
    CHECK(move_named(0, "l"));
    /* but not forever: a press long before the end is dropped */
    fresh(); close_in(120);
    step(btn(0, 0, GE_FB_H), NONE);
    step(NONE, NONE);
    step(btn(0, 0, GE_FB_L), NONE);                                 /* 20+ ticks before 5H ends */
    for (int i = 0; i < 40; i++) step(NONE, NONE);
    CHECK(!move_named(0, "l"));
    /* the throw can be broken up to 15 ticks after it starts (human reaction time) */
    fresh(); close_in(20);
    step(btn(0, 0, GE_FB_L | GE_FB_M), NONE);
    CHECK(F.f[1].state == GE_FS_THROWN);
    for (int i = 0; i < 12; i++) step(NONE, NONE);
    step(NONE, btn(0, 0, GE_FB_L | GE_FB_M));
    CHECK(F.f[1].state != GE_FS_THROWN && E(1)->hp == 100);
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
    test_shoulder_buttons();
    test_buffer_and_tech();
    test_forgiving_guard();
    test_throws_cancels_juggles();
    test_rounds();
    test_cpu();
    if (F_live) ge_fight_free(&F);
    ge_sprite_free(&spr);
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
