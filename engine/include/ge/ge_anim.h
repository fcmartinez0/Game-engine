#ifndef GE_ANIM_H
#define GE_ANIM_H

#include "ge_gfx.h"

/* Sprite sheets + frame-based animation clips.
 *
 * A clip is a list of frames; each frame names a cell in the sheet, how long
 * it lasts (in 60 Hz ticks), and optionally a hitbox. Because the hitbox lives
 * on the *frame*, attacks are authored like fighting-game frame data:
 * startup frames have no hitbox, active frames do, recovery frames don't.
 *
 * Clips are normally written in a small text format (see ge_sprite_parse). */

#define GE_ANIM_MAX_FRAMES 16
#define GE_SPRITE_MAX_CLIPS 64
#define GE_NAME_LEN 24

typedef struct {
    int cell;       /* index into the sheet, row-major */
    int ticks;      /* duration */
    bool has_hit;
    GeRect hit;     /* relative to the sprite origin, facing right */
} GeAnimFrame;

/* how an attack must be blocked in the fighting-game mode */
enum { GE_GUARD_MID = 0, GE_GUARD_LOW = 1, GE_GUARD_HIGH = 2, GE_GUARD_NONE = 3 };

typedef struct {
    char name[GE_NAME_LEN];
    GeAnimFrame frames[GE_ANIM_MAX_FRAMES];
    int nframes;
    bool loop;
    /* attack data (is_attack when dmg= was given) */
    bool is_attack;
    int damage;
    float kb_x, kb_y;   /* knockback applied to the victim (x is away from attacker) */
    float lunge;        /* forward speed given to the attacker when the clip starts */
    float rise;         /* upward speed given to the attacker when the clip starts (rising slashes) */
    int hitstop;        /* freeze frames on both fighters when it connects */
    int stun;           /* victim hit-stun ticks */
    int cancel_from;    /* frame index from which a buffered attack may chain; -1 = never */
    char next_name[GE_NAME_LEN];
    int next;           /* resolved index of next_name, or -1 */
    float pogo;         /* attacker bounces up at this speed when it hits (down-slash) */
    float recoil;       /* attacker is pushed back at this speed when it hits */
    int shoot;          /* frame index that fires the actor's projectile, -1 = none */
    int inv;            /* attacker is invulnerable for this many ticks from the start */
    /* fighting-game mode */
    int guard;          /* GE_GUARD_*: mid blocks either way, low must crouch, high must stand */
    int blockstun, chip;
    bool knockdown;     /* the victim is floored even from a grounded hit */
} GeClip;

typedef struct {
    GeImage sheet;
    int cell_w, cell_h;
    int origin_x, origin_y; /* the pixel inside a cell that sits on the entity's feet */
    int cols;
    GeClip clips[GE_SPRITE_MAX_CLIPS];
    int nclips;
} GeSprite;

/* Parse clip definitions. Set `s->sheet` first. Returns 0, or -N for an error on line N.
 *
 *   cell 32 32                  # cell size in the sheet
 *   origin 16 31                # feet pixel inside a cell
 *   clip run loop               # loop | once
 *   frame 2 5                   # frame <cell> <ticks>
 *   clip slash once dmg=8 kb=2.2,-1.5 stop=4 stun=14 cancel=2 next=slash2 lunge=1.6
 *   frame 8 4                   # startup
 *   frame 9 3 hit=6,-27,28,22   # active: hitbox x,y,w,h relative to origin
 *   frame 10 6                  # recovery
 *
 * More attack keys: pogo=F recoil=F rise=F shoot=N inv=N  (overworld)
 *                   guard=mid|low|high|none bstun=N chip=N kd=1  (fighting game)
 */
int ge_sprite_parse(GeSprite *s, const char *text);
int ge_sprite_find_clip(const GeSprite *s, const char *name); /* -1 if absent */
/* Load a sheet (.pam) and its clip text (.anim) from disk, e.g. files exported by
 * tools/asset-prep. Returns 0, -1 (image), -2 (.anim unreadable) or -3 (.anim syntax error). */
int ge_sprite_load(GeSprite *s, const char *pam_path, const char *anim_path);
/* Write the clips back out in the same text format (ge_sprite_parse reads it back unchanged). 0 on success. */
int ge_sprite_save_anim(const GeSprite *s, const char *anim_path);
void ge_sprite_free(GeSprite *s);

typedef struct {
    const GeSprite *spr;
    int clip, frame, timer;
    bool done; /* a non-looping clip has played its last frame */
} GeAnimator;

void ge_anim_play(GeAnimator *a, int clip, bool restart);
void ge_anim_update(GeAnimator *a);
const GeAnimFrame *ge_anim_frame(const GeAnimator *a);
void ge_anim_draw(const GeAnimator *a, GeImage *dst, int x, int y, bool flip, int flags, uint32_t tint);
void ge_anim_draw_scaled(const GeAnimator *a, GeImage *dst, int x, int y, bool flip, int flags, uint32_t tint, int scale);

#endif
