#ifndef GE_WORLD_H
#define GE_WORLD_H

#include "ge_anim.h"
#include "ge_gfx.h"
#include "ge_pattern.h"

/* ---------------------------------------------------------------------------
 * The platformer world: tilemap, actors, projectiles, particles, parallax,
 * camera. Combat here is "action" combat -- slashes, pogo, tools -- in the
 * style of Hollow Knight / Silksong. The fighting-game mode (ge_fight.h)
 * reuses a GeWorld as its arena but runs its own combat rules.
 * ------------------------------------------------------------------------- */

#define GE_TILE 16
#define GE_MAX_ENTS 48
#define GE_MAX_PARTS 384
#define GE_MAX_LAYERS 10
#define GE_MAX_SHOTS 32

/* ---- tilemap ---- */
typedef enum { GE_TILE_EMPTY = 0, GE_TILE_SOLID = 1, GE_TILE_ONEWAY = 2, GE_TILE_SPIKE = 3 } GeTileType;

typedef struct {
    int w, h;
    uint8_t *type; /* GeTileType, for collision */
    uint8_t *vis;  /* cell in the tileset image, for drawing */
} GeTilemap;

int  ge_tilemap_create(GeTilemap *m, int w, int h);
void ge_tilemap_free(GeTilemap *m);
/* '#' solid, '=' one-way platform, '^' spikes, anything else empty. Rows must be equal width. */
int  ge_tilemap_from_ascii(GeTilemap *m, const char *const *rows, int nrows);
/* Outside the left/right edges counts as solid (walls), below as empty. Above the top a column goes on as its top
 * cell is, for three rows: a wall that reaches the top of the room can't be climbed over, an open column is open sky;
 * and over those three rows lies a lid, so climbing a wall that goes on up stops there. */
#define GE_SKY_ROWS 3
static inline int ge_tilemap_get(const GeTilemap *m, int tx, int ty) {
    if (tx < 0 || tx >= m->w) return GE_TILE_SOLID;
    if (ty >= m->h || m->h <= 0) return GE_TILE_EMPTY;
    if (ty < -GE_SKY_ROWS) return GE_TILE_SOLID;
    if (ty < 0) return m->type[tx] == GE_TILE_SOLID ? GE_TILE_SOLID : GE_TILE_EMPTY;
    return m->type[ty * m->w + tx];
}

/* ---- projectiles ("shots": thrown tools, enemy spit, shockwaves, fireballs) ---- */
typedef struct {
    const GeSprite *spr;
    int clip;
    float speed, vy, gravity;  /* initial speed along facing, initial vertical speed, gravity */
    float w, h;                /* hitbox, centred on the projectile */
    int damage;
    float kb_x, kb_y;
    int hitstop, stun;
    int blockstun, chip;       /* used by the fighting-game mode */
    int life;                  /* ticks */
    bool pierce;               /* keeps flying after a hit */
    bool solid_stops;          /* destroyed by solid tiles */
    bool breakable;            /* an opposing slash destroys it (and can pogo off it) */
    bool grounded;             /* hugs the floor (shockwaves); dies at ledges */
} GeShotDef;

typedef struct {
    bool active;
    int team, owner;           /* owner = entity index or -1 */
    const GeShotDef *def;
    GeVec2 pos, vel;
    int life, facing;
    GeAnimator anim;
    uint64_t hit_mask;
} GeShot;

/* ---- actors ---- */
typedef enum {
    GE_ST_IDLE, GE_ST_RUN, GE_ST_AIR, GE_ST_WALL, GE_ST_ATTACK, GE_ST_HURT, GE_ST_DASH, GE_ST_CHANNEL, GE_ST_DEAD
} GeActorState;

/* Tuning + clip bindings shared by every entity of a kind. Speeds are
 * pixels/tick, accelerations pixels/tick^2 (60 ticks per second).
 * Any clip_* left at -1 disables that ability. */
typedef struct {
    float run_speed, accel, friction, air_accel;
    float gravity, max_fall;
    float jump_vel;           /* initial upward speed */
    float jump_cut_vel;       /* upward speed is clamped to this when jump is released early */
    int coyote_ticks;         /* grace period to jump after walking off a ledge */
    int jump_buffer_ticks;    /* grace period for pressing jump just before landing */
    int air_jumps;            /* extra jumps in the air (double jump = 1) */
    float air_jump_vel;
    float wall_slide_speed;   /* max fall speed while sliding down a wall; 0 = no wall moves */
    float wall_jump_vx, wall_jump_vy;
    int wall_jump_lock;       /* ticks of no steering after a kick off the wall, so it pushes off */
    /* How the stick picks the wall jump (the direction held when jump is pressed, relative to the wall): AWAY from it is the
     * kick (wall_jump_vx sideways, locked for wall_jump_lock ticks); NOTHING is a hop (wall_hop_vx away, free to steer);
     * TOWARD it is a climb (straight up along it, free to steer, so it carries on over a ledge or back onto the wall). */
    float wall_hop_vx;
    float wall_chain_decay;   /* a wall jump off the same wall as the last one (no ground, no other wall between) is this
                               * fraction of the last one's height, so one wall can be climbed a little but not forever (0 = no limit) */
    int wall_grace_ticks;     /* a wall jump still works this many ticks after losing the wall (a press a hair late) */
    int wall_boost_ticks;     /* after a wall jump, reversing in the air is quicker for this many ticks (getting back to the wall) */
    float dash_speed; int dash_ticks, dash_cooldown;
    bool air_dash;            /* one dash per airtime */
    bool flying;              /* no gravity; move_y steers vertically */
    bool armored;             /* takes damage but is never knocked back or stunned (bosses) */
    int invuln_ticks;         /* after being hit */
    int dead_ticks;           /* corpse lifetime */
    float body_w, body_h;     /* hurtbox / collision box, centred on x, bottom at y */
    /* contact damage: touching this actor hurts the other team (most enemies) */
    int contact_damage; float contact_kb_x, contact_kb_y;
    /* silk-style meter: filled by landing hits, spent on skills and channels */
    int meter_max, meter_per_hit;
    int skill_cost, channel_cost, channel_ticks;
    /* tool / projectile */
    int ammo_max, ammo_recharge; /* ticks per charge */
    const GeShotDef *shot;    /* fired on the shoot= frame of clip_tool (or any attack clip with shoot=) */

    int clip_idle, clip_run, clip_jump, clip_fall, clip_wall, clip_hurt, clip_dead, clip_dash;
    int clip_attack;          /* ground slash, first clip of its combo chain */
    int clip_attack_air;      /* side slash in the air (-1 = use clip_attack) */
    int clip_attack_up, clip_attack_down; /* down = air only; give it pogo= */
    int clip_skill, clip_tool, clip_channel;
} GeActorDef;

/* What an actor wants to do this tick. Players fill it from input, enemies
 * from AI -- the actor code can't tell the difference. */
typedef struct {
    float move_x, move_y;     /* -1..1; move_y < 0 is up (aims slashes, steers flyers) */
    bool jump_pressed, jump_held;
    bool attack_pressed, dash_pressed, tool_pressed, skill_pressed;
    bool channel_held;        /* e.g. heal: hold while standing */
} GeIntent;

typedef struct {
    bool active;
    bool external;            /* driven by another module (fighting game): the world won't run its AI/physics */
    int team;                 /* attacks only hurt other teams */
    int kind;                 /* free for the game */
    GeVec2 pos, vel;          /* pos = feet centre */
    int facing;               /* +1 right, -1 left */
    bool on_ground;
    int hp, max_hp;
    GeActorState state;
    int state_timer;
    GeAnimator anim;
    const GeActorDef *def;
    int hitstop, invuln, flash, coyote, jump_buf, dash_cd, drop_timer, dead_timer, wall_lock;
    int air_jumps_left;
    bool air_dashed;
    int wall_dir;             /* -1/+1 while touching a wall in the air */
    int wall_grace, wall_boost, wall_chain;   /* wall coyote ticks left, the quicker-reversal window, wall jumps in a row off one wall */
    int8_t wall_mem, wall_last;               /* the side the wall was on when last touched / last jumped off (-1 left, +1 right) */
    int meter, ammo, ammo_timer;
    bool shot_fired;
    bool buffered_attack;
    int combo;                /* 1-based step in the current chain */
    uint64_t hit_mask;        /* who the current swing already hit (bit 63: spikes) */
    int ai_state, ai_timer, ai_cd; /* free for the game */
    float ai_x0, ai_y0;
} GeEntity;

/* ---- parallax ---- */
typedef struct {
    GeImage img;
    float factor_x, factor_y; /* 0 = fixed to screen, 1 = moves with the world, >1 = foreground */
    float auto_x;             /* extra drift in px/tick (clouds, fog) */
    int y;                    /* screen y of the layer's top edge at camera y = 0 */
    bool foreground;          /* drawn over the entities */
    bool additive;            /* light shafts, glows */
    bool opaque;              /* no transparent pixels: drawn with a fast row copy */
    bool optional;            /* skipped when the world is in low_detail mode */
    bool borrowed;            /* image owned by someone else: not freed with this world */
    uint8_t alpha;            /* baked into the image when the layer is added, then reads as 255 */
    GeSpans spans;            /* built by ge_world_add_layer */
    bool translucent;         /* has partly see-through pixels (set by ge_world_add_layer) */
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
    bool locked;              /* boss rooms: keep the view inside [lock_x0, lock_x1] */
    float lock_x0, lock_x1;
} GeCamera;

/* Events. a = attacker / subject (-1 if none), b = victim / extra. */
typedef enum {
    GE_EV_HIT,      /* a hit b */
    GE_EV_KILL,     /* a killed b */
    GE_EV_FELL,     /* a fell out of the world (removed) */
    GE_EV_HAZARD,   /* a touched spikes */
    GE_EV_CHANNEL,  /* a finished channelling (e.g. heal) */
    GE_EV_POGO,     /* a bounced off something with a down-slash */
    GE_EV_SHOOT,    /* a fired shot b */
    GE_EV_SHOT_TILE /* a shot fired by entity a (-1: nobody) stopped against the solid cell b (row * map width + column;
                     * -1 past the map's edge) */
} GeEventType;

/* One hit, as applied to a victim. */
typedef struct {
    int damage;
    float kb_x, kb_y;         /* kb_x is away from the attacker */
    int hitstop, stun;
    int dir;                  /* +1 = victim knocked right */
    float x, y;               /* where sparks fly */
    float pogo, recoil;       /* attacker bounce / push-back */
} GeHit;

typedef struct GeWorld {
    GeTilemap map;
    GeImage tileset;          /* GE_TILE x GE_TILE cells in one row */
    bool tileset_borrowed;
    uint8_t tile_opaque[256];     /* per tileset cell: fully opaque, so it can be copied as plain memory */
    bool tile_opaque_ready;
    GeParallaxLayer layers[GE_MAX_LAYERS];
    int nlayers;
    GeEntity ents[GE_MAX_ENTS];
    GeIntent intents[GE_MAX_ENTS];
    GeShot shots[GE_MAX_SHOTS];
    GeParticle parts[GE_MAX_PARTS];
    GeCamera cam;
    int follow;               /* entity the camera tracks, -1 = none */
    uint32_t tick;
    GeRng rng;
    int vignette;
    bool low_detail;          /* drop optional layers + vignette (original 3DS) */
    bool foreground_off;      /* skip the foreground layers (e.g. indoors) */
    bool background_off;      /* skip the clear and the background layers: the game's own art covers the view */
    int background_clip_x;    /* if > 0: the background is only needed left of this screen column (the rest is covered) */
    bool reference_background; /* tests: paint the background layers the plain back-to-front way */
    GePattern *backdrop_pattern;  /* when set, drawn instead of the background layers (fight screens) */
    bool custom_combat;       /* another module resolves hits (ge_fight) */
    int zoom;                 /* draw tiles/entities/effects magnified (parallax stays native); 0 or 1 = none.
                                 cam.vw/vh are then in world units (e.g. 200x120 for a 2x view of 400x240) */
    void (*on_event)(void *user, GeEventType ev, int a, int b);
    /* optional: draw scenery between the tiles and the entities (props, signs) */
    void (*draw_props)(void *user, GeImage *fb, int camx, int camy);
    void *user;
} GeWorld;

void ge_world_init(GeWorld *w, int view_w, int view_h, uint32_t seed);
void ge_world_free(GeWorld *w);
int  ge_world_add_layer(GeWorld *w, GeParallaxLayer layer);
int  ge_world_spawn(GeWorld *w, const GeActorDef *def, const GeSprite *spr, int team, float x, float y, int hp);
void ge_world_update(GeWorld *w);
void ge_world_draw(GeWorld *w, GeImage *fb);

/* ---- parallax layers from a file (.layers), e.g. exported by tools/asset-prep ----
 *
 *   # back to front; image paths are relative to this file
 *   layer sky.pam factor=0
 *   layer hills.pam factor=0.22 y=100
 *   layer clouds.pam factor=0.12 drift=0.05 y=30 additive optional
 *   layer grass.pam factor=1.45 y=206 front
 *
 * keys:  factor= (horizontal scroll, default 1)  factor_y= (default 0)
 *        drift= (px/tick, default 0)  y= (default 0)  alpha= (1-255, default 255)
 * flags: front (drawn over the entities)  additive  optional (skipped in low_detail)
 * Fully opaque images are detected and get the fast row-copy path automatically.
 * On any error nothing is added and *err_line (if given) names the offending line. */
enum { GE_LAYERS_OK = 0, GE_LAYERS_EOPEN = -1, GE_LAYERS_ESYNTAX = -2, GE_LAYERS_EIMAGE = -3, GE_LAYERS_EFULL = -4 };
int ge_world_load_layers(GeWorld *w, const char *path, int *err_line);
int ge_world_parse_layers(GeWorld *w, const char *text, const char *base_dir, int *err_line);

/* ---- effects ---- */
void ge_world_emit(GeWorld *w, float x, float y, float vx, float vy, float g, int life, uint32_t color, int size, int flags);
void ge_world_spark(GeWorld *w, float x, float y, int dir, int count, uint32_t color);
void ge_world_shake(GeWorld *w, float amount);
int  ge_world_particle_count(const GeWorld *w);

/* ---- shots ---- */
int  ge_world_shoot(GeWorld *w, int team, int owner, const GeShotDef *def, float x, float y, int facing);
GeRect ge_shot_box(const GeShot *s);

/* ---- queries / building blocks (also used by ge_fight) ---- */
float ge_world_ground_y(const GeWorld *w, float x, float y);   /* first floor at/below (x,y), or -1 */
bool  ge_world_touches_tile(const GeWorld *w, GeRect r, int tile_type);
int   ge_entity_wall_contact(const GeWorld *w, const GeEntity *e); /* -1 wall on the left, +1 right, 0 none */
bool  ge_entity_physics(GeWorld *w, int idx);                  /* move + collide; false if it fell out */
GeRect ge_entity_hurtbox(const GeEntity *e);
bool  ge_entity_hitbox(const GeEntity *e, GeRect *out);         /* active hitbox of the current frame */
void  ge_entity_pogo(GeWorld *w, int idx, float speed);         /* bounce up as off spikes: refreshes the air jump and dash */
void  ge_world_apply_hit(GeWorld *w, int attacker, int victim, const GeHit *h);
void  ge_world_event(GeWorld *w, GeEventType ev, int a, int b);

/* used internally by ge_world_update */
void ge_actor_update(GeWorld *w, int idx);
void ge_shots_update(GeWorld *w);
void ge_combat_resolve(GeWorld *w);

#endif
