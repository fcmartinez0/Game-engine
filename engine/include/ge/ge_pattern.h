#ifndef GE_PATTERN_H
#define GE_PATTERN_H

#include "ge_gfx.h"

/* ---------------------------------------------------------------------------
 * Animated pattern backgrounds, in the spirit of EarthBound's battle screens.
 *
 * A small tiling picture made of palette indices is shown through three cheap
 * tricks, the same ones the SNES used:
 *   - palette cycling: every few ticks each index moves to the next colour,
 *     so rings seem to flow outward and stripes march;
 *   - a horizontal ripple: every screen row is shifted sideways by a sine of
 *     its height (with `interleave`, odd rows go the other way, the classic
 *     EarthBound shimmer);
 *   - a vertical ripple and a slow scroll.
 * Each row is then a plain copy out of the cached tile, so a full screen costs
 * about as much as one opaque parallax layer (no per-pixel maths).
 *
 * Analogy: a striped sock pulled over a wobbling hand. The stripes never
 * change; only the stretching does.
 * ------------------------------------------------------------------------- */

#define GE_PAT_SIZE 64
#define GE_PAT_MAX_COLORS 16

enum { GE_PAT_RINGS, GE_PAT_DIAMONDS, GE_PAT_CHECKER, GE_PAT_STRIPES, GE_PAT_PLASMA, GE_PAT_EYES, GE_PAT_COUNT };

typedef struct GePattern {
    uint8_t idx[GE_PAT_SIZE * GE_PAT_SIZE];
    uint32_t pal[GE_PAT_MAX_COLORS];
    int npal;
    int cycle_ticks;                          /* palette steps once every this many ticks (0 = still) */
    int cycle_from;                           /* colours before this index never cycle (an eye's pupil, say) */
    int cycle_split;                          /* if > cycle_from: [from, split) and [split, npal) cycle separately */
    float wave_amp, wave_len, wave_speed;     /* horizontal ripple: pixels, rows per wave, radians per tick */
    bool interleave;                          /* odd rows ripple the opposite way */
    float vwave_amp, vwave_len, vwave_speed;  /* vertical ripple */
    float scroll_x, scroll_y;                 /* pixels per tick */
    int scale;                                /* 1 or 2: size of one tile pixel on screen */
    /* cache: the tile in colour for the current palette step */
    uint32_t tile[GE_PAT_SIZE * 2 * GE_PAT_SIZE * 2];
    int tile_step;
} GePattern;

/* Fill `p` with one of the built-in GE_PAT_* pictures and a palette (colours in order; cycling walks this
 * list). Motion defaults to a gentle ripple; change the fields afterwards for a different feel. */
void ge_pattern_make(GePattern *p, int kind, const uint32_t *pal, int npal);
/* Draw over a rectangle of `fb` (clipped), as if the pattern filled the whole screen. */
void ge_pattern_draw_rect(GePattern *p, GeImage *fb, int x0, int y0, int w, int h, int tick);
void ge_pattern_draw(GePattern *p, GeImage *fb, int tick);

#endif
