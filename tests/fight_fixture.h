/* Shared fixture for the fight tests: a tiny sprite, a generic fighter def and helpers to step a fight with real inputs. */
#ifndef FIGHT_FIXTURE_H
#define FIGHT_FIXTURE_H
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
static __attribute__((unused)) int move_named(int s, const char *clip) {
    if (F.f[s].state != GE_FS_MOVE) return 0;
    return !strcmp(spr.clips[F.f[s].def->moves[F.f[s].move].clip_idx].name, clip);
}
static __attribute__((unused)) void until_neutral(int s) { for (int i = 0; i < 200 && F.f[s].state != GE_FS_STAND && F.f[s].state != GE_FS_CROUCH; i++) idle(1); }

#endif
