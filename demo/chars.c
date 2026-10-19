/* Every character's animation clips, move list and tuning. This is the file
 * to edit to change how the game plays. Cell numbers refer to the layouts in
 * demo.h; damage in the overworld is in masks (the hero has 5). */

#include "demo.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char g_buf[16384];
static size_t g_len;

static void emit(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(g_buf + g_len, sizeof g_buf - g_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_len += (size_t)n;
}

static int parse(GeSprite *s, const char *who) {
    int rc = ge_sprite_parse(s, g_buf);
    if (rc != 0) fprintf(stderr, "%s clips: syntax error on line %d\n", who, -rc);
    g_len = 0;
    g_buf[0] = 0;
    return rc;
}

/* ---- overworld clips ---------------------------------------------------- */

static void hero_overworld(void) {
    emit("cell %d %d\norigin %d %d\n", HUM_CELL_W, HUM_CELL_H, HUM_BODY_X, HUM_CELL_H - 1);
    emit("clip idle loop\n frame %d 28\n frame %d 28\n", H_IDLE0, H_IDLE1);
    emit("clip run loop\n frame %d 5\n frame %d 5\n frame %d 5\n frame %d 5\n", H_RUN0, H_RUN1, H_RUN2, H_RUN3);
    emit("clip jump once\n frame %d 8\nclip fall once\n frame %d 8\n", H_JUMP, H_FALL);
    emit("clip wall loop\n frame %d 99\nclip hurt once\n frame %d 99\nclip dead once\n frame %d 999\nclip dash once\n frame %d 99\n",
         H_WALL, H_HURT, H_DEAD, H_DASH);
    /* needle slashes: 2-hit chain on the ground, single swing in the air */
    emit("clip slash1 once dmg=5 kb=2.5,-1 stop=3 stun=14 cancel=1 next=slash2 recoil=1.2\n"
         " frame %d 2\n frame %d 3 hit=0,-30,40,24\n frame %d 6\n", H_S1A, H_S1B, H_S1C);
    emit("clip slash2 once dmg=5 kb=3,-1.5 stop=3 stun=14 recoil=1.2\n"
         " frame %d 2\n frame %d 3 hit=0,-30,40,24\n frame %d 7\n", H_S2A, H_S2B, H_S2C);
    emit("clip aslash once dmg=5 kb=2.5,-1 stop=3 stun=14\n frame %d 2\n frame %d 3 hit=0,-30,40,24\n frame %d 5\n", H_S1A, H_S1B, H_S1C);
    emit("clip upslash once dmg=5 kb=0.5,-3 stop=3 stun=14\n frame %d 2\n frame %d 3 hit=-14,-52,28,28\n frame %d 6\n", H_UPA, H_UPB, H_UPC);
    /* down-slash: bounce off enemies, enemy shots and spikes */
    emit("clip downslash once dmg=5 kb=0.5,2 stop=2 stun=12 pogo=6.4\n frame %d 1\n frame %d 7 hit=-11,-6,22,20\n frame %d 4\n", H_DNA, H_DNB, H_DNC);
    emit("clip spin once dmg=12 kb=4,-3 stop=5 stun=24\n");
    for (int i = 0; i < 6; i++) emit(" frame %d 3 hit=-28,-38,56,42\n", H_SP0 + (i & 3));
    emit("clip toss once shoot=1\n frame %d 4\n frame %d 4\n frame %d 8\n", H_THA, H_THB, H_THC);
    emit("clip heal loop\n frame %d 8\n frame %d 8\n", H_HEAL0, H_HEAL1);
}

static void humanoid_overworld_enemy(int windup) {
    emit("cell %d %d\norigin %d %d\n", HUM_CELL_W, HUM_CELL_H, HUM_BODY_X, HUM_CELL_H - 1);
    emit("clip idle loop\n frame %d 30\n frame %d 30\n", H_IDLE0, H_IDLE1);
    emit("clip run loop\n frame %d 7\n frame %d 7\n frame %d 7\n frame %d 7\n", H_RUN0, H_RUN1, H_RUN2, H_RUN3);
    emit("clip hurt once\n frame %d 99\nclip dead once\n frame %d 999\n", H_HURT, H_DEAD);
    /* the long wind-up is the tell: dodge, dash through it, or strike first */
    emit("clip attack once dmg=1 kb=3.5,-2.5 stop=5 stun=20\n frame %d %d\n frame %d 4 hit=0,-28,28,24\n frame %d 16\n", H_S1A, windup, H_S1B, H_S1C);
}

/* ---- fighting-game clips: one template, three characters ---------------- */

typedef struct {
    int reach;      /* added to hitbox widths */
    float dmg;      /* damage multiplier */
    int slow;       /* extra startup ticks */
    float kb;       /* knockback multiplier */
} Style;

static int D(const Style *s, int base) { int v = (int)((float)base * s->dmg + 0.5f); return v < 1 ? 1 : v; }

static void fight_clips(const Style *s) {
    emit("clip f_idle loop\n frame %d 28\n frame %d 28\n", H_IDLE0, H_IDLE1);
    emit("clip f_walk loop\n frame %d 7\n frame %d 7\n frame %d 7\n frame %d 7\n", H_RUN0, H_RUN1, H_RUN2, H_RUN3);
    emit("clip f_walkb loop\n frame %d 8\n frame %d 8\n frame %d 8\n frame %d 8\n", H_RUN3, H_RUN2, H_RUN1, H_RUN0);
    emit("clip f_crouch loop\n frame %d 99\nclip f_jump once\n frame %d 99\nclip f_fall once\n frame %d 99\n", H_CROUCH, H_JUMP, H_FALL);
    emit("clip f_hurt once\n frame %d 99\nclip f_hurt_air once\n frame %d 99\n", H_HURT, H_HURTAIR);
    emit("clip f_block once\n frame %d 99\nclip f_block_low once\n frame %d 99\n", H_BLOCK, H_BLOCKLO);
    emit("clip f_down once\n frame %d 999\nclip f_ko once\n frame %d 999\nclip f_win loop\n frame %d 99\n", H_DEAD, H_DEAD, H_WIN);
    emit("clip f_throw once\n frame %d 14\n frame %d 8\n frame %d 10\nclip f_thrown once\n frame %d 99\n", H_GRAB, H_S1B, H_S1C, H_HURTAIR);
    emit("clip f_dash once\n frame %d 99\nclip f_backdash once\n frame %d 99\n", H_DASH, H_BLOCK);
    /* standing normals: light / medium / heavy */
    emit("clip f_5l once dmg=%d kb=%.1f,0 stop=6 stun=13 bstun=9 cancel=1\n frame %d %d\n frame %d 3 hit=4,-26,%d,10\n frame %d 6\n",
         D(s, 5), 1.2f * s->kb, H_JABA, 3 + s->slow / 2, H_JABB, 36 + s->reach, H_JABA);
    emit("clip f_5m once dmg=%d kb=%.1f,0 stop=8 stun=17 bstun=12 cancel=1\n frame %d %d\n frame %d 3 hit=0,-30,%d,22\n frame %d 10\n",
         D(s, 8), 1.8f * s->kb, H_S1A, 5 + s->slow, H_S1B, 44 + s->reach, H_S1C);
    emit("clip f_5h once dmg=%d kb=%.1f,-3 stop=10 stun=24 bstun=15 cancel=2\n frame %d %d\n frame %d 3 hit=0,-34,%d,30\n frame %d 3 hit=0,-30,%d,28\n frame %d 14\n",
         D(s, 13), 3.2f * s->kb, H_HVA, 8 + s->slow, H_HVB, 52 + s->reach, H_HVC, 52 + s->reach, H_HVD);
    /* crouching: all hit low, the heavy sweeps */
    emit("clip f_2l once dmg=%d kb=1,0 stop=6 stun=12 bstun=8 guard=low cancel=1\n frame %d 3\n frame %d 3 hit=2,-12,%d,10\n frame %d 6\n",
         D(s, 4), H_CROUCH, H_KICK, 32 + s->reach / 2, H_CROUCH);
    emit("clip f_2m once dmg=%d kb=1.5,0 stop=8 stun=16 bstun=11 guard=low cancel=1\n frame %d %d\n frame %d 3 hit=0,-16,%d,14\n frame %d 10\n",
         D(s, 7), H_CSA, 5 + s->slow, H_CSB, 44 + s->reach, H_CROUCH);
    emit("clip f_2h once dmg=%d kb=2,-2.5 stop=9 stun=20 bstun=14 guard=low kd=1\n frame %d %d\n frame %d 4 hit=0,-8,%d,10\n frame %d 16\n",
         D(s, 11), H_SWA, 8 + s->slow, H_SWB, 50 + s->reach, H_CROUCH);
    /* forward + medium: an overhead that beats crouch-blocking */
    emit("clip f_6m once dmg=%d kb=1.5,0 stop=8 stun=18 bstun=12 guard=high lunge=1.5\n frame %d %d\n frame %d 3 hit=0,-34,%d,30\n frame %d 12\n",
         D(s, 9), H_HVA, 14 + s->slow, H_HVB, 44 + s->reach, H_HVD);
    /* jumping attacks hit high */
    emit("clip f_jl once dmg=%d kb=1,0 stop=6 stun=14 guard=high\n frame %d 2\n frame %d 6 hit=0,-28,%d,22\n frame %d 4\n", D(s, 5), H_S1A, H_S1B, 34 + s->reach, H_S1C);
    emit("clip f_jm once dmg=%d kb=1.2,0 stop=7 stun=17 guard=high\n frame %d 3\n frame %d 6 hit=0,-26,%d,26\n frame %d 5\n", D(s, 7), H_S2A, H_S2B, 36 + s->reach, H_S2C);
    emit("clip f_jh once dmg=%d kb=1.5,0 stop=8 stun=20 guard=high\n frame %d 4\n frame %d 6 hit=-4,-14,%d,24\n frame %d 6\n", D(s, 10), H_DNA, H_JH, 36 + s->reach, H_DNC);
    /* K: the sword at full stretch. A slow thrust that out-ranges everything, a low one, and an air cut */
    emit("clip f_5k once dmg=%d kb=%.1f,0 stop=8 stun=18 bstun=12 cancel=2\n frame %d %d\n frame %d 4 hit=8,-24,%d,8\n frame %d 14\n",
         D(s, 9), 2.0f * s->kb, H_TRA, 7 + s->slow, H_TRB, 54 + s->reach, H_TRC);
    emit("clip f_2k once dmg=%d kb=1.5,0 stop=8 stun=16 bstun=11 guard=low cancel=2\n frame %d %d\n frame %d 4 hit=4,-10,%d,8\n frame %d 14\n",
         D(s, 8), H_CROUCH, 8 + s->slow, H_CSB, 50 + s->reach, H_CROUCH);
    emit("clip f_jk once dmg=%d kb=1.5,-1 stop=8 stun=20 guard=high\n frame %d 3\n frame %d 6 hit=0,-28,%d,30\n frame %d 6\n",
         D(s, 9), H_S1A, H_JK, 42 + s->reach, H_S1C);
    /* specials */
    emit("clip f_shoot once shoot=1\n frame %d %d\n frame %d 4\n frame %d 14\n", H_THA, 6 + s->slow, H_THB, H_THC);
    emit("clip f_rise once dmg=%d kb=2,-6 stop=10 stun=30 bstun=16 inv=7 rise=6.5\n frame %d 2\n frame %d 10 hit=-6,-46,%d,44\n frame %d 14\n",
         D(s, 12), H_UPA, H_RISE, 30 + s->reach / 2, H_FALL);
    emit("clip f_rush once dmg=%d kb=3,-2 stop=8 stun=22 bstun=14 lunge=5\n frame %d %d\n frame %d 4 hit=0,-30,%d,24\n frame %d 14\n",
         D(s, 10), H_DASH, 6 + s->slow, H_S1B, 44 + s->reach, H_S1C);
    emit("clip f_super once dmg=%d kb=4.5,-5.5 stop=14 stun=50 inv=24 lunge=3\n frame %d 4\n", D(s, 32), H_SP0);
    for (int i = 0; i < 5; i++) emit(" frame %d 4 hit=-30,-40,%d,44\n", H_SP1 + (i % 3), 68 + s->reach);
    emit(" frame %d 10\n", H_IDLE0);
}

#define MV(i, c, lv, cost, sh, role) (GeMove){ .input = i, .clip = c, .level = lv, .meter_cost = cost, .shot = sh, .ai = role }

static void fighter_def(GeFighterDef *d, const GeSprite *spr, const GeShotDef *shot, float walk, float jump, int throw_dmg) {
    memset(d, 0, sizeof *d);
    d->body.gravity = 0.42f; d->body.max_fall = 9.0f; d->body.jump_vel = jump; d->body.run_speed = walk;
    d->body.body_w = 14; d->body.body_h = 28; d->body.dead_ticks = 9999;
    d->walk_back = walk * 0.8f; d->jump_vx = 1.7f; d->dash_speed = 3.4f; d->dash_ticks = 12;
    d->crouch_h = 18; d->max_hp = 100; d->throw_range = 30; d->throw_damage = throw_dmg;
#define C(n) ge_sprite_find_clip(spr, n)
    d->clip_idle = C("f_idle"); d->clip_walk_f = C("f_walk"); d->clip_walk_b = C("f_walkb"); d->clip_crouch = C("f_crouch");
    d->clip_jump = C("f_jump"); d->clip_fall = C("f_fall"); d->clip_hurt = C("f_hurt"); d->clip_hurt_air = C("f_hurt_air");
    d->clip_block = C("f_block"); d->clip_block_low = C("f_block_low"); d->clip_down = C("f_down"); d->clip_ko = C("f_ko");
    d->clip_win = C("f_win"); d->clip_throw = C("f_throw"); d->clip_thrown = C("f_thrown"); d->clip_dash = C("f_dash");
    d->clip_backdash = C("f_backdash");
#undef C
    GeMove moves[] = {
        MV("HK", "f_super", 4, GE_FIGHT_METER, NULL, GE_AI_SUPER),   /* A+B together on a 3DS */
        MV("623H", "f_rise", 3, 0, NULL, GE_AI_ANTIAIR),
        MV("623M", "f_rise", 3, 0, NULL, GE_AI_ANTIAIR),
        MV("236H", "f_shoot", 3, 0, shot, GE_AI_ZONE),
        MV("236M", "f_shoot", 3, 0, shot, GE_AI_ZONE),
        MV("236L", "f_shoot", 3, 0, shot, GE_AI_ZONE),
        MV("214H", "f_rush", 3, 0, NULL, GE_AI_RUSH),
        MV("214M", "f_rush", 3, 0, NULL, GE_AI_RUSH),
        MV("LM", "f_throw", 0, 0, NULL, GE_AI_THROW),
        MV("6M", "f_6m", 1, 0, NULL, GE_AI_OVERHEAD),
        MV("j.K", "f_jk", 2, 0, NULL, GE_AI_AIR),
        MV("2K", "f_2k", 2, 0, NULL, GE_AI_LOW),
        MV("5K", "f_5k", 2, 0, NULL, GE_AI_POKE),
        MV("j.H", "f_jh", 2, 0, NULL, GE_AI_AIR),
        MV("j.M", "f_jm", 1, 0, NULL, GE_AI_AIR),
        MV("j.L", "f_jl", 0, 0, NULL, GE_AI_AIR),
        MV("2H", "f_2h", 2, 0, NULL, GE_AI_LOW),
        MV("2M", "f_2m", 1, 0, NULL, GE_AI_LOW),
        MV("2L", "f_2l", 0, 0, NULL, GE_AI_LOW),
        MV("5H", "f_5h", 2, 0, NULL, GE_AI_COMBO),
        MV("5M", "f_5m", 1, 0, NULL, GE_AI_COMBO),
        MV("5L", "f_5l", 0, 0, NULL, GE_AI_POKE),
    };
    d->nmoves = (int)(sizeof moves / sizeof moves[0]);
    memcpy(d->moves, moves, sizeof moves);
}

/* ---- the cast ------------------------------------------------------------ */

static void clips_common(GeActorDef *d) {
    d->clip_idle = d->clip_run = d->clip_jump = d->clip_fall = d->clip_wall = d->clip_hurt = d->clip_dead = d->clip_dash = -1;
    d->clip_attack = d->clip_attack_air = d->clip_attack_up = d->clip_attack_down = -1;
    d->clip_skill = d->clip_tool = d->clip_channel = -1;
}

/* Step 1: every sprite's built-in clips (the mods folder may replace some of them after this). */
int chars_parse(Sprites *s) {
    int rc = 0;
    const Style hero_style = { 0, 1.0f, 0, 1.0f }, brawler_style = { -4, 1.25f, 3, 1.2f }, fencer_style = { 8, 0.85f, -1, 0.9f };

    hero_overworld();
    fight_clips(&hero_style);
    rc |= parse(&s->hero, "hero");
    humanoid_overworld_enemy(24);
    fight_clips(&brawler_style);
    rc |= parse(&s->brawler, "brawler");
    humanoid_overworld_enemy(18);
    fight_clips(&fencer_style);
    rc |= parse(&s->fencer, "fencer");

    /* the Paragon: the hero's move list with its own frame data (longer reach, one tick quicker), plus a
     * finisher pose and a gift from the last duelist: an invulnerable-startup charge */
    const Style paragon_style = { 10, 1.0f, -1, 1.1f };
    hero_overworld();
    fight_clips(&paragon_style);
    emit("clip f_finisher once\n frame %d 40\n frame %d 8\n frame %d 12\n frame %d 99\n", H_HVA, H_HVB, H_HVC, H_HVD);
    emit("clip f_charge once dmg=14 kb=4,-3 stop=10 stun=28 bstun=16 inv=18 lunge=6\n frame %d 6\n frame %d 5 hit=0,-32,44,28\n frame %d 12\n",
         H_DASH, H_S1B, H_S1C);
    rc |= parse(&s->paragon, "paragon");

    /* the boss: the same puppet at double size, so double-size boxes */
    emit("cell %d %d\norigin %d %d\n", HUM_CELL_W * 2, HUM_CELL_H * 2, HUM_BODY_X * 2, HUM_CELL_H * 2 - 1);
    emit("clip idle loop\n frame %d 30\n frame %d 30\n", H_IDLE0, H_IDLE1);
    emit("clip run loop\n frame %d 6\n frame %d 6\n frame %d 6\n frame %d 6\n", H_RUN0, H_RUN1, H_RUN2, H_RUN3);
    emit("clip jump once\n frame %d 99\nclip fall once\n frame %d 99\nclip hurt once\n frame %d 99\nclip dead once\n frame %d 999\n",
         H_JUMP, H_FALL, H_HURT, H_DEAD);
    emit("clip swing once dmg=2 kb=4,-3 stop=6 stun=24\n frame %d 26\n frame %d 5 hit=0,-62,66,58\n frame %d 20\n", H_HVA, H_HVB, H_HVD);
    emit("clip charge once dmg=2 kb=5,-3 stop=6 stun=24\n frame %d 24\n frame %d 34 hit=0,-56,40,52\n frame %d 18\n", H_BLOCK, H_DASH, H_S1C);
    emit("clip leap once\n frame %d 20\n", H_CROUCH);
    emit("clip throw once shoot=1\n frame %d 20\n frame %d 6\n frame %d 16\n", H_THA, H_THB, H_THC);
    rc |= parse(&s->warden, "warden");

    emit("cell 24 16\norigin 12 15\nclip idle loop\n frame 0 20\n frame 1 20\nclip run loop\n frame 0 9\n frame 1 9\n"
         "clip hurt once\n frame 2 99\nclip dead once\n frame 3 999\n");
    rc |= parse(&s->crawler, "crawler");
    emit("cell 24 16\norigin 12 15\nclip idle loop\n frame 0 6\n frame 1 6\nclip run loop\n frame 0 4\n frame 1 4\n"
         "clip hurt once\n frame 2 99\nclip dead once\n frame 3 999\n");
    rc |= parse(&s->bat, "bat");
    emit("cell 24 24\norigin 12 23\nclip idle loop\n frame 0 20\n frame 1 20\nclip spit once shoot=1\n frame 2 18\n frame 2 6\n frame 0 10\n"
         "clip hurt once\n frame 3 99\nclip dead once\n frame 4 999\n");
    rc |= parse(&s->spitter, "spitter");
    emit("cell 16 16\norigin 8 8\nclip needle loop\n frame %d 4\n frame %d 4\nclip glob loop\n frame %d 6\n frame %d 6\n"
         "clip rock loop\n frame %d 99\nclip wave loop\n frame %d 5\n frame %d 5\nclip orb loop\n frame %d 5\n frame %d 5\n"
         "clip spear loop\n frame %d 99\nclip dust loop\n frame %d 99\n",
         FX_NEEDLE0, FX_NEEDLE1, FX_GLOB0, FX_GLOB1, FX_ROCK, FX_WAVE0, FX_WAVE1, FX_ORB0, FX_ORB1, FX_SPEAR, FX_DUST);
    rc |= parse(&s->fx, "fx");
    return rc;
}

/* Step 2: the defs, which look clips up by name in whatever the sprites hold now. */
int chars_build(Chars *c, Sprites *s) {
    memset(c, 0, sizeof *c);

    /* shots */
    const GeSprite *fx = &s->fx;
#define FXC(n) ge_sprite_find_clip(fx, n)
    c->needle = (GeShotDef){ .spr = fx, .clip = FXC("needle"), .speed = 6.5f, .w = 12, .h = 6, .damage = 7, .kb_x = 2, .kb_y = -1,
                             .hitstop = 2, .stun = 10, .life = 50, .pierce = true, .solid_stops = true };
    c->glob = (GeShotDef){ .spr = fx, .clip = FXC("glob"), .speed = 1.6f, .vy = -4.2f, .gravity = 0.18f, .w = 8, .h = 8, .damage = 1,
                           .kb_x = 2.5f, .kb_y = -2, .hitstop = 3, .stun = 14, .life = 220, .solid_stops = true, .breakable = true };
    c->spear = (GeShotDef){ .spr = fx, .clip = FXC("spear"), .speed = 3.6f, .vy = -3.6f, .gravity = 0.11f, .w = 14, .h = 6, .damage = 1,
                            .kb_x = 3, .kb_y = -2, .hitstop = 3, .stun = 16, .life = 220, .solid_stops = true, .breakable = true };
    c->shockwave = (GeShotDef){ .spr = fx, .clip = FXC("wave"), .speed = 2.8f, .w = 12, .h = 14, .damage = 1, .kb_x = 3, .kb_y = -3,
                                .hitstop = 3, .stun = 16, .life = 110, .pierce = true, .grounded = true };
    c->f_needle = (GeShotDef){ .spr = fx, .clip = FXC("needle"), .speed = 4.5f, .w = 12, .h = 6, .damage = 8, .kb_x = 1.5f,
                               .hitstop = 6, .stun = 18, .blockstun = 12, .chip = 2, .life = 150 };
    c->f_rock = (GeShotDef){ .spr = fx, .clip = FXC("rock"), .speed = 2.6f, .vy = -4.5f, .gravity = 0.2f, .w = 10, .h = 10, .damage = 10,
                             .kb_x = 2, .kb_y = -2, .hitstop = 7, .stun = 20, .blockstun = 12, .chip = 3, .life = 150, .solid_stops = true };
    c->f_dust = (GeShotDef){ .spr = fx, .clip = FXC("dust"), .speed = 3.2f, .w = 14, .h = 12, .damage = 7, .kb_x = 1.5f,
                             .hitstop = 6, .stun = 16, .blockstun = 10, .chip = 2, .life = 140, .grounded = true };
#undef FXC

    /* the hero: Silksong-style movement */
    GeActorDef *h = &c->hero;
    clips_common(h);
    h->run_speed = 1.9f; h->accel = 0.3f; h->friction = 0.38f; h->air_accel = 0.2f;
    h->gravity = 0.4f; h->max_fall = 7.5f; h->jump_vel = 6.8f; h->jump_cut_vel = 2.4f;
    h->coyote_ticks = 6; h->jump_buffer_ticks = 7;
    h->air_jumps = 1; h->air_jump_vel = 6.0f;
    h->wall_slide_speed = 1.2f; h->wall_jump_vx = 2.9f; h->wall_jump_vy = 6.3f; h->wall_jump_lock = 9;
    h->dash_speed = 4.8f; h->dash_ticks = 10; h->dash_cooldown = 24; h->air_dash = true;
    h->invuln_ticks = 70; h->dead_ticks = 80; h->body_w = 10; h->body_h = 22;
    h->meter_max = 99; h->meter_per_hit = 11; h->skill_cost = 33; h->channel_cost = 33; h->channel_ticks = 55;
    h->ammo_max = 3; h->ammo_recharge = 180; h->shot = &c->needle;
#define HC(n) ge_sprite_find_clip(&s->hero, n)
    h->clip_idle = HC("idle"); h->clip_run = HC("run"); h->clip_jump = HC("jump"); h->clip_fall = HC("fall"); h->clip_wall = HC("wall");
    h->clip_hurt = HC("hurt"); h->clip_dead = HC("dead"); h->clip_dash = HC("dash");
    h->clip_attack = HC("slash1"); h->clip_attack_air = HC("aslash"); h->clip_attack_up = HC("upslash"); h->clip_attack_down = HC("downslash");
    h->clip_skill = HC("spin"); h->clip_tool = HC("toss"); h->clip_channel = HC("heal");
#undef HC

    GeActorDef base;
    memset(&base, 0, sizeof base);
    clips_common(&base);
    base.gravity = 0.4f; base.max_fall = 7.0f; base.dead_ticks = 40; base.contact_damage = 1; base.contact_kb_x = 3; base.contact_kb_y = -2.5f;

    c->crawler = base;
    c->crawler.run_speed = 0.75f; c->crawler.accel = 0.1f;   /* (it paces at 0.6 of this, the old 0.45, and charges at all of it) */ c->crawler.friction = 0.2f; c->crawler.air_accel = 0.05f;
    c->crawler.body_w = 16; c->crawler.body_h = 10;
    c->crawler.clip_idle = ge_sprite_find_clip(&s->crawler, "idle"); c->crawler.clip_run = ge_sprite_find_clip(&s->crawler, "run");
    c->crawler.clip_hurt = ge_sprite_find_clip(&s->crawler, "hurt"); c->crawler.clip_dead = ge_sprite_find_clip(&s->crawler, "dead");
    c->crawler.clip_jump = c->crawler.clip_fall = c->crawler.clip_idle;

    c->bat = base;
    c->bat.flying = true; c->bat.run_speed = 1.1f; c->bat.accel = 0.06f; c->bat.friction = 0.05f;
    c->bat.body_w = 12; c->bat.body_h = 10;
    c->bat.clip_idle = ge_sprite_find_clip(&s->bat, "idle"); c->bat.clip_run = ge_sprite_find_clip(&s->bat, "run");
    c->bat.clip_hurt = ge_sprite_find_clip(&s->bat, "hurt"); c->bat.clip_dead = ge_sprite_find_clip(&s->bat, "dead");
    c->bat.clip_jump = c->bat.clip_fall = c->bat.clip_idle;

    c->spitter = base;
    c->spitter.body_w = 12; c->spitter.body_h = 20; c->spitter.friction = 1;
    c->spitter.shot = &c->glob; c->spitter.ammo_max = 99; c->spitter.ammo_recharge = 1;
    c->spitter.clip_idle = ge_sprite_find_clip(&s->spitter, "idle"); c->spitter.clip_tool = ge_sprite_find_clip(&s->spitter, "spit");
    c->spitter.clip_hurt = ge_sprite_find_clip(&s->spitter, "hurt"); c->spitter.clip_dead = ge_sprite_find_clip(&s->spitter, "dead");
    c->spitter.clip_run = c->spitter.clip_jump = c->spitter.clip_fall = c->spitter.clip_idle;

    c->grunt = base;
    c->grunt.run_speed = 0.8f; c->grunt.accel = 0.1f; c->grunt.friction = 0.25f; c->grunt.air_accel = 0.08f;
    c->grunt.jump_vel = 5.0f; c->grunt.jump_cut_vel = 2.4f; c->grunt.coyote_ticks = 3; c->grunt.jump_buffer_ticks = 3;   /* hops a one-tile step, not two */
    c->grunt.body_w = 14; c->grunt.body_h = 22;
#define BC(n) ge_sprite_find_clip(&s->brawler, n)
    c->grunt.clip_idle = BC("idle"); c->grunt.clip_run = BC("run"); c->grunt.clip_hurt = BC("hurt"); c->grunt.clip_dead = BC("dead");
    c->grunt.clip_attack = BC("attack"); c->grunt.clip_jump = c->grunt.clip_fall = BC("idle");
#undef BC

    c->duelist = base;   /* overworld stand-in: touching or striking it starts a duel */
    c->duelist.contact_damage = 0; c->duelist.run_speed = 0.5f; c->duelist.accel = 0.1f; c->duelist.friction = 0.25f;
    c->duelist.body_w = 14; c->duelist.body_h = 22;
    /* (clips are filled per duelist at spawn time: brawler or fencer sheet) */

    GeActorDef *wd = &c->warden;
    *wd = base;
    wd->run_speed = 1.0f; wd->accel = 0.12f; wd->friction = 0.25f; wd->air_accel = 0.1f;
    wd->gravity = 0.42f; wd->max_fall = 9.0f; wd->jump_vel = 7.8f;
    wd->coyote_ticks = 4; wd->jump_buffer_ticks = 4;   /* (without these the actor code never lets it leave the floor: its leap was a slide) */
    wd->body_w = 26; wd->body_h = 52; wd->contact_kb_x = 4; wd->contact_kb_y = -3; wd->dead_ticks = 150; wd->armored = true;
    wd->shot = &c->spear;
#define WC(n) ge_sprite_find_clip(&s->warden, n)
    wd->clip_idle = WC("idle"); wd->clip_run = WC("run"); wd->clip_jump = WC("jump"); wd->clip_fall = WC("fall");
    wd->clip_hurt = WC("hurt"); wd->clip_dead = WC("dead"); wd->clip_attack = WC("swing"); wd->clip_tool = WC("throw");
    wd->clip_attack_up = WC("charge");   /* (its charge is an "up attack": the brain presses attack with up held) */
    wd->ammo_max = 99; wd->ammo_recharge = 1;
#undef WC

    fighter_def(&c->f_hero, &s->hero, &c->f_needle, 1.3f, 6.2f, 12);
    fighter_def(&c->f_brawler, &s->brawler, &c->f_rock, 1.1f, 5.9f, 16);
    fighter_def(&c->f_fencer, &s->fencer, &c->f_dust, 1.45f, 6.4f, 10);
    fighter_def(&c->f_paragon, &s->paragon, &c->f_needle, 1.3f, 6.2f, 12);
    c->f_brawler.max_hp = 115;
    c->f_fencer.max_hp = 90;
    if (ge_fighter_def_finish(&c->f_hero, &s->hero) || ge_fighter_def_finish(&c->f_brawler, &s->brawler) ||
        ge_fighter_def_finish(&c->f_fencer, &s->fencer) || ge_fighter_def_finish(&c->f_paragon, &s->paragon)) {
        fprintf(stderr, "fighter move list: unknown clip or bad input\n");
        return 1;
    }
    return 0;
}

int chars_init(Chars *c, Sprites *s) {
    int rc = chars_parse(s);
    return rc ? rc : chars_build(c, s);
}
