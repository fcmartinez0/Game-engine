#ifndef GE_MATH_H
#define GE_MATH_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct { float x, y; } GeVec2;
typedef struct { float x, y, w, h; } GeRect;
typedef struct { int x, y, w, h; } GeIRect;

static inline float ge_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int   ge_clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float ge_lerpf(float a, float b, float t) { return a + (b - a) * t; }

static inline bool ge_rect_overlap(GeRect a, GeRect b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

/* Deterministic xorshift32 RNG. Same seed => same game, which keeps replays
 * and the headless tests reproducible. */
typedef struct { uint32_t s; } GeRng;

static inline void ge_rng_seed(GeRng *r, uint32_t seed) { r->s = seed ? seed : 0x9E3779B9u; }
static inline uint32_t ge_rng_next(GeRng *r) {
    uint32_t x = r->s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return r->s = x;
}
static inline float ge_rng_float(GeRng *r) { return (float)(ge_rng_next(r) >> 8) * (1.0f / 16777216.0f); }
static inline float ge_rng_range(GeRng *r, float lo, float hi) { return lo + (hi - lo) * ge_rng_float(r); }

#endif
