/* The duel CPU (engine/src/ge_cpu.c) with the game's real fighters. A scripted "player" (side 0, a brawler) does
 * one thing over and over and we count how the CPU (side 1, a fencer) answers. Fights are seeded, so a count is a
 * fact about the code, not luck. Set CPUDBG=1 to print the measurements. */

#include "../demo/demo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(c) do { checks++; if (!(c)) { failures++; printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static Sprites spr;
static Chars ch;
static GeFight F;
static bool inited, verbose;
static uint32_t g_seed_off;         /* CPUSEED=n: every fight's seed is shifted by n (to see whether a threshold only holds for the seeds it was fitted on) */

static GeFightInput none(void) { return (GeFightInput){ 0, 0, 0, 0, -1 }; }
static GeEntity *E(int s) { return &F.world.ents[F.f[s].ent]; }
static void tick(GeFightInput p, GeFightInput c) { GeFightInput in[2] = { p, c }; ge_fight_update(&F, in); }
static bool free_(int s) { return F.f[s].state == GE_FS_STAND || F.f[s].state == GE_FS_CROUCH; }
static int move_named(int s, const char *input) {
    for (int i = 0; i < F.f[s].def->nmoves; i++) if (!strcmp(F.f[s].def->moves[i].input, input)) return i;
    return -1;
}

/* a fight with the intro over */
static void start(const GeFighterDef *d0, const GeSprite *s0, const GeFighterDef *d1, const GeSprite *s1, uint32_t seed) {
    if (inited) ge_fight_free(&F);
    inited = true;
    ge_fight_init(&F, d0, s0, d1, s1, NULL, seed + g_seed_off);
    F.infinite_clock = true;
    for (int i = 0; i < 400 && F.phase != GE_FP_FIGHT; i++) tick(none(), none());
}
static void start_default(uint32_t seed) { start(&ch.f_brawler, &spr.brawler, &ch.f_fencer, &spr.fencer, seed); }

/* let the last exchange play out (nobody does anything), then put the fighters back: p0 at x0, p1 at x1, full health */
static void settle(float x0, float x1) {
    for (int i = 0; i < 300; i++) {
        bool shots = false;
        for (int k = 0; k < GE_MAX_SHOTS; k++) shots |= F.world.shots[k].active;
        if (free_(0) && free_(1) && E(0)->hitstop == 0 && E(1)->hitstop == 0 && E(0)->invuln == 0 && E(1)->invuln == 0 && !shots && E(0)->on_ground && E(1)->on_ground) break;
        tick(none(), none());
    }
    E(0)->pos.x = x0; E(1)->pos.x = x1;
    E(0)->hp = F.f[0].def->max_hp; E(1)->hp = F.f[1].def->max_hp;
    F.f[0].combo = F.f[1].combo = 0;
    F.f[0].contact = F.f[0].hit_landed = F.f[1].contact = F.f[1].hit_landed = false;
}

/* ---- punishing what misses ---------------------------------------------------------------------------- */

/* The player throws a 5K from just out of its own reach (58 px of sword + 7 of body; the fencer's own 5K reaches 70 + 7); the CPU (not allowed to start anything of its own: that
 * is what ai_cd does) may only answer. Of the cycles where the 5K really missed, how many did the CPU hit it in? */
typedef struct { int whiffs, hits, answered; double latency_sum; } Punish;      /* latency: ticks from the 5K starting to the CPU's answer starting */

static Punish punish_cycles(int level, int cycles) {
    start_default(7);
    Punish r = { 0, 0, 0, 0.0 };
    int k5 = move_named(0, "5K");
    for (int c = 0; c < cycles; c++) {
        settle(120.0f, 120.0f + 74.0f);
        int cpu_hp = E(1)->hp, player_hp = E(0)->hp, started = -1;
        F.f[1].ai_cd = 200;
        for (int t = 0; t < 70; t++) {
            GeFightInput p = none(), q = none();
            if (t == 0) p.force_move = k5;
            ge_fight_cpu(&F, 1, level, &q);
            if (q.force_move >= 0 && started < 0) started = t;
            tick(p, q);
        }
        if (E(1)->hp != cpu_hp) continue;               /* it touched the CPU: not a whiff (the CPU wandered into it) */
        r.whiffs++;
        if (started >= 0) { r.answered++; r.latency_sum += started; }
        if (E(0)->hp < player_hp) r.hits++;
    }
    return r;
}

static void test_punish(void) {
    puts("the CPU punishes a move that misses: sharp ones nearly always, beginners rarely, and the sharper the faster");
    Punish p3 = punish_cycles(3, 40), p0 = punish_cycles(0, 40), pb = punish_cycles(-1, 40);
    double l3 = p3.answered ? p3.latency_sum / p3.answered : 0, l0 = p0.answered ? p0.latency_sum / p0.answered : 0, lb = pb.answered ? pb.latency_sum / pb.answered : 0;
    if (verbose) printf("  punished: level 3 %d/%d, level 0 %d/%d, beginner %d/%d;  mean time to answer: %.1f, %.1f, %.1f ticks\n",
                        p3.hits, p3.whiffs, p0.hits, p0.whiffs, pb.hits, pb.whiffs, l3, l0, lb);
    CHECK(p3.whiffs >= 30 && p0.whiffs >= 30 && pb.whiffs >= 30);     /* the 5K does miss */
    CHECK(p3.hits * 100 >= p3.whiffs * 70);                    /* sharp: at least 70% */
    CHECK(pb.hits * 100 <= pb.whiffs * 25);                    /* beginner: at most a quarter */
    CHECK(p3.hits > p0.hits && p0.hits >= pb.hits);            /* and it climbs with the level */
    CHECK(p3.answered >= 25 && p0.answered >= 15 && pb.answered >= 1);
    CHECK(l3 + 3.0 < l0 && l3 + 5.0 < lb);                     /* a reaction time: ~9 ticks for the sharpest, ~15 at level 0, ~18 for a beginner */
}

/* A command-grab super (the fighters' HK, 100 meter) is a punish for a sharp fighter. Whole fights between two CPUs with the
 * fencer's meter held full: how many supers does it start, by its level? A beginner is "passive, no supers". */
static int supers_started(int level, int fights) {
    int count = 0;
    for (int n = 0; n < fights; n++) {
        start(&ch.f_brawler, &spr.brawler, &ch.f_fencer, &spr.fencer, (uint32_t)(300 + n));
        for (int t = 0; t < 2500 && !F.over; t++) {
            GeFightInput a = none(), b = none();
            F.f[1].meter = GE_FIGHT_METER;
            ge_fight_cpu(&F, 0, 1, &a);
            ge_fight_cpu(&F, 1, level, &b);
            if (b.force_move >= 0) count += F.f[1].def->moves[b.force_move].ai == GE_AI_SUPER;
            tick(a, b);
        }
    }
    return count;
}

static void test_no_supers_for_beginners(void) {
    puts("supers: a sharp fighter uses one when it can; a beginner or a level-0 fighter never does");
    int s3 = supers_started(3, 24), s1 = supers_started(1, 24), s0 = supers_started(0, 24), sb = supers_started(-1, 24);
    if (verbose) printf("  supers started in 24 fights with a full meter: level 3 %d, level 1 %d, level 0 %d, beginner %d\n", s3, s1, s0, sb);
    CHECK(s3 >= 5 && s1 >= 2);                                 /* (so the setup does tempt it) */
    CHECK(s0 == 0 && sb == 0);
}

/* a move that is blocked has a recovery too: a fast answer to it should land, now and then */
/* The player hits the guard with `name` from a hair inside its reach; of the cycles where the CPU took no damage, how
 * many ended with the CPU hitting the player back? */
static int blocked_cycles(const char *name, int cycles, int *blocked) {
    start_default(11);
    int mv = move_named(0, name), answered = 0;
    *blocked = 0;
    for (int c = 0; c < cycles; c++) {
        settle(120.0f, 120.0f + (float)ge_fight_move_reach(&F, 0, mv) + 5.0f);
        int cpu_hp = E(1)->hp, player_hp = E(0)->hp;
        bool contact = false;
        F.f[1].ai_cd = 200;
        for (int t = 0; t < 70; t++) {
            GeFightInput p = none(), q = none();
            if (t == 0) p.force_move = mv;
            ge_fight_cpu(&F, 1, 3, &q);
            tick(p, q);
            contact |= F.f[0].contact;
        }
        if (E(1)->hp != cpu_hp || !contact) continue;
        (*blocked)++;
        if (E(0)->hp < player_hp) answered++;
    }
    return answered;
}

static void test_block_then_punish(void) {
    puts("the CPU guards what can reach it, answers a blocked move that leaves it open, and leaves a safe one alone");
    int bo, bs;
    int open_ = blocked_cycles("2K", 30, &bo), safe = blocked_cycles("5M", 30, &bs);
    if (verbose) printf("  2K (long recovery): blocked %d, answered %d;  5M (safe on block): blocked %d, answered %d\n", bo, open_, bs, safe);
    CHECK(bo >= 25 && bs >= 25);                               /* a level-3 fencer guards nearly everything */
    CHECK(open_ * 100 >= bo * 70);                             /* it takes the free hit */
    CHECK(safe * 100 <= bs * 10);                              /* and does not swing into one that is safe */
}

/* ---- breaking throws ---------------------------------------------------------------------------------- */

static int tech_cycles(int level, int cycles, int *thrown, int *countered) {
    start_default(5);
    int teched = 0, thrown_move = move_named(0, "LM");
    *thrown = *countered = 0;
    for (int c = 0; c < cycles; c++) {
        settle(120.0f, 120.0f + 22.0f);
        int cpu_hp = E(1)->hp, player_hp = E(0)->hp;
        bool caught = false;
        F.f[1].ai_cd = 200;
        for (int t = 0; t < 60; t++) {
            GeFightInput p = none(), q = none();
            if (t == 0) p.force_move = thrown_move;
            ge_fight_cpu(&F, 1, level, &q);
            tick(p, q);
            caught |= F.f[1].state == GE_FS_THROWN;
        }
        if (!caught) continue;                          /* the throw did not land (blocked, jumped...): not a trial */
        (*thrown)++;
        if (E(1)->hp == cpu_hp) { teched++; *countered += E(0)->hp < player_hp; }    /* (broke it, and the thrower still lost health: the break press started a throw of its own) */
    }
    return teched;
}

static void test_tech(void) {
    puts("the CPU breaks throws in proportion to its level, and a beginner never does");
    int n3, n1, nb, c3, c1, cb;
    int t3 = tech_cycles(3, 40, &n3, &c3), t1 = tech_cycles(1, 40, &n1, &c1), tb = tech_cycles(-1, 40, &nb, &cb);
    if (verbose) printf("  throws broken: level 3 %d/%d, level 1 %d/%d, beginner %d/%d\n", t3, n3, t1, n1, tb, nb);
    CHECK(n3 >= 30 && n1 >= 30 && nb >= 30);                   /* the throw lands */
    CHECK(t3 * 100 >= n3 * 70);                                /* about 85% */
    CHECK(t1 * 100 >= n1 * 15 && t1 * 100 <= n1 * 60);         /* about 35% */
    CHECK(tb == 0);
    CHECK(t3 > t1);
    CHECK(c3 == 0 && c1 == 0 && cb == 0);                      /* breaking a throw is not also a throw: the press that broke it is spent */
}

/* ---- temperament ---------------------------------------------------------------------------------------- */

static GeCpuStyle style(float aggression, float spacing, float grab, float patience, float mixup, float adapt) {
    return (GeCpuStyle){ aggression, spacing, grab, patience, mixup, adapt };
}

/* the average distance a fencer keeps from a player who just stands there, in ticks it is free to choose */
static float keeps_distance(GeCpuStyle st, int level, uint32_t seed) {
    start_default(seed);
    F.cpu_style[1] = st;
    double sum = 0; int n = 0;
    for (int c = 0; c < 6; c++) {
        settle(100.0f, 100.0f + 200.0f);
        for (int t = 0; t < 360; t++) {
            GeFightInput q = none();
            ge_fight_cpu(&F, 1, level, &q);
            tick(none(), q);
            if (free_(1) && t > 20) { sum += fabs(E(1)->pos.x - E(0)->pos.x); n++; }
        }
    }
    return n ? (float)(sum / n) : 0.0f;
}

static void test_style(void) {
    puts("temperament: a spacing fighter keeps its distance, a rush-down fighter closes it");
    float rush = 0, space = 0;
    for (uint32_t seed = 3; seed <= 8; seed++) {
        rush += keeps_distance(style(0.95f, 0.05f, 0.3f, 0.1f, 0.4f, 0.0f), 2, seed) / 6.0f;
        space += keeps_distance(style(0.15f, 0.95f, 0.1f, 0.6f, 0.4f, 0.0f), 2, seed) / 6.0f;
    }
    if (verbose) printf("  average distance: rush-down %.0f, spacing %.0f\n", rush, space);
    CHECK(space > rush + 12.0f);
}

/* ---- reading what you keep doing ------------------------------------------------------------------------ */

/* The player (who can't be hit, so the CPU's answers don't change what happens next) jumps in over and over from 70 px.
 * How soon after each jump does the CPU swing at it from the ground? (40 ticks if it never does.) */
static double anti_air_latency(float adapt, uint32_t seed, int *jumps_answered) {
    start_default(seed);
    F.cpu_style[1] = style(0.4f, 0.4f, 0.3f, 0.4f, 0.4f, adapt);
    double sum = 0; int n = 0, prev_move = -1, prev_state = -1;
    *jumps_answered = 0;
    for (int c = 0; c < 8; c++) {
        settle(120.0f, 120.0f + 70.0f);
        F.f[1].ai_cd = 0;
        int jump_at = -1, answered_at = -1;
        for (int t = 0; t < 240; t++) {
            GeFightInput p = none(), q = none();
            if (free_(0) && E(0)->on_ground && t % 40 == 5) { p.y = -1; jump_at = t; answered_at = -1; }     /* the player jumps, and jumps, and jumps */
            E(0)->invuln = 5;
            ge_fight_cpu(&F, 1, 3, &q);
            tick(p, q);
            bool started = F.f[1].state == GE_FS_MOVE && (prev_state != GE_FS_MOVE || prev_move != F.f[1].move);
            if (started && jump_at >= 0 && answered_at < 0 && F.f[1].def->moves[F.f[1].move].ai == GE_AI_ANTIAIR && F.f[0].state == GE_FS_AIR) answered_at = t;
            prev_state = F.f[1].state; prev_move = F.f[1].move;
            if (jump_at >= 0 && t == jump_at + 38) {                                    /* the jump is over: score it */
                sum += answered_at >= 0 ? answered_at - jump_at : 40; n++; *jumps_answered += answered_at >= 0;
                jump_at = -1;
            }
        }
    }
    return n ? sum / n : 40.0;
}

static void test_adapts(void) {
    puts("adapting: a fighter that adapts answers a player who keeps jumping in sooner than one that doesn't");
    double with = 0, without = 0; int aw = 0, ao = 0, total = 0;
    for (uint32_t seed = 1; seed <= 10; seed++) {
        int a, b;
        with += anti_air_latency(1.0f, seed, &a) / 10.0; without += anti_air_latency(0.0f, seed, &b) / 10.0;
        aw += a; ao += b; total += 8 * 5;
    }
    if (verbose) printf("  mean time to answer a jump-in: adapting %.1f ticks (%d of %d jumps answered), not adapting %.1f ticks (%d answered)\n", with, aw, total, without, ao);
    CHECK(aw * 100 >= total * 50 && ao * 100 >= total * 30);   /* (so the setup does provoke it) */
    CHECK(with + 3.0 < without);
}

/* ---- the rest ------------------------------------------------------------------------------------------- */

static void test_deterministic(void) {
    puts("the same fight played twice gives the same result");
    int result[2][4];
    for (int run = 0; run < 2; run++) {
        start(&ch.f_brawler, &spr.brawler, &ch.f_fencer, &spr.fencer, 99);
        F.infinite_clock = false;
        for (int t = 0; t < 1500 && !F.over; t++) {
            GeFightInput a = none(), b = none();
            ge_fight_cpu(&F, 0, 2, &a);
            ge_fight_cpu(&F, 1, 3, &b);
            tick(a, b);
        }
        result[run][0] = E(0)->hp; result[run][1] = E(1)->hp; result[run][2] = (int)E(0)->pos.x; result[run][3] = (int)E(1)->pos.x;
    }
    CHECK(!memcmp(result[0], result[1], sizeof result[0]));
}

static void test_cpu_vs_cpu_finishes(void) {
    puts("two CPU fighters (every level against every level) finish their rounds, nobody stands frozen");
    int bad = 0;
    for (int l0 = -1; l0 <= 3; l0++)
        for (int l1 = -1; l1 <= 3; l1++) {
            start(&ch.f_brawler, &spr.brawler, &ch.f_fencer, &spr.fencer, (uint32_t)(l0 * 7 + l1 + 20));
            F.infinite_clock = false;
            int t = 0;
            for (; t < 12000 && !F.over; t++) {
                GeFightInput a = none(), b = none();
                ge_fight_cpu(&F, 0, l0, &a);
                ge_fight_cpu(&F, 1, l1, &b);
                tick(a, b);
            }
            if (!F.over) bad++;
        }
    CHECK(bad == 0);
}

/* Two identical CPU fighters at the same level should win about equally often from either side of the screen. (Before the
 * throw-break press stopped counting as a throw of its own, the side that was thrown first won six matches in ten.) */
static void test_mirror_is_fair(void) {
    puts("fairness: the same fighter at the same level wins about as often from either side");
    int wins0 = 0, played = 0;
    for (int n = 0; n < 160; n++) {
        start(&ch.f_brawler, &spr.brawler, &ch.f_brawler, &spr.brawler, (uint32_t)(500 + n));
        F.infinite_clock = false;
        for (int t = 0; t < 9000 && !F.over; t++) {
            GeFightInput a = none(), b = none();
            ge_fight_cpu(&F, 0, 3, &a);
            ge_fight_cpu(&F, 1, 3, &b);
            tick(a, b);
        }
        if (!F.over || F.f[0].wins == F.f[1].wins) continue;
        played++;
        wins0 += F.f[0].wins > F.f[1].wins;
    }
    if (verbose) printf("  side 0 won %d of %d decided matches\n", wins0, played);
    CHECK(played >= 120);
    CHECK(wins0 * 100 >= played * 35 && wins0 * 100 <= played * 65);
}

int main(void) {
    verbose = getenv("CPUDBG") != NULL;
    if (getenv("CPUSEED")) g_seed_off = (uint32_t)atoi(getenv("CPUSEED"));
    art_make_sheets(&spr);
    if (chars_init(&ch, &spr) != 0) { puts("characters failed to build"); return 1; }
    test_punish();
    test_no_supers_for_beginners();
    test_block_then_punish();
    test_tech();
    test_style();
    test_adapts();
    test_deterministic();
    test_cpu_vs_cpu_finishes();
    test_mirror_is_fair();
    if (inited) ge_fight_free(&F);
    art_free(&spr);
    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
