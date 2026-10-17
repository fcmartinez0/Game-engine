#ifndef GE_GFX_H
#define GE_GFX_H

#include "ge_math.h"

/* Software renderer. Pixels are 0xAARRGGBB, straight (non-premultiplied)
 * alpha. The internal resolution is 400x240 -- the 3DS top screen -- so the
 * same framebuffer goes to a desktop window (scaled) or to the console as-is. */

typedef struct { int w, h; uint32_t *px; } GeImage;

#define GE_ARGB(a, r, g, b) \
    ((uint32_t)(((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b)))
#define GE_RGB(r, g, b) GE_ARGB(255, r, g, b)
#define GE_WHITE 0xFFFFFFFFu

enum {
    GE_FLIP_X   = 1 << 0,
    GE_FLIP_Y   = 1 << 1,
    GE_ADDITIVE = 1 << 2, /* add colour instead of blending (glows) */
    GE_FLASH    = 1 << 3, /* replace RGB with the tint's RGB (hit flash) */
    GE_OPAQUE   = 1 << 4  /* source has no transparency: plain row copy (fast; ignores tint/flips) */
};

GeImage ge_image_create(int w, int h);
void    ge_image_free(GeImage *img);
int     ge_image_load_pam(GeImage *out, const char *path); /* P7 RGB_ALPHA; 0 on success */
int     ge_image_save_pam(const GeImage *img, const char *path); /* the same format back out; 0 on success */
int     ge_image_save_png(const GeImage *img, const char *path); /* screenshots; 0 on success */
uint32_t ge_image_checksum(const GeImage *img);

void ge_clear(GeImage *dst, uint32_t color);
void ge_fill_rect(GeImage *dst, int x, int y, int w, int h, uint32_t color, int flags);
void ge_gradient_v(GeImage *dst, int y0, int y1, uint32_t top, uint32_t bottom);
void ge_draw_line(GeImage *dst, int x0, int y0, int x1, int y1, int thick, uint32_t color);
void ge_fill_circle(GeImage *dst, int cx, int cy, int r, uint32_t color, int flags);
void ge_vignette(GeImage *dst, int depth, int max_alpha);

/* Copy a source rectangle. `tint` multiplies colour and alpha (GE_WHITE = none). */
void ge_blit(GeImage *dst, const GeImage *src, GeIRect sr, int dx, int dy, int flags, uint32_t tint);
/* Same, magnified by an integer `scale` (nearest neighbour). */
void ge_blit_scaled(GeImage *dst, const GeImage *src, GeIRect sr, int dx, int dy, int scale, int flags, uint32_t tint);
/* Pre-digested image for drawing wide, mostly hard-edged art (parallax layers) fast.
 * Each row becomes a list of runs: fully opaque runs are copied as plain memory,
 * runs with any partial alpha are blended per pixel, transparent gaps cost nothing. */
typedef struct { uint16_t x, len; uint8_t opaque; } GeSpan;
/* Also kept per row: which pixels are fully opaque, as a bitmask (bit x % 32 of word x / 32; mask_words
 * words per row, the last one always zero so a 32-bit window can be read at any x), and whether the row
 * has no opaque pixel (GE_ROW_NONE), only some (GE_ROW_SOME) or nothing else (GE_ROW_ALL). The background
 * compositor (ge_world.c) works out what is hidden from these. NULL if they couldn't be allocated. */
enum { GE_ROW_NONE, GE_ROW_SOME, GE_ROW_ALL };
typedef struct { GeSpan *spans; uint32_t *row_start; int rows; uint32_t *mask; uint8_t *row_kind; int mask_words; } GeSpans;
int  ge_spans_build(GeSpans *out, const GeImage *img);   /* 0 on success */
/* blend n source pixels over n destination pixels (GE_ADDITIVE or 0), as the span drawer does */
void ge_blend_row(uint32_t *dst, const uint32_t *src, int n, int flags);
/* as ge_spans_draw_wrap_x, but only the screen columns left of clip_x1 */
void ge_spans_draw_wrap_x_clip(GeImage *dst, const GeImage *src, const GeSpans *sp, int scroll_x, int dy, int flags, int clip_x1);
void ge_spans_free(GeSpans *s);
/* Like ge_blit_wrap_x, from a span table. `flags` may be GE_ADDITIVE; tint must be GE_WHITE
 * (bake any layer alpha into the image first with ge_image_scale_alpha). */
void ge_spans_draw_wrap_x(GeImage *dst, const GeImage *src, const GeSpans *sp, int scroll_x, int dy, int flags);
void ge_image_scale_alpha(GeImage *img, int alpha); /* multiply every pixel's alpha by alpha/255 */

/* 5x7 bitmap rows (bit 4 = leftmost), drawn magnified `scale` times; used by the font. */
void ge_draw_bits(GeImage *dst, int x, int y, const uint8_t rows[7], int scale, uint32_t color);
void ge_draw_bits_outline(GeImage *dst, int x, int y, const uint8_t rows[7], int scale, uint32_t color);

/* Draw `src` repeated horizontally so it covers the whole destination; `scroll_x`
 * is how far the layer has moved. Used for parallax. */
void ge_blit_wrap_x(GeImage *dst, const GeImage *src, int scroll_x, int dy, int flags, uint32_t tint);

#endif
