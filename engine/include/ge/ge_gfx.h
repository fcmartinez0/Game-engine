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
/* Draw `src` repeated horizontally so it covers the whole destination; `scroll_x`
 * is how far the layer has moved. Used for parallax. */
void ge_blit_wrap_x(GeImage *dst, const GeImage *src, int scroll_x, int dy, int flags, uint32_t tint);

#endif
