/* Paragon tests: the player model, the undying/finisher rules. */

#include "fight_fixture.h"
#include "ge/ge_model.h"
#include "ge/ge_paragon.h"

static GeFightInput poke(void) { return btn(0, 0, GE_FB_L); }

static void test_model_learns(void) {
    puts("player model");
    GePlayerModel M;
    ge_model_init(&M);
    CHECK(ge_model_confidence(&M) < 0.05f);
    fresh();
    ge_fight_observe(&F, &M, 0);
    close_in(30);
    for (int i = 0; i < 1200 && F.phase == GE_FP_FIGHT; i++) {      /* P0 spams the light poke; P1 stands there */
        E(1)->hp = 100;                                                /* keep the fight going */
        close_in(30);
        step((i & 1) ? NONE : poke(), NONE);
    }
    float p[GE_INT_COUNT];
    ge_model_predict(&M, ge_dist_bucket(30), -1, p);
    float sum = 0;
    for (int i = 0; i < GE_INT_COUNT; i++) sum += p[i];
    printf("  p(poke)=%.2f confidence=%.2f\n", p[GE_INT_POKE], ge_model_confidence(&M));
    CHECK(sum > 0.99f && sum < 1.01f);
    CHECK(p[GE_INT_POKE] > 0.5f);
    CHECK(ge_model_confidence(&M) > 0.2f);
    CHECK(M.hits_landed > 5.0f);

    char path[] = "/tmp/ge_model_test.txt";
    CHECK(ge_model_save(&M, path) == 0);
    GePlayerModel L;
    ge_model_init(&L);
    CHECK(ge_model_load(&L, path) == 0);
    float q[GE_INT_COUNT];
    ge_model_predict(&L, ge_dist_bucket(30), -1, q);
    CHECK(fabsf(q[GE_INT_POKE] - p[GE_INT_POKE]) < 0.01f);
    remove(path);

    char facts[6][72];
    int n = ge_model_describe(&M, facts, 6);
    CHECK(n > 0);
    for (int i = 0; i < n; i++) printf("  - %s\n", facts[i]);

    float before = ge_model_confidence(&M);
    ge_model_decay(&M, 0.5f);
    CHECK(ge_model_confidence(&M) < before);
}

static GeFighterDef BOSS;

static void boss_fight(uint32_t seed) {
    if (F_live) ge_fight_free(&F);
    BOSS = D1;
    BOSS.undying = true;
    BOSS.clip_finisher = BOSS.clip_idle;
    ge_fight_init(&F, &D0, &spr, &BOSS, &spr, NULL, seed);
    F_live = true;
    F.rounds_to_win = 1;
    F.infinite_clock = true;
    for (int i = 0; i < 200 && F.phase != GE_FP_FIGHT; i++) idle(1);
}

static void test_finisher(void) {
    puts("undying + finisher");
    boss_fight(3);
    bool saw_finisher = false;
    int min_boss_hp = 999;
    for (int i = 0; i < 4000 && !F.over; i++) {                      /* P0 hammers the boss; it never fights back */
        GeFightInput a = NONE;
        if (F.phase == GE_FP_FIGHT) {
            close_in(30);
            a = (i & 1) ? NONE : poke();
        }
        step(a, NONE);
        if (F.phase == GE_FP_FINISHER) saw_finisher = true;
        if (E(1)->hp < min_boss_hp) min_boss_hp = E(1)->hp;
    }
    CHECK(min_boss_hp == 1);                      /* it bottomed out at 1, never 0 */
    CHECK(saw_finisher && F.finisher_seen);
    CHECK(F.winner == 1);                         /* the boss took the match */
    CHECK(E(0)->hp <= 0 && E(1)->hp == 1);

    puts("finisher clock");
    boss_fight(4);
    F.finisher_tick = 300;
    for (int i = 0; i < 1000 && !F.over; i++) {   /* nobody touches anybody */
        E(0)->pos.x = 40; E(1)->pos.x = 500;
        step(NONE, NONE);
    }
    CHECK(F.finisher_seen && F.winner == 1);
    CHECK(F.fight_ticks >= 300);

    puts("a normal fighter is still mortal");
    fresh();
    for (int i = 0; i < 2000 && F.phase == GE_FP_FIGHT; i++) { close_in(30); step((i & 1) ? NONE : poke(), NONE); }
    CHECK(E(1)->hp == 0 && !F.finisher_seen);
}

static void test_frame_data(void) {
    puts("frame data helpers");
    fresh();
    int l = 7, m = 8;                               /* indexes of 5L and 5M in the fixture */
    CHECK(ge_fight_move_startup(&F, 0, l) == 3);
    CHECK(ge_fight_move_startup(&F, 0, m) == 5);
    CHECK(ge_fight_move_total(&F, 0, l) == 13);
    CHECK(ge_fight_move_reach(&F, 0, l) == 38);
    CHECK(ge_fight_frames_until_free(&F, 0) == 0);
    close_in(120);                                  /* whiff, so there is no hit-stop */
    step(btn(0, 0, GE_FB_L), NONE);
    CHECK(F.f[0].state == GE_FS_MOVE);
    int left = ge_fight_frames_until_free(&F, 0);
    CHECK(left > 8 && left <= 13);
    int start = ge_fight_frames_until_free(&F, 0), n = 0;
    while (F.f[0].state == GE_FS_MOVE && n < 100) { step(NONE, NONE); n++; }
    printf("  predicted %d actual %d\n", start, n);
    CHECK(n >= start - 3 && n <= start + 3);        /* the estimate matches the real recovery */
}


/* ---- the brain ---------------------------------------------------------- */

typedef struct { int winner, ticks, hp_lost, hp_dealt; } Result;
typedef GeFightInput (*BotFn)(int tick);

/* A habitual player: walks in, throws the same medium punch whenever close, never blocks. */
static GeFightInput habit_bot(int tick) {
    (void)tick;
    GeEntity *a = E(0), *b = E(1);
    float gap = fabsf(b->pos.x - a->pos.x);
    int toward = b->pos.x > a->pos.x ? 1 : -1;
    if (F.f[0].state != GE_FS_STAND && F.f[0].state != GE_FS_CROUCH) return NONE;
    if (gap > 44) return dirs(toward, 0);
    return (tick & 7) == 0 ? btn(0, 0, GE_FB_M) : NONE;
}

/* `bot` null = the built-in CPU at level 3. Returns the result of one first-to-`rounds` match. */
static Result run_match(uint32_t seed, BotFn bot, GeParagon *brain, GePlayerModel *model, int rounds, bool undying) {
    if (F_live) ge_fight_free(&F);
    BOSS = D1;
    BOSS.undying = undying;
    BOSS.clip_finisher = BOSS.clip_idle;
    ge_fight_init(&F, &D0, &spr, &BOSS, &spr, NULL, seed);
    F_live = true;
    F.rounds_to_win = rounds;
    ge_fight_observe(&F, model, 0);
    Result r = { -1, 0, 0, 0 };
    int tick = 0, hp1 = BOSS.max_hp;
    for (; tick < 60000 && !F.over; tick++) {
        GeFightInput a = NONE, b = NONE;
        if (F.phase == GE_FP_FIGHT) {
            if (bot) a = bot(tick); else ge_fight_cpu(&F, 0, 3, &a);
            ge_paragon_think(brain, &F, 1, &b);
        }
        step(a, b);
        if (E(1)->hp < hp1) { r.hp_lost += hp1 - E(1)->hp; }
        hp1 = E(1)->hp > hp1 ? E(1)->hp : E(1)->hp;   /* refills between rounds are not damage */
        if (F.phase == GE_FP_INTRO) hp1 = E(1)->hp;
    }
    r.winner = F.winner;
    r.ticks = tick;
    return r;
}

static void test_brain(void) {
    puts("paragon brain");
    /* 1. against the built-in level-3 CPU, with nothing learned yet */
    int wins = 0, n = 12;
    for (int sd = 1; sd <= n; sd++) {
        GePlayerModel M; ge_model_init(&M);
        GeParagon P; ge_paragon_init(&P, &M);
        ge_paragon_set_learn(&P, 0, 0);
        Result r = run_match(100 + sd, NULL, &P, &M, 2, false);
        wins += r.winner == 1;
    }
    printf("  untrained paragon vs level-3 CPU: %d / %d matches won\n", wins, n);
    CHECK(wins >= n * 3 / 4);

    /* 2. the same, fully grown */
    wins = 0;
    for (int sd = 1; sd <= n; sd++) {
        GePlayerModel M; ge_model_init(&M);
        GeParagon P; ge_paragon_init(&P, &M);
        ge_paragon_set_learn(&P, 100, 1);
        Result r = run_match(100 + sd, NULL, &P, &M, 2, false);
        wins += r.winner == 1;
    }
    printf("  grown paragon vs level-3 CPU:     %d / %d matches won\n", wins, n);
    CHECK(wins >= n * 11 / 12);

    /* 3. it learns: against one habit, an informed Paragon takes less damage than an ignorant one */
    int lost_blank = 0, lost_taught = 0;
    GePlayerModel taught; ge_model_init(&taught);
    for (int sd = 1; sd <= 6; sd++) {                             /* a few earlier duels teach it */
        GeParagon P; ge_paragon_init(&P, &taught);
        ge_paragon_set_learn(&P, 40, 0);
        run_match(500 + sd, habit_bot, &P, &taught, 1, false);
    }
    printf("  after 6 duels its confidence in the model is %.2f\n", ge_model_confidence(&taught));
    for (int sd = 1; sd <= 10; sd++) {
        GePlayerModel blank; ge_model_init(&blank);
        GeParagon P; ge_paragon_init(&P, &blank);
        ge_paragon_set_learn(&P, 40, 0);
        lost_blank += run_match(900 + sd, habit_bot, &P, &blank, 1, false).hp_lost;
        GePlayerModel copy = taught;
        GeParagon Q; ge_paragon_init(&Q, &copy);
        ge_paragon_set_learn(&Q, 40, 0);
        lost_taught += run_match(900 + sd, habit_bot, &Q, &copy, 1, false).hp_lost;
    }
    printf("  damage taken vs a habitual player over 10 fights: blank memory %d, taught memory %d\n", lost_blank, lost_taught);
    CHECK(lost_taught <= lost_blank);
}

int main(void) {
    memset(&spr, 0, sizeof spr);
    spr.sheet = ge_image_create(64, 16);
    if (ge_sprite_parse(&spr, CLIPS) != 0) { puts("clip parse failed"); return 1; }
    make_def(&D0); make_def(&D1);
    if (ge_fighter_def_finish(&D0, &spr) || ge_fighter_def_finish(&D1, &spr)) { puts("def finish failed"); return 1; }
    test_model_learns();
    test_finisher();
    test_frame_data();
    test_brain();
    if (F_live) ge_fight_free(&F);
    ge_sprite_free(&spr);
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
