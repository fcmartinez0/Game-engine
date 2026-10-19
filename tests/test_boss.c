/* Thornwarden's mind (demo/ai.c, boss_think), played in a small arena with the game's own boss, hero, art and physics.
 * The hero is unhurtable and does only what a test tells them (stand, jump, heal, run, cling); we count what the boss
 * does about it. Worlds are seeded, so a count is a fact about the code, not luck. Set BOSSDBG=1 to print numbers. */

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
static int hero_i, boss_i;
static BossBrain brain;
static BossCtx ctx;
static bool verbose;
static uint32_t g_seed_off;         /* BOSSSEED=n: every world's seed is shifted by n (to see whether a threshold only holds for the seeds it was fitted on) */

#define RW 40
#define RH 15
#define WALL0 80.0f          /* the arena's floor runs from column 5 to column 34; like the game's, its walls are one tile thick (columns 4 and 35) with open air beyond */
#define WALL1 560.0f

static GeEntity *E(int i) { return &w.ents[i]; }
#define BOSS E(boss_i)
#define HERO E(hero_i)

/* an arena with a floor at row 12 and walls at columns 4 and 35; the boss at bx, the hero at hx (pixels, on the floor) */
static void arena(uint32_t seed, float bx, float hx, float tempo) {
    ge_world_free(&w);
    ge_world_init(&w, SCREEN_W, SCREEN_H, seed + g_seed_off);
    ge_tilemap_create(&w.map, RW, RH);
    for (int y = 0; y < RH; y++)
        for (int x = 0; x < RW; x++)
            w.map.type[y * RW + x] = (y >= 12 || x == 4 || x == 35) ? GE_TILE_SOLID : GE_TILE_EMPTY;
    float floor_y = 12.0f * GE_TILE;
    hero_i = ge_world_spawn(&w, &ch.hero, &spr.hero, 0, hx, floor_y, 99);
    boss_i = ge_world_spawn(&w, &ch.warden, &spr.warden, 1, bx, floor_y, 150);
    BOSS->kind = EN_BOSS; BOSS->max_hp = 150; BOSS->hp = 150; BOSS->facing = hx > bx ? 1 : -1;
    w.follow = hero_i;
    boss_reset(&brain);
    ctx = (BossCtx){ WALL0, WALL1, tempo, &ch.shockwave };
}

static float g_force_jumpy = -1.0f, g_force_ranged = -1.0f;     /* tests that pin what it believes about the hero's habits */

typedef struct { bool air, heal; float vx; bool dash; } Hero;      /* what the hero does this tick (dash: along the floor, which the physics counts as off the ground) */

static void step(Hero hv) {
    GeEntity *h = HERO;
    h->invuln = 5;                                      /* the hero can't be hurt or pushed around */
    h->hp = h->max_hp;
    memset(&w.intents[hero_i], 0, sizeof(GeIntent));
    if (hv.vx != 0.0f) { h->pos.x += hv.vx; h->vel.x = hv.vx; }
    if (hv.heal) h->state = GE_ST_CHANNEL;
    if (hv.air) h->on_ground = false;
    if (hv.dash) { h->state = GE_ST_DASH; h->on_ground = false; }
    if (g_force_jumpy >= 0.0f) brain.jumpy = g_force_jumpy;
    if (g_force_ranged >= 0.0f) brain.ranged = g_force_ranged;
    boss_think(&w, boss_i, h, &brain, &ctx, &w.intents[boss_i]);
    h->vel.x = 0.0f;                                    /* (the hero is moved by hand; the boss saw the speed) */
    ge_world_update(&w);
    BOSS->hp = BOSS->hp > 0 ? BOSS->hp : 1;
}
static const Hero STAND = { false, false, 0.0f, false };

static bool attacking_mode(int m) { return m == B_SWING || m == B_CHARGE || m == B_LEAP || m == B_THROW; }

/* the attacks it chooses, in order, over `ticks` of the hero doing `hv`; returns how many */
static int sequence(Hero hv, int ticks, int *out, int cap) {
    int n = 0, prev = brain.mode;
    for (int t = 0; t < ticks; t++) {
        step(hv);
        if (prev == B_IDLE && attacking_mode(brain.mode) && !brain.away && n < cap) out[n++] = brain.mode;      /* (a leap clear of a clinging hero is repositioning, not a choice) */
        prev = brain.mode;
    }
    return n;
}

/* ---- the opening ------------------------------------------------------------------------------------------ */

static void test_intro(void) {
    puts("the intro: it does nothing for a moment, then fights");
    arena(1, 300, 440, 1.0f);
    CHECK(brain.mode == B_SLEEP);
    for (int t = 0; t < 10; t++) step(STAND);
    CHECK(brain.mode == B_SLEEP && BOSS->state != GE_ST_ATTACK);        /* asleep: nothing happens until it is woken */
    boss_wake(&brain);
    bool acted = false;
    for (int t = 0; t < 69; t++) { step(STAND); acted |= BOSS->state == GE_ST_ATTACK || brain.mode >= B_SWING; }
    CHECK(!acted && brain.mode == B_INTRO);
    for (int t = 0; t < 200 && !attacking_mode(brain.mode); t++) step(STAND);
    CHECK(attacking_mode(brain.mode));
    brain.mode = B_DEAD;
    memset(&w.intents[boss_i], 7, sizeof(GeIntent));
    boss_think(&w, boss_i, HERO, &brain, &ctx, &w.intents[boss_i]);
    CHECK(!w.intents[boss_i].attack_pressed && !w.intents[boss_i].jump_pressed && w.intents[boss_i].move_x == 0.0f);    /* dead: nothing */
}

/* ---- it hits a hero who stops to heal ----------------------------------------------------------------------- */

/* wake it, wait for its next idle moment, then start healing; returns ticks until it commits to an attack (or 99) and the attack */
static int heal_latency(uint32_t seed, float dist, int *attack) {
    arena(seed, 300, 300 + dist, 1.0f);
    boss_wake(&brain);
    for (int t = 0; t < 400 && !(brain.mode == B_IDLE && brain.timer > 25); t++) step(STAND);
    if (brain.mode != B_IDLE) return 99;
    for (int t = 1; t <= 60; t++) {
        step((Hero){ false, true, 0.0f, false });
        if (attacking_mode(brain.mode)) { *attack = brain.mode; return t; }
    }
    return 99;
}

static int idle_latency(uint32_t seed, float dist) {
    arena(seed, 300, 300 + dist, 1.0f);
    boss_wake(&brain);
    for (int t = 0; t < 400 && !(brain.mode == B_IDLE && brain.timer > 25); t++) step(STAND);
    for (int t = 1; t <= 120; t++) { step(STAND); if (attacking_mode(brain.mode)) return t; }
    return 120;
}

static void test_heal(void) {
    puts("healing: a hero who stops to heal is hit at once, with whatever reaches");
    int worst = 0, base_sum = 0, n = 0;
    int swing = 0, charge = 0, leap = 0;
    for (uint32_t s = 1; s <= 24; s++) {
        int a = -1;
        int lat = heal_latency(s, 60.0f, &a);
        if (lat > worst) worst = lat;
        swing += a == B_SWING;
        lat = heal_latency(s, 140.0f, &a);
        if (lat > worst) worst = lat;
        charge += a == B_CHARGE;
        lat = heal_latency(s, 230.0f, &a);
        if (lat > worst) worst = lat;
        leap += a == B_LEAP;
        base_sum += idle_latency(s, 140.0f); n++;
    }
    if (verbose) printf("  slowest answer to a heal: %d ticks;  swing %d, charge %d, leap %d of 24;  its usual wait: %d ticks\n",
                        worst, swing, charge, leap, base_sum / n);
    CHECK(worst <= 12);                                          /* a reaction time, nothing more */
    CHECK(swing == 24 && charge == 24 && leap == 24);            /* close: the sword; mid: the charge; far: the leap */
    CHECK(base_sum / n >= 25);                                   /* (without a heal, it waits for its rhythm) */
}

/* ---- it varies, and reads habits ---------------------------------------------------------------------------- */

static void test_variety(void) {
    puts("variety: never the same attack three times running, and all of its moves show up");
    int seq[200];
    int worst_run = 0, used[4] = { 0, 0, 0, 0 };
    for (uint32_t s = 1; s <= 6; s++) {
        arena(s, 300, 450, 1.0f);
        boss_wake(&brain);
        int n = sequence(STAND, 9000, seq, 200), run = 1;
        for (int i = 0; i < n; i++) {
            used[seq[i] - B_SWING]++;
            run = i > 0 && seq[i] == seq[i - 1] ? run + 1 : 1;
            if (run > worst_run) worst_run = run;
            if (run == 3 && verbose) printf("   seed %u: run of 3 at %d: %d %d %d (hp %d)\n", (unsigned)s, i, seq[i-2], seq[i-1], seq[i], BOSS->hp);
        }
    }
    if (verbose) printf("  swing %d, charge %d, leap %d, throw %d; longest run of one attack: %d\n", used[0], used[1], used[2], used[3], worst_run);
    CHECK(worst_run <= 2);
    CHECK(used[1] > 0 && used[2] > 0 && used[3] > 0);
}

static float share_of_ground_attacks(Hero (*who)(int), uint32_t seeds) {
    int ground = 0, total = 0;
    for (uint32_t s = 1; s <= seeds; s++) {
        arena(s, 300, 450, 1.0f);
        boss_wake(&brain);
        int prev = brain.mode;
        for (int t = 0; t < 9000; t++) {
            step(who(t));
            if (prev == B_IDLE && attacking_mode(brain.mode)) { total++; ground += brain.mode == B_CHARGE || brain.mode == B_LEAP; }
            prev = brain.mode;
        }
    }
    return total ? (float)ground / (float)total : 0.0f;
}
static Hero stands(int t) { (void)t; return STAND; }
static Hero hops(int t) { return (Hero){ (t / 40) % 2 == 1, false, 0.0f, false }; }          /* in the air half the time */
static Hero hops_always(int t) { (void)t; return (Hero){ true, false, 0.0f, false }; }          /* in the air all the time */
static Hero dashes(int t) { return (Hero){ false, false, 0.0f, t % 34 < 10 }; }              /* a dash along the floor every 34 ticks (as often as the cooldown allows) */

static void test_jumpy(void) {
    puts("habits: a hero who keeps jumping gets fewer charges and waves (which they hop) than one who stands still");
    float standing = share_of_ground_attacks(stands, 10), jumping = share_of_ground_attacks(hops, 10);
    if (verbose) printf("  charges and leaps, of all its attacks: against a hero who stands %.0f%%, against one who jumps %.0f%%\n", standing * 100, jumping * 100);
    CHECK(jumping + 0.12f < standing);
    float dashing = share_of_ground_attacks(dashes, 10);                 /* a hero who only ever dashes along the floor is not a jumper */
    if (verbose) printf("  ...against one who dashes along the floor %.0f%%\n", dashing * 100);
    CHECK(dashing + 0.08f > standing);
}

static void test_habits_pinned(void) {
    puts("habits: what it believes about the hero changes what it picks (a hero who stays away is closed on; one in the air is swung at)");
    g_force_jumpy = 0.0f; g_force_ranged = 0.0f;
    float base = share_of_ground_attacks(stands, 14);
    g_force_ranged = 0.8f;
    float far_habit = share_of_ground_attacks(stands, 14);
    g_force_ranged = 0.0f;
    float airborne = share_of_ground_attacks(hops_always, 14);
    g_force_jumpy = g_force_ranged = -1.0f;
    if (verbose) printf("  charges and leaps, of all its attacks: %.0f%% normally, %.0f%% against a hero it has seen stay away, %.0f%% against one in the air right now\n",
                        base * 100, far_habit * 100, airborne * 100);
    CHECK(far_habit > base + 0.05f);
    CHECK(airborne + 0.08f < base);
}

/* what the attacks it picks actually do, in the world: the swing and the charge are swings, the volley is spears, the landing is waves */
static int shots_of(const GeShotDef *d) {
    int n = 0;
    for (int i = 0; i < GE_MAX_SHOTS; i++) n += w.shots[i].active && w.shots[i].def == d;
    return n;
}

static void test_attacks_are_real(void) {
    puts("attacks: a swing and a charge are attack animations, a volley is spears, a leap leaves the floor and lands in shockwaves");
    int swings = 0, swings_ok = 0, charges = 0, charges_ok = 0, throws = 0, throws_ok = 0, spears_seen = 0, leaps = 0, leaps_ok = 0, tells_ok = 0;
    for (uint32_t s = 1; s <= 4; s++) {
        arena(s, 300, 450, 1.0f);
        boss_wake(&brain);
        for (int t = 0; t < 5000; t++) {
            int before = brain.mode;
            step(STAND);
            if (before != B_IDLE || !attacking_mode(brain.mode) || brain.away) continue;
            int mv = brain.mode, attacked = 0, airborne = 0, throws_made = 0, spears = 0, waves = 0, prev_spears = 0, prev_waves = 0, was_throwing = 0, crouch = 0, left_floor_at = -1;
            for (int k = 0; k < 260 && brain.mode == mv; k++) {
                if (BOSS->state == GE_ST_ATTACK) attacked = 1;
                if (!BOSS->on_ground) { airborne = 1; if (left_floor_at < 0) left_floor_at = k; }
                if (mv == B_LEAP && left_floor_at < 0 && BOSS->state == GE_ST_ATTACK && BOSS->anim.clip == ch.warden.clip_skill) crouch++;     /* the tell */
                int throwing = BOSS->state == GE_ST_ATTACK && BOSS->anim.clip == ch.warden.clip_tool;       /* (a spear thrown at a hero this close can be gone the same tick: count the throws by the animation too) */
                throws_made += throwing && !was_throwing; was_throwing = throwing;
                int sp = shots_of(&ch.spear), wv = shots_of(&ch.shockwave);
                spears += sp > prev_spears ? sp - prev_spears : 0; waves += wv > prev_waves ? wv - prev_waves : 0;
                prev_spears = sp; prev_waves = wv;
                step(STAND);
            }
            for (int k = 0; k < 3; k++) {                                   /* (the waves appear on the landing tick) */
                int sp = shots_of(&ch.spear), wv = shots_of(&ch.shockwave);
                spears += sp > prev_spears ? sp - prev_spears : 0; waves += wv > prev_waves ? wv - prev_waves : 0;
                prev_spears = sp; prev_waves = wv;
                step(STAND);
            }
            if (mv == B_SWING) { swings++; swings_ok += attacked; }
            if (mv == B_CHARGE) { charges++; charges_ok += attacked; }
            if (mv == B_THROW) { throws++; throws_ok += throws_made >= 2; spears_seen += spears >= 1; }
            if (mv == B_LEAP) { leaps++; leaps_ok += airborne && waves >= 2; tells_ok += crouch >= 15 && left_floor_at >= crouch; }
        }
    }
    if (verbose) printf("  of %d swings %d swung, of %d charges %d charged, of %d volleys %d made two throws (spears seen in flight in %d), of %d leaps %d jumped and sent waves (%d crouched first)\n",
                        swings, swings_ok, charges, charges_ok, throws, throws_ok, spears_seen, leaps, leaps_ok, tells_ok);
    CHECK(swings >= 10 && charges >= 10 && throws >= 5 && leaps >= 10);
    CHECK(swings_ok == swings && charges_ok == charges && throws_ok == throws && leaps_ok == leaps);
    CHECK(tells_ok == leaps);                                           /* a leap is announced by a crouch of at least a quarter second */
    CHECK(spears_seen * 100 >= throws * 90);                            /* ...and the throws really fire spears */
}

/* ---- aim ---------------------------------------------------------------------------------------------------- */

static void test_leap_aim(void) {
    puts("aim: it leaps to where a running hero will be, not where they were");
    double err = 0; int n = 0;
    for (uint32_t s = 1; s <= 40; s++) {
        arena(s, 200, 300, 1.0f);
        boss_wake(&brain);
        float dir = 1.0f, start_dist = 0.0f, start_dir = 1.0f;
        bool in_leap = false;
        for (int t = 0; t < 4000; t++) {
            if (HERO->pos.x > 470.0f) dir = -1.0f;
            if (HERO->pos.x < 190.0f) dir = 1.0f;
            step((Hero){ false, false, 1.8f * dir, false });
            if (brain.mode == B_LEAP && !brain.away && brain.leaping && !in_leap && !BOSS->on_ground) {      /* it has jumped */
                in_leap = true; start_dist = fabsf(HERO->pos.x - BOSS->pos.x); start_dir = dir;
            } else if (in_leap && brain.mode != B_LEAP) {                                                  /* it landed */
                in_leap = false;
                if (start_dist < 95.0f && start_dir == dir) { err += fabs(BOSS->pos.x - HERO->pos.x); n++; }       /* (a leap covers ~130 px; and the hero didn't turn round in mid-air, which nobody can predict) */
            }
        }
    }
    if (verbose) printf("  mean distance from the hero where it lands: %.0f px over %d leaps (the hero covers 66 px while it is in the air)\n", n ? err / n : 0.0, n);
    CHECK(n >= 10);
    CHECK(n && err / n < 40.0);
}

/* ---- a hero who clings to it ---------------------------------------------------------------------------------- */

static void test_hug(void) {
    puts("hugging: a hero standing right on it is attacked, and it leaps clear if it has room behind it");
    arena(3, 300, 322, 1.0f);
    boss_wake(&brain);
    int attacks = 0, aways = 0, prev = brain.mode;
    float farthest = 0;
    for (int t = 0; t < 900; t++) {
        step(STAND);
        if (prev == B_IDLE && attacking_mode(brain.mode)) { attacks++; aways += brain.away; }
        if (brain.mode == B_IDLE) { float d = fabsf(HERO->pos.x - BOSS->pos.x); if (d > farthest) farthest = d; }
        prev = brain.mode;
    }
    if (verbose) printf("  clinging hero: %d attacks, %d leaps clear, it got as far as %.0f px away\n", attacks, aways, farthest);
    CHECK(attacks >= 3 && aways >= 1 && farthest > 70.0f);

    /* its back to a wall: no room to leap clear, so it fights. Between its attacks it is put back in the corner with the hero on it. */
    for (int side = 0; side < 2; side++) {
        arena(3 + (uint32_t)side, side ? WALL1 - 20.0f : WALL0 + 20.0f, side ? WALL1 - 42.0f : WALL0 + 42.0f, 1.0f);
        boss_wake(&brain);
        attacks = aways = 0; prev = brain.mode;
        int cornered = 0;
        for (int t = 0; t < 1500; t++) {
            if (brain.mode == B_IDLE && fabsf(BOSS->pos.x - (side ? WALL1 : WALL0)) > 30.0f && BOSS->on_ground) {
                BOSS->pos.x = side ? WALL1 - 20.0f : WALL0 + 20.0f; BOSS->vel.x = 0.0f;
                HERO->pos.x = side ? WALL1 - 42.0f : WALL0 + 42.0f;
            }
            step(STAND);
            if (brain.mode == B_IDLE && brain.hug_ticks >= 50 && fabsf(BOSS->pos.x - (side ? WALL1 : WALL0)) < 30.0f) cornered++;
            if (prev == B_IDLE && attacking_mode(brain.mode)) { attacks++; aways += brain.away; }
            prev = brain.mode;
        }
        if (verbose) printf("  cornered (%s wall): %d attacks, %d leaps clear, %d idle ticks of a clinging hero\n", side ? "right" : "left", attacks, aways, cornered);
        CHECK(attacks >= 5 && aways == 0);
    }
}

/* ---- the charge, and the wall --------------------------------------------------------------------------------- */

static void test_charge_and_wall(void) {
    puts("the charge: a real run across the arena; into the wall it is dazed, and the hero gets a moment");
    /* it travels */
    float best = 0;
    for (uint32_t s = 1; s <= 8 && best < 100.0f; s++) {
        arena(s, 200, 400, 1.0f);
        boss_wake(&brain);
        for (int t = 0; t < 1500 && best < 100.0f; t++) {
            step(STAND);
            if (brain.mode == B_CHARGE) {
                float x0 = BOSS->pos.x;
                for (int k = 0; k < 90 && brain.mode == B_CHARGE; k++) step(STAND);
                float d = fabsf(BOSS->pos.x - x0);
                if (d > best) best = d;
            }
        }
    }
    if (verbose) printf("  a charge carried it %.0f px\n", best);
    CHECK(best > 100.0f);

    /* the wall */
    int slams = 0, daze_min = 9999, attacked_in_daze = 0, hurt_hero_free = 0;
    for (uint32_t s = 1; s <= 24; s++) {
        arena(s, WALL1 - 190, WALL1 - 70, 1.0f);              /* the hero stands near the far wall: a charge at them runs on into it */
        boss_wake(&brain);
        int daze = 0, prev = brain.mode;
        for (int t = 0; t < 1500; t++) {
            step(STAND);
            if (brain.mode == B_DAZED) {
                if (prev != B_DAZED) { daze = 0; slams++; }
                daze++;
                if (BOSS->state == GE_ST_ATTACK && BOSS->anim.frame < 2) attacked_in_daze++;
                if (w.intents[boss_i].attack_pressed || w.intents[boss_i].tool_pressed || w.intents[boss_i].jump_pressed) attacked_in_daze++;
            } else if (prev == B_DAZED) {
                if (daze < daze_min) daze_min = daze;
                hurt_hero_free++;
            }
            prev = brain.mode;
        }
    }
    if (verbose) printf("  into the wall %d times; shortest daze %d ticks\n", slams, daze_min);
    CHECK(slams >= 10);
    CHECK(daze_min >= 70);
    CHECK(attacked_in_daze == 0);
    CHECK(hurt_hero_free >= 10);
}

/* ---- phases, pace, patience ---------------------------------------------------------------------------------- */

static int attacks_in(float tempo, int hp, uint32_t seed, int ticks, int *doubles) {
    arena(seed, 300, 400, tempo);
    BOSS->hp = hp;
    boss_wake(&brain);
    int n = 0, prev = brain.mode, swing_started = -1000;
    *doubles = 0;
    for (int t = 0; t < ticks; t++) {
        step(STAND);
        BOSS->hp = hp;
        if (brain.mode == B_SWING && BOSS->state == GE_ST_ATTACK && swing_started < 0) swing_started = t;
        if (brain.mode != B_SWING) swing_started = -1000;
        if ((prev == B_IDLE && attacking_mode(brain.mode)) || (prev == B_SWING && brain.mode == B_SWING && brain.age == 0)) {
            n++;
            if (prev == B_SWING) (*doubles)++;
        }
        prev = brain.mode;
    }
    return n;
}

static void test_tempo_and_phases(void) {
    puts("pace: gentler difficulties wait longer between attacks; below half health it presses and swings twice");
    int easy = 0, normal = 0, hard = 0, d = 0, d_calm = 0, d_rage = 0;
    for (uint32_t s = 1; s <= 6; s++) {
        easy += attacks_in(1.35f, 150, s, 3600, &d);
        normal += attacks_in(1.0f, 150, s, 3600, &d);
        hard += attacks_in(0.8f, 150, s, 3600, &d);
    }
    int calm = 0, rage = 0;
    for (uint32_t s = 1; s <= 6; s++) {
        calm += attacks_in(1.0f, 150, s, 6000, &d); d_calm += d;
        rage += attacks_in(1.0f, 60, s, 6000, &d); d_rage += d;
    }
    if (verbose) printf("  attacks in 6 minutes of play: easy %d, normal %d, hard %d;  full health %d (%d double swings), under half %d (%d double swings)\n",
                        easy, normal, hard, calm, d_calm, rage, d_rage);
    CHECK(easy < normal && normal < hard);
    CHECK(hard * 100 >= easy * 115);
    CHECK(rage > calm);
    CHECK(d_calm == 0 && d_rage >= 1);
}

static void test_always_acts(void) {
    puts("liveness: wherever the hero is, it never stands about for long");
    const float where[] = { 335.0f, 410.0f, 520.0f, 150.0f };       /* the boss is at 300: very close, mid, far, behind it */
    int worst = 0;
    for (unsigned i = 0; i < sizeof where / sizeof *where; i++) {
        arena(i + 1, 300, where[i], 1.0f);
        boss_wake(&brain);
        int since = 0, prev = brain.mode;
        for (int t = 0; t < 6000; t++) {
            step(STAND);
            since++;
            if (prev == B_IDLE && attacking_mode(brain.mode)) since = 0;
            if (since > worst) worst = since;
            prev = brain.mode;
        }
    }
    if (verbose) printf("  longest stretch without an attack: %d ticks\n", worst);
    CHECK(worst < 300);
}

static void test_deterministic(void) {
    puts("the same fight played twice gives the same result");
    float result[2][3];
    for (int run = 0; run < 2; run++) {
        arena(9, 300, 430, 1.0f);
        boss_wake(&brain);
        for (int t = 0; t < 4000; t++) step(hops(t));
        result[run][0] = BOSS->pos.x; result[run][1] = (float)brain.slams; result[run][2] = (float)brain.mode;
    }
    CHECK(!memcmp(result[0], result[1], sizeof result[0]));
}

int main(void) {
    verbose = getenv("BOSSDBG") != NULL;
    if (getenv("BOSSSEED")) g_seed_off = (uint32_t)atoi(getenv("BOSSSEED"));
    art_make_sheets(&spr);
    if (chars_init(&ch, &spr) != 0) { puts("characters failed to build"); return 1; }
    test_intro();
    test_heal();
    test_variety();
    test_jumpy();
    test_habits_pinned();
    test_attacks_are_real();
    test_leap_aim();
    test_hug();
    test_charge_and_wall();
    test_tempo_and_phases();
    test_always_acts();
    test_deterministic();
    ge_world_free(&w);
    art_free(&spr);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
