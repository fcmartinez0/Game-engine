#ifndef GE_FIGHT_H
#define GE_FIGHT_H

#include "ge_world.h"

/* ---------------------------------------------------------------------------
 * Fighting-game mode: a 1v1 versus battle in the style of Street Fighter.
 *
 * It reuses a GeWorld as the arena (physics, particles, shots, parallax,
 * camera) but runs its own rules on top: walking/crouching/jumping, three
 * attack strengths, command normals, motion specials, supers, throws, blocking
 * high/low/mid with chip damage, cancels, juggles, knockdowns, rounds and a
 * clock. Fighters are drawn from the same sprite/clip format as everything
 * else, so frame data (startup/active/recovery, hitboxes) is authored the
 * same way.
 *
 * Moves are written in numpad notation, relative to the way the fighter faces:
 *
 *      7 8 9      5 = neutral, 6 = toward the opponent, 4 = away (block),
 *      4 5 6      2 = down. 236 = quarter circle forward, 623 = "dragon punch",
 *      1 2 3      214 = quarter circle back, 66 / 44 = dash / backdash.
 *
 *   "5L"   standing light        "2M"  crouching medium     "j.H" jumping heavy
 *   "6M"   forward + medium (command normal, list it before 5M)
 *   "236L" special               "623H" special              "S"   super button
 *   "LM"   two buttons together = throw (needs to be close)
 *   "HK"   any chord works the same way
 *   "6246L" a longer motion works too (forward, down, back, then light + forward: the demo's super; four
 *          directions get 30 ticks to complete instead of 18)
 *
 * Moves are tried in list order, so put supers and specials first, command
 * normals before plain ones.
 *
 * Blocking has two forms. HOLDING BACK is the classic one: it lets you walk, but lows must be blocked
 * crouching and overheads / jump-ins standing. The BLOCK button (GE_FB_BLOCK, a shoulder button) is a
 * guard: you stand still, and it stops every height. Only throws beat it, so a fighter who guards too
 * much gets grabbed (which can be broken by pressing L+M as it starts).
 * ------------------------------------------------------------------------- */

/* Attack buttons: light, medium, heavy, special (S), and K, a fourth normal (the long sword thrusts).
 * Two more bits carry the shoulder buttons: BLOCK (held) guards without holding back, DASH (pressed) dashes
 * the way you are holding, or forward from neutral. They go in the same fields as the attack buttons. */
enum { GE_FB_L = 1, GE_FB_M = 2, GE_FB_H = 4, GE_FB_S = 8, GE_FB_K = 16, GE_FB_BLOCK = 32, GE_FB_DASH = 64 };

typedef struct GePlayerModel GePlayerModel;   /* see ge_model.h */

#define GE_FIGHT_MAX_MOVES 32
#define GE_FIGHT_HIST 32
#define GE_FIGHT_METER 100
#define GE_FIGHT_JUGGLE_MAX 3

/* One fighter's controls for one tick, in absolute screen directions. */
typedef struct {
    int x, y;                /* -1/0/+1; y = -1 is up (jump), +1 is down (crouch) */
    uint8_t pressed, held;   /* GE_FB_* */
    int force_move;          /* CPU only: start this move if it's allowed right now; -1 = none */
} GeFightInput;

/* Roles, so the CPU knows what a move is for. */
enum { GE_AI_NONE, GE_AI_POKE, GE_AI_LOW, GE_AI_OVERHEAD, GE_AI_ANTIAIR, GE_AI_ZONE, GE_AI_RUSH,
       GE_AI_COMBO, GE_AI_SUPER, GE_AI_AIR, GE_AI_THROW };

typedef struct {
    const char *input;       /* numpad notation, see above */
    const char *clip;        /* clip name in the fighter's sprite (damage, hitboxes, guard... live there) */
    int level;               /* cancel strength: on contact a move can cancel into a higher level.
                                0 light, 1 medium, 2 heavy, 3 special, 4 super */
    int meter_cost;          /* out of GE_FIGHT_METER */
    const GeShotDef *shot;   /* fired on the clip's shoot= frame (one on screen at a time) */
    int ai;                  /* GE_AI_* */
    /* filled in by ge_fighter_def_finish */
    int clip_idx;
    int motion[8], nmotion;
    int stance;              /* see GE_STANCE_* in ge_fight.c */
    int stance_dir;
    uint8_t buttons;
    bool is_throw;
} GeMove;

typedef struct {
    GeActorDef body;         /* physics + body box: gravity, max_fall, jump_vel, run_speed (= walk forward),
                                body_w, body_h. Clip fields of the body are unused. */
    float walk_back, jump_vx;
    float dash_speed; int dash_ticks;
    float crouch_h;          /* hurtbox height while crouching */
    int max_hp;
    float throw_range; int throw_damage;
    int clip_idle, clip_walk_f, clip_walk_b, clip_crouch, clip_jump, clip_fall;
    int clip_hurt, clip_hurt_air, clip_block, clip_block_low, clip_down, clip_ko, clip_win;
    int clip_throw, clip_thrown, clip_dash, clip_backdash;
    /* Boss rules: an `undying` fighter's health stops at 1 instead of 0, and the moment that happens (or the
     * fight clock `finisher_tick` runs out, or its opponent's health would reach 0) it performs its finisher:
     * a scripted, unblockable KO of the other fighter, so that is always how the fight ends. clip_finisher is the pose it plays while doing it (-1 = the idle clip). */
    bool undying;
    int clip_finisher;
    GeMove moves[GE_FIGHT_MAX_MOVES];
    int nmoves;
} GeFighterDef;

/* Resolve clip names and parse the move notation. 0 on success, otherwise
 * -(1 + index) of the first bad move (unknown clip or unreadable input). */
int ge_fighter_def_finish(GeFighterDef *d, const GeSprite *spr);

typedef enum {
    GE_FS_STAND, GE_FS_CROUCH, GE_FS_AIR, GE_FS_MOVE, GE_FS_HITSTUN, GE_FS_BLOCKSTUN,
    GE_FS_DOWN, GE_FS_THROWN, GE_FS_DASH, GE_FS_KO, GE_FS_WIN
} GeFighterState;

typedef struct {
    int ent;
    const GeFighterDef *def;
    GeFighterState state;
    int timer;
    int move;                /* current move while state == GE_FS_MOVE */
    bool contact, hit_landed;/* the current move touched the opponent / actually hit */
    bool shot_fired, air_hit, air_attacked, crouch_block;
    int juggle;              /* airborne hits taken in the current combo */
    int combo;               /* hits taken in the current combo */
    int meter;
    int wins;
    int8_t hist_dir[GE_FIGHT_HIST];   /* numpad direction per tick, newest at hist_head */
    uint8_t hist_btn[GE_FIGHT_HIST];  /* buttons pressed per tick */
    int hist_head;
    GeFightInput in;
    float shown_hp;          /* for the draining health-bar effect */
    int ai_timer, ai_cd, ai_hold_x, ai_hold_y, ai_block_for; /* CPU state */
    uint8_t buf_btn; int buf_timer;   /* attack buttons pressed while busy, tried again for a few ticks (input buffer) */
    bool guarding;                    /* holding a guard right now (drawn as the guard pose) */
    int pop_timer; const char *pop;   /* a short word over the fighter: BLOCK, LOW!, ... */
    uint32_t pop_color;
} GeFighter;

typedef enum { GE_FP_INTRO, GE_FP_FIGHT, GE_FP_KO, GE_FP_TIME, GE_FP_ROUND_END, GE_FP_MATCH_END, GE_FP_FINISHER } GeFightPhase;

/* ---- the CPU fighter's temperament and memory (see ge_cpu.c) ----
 * The level (-1 beginner .. 3 sharp) says how well a CPU fighter plays; the style says how it likes to play.
 * Analogy: the level is how good the cook is, the style is the cuisine. */
typedef struct {
    float aggression;        /* 0..1: closes the distance, dashes in, keeps pressing after a block */
    float spacing;           /* keeps a range: pokes at the edge of its reach, fires projectiles, steps back */
    float grab;              /* throws up close */
    float patience;          /* blocks and waits for a mistake to punish instead of attacking into you */
    float mixup;             /* low / overhead variety up close */
    float adapt;             /* how fast it changes its play to what you keep doing (jumping in, fireballs, turtling) */
} GeCpuStyle;

/* What a CPU fighter remembers within one fight. Nothing is kept between fights (the Paragon's memory is its own). */
typedef struct {
    float jumps, shots, blocks, throws, attacks;   /* the opponent's habits so far, fading over about ten seconds */
    int last_move, repeat;                         /* the opponent's last move, and how many times running */
    int prev_state, prev_move;
    bool was_open;                                 /* the opponent's attack was over but it couldn't act yet, last tick */
    int react;                                     /* ticks until it may act on that opening (reaction time) */
    bool punish;                                   /* whether it will act on this opening at all */
    bool tech_decided, tech;                       /* being thrown: has it decided, and will it break the throw */
    int tech_at;                                   /* ...and at which tick of the throw */
    int dash_cd;                                   /* ticks before it dashes again */
    int bait;                                      /* ticks left stepping back out of reach, so a move misses */
} GeCpuMind;

typedef struct {
    GeWorld world;           /* the arena; layers and tileset are borrowed from the caller */
    GeFighter f[2];
    const GeSprite *spr[2];
    const char *names[2];
    GeFightPhase phase;
    int phase_timer;
    int round, rounds_to_win;
    int clock;               /* ticks left in the round */
    int round_winner;        /* -1 = draw */
    int winner;              /* match result once over: 0, 1 or -1 (draw) */
    bool over;
    int super_freeze, super_side;
    int combo_show[2], combo_count[2];
    int start_hp[2];         /* hp for round 1 (e.g. a first-strike bonus); later rounds start full */
    int arena_w;
    /* observer: a player model fed from every tick of the fight (see ge_model.h) */
    GePlayerModel *model;
    int model_side;
    /* boss rules */
    bool infinite_clock;     /* no round timer (the HUD shows --) */
    int fight_ticks;         /* ticks spent in the FIGHT phase this round */
    int finisher_tick;       /* when > 0: an undying fighter's finisher fires at this fight_ticks */
    int finisher_side;       /* who performs it */
    bool finisher_pending;   /* an undying fighter hit its floor: the finisher fires at the next chance */
    bool finisher_seen;      /* it has happened at least once this fight (for the game to react to) */
    /* teaching aids, per side (bit 0 = side 0): name what got through ("LOW: CROUCH TO BLOCK"), show which
     * attacks hit low or high, and prompt the throw break. The demo turns them on for the player. */
    unsigned hints;
    int intro_ticks;         /* length of the "ROUND 1 ... FIGHT!" intro (0 = the default 100) */
    float damage_scale[2];   /* damage dealt BY each side is multiplied by this (1 by default): difficulty settings */
    /* per side, for play logs: attacks blocked, hits taken while trying to block at the wrong height, other hits, throws */
    int stat_blocked[2], stat_wrong_height[2], stat_hit[2], stat_thrown[2];
    /* ...and what the hit fighter was doing when a hit got through: attacking, in the air, already in a combo,
     * standing open (no guard), or holding the guard button while busy */
    int stat_hit_attacking[2], stat_hit_air[2], stat_hit_combo[2], stat_hit_open[2], stat_hit_guard_busy[2];
    GeCpuStyle cpu_style[2]; /* how each side plays when a CPU drives it (ge_fight_init gives a middling default) */
    GeCpuMind cpu[2];        /* ...and what it remembers of this fight */
    bool forgiving_guard;    /* the guard button also blocks in the air, mid-dash and in your own move's recovery */
    unsigned forgiving_tech; /* bit s: side s can also break a throw with a fresh press of the guard button, over a
                              * longer window (22 ticks instead of 16): one button newcomers already reach for */
} GeFight;

/* Set up a match. `backdrop` (may be NULL) lends its parallax layers and tileset. */
void ge_fight_init(GeFight *f, const GeFighterDef *d0, const GeSprite *s0, const GeFighterDef *d1, const GeSprite *s1,
                   const GeWorld *backdrop, uint32_t seed);
void ge_fight_free(GeFight *f);
void ge_fight_update(GeFight *f, const GeFightInput in[2]);
void ge_fight_draw(GeFight *f, GeImage *fb);

/* A CPU opponent: fills `out` for `side`. level -1 (beginner: slow, passive, no supers) .. 3 (sharp), played
 * the way f->cpu_style[side] says. At every level it reacts after a reaction time that shrinks with the level, and
 * punishes (a beginner takes one opening in ten, a level-3 fighter about nine in ten): an attack that cannot reach it
 * is an opening from its first frame (it answers while the attack is still winding up); so is a move that was blocked
 * when it has frames to spare, and any attack once its active frames are over. It also breaks throws in proportion to
 * its level, blocks by the real reach of what is coming, presses on after a blocked hit (from level 1), and (by
 * `adapt`) changes its play to what you keep doing: jumping in, fireballs, turtling, repeating a move. Supers are for
 * level 1 and up. */
void ge_fight_cpu(GeFight *f, int side, int level, GeFightInput *out);

/* exposed for tests / tools */
bool ge_fight_motion_done(const GeFighter *F, const int *motion, int n, int window);
GeRect ge_fighter_hurtbox(const GeFight *f, int side);
/* Frames until the fighter can act again (0 = free now). Counts the rest of the current attack animation,
 * hit-stun, block-stun and knockdown. Used by the AI to find punish windows. */
int  ge_fight_frames_until_free(const GeFight *f, int side);
/* Startup (ticks before the first hit frame) and reach (furthest hitbox edge in front of the fighter) of a move. */
int  ge_fight_move_startup(const GeFight *f, int side, int move);
int  ge_fight_move_reach(const GeFight *f, int side, int move);
int  ge_fight_move_total(const GeFight *f, int side, int move);   /* whole animation, in ticks */
bool ge_fight_can_start(const GeFight *f, int side, int move);

#endif
