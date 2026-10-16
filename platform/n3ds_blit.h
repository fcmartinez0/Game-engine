#ifndef GE_N3DS_BLIT_H
#define GE_N3DS_BLIT_H

/* Pure pixel shuffling for the 3DS top screen, kept free of libctru so the
 * desktop tests can check it against the obvious per-pixel version.
 *
 * The top screen's framebuffer is the 400x240 picture rotated: 240 bytes-
 * triplets per column, columns left to right, each column stored bottom-up,
 * BGR. So pixel (x, y) lives at ((x * h) + (h - 1 - y)) * 3.
 *
 * Walking it column by column reads the source with a 1600-byte stride, which
 * misses the cache on every pixel on a 268 MHz original 3DS (no L2). Going
 * through 8x8 tiles keeps both the reads and the writes in cache. */

#include <stddef.h>
#include <stdint.h>

static inline void ge_3ds_rotate_bgr8(const uint32_t *src, int w, int h, uint8_t *dst) {
    for (int by = 0; by < h; by += 8)
        for (int bx = 0; bx < w; bx += 8) {
            int ex = bx + 8 < w ? bx + 8 : w, ey = by + 8 < h ? by + 8 : h;
            for (int x = bx; x < ex; x++) {
                uint8_t *col = dst + (size_t)x * (size_t)h * 3;
                const uint32_t *s = src + (size_t)by * (size_t)w + x;
                for (int y = by; y < ey; y++, s += w) {
                    uint8_t *o = col + (size_t)(h - 1 - y) * 3;
                    uint32_t p = *s;
                    o[0] = (uint8_t)p; o[1] = (uint8_t)(p >> 8); o[2] = (uint8_t)(p >> 16);
                }
            }
        }
}

#endif
