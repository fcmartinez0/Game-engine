#ifndef GE_WORLD_H
#define GE_WORLD_H

#include "ge_anim.h"
#include "ge_gfx.h"

/* ---------------------------------------------------------------------------
 * 2.5D model
 *
 * Physics is a classic 2D tile platformer. Depth comes from two things:
 *   - parallax layers: any number of background/foreground images scrolling at
 *     different speeds around the play plane;
 *   - depth lanes: every entity sits in lane -1 (back), 0 (middle) or +1
 *     (front). Lanes are drawn with a perspective offset/shade, and combat only
 *     connects between entities in the same lane -- so you can sidestep an
 *     attack by hopping lanes, beat-'em-up style.
 * ------------------------------------------------------------------------- */

#define GE_TILE 16
#define GE_MAX_ENTS 48
#define GE_MAX_PARTS 384
#define GE_MAX_LAYERS 10
#define GE_LANE_BACK (-1)
#define GE_LANE_FRONT 1

/* ---- tilemap ---- */
typedef enum { GE_TILE_EMPTY = 0, GE_TILE_SOLID = 1, GE_TILE_ONEWAY = 2 } GeTileType;

typedef struct {
    int w, h;
    uint8_t *type; /* GeTileType, for collision */
    uint8_t *vis;  /* cell in the tileset image, for drawing */
} GeTilemap;

int  ge_tilemap_create(GeTilemap *m, int w, int h);
void ge_tilemap_free(GeTilemap *m);
/* '#' solid, '=' one-way platform, anything else empty. Rows must be equal width. */
int  ge_tilemap_from_ascii(GeTilemap *m, const char *const *rows, int nrows);
/* Outside the left/right edges counts as solid (walls); above/below as empty. */
static inline int ge_tilemap_get(const GeTilemap *m, int tx, int ty) {
    if (tx < 0 || tx >= m->w) return GE_TILE_SOLID;
    if (ty < 0 || ty >= m->h) return GE_TILE_EMPTY;
    return m->type[ty * m->w + tx];
}

/* ---- actors ---- */
typedef enum { GE_ST_IDLE, GE_ST_RUN, GE_ST_AIR, GE_ST_ATTACK, GE_ST_HURT, GE_ST_DASH, GE_ST_DEAD } GeActorState;

/* Tuning + clip bindings shared by every entity of a kind. Speeds are
 * pixels/tick, accelerations pixels/tick^2 (60 ticks per second). */
typedef struct {
    float run_speed, accel, friction, air_accel;
    float gravity, max_fall;
    float jump_vel;       /* initial upward speed */
    float jump_cut_vel;   /* upward speed is clamped to this when jump is released early */
    int coyote_ticks;     /* grace period to jump after walking off a ledge */
    int jump_buffer_ticks;/* grace period for pressing jump just before landing */
    float dash_speed; int dash_ticks, dash_cooldown;
    int invuln_ticks;     /* after being hit */
    int dead_ticks;       /* corpse lifetime */
    float body_w, body_h; /* hurtbox / collision box, centred on x, bottom at y */
    int clip_idle, clip_run, clip_jump, clip_fall, clip_hurt, clip_dead, clip_dash;
    int clip_attack;      /* first clip of the combo chain, -1 = cannot attack */
} GeActorDef;

/* What an actor wants to do this tick. Players fill it from input, enemies
 * from AI -- the actor code can't tell the difference. */
typedef struct {
    float move_x;                 /* -1..1 */
    bool jump_pressed, jump_held, attack_pressed, dash_pressed, drop_held;
    int lane_shift;               /* -1 = toward back, +1 = toward front, 0 = stay */
} GeIntent;

typedef struct {
    bool active;
    int team;                     /* attacks only hurt other teams */
    int kind;                     /* free for the game */
    GeVec2 pos, vel;              /* pos = feet centre */
    int facing;                   /* +1 right, -1 left */
    int lane;
    bool on_ground;
    int hp, max_hp;
    GeActorState state;
    int state_timer;
    GeAnimator anim;
    const GeActorDef *def;
    int hitstop, invuln, flash, coyote, jump_buf, dash_cd, drop_timer, dead_timer, lane_cd;
    bool buffered_attack;
    int combo;                    /* 1-based step in the current chain */
    uint64_t hit_mask;            /* who the current swing already hit */
    int ai_state, ai_timer;       /* free for the game */
    float ai_x0;
} GeEntity;

/* ---- parallax ---- */
typedef struct {
    GeImage img;
    float factor_x, factor_y;     /* 0 = fixed to screen, 1 = moves with the world, >1 = foreground */
    float auto_x;                 /* extra drift in px/tick (clouds, fog) */
    int y;                        /* screen y of the layer's top edge at camera y = 0 */
    bool foreground;              /* drawn over the entities */
    bool additive;                /* light shafts, glows */
    bool opaque;                  /* no transparent pixels: drawn with a fast row copy */
    bool optional;                /* skipped when the world is in low_detail mode */
    uint8_t alpha;
} GeParallaxLayer;

/* ---- particles ---- */
enum { GE_P_ADD = 1, GE_P_WANDER = 2, GE_P_TWINKLE = 4 };
typedef struct {
    float x, y, vx, vy, g;
    int life, max_life;
    uint32_t color;
    uint8_t size, flags;
} GeParticle;

/* ---- camera ---- */
typedef struct {
    float x, y;
    int vw, vh;
    float shake, shake_x, shake_y;
    float lookahead, smooth;
} GeCamera;

typedef enum { GE_EV_HIT, GE_EV_KILL, GE_EV_FELL } GeEventType;

typedef struct GeWorld {
    GeTilemap map;
    GeImage tileset;              /* GE_TILE x GE_TILE cells in one row */
    GeParallaxLayer layers[GE_MAX_LAYERS];
    int nlayers;
    GeEntity ents[GE_MAX_ENTS];
    GeIntent intents[GE_MAX_ENTS];
    GeParticle parts[GE_MAX_PARTS];
    GeCamera cam;
    int follow;                   /* entity the camera tracks, -1 = none */
    uint32_t tick;
    GeRng rng;
    int vignette;
    bool low_detail;              /* drop optional layers + vignette (original 3DS) */
    /* a = attacker (or the entity that fell), b = victim */
    void (*on_event)(void *user, GeEventType ev, int a, int b);
    void *user;
} GeWorld;

void ge_world_init(GeWorld *w, int view_w, int view_h, uint32_t seed);
void ge_world_free(GeWorld *w);
int  ge_world_add_layer(GeWorld *w, GeParallaxLayer layer);
int  ge_world_spawn(GeWorld *w, const GeActorDef *def, const GeSprite *spr, int team, float x, float y, int hp);
void ge_world_update(GeWorld *w);
void ge_world_draw(GeWorld *w, GeImage *fb);
void ge_world_emit(GeWorld *w, float x, float y, float vx, float vy, float g, int life, uint32_t color, int size, int flags);
void ge_world_shake(GeWorld *w, float amount);
int  ge_world_particle_count(const GeWorld *w);
/* y of the first floor at or below (x, y), or -1 if there is none */
float ge_world_ground_y(const GeWorld *w, float x, float y);

/* used internally by ge_world_update */
void ge_actor_update(GeWorld *w, int idx);
void ge_combat_resolve(GeWorld *w);
GeRect ge_entity_hurtbox(const GeEntity *e);
bool ge_entity_hitbox(const GeEntity *e, GeRect *out);

#endif
