/* Animated pattern backgrounds. See ge_pattern.h. */

#include "ge/ge_pattern.h"

#include <math.h>
#include <string.h>

#define N GE_PAT_SIZE
#define TAU 6.28318531f

/* distances on a torus, so every picture tiles without a seam */
static float tdx(int a, int c) { int d = a - c; if (d > N / 2) d -= N; if (d < -N / 2) d += N; return (float)d; }

void ge_pattern_make(GePattern *p, int kind, const uint32_t *pal, int npal) {
    memset(p, 0, sizeof *p);
    if (npal < 1) npal = 1;
    if (npal > GE_PAT_MAX_COLORS) npal = GE_PAT_MAX_COLORS;
    memcpy(p->pal, pal, sizeof(uint32_t) * (size_t)npal);
    p->npal = npal;
    p->cycle_ticks = 6;
    p->wave_amp = 6.0f; p->wave_len = 48.0f; p->wave_speed = 0.05f;
    p->scale = 2;
    p->tile_step = -1;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float dx = tdx(x, N / 2), dy = tdx(y, N / 2);
            int v = 0;
            switch (kind) {
            case GE_PAT_RINGS:    v = (int)(sqrtf(dx * dx + dy * dy) / 3.0f); break;
            case GE_PAT_DIAMONDS: v = (int)((fabsf(dx) + fabsf(dy)) / 3.0f); break;
            case GE_PAT_CHECKER:  v = (((x >> 3) + (y >> 3)) & 1) * (npal / 2) + ((x + y) >> 3) % (npal / 2 > 0 ? npal / 2 : 1); break;
            case GE_PAT_STRIPES:  v = ((x + y) % N) * npal / N; break;
            case GE_PAT_PLASMA: {
                float s = sinf(TAU * (float)x / N) + sinf(TAU * (float)y / N) + sinf(TAU * (float)(x + y) / N) + sinf(TAU * (float)(x - y + N) / N);
                v = (int)((s + 4.0f) * (float)npal / 4.0f);   /* two passes through the palette */
                break;
            }
            case GE_PAT_EYES: {
                /* an almond eye in every tile: iris rings inside, a slow tide of stripes outside */
                float ax = dx / 26.0f, open = 11.0f * (1.0f - ax * ax);
                /* index 0 is the pupil, the first half of the rest the iris, the second half the outside */
                int half = (npal - 1) / 2 > 0 ? (npal - 1) / 2 : 1;
                if (fabsf(dx) < 26.0f && fabsf(dy) < open) {
                    float r = sqrtf(dx * dx + dy * dy);
                    v = r < 4.0f ? 0 : 1 + (int)(r / 3.0f) % half;
                } else {
                    v = 1 + half + ((x + 2 * y) % N) * (npal - 1 - half) / N;
                }
                break;
            }
            default: break;
            }
            p->idx[y * N + x] = (uint8_t)(((v % npal) + npal) % npal);
        }
}

static void build_tile(GePattern *p, int step) {
    int sc = p->scale == 2 ? 2 : 1, tw = N * sc;
    for (int y = 0; y < tw; y++)
        for (int x = 0; x < tw; x++) {
            int i = p->idx[(y / sc) * N + x / sc], c0 = p->cycle_from, c1 = p->npal;
            if (p->cycle_split > c0) { if (i < p->cycle_split) c1 = p->cycle_split; else c0 = p->cycle_split; }
            int nc = c1 - c0;
            p->tile[y * tw + x] = p->pal[i < c0 || nc <= 0 ? i : c0 + (i - c0 + step) % nc];
        }
    p->tile_step = step;
}

void ge_pattern_draw_rect(GePattern *p, GeImage *fb, int x0, int y0, int w, int h, int tick) {
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > fb->w) w = fb->w - x0;
    if (y0 + h > fb->h) h = fb->h - y0;
    if (w <= 0 || h <= 0 || p->npal == 0) return;
    int step = p->cycle_ticks > 0 ? (tick / p->cycle_ticks) % p->npal : 0;
    if (step != p->tile_step) build_tile(p, step);
    int tw = N * (p->scale == 2 ? 2 : 1), mask = tw - 1;   /* tw is a power of two */
    float t = (float)tick;
    int sy = (int)floorf(p->scroll_y * t), sx = (int)floorf(p->scroll_x * t);
    for (int y = y0; y < y0 + h; y++) {
        float fy = (float)y;
        int vy = p->vwave_amp != 0.0f ? (int)lroundf(p->vwave_amp * sinf(TAU * fy / p->vwave_len + t * p->vwave_speed)) : 0;
        float ripple = p->wave_amp != 0.0f ? p->wave_amp * sinf(TAU * fy / p->wave_len + t * p->wave_speed) : 0.0f;
        if (p->interleave && (y & 1)) ripple = -ripple;
        const uint32_t *row = p->tile + ((y + sy + vy) & mask) * tw;
        uint32_t *dst = fb->px + y * fb->w + x0;
        int tx = (x0 + sx + (int)lroundf(ripple)) & mask, left = w;
        while (left > 0) {
            int n = tw - tx < left ? tw - tx : left;
            memcpy(dst, row + tx, sizeof(uint32_t) * (size_t)n);
            dst += n; left -= n; tx = 0;
        }
    }
}

void ge_pattern_draw(GePattern *p, GeImage *fb, int tick) { ge_pattern_draw_rect(p, fb, 0, 0, fb->w, fb->h, tick); }
