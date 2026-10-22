#ifndef GE_N3DS_BLIT_H
#define GE_N3DS_BLIT_H

/* Pure pixel shuffling for the 3DS top screen, kept free of libctru so the
 * desktop tests can check it against the obvious per-pixel version.
 *
 * The top screen's framebuffer is the 400x240 picture rotated: columns left to
 * right, each stored bottom-up, 3 bytes (B, G, R) per pixel. Pixel (x, y) lives
 * at ((x * h) + (h - 1 - y)) * 3.
 *
 * On the original 3DS (16 KB of data cache, no L2) this copy costs about as much
 * as drawing a frame, so it is written for the memory system:
 *  - the source is read in 8x8 tiles: a tile's eight rows are eight cache lines
 *    (the picture is 32-byte aligned, see ge_image_create), loaded once and used
 *    for all eight columns, where row-by-row reading would miss on every pixel;
 *  - each column of a tile is eight pixels = 24 bytes of output, stored as six
 *    aligned words instead of twenty-four byte stores;
 *  - the next tile's rows are requested (PLD) while this one is shuffled, so the
 *    wait for main memory overlaps the work. PLD is only a hint: harmless where
 *    it does nothing. */

#include <stddef.h>
#include <stdint.h>

static inline void ge_3ds_rotate_bgr8(const uint32_t *src, int w, int h, uint8_t *dst) {
    if ((h & 7) == 0 && ((uintptr_t)dst & 3) == 0) {
        for (int bx = 0; bx < w; bx += 8) {
            int ex = bx + 8 < w ? bx + 8 : w;
            for (int by = 0; by < h; by += 8) {
                const uint32_t *t = src + (size_t)by * (size_t)w;
                const uint32_t *next = by + 8 < h ? t + 8 * (size_t)w + bx : src + bx + 8;
                if (by + 8 < h || bx + 8 < w)
                    for (int r = 0; r < 8; r++) __builtin_prefetch(next + (size_t)r * (size_t)w);
                for (int x = bx; x < ex; x++) {
                    const uint32_t *s = t + x;
                    uint32_t p0 = s[0], p1 = s[w], p2 = s[2 * (size_t)w], p3 = s[3 * (size_t)w];
                    uint32_t p4 = s[4 * (size_t)w], p5 = s[5 * (size_t)w], p6 = s[6 * (size_t)w], p7 = s[7 * (size_t)w];
                    /* pixels by..by+7 fill bytes [(h-8-by)*3, (h-by)*3) of the column, lowest address = pixel by+7 */
                    uint32_t *o = (uint32_t *)(dst + ((size_t)x * (size_t)h + (size_t)(h - 8 - by)) * 3);
                    o[0] = (p7 & 0xFFFFFFu) | (p6 << 24);                  /* B7 G7 R7 B6 */
                    o[1] = ((p6 >> 8) & 0xFFFFu) | (p5 << 16);             /* G6 R6 B5 G5 */
                    o[2] = ((p5 >> 16) & 0xFFu) | (p4 << 8);               /* R5 B4 G4 R4 */
                    o[3] = (p3 & 0xFFFFFFu) | (p2 << 24);                  /* B3 G3 R3 B2 */
                    o[4] = ((p2 >> 8) & 0xFFFFu) | (p1 << 16);             /* G2 R2 B1 G1 */
                    o[5] = ((p1 >> 16) & 0xFFu) | (p0 << 8);               /* R1 B0 G0 R0 */
                }
            }
        }
        return;
    }
    /* any other size or alignment: the same tiles, one byte at a time */
    for (int bx = 0; bx < w; bx += 8) {
        int ex = bx + 8 < w ? bx + 8 : w;
        for (int by = 0; by < h; by += 8) {
            int ey = by + 8 < h ? by + 8 : h;
            for (int x = bx; x < ex; x++)
                for (int y = by; y < ey; y++) {
                    uint8_t *o = dst + ((size_t)x * (size_t)h + (size_t)(h - 1 - y)) * 3;
                    uint32_t p = src[(size_t)y * (size_t)w + x];
                    o[0] = (uint8_t)p; o[1] = (uint8_t)(p >> 8); o[2] = (uint8_t)(p >> 16);
                }
        }
    }
}

#endif
