#ifndef GE_N3DS_BLIT_H
#define GE_N3DS_BLIT_H

/* Pure pixel shuffling for the 3DS top screen, kept free of libctru so the
 * desktop tests can check it against the obvious per-pixel version.
 *
 * The top screen's framebuffer is the 400x240 picture rotated: columns left to
 * right, each stored bottom-up, 3 bytes (B, G, R) per pixel. Pixel (x, y) lives
 * at ((x * h) + (h - 1 - y)) * 3.
 *
 * Both costs that matter on the original 3DS are avoided: the source is read in
 * 8-pixel-tall tiles (its 1600-byte row stride would miss the cache on nearly
 * every pixel), and the output is written as aligned 32-bit words, four pixels
 * (12 bytes = three words) at a time instead of twelve single-byte stores. */

#include <stddef.h>
#include <stdint.h>

static inline void ge_3ds_rotate_bgr8(const uint32_t *src, int w, int h, uint8_t *dst) {
    for (int bx = 0; bx < w; bx += 8) {
        int ex = bx + 8 < w ? bx + 8 : w;
        for (int by = 0; by < h; by += 8)
            for (int x = bx; x < ex; x++) {
                uint8_t *col = dst + (size_t)x * (size_t)h * 3;
                const uint32_t *s = src + (size_t)by * (size_t)w + x;
                int y = by, ey = by + 8 < h ? by + 8 : h;
                /* four pixels y..y+3 occupy bytes [(h-4-y)*3, (h-y)*3) of the column, lowest address = pixel y+3 */
                for (; y + 4 <= ey && ((((size_t)(h - 4 - y)) * 3) & 3) == 0 && (((uintptr_t)col) & 3) == 0; y += 4, s += 4 * (size_t)w) {
                    uint32_t p0 = s[0], p1 = s[w], p2 = s[2 * (size_t)w], p3 = s[3 * (size_t)w];
                    uint32_t *o = (uint32_t *)(col + (size_t)(h - 4 - y) * 3);
                    o[0] = (p3 & 0xFFFFFFu) | (p2 << 24);                         /* B3 G3 R3 B2 */
                    o[1] = ((p2 >> 8) & 0xFFFFu) | ((p1 & 0xFFFFu) << 16);        /* G2 R2 B1 G1 */
                    o[2] = ((p1 >> 16) & 0xFFu) | ((p0 & 0xFFFFFFu) << 8);        /* R1 B0 G0 R0 */
                }
                for (; y < ey; y++, s += w) {                                      /* any leftovers */
                    uint8_t *o = col + (size_t)(h - 1 - y) * 3;
                    uint32_t p = *s;
                    o[0] = (uint8_t)p; o[1] = (uint8_t)(p >> 8); o[2] = (uint8_t)(p >> 16);
                }
            }
    }
}

#endif
