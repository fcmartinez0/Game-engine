#include "ge/ge_gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pixels start on a 32-byte boundary (one cache line on the 3DS's ARM11): a row of the 400-wide screen is
 * then exactly 50 lines, and an 8-pixel run never straddles two of them. malloc only promises 8 or 16, so
 * the block is over-allocated and the real start is kept just before the pixels for ge_image_free. */
#define GE_IMAGE_ALIGN 32

GeImage ge_image_create(int w, int h) {
    GeImage img = { 0, 0, NULL };
    if (w <= 0 || h <= 0) return img;
    size_t bytes = (size_t)w * (size_t)h * sizeof(uint32_t);
    unsigned char *raw = (unsigned char *)calloc(1, bytes + GE_IMAGE_ALIGN + sizeof(void *));
    if (!raw) return img;
    uintptr_t at = ((uintptr_t)(raw + sizeof(void *)) + GE_IMAGE_ALIGN - 1) & ~(uintptr_t)(GE_IMAGE_ALIGN - 1);
    ((void **)at)[-1] = raw;
    img.w = w; img.h = h; img.px = (uint32_t *)at;
    return img;
}

void ge_image_free(GeImage *img) {
    if (img->px) free(((void **)img->px)[-1]);
    img->px = NULL;
    img->w = img->h = 0;
}

uint32_t ge_image_checksum(const GeImage *img) {
    uint32_t h = 2166136261u; /* FNV-1a */
    size_t n = (size_t)img->w * (size_t)img->h;
    for (size_t i = 0; i < n; i++) { h ^= img->px[i]; h *= 16777619u; }
    return h;
}

/* ---- pixel blending ---------------------------------------------------- */

/* x / 255, rounded, for x in 0..65535 -- without a divide. The original 3DS's
 * ARM11 has no hardware integer divide, so a plain "/ 255" is a library call
 * per channel per pixel. */
static inline uint32_t div255(uint32_t x) { x += 128; return (x + (x >> 8)) >> 8; }

static inline uint32_t px_blend(uint32_t d, uint32_t s, int flags) {
    uint32_t sa = s >> 24;
    if (sa == 0) return d;
    if (flags & GE_ADDITIVE) {
        uint32_t r = ((d >> 16) & 255) + ((((s >> 16) & 255) * sa) >> 8);
        uint32_t g = ((d >> 8) & 255) + ((((s >> 8) & 255) * sa) >> 8);
        uint32_t b = (d & 255) + (((s & 255) * sa) >> 8);
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        return (d & 0xFF000000u) | (r << 16) | (g << 8) | b;
    }
    if (sa == 255) return s;
    uint32_t da = d >> 24;
    if (da == 255) { /* opaque destination: the common case (framebuffer) */
        /* red+blue share one multiply, green gets another: no per-channel divide. The weights sum
         * to 256 (alpha 0..255 stretched to 0..256), so the shift by 8 is the divide. */
        uint32_t a = sa + (sa >> 7), ia = 256 - a;
        uint32_t rb = (((s & 0x00FF00FFu) * a + (d & 0x00FF00FFu) * ia) >> 8) & 0x00FF00FFu;
        uint32_t g = (((s & 0x0000FF00u) * a + (d & 0x0000FF00u) * ia) >> 8) & 0x0000FF00u;
        return 0xFF000000u | rb | g;
    }
    /* general "over" for textures with transparency (used when generating art) */
    uint32_t oa = sa + div255(da * (255 - sa));
    if (oa == 0) return 0;
    uint32_t wd = div255(da * (255 - sa));
    uint32_t r = (((s >> 16) & 255) * sa + ((d >> 16) & 255) * wd) / oa;
    uint32_t g = (((s >> 8) & 255) * sa + ((d >> 8) & 255) * wd) / oa;
    uint32_t b = ((s & 255) * sa + (d & 255) * wd) / oa;
    return (oa << 24) | (r << 16) | (g << 8) | b;
}

static inline uint32_t px_tint(uint32_t s, uint32_t t, int flash) {
    uint32_t a = div255((s >> 24) * (t >> 24));
    if (flash) return (a << 24) | (t & 0x00FFFFFFu);
    uint32_t r = div255(((s >> 16) & 255) * ((t >> 16) & 255));
    uint32_t g = div255(((s >> 8) & 255) * ((t >> 8) & 255));
    uint32_t b = div255((s & 255) * (t & 255));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* ---- primitives -------------------------------------------------------- */

/* n pixels of one colour, eight stores per loop trip (the compiler only unrolls this loop twice by itself:
 * on the ARM11 that's 2.5 instructions a pixel instead of under 1.5) */
static inline void fill_px(uint32_t *d, int n, uint32_t c) {
    for (; n >= 8; n -= 8, d += 8) { d[0] = c; d[1] = c; d[2] = c; d[3] = c; d[4] = c; d[5] = c; d[6] = c; d[7] = c; }
    for (; n > 0; n--) *d++ = c;
}

void ge_clear(GeImage *dst, uint32_t color) {
    for (int y = 0; y < dst->h; y++) fill_px(dst->px + (size_t)y * dst->w, dst->w, color);
}

void ge_fill_rect(GeImage *dst, int x, int y, int w, int h, uint32_t color, int flags) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > dst->w ? dst->w : x + w, y1 = y + h > dst->h ? dst->h : y + h;
    if (x1 <= x0) return;
    if ((color >> 24) == 255 && !(flags & GE_ADDITIVE)) {       /* opaque: a plain fill, no blending */
        for (int yy = y0; yy < y1; yy++) fill_px(dst->px + (size_t)yy * dst->w + x0, x1 - x0, color);
        return;
    }
    if ((color >> 24) == 0) return;
    if (!(flags & GE_ADDITIVE)) {
        /* one colour over an opaque framebuffer (the usual dimming/flash overlay): px_blend's arithmetic
         * with the colour's half of it worked out once */
        uint32_t sa = color >> 24, a = sa + (sa >> 7), ia = 256 - a;
        uint32_t srb = (color & 0x00FF00FFu) * a, sg = (color & 0x0000FF00u) * a;
        for (int yy = y0; yy < y1; yy++) {
            uint32_t *row = dst->px + (size_t)yy * dst->w;
            for (int xx = x0; xx < x1; xx++) {
                uint32_t d = row[xx];
                if ((d >> 24) == 255)
                    row[xx] = 0xFF000000u | (((srb + (d & 0x00FF00FFu) * ia) >> 8) & 0x00FF00FFu) |
                              (((sg + (d & 0x0000FF00u) * ia) >> 8) & 0x0000FF00u);
                else row[xx] = px_blend(d, color, 0);
            }
        }
        return;
    }
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = dst->px + (size_t)yy * dst->w;
        for (int xx = x0; xx < x1; xx++) row[xx] = px_blend(row[xx], color, flags);
    }
}

void ge_gradient_v(GeImage *dst, int y0, int y1, uint32_t top, uint32_t bottom) {
    if (y1 <= y0) return;
    for (int y = y0; y < y1 && y < dst->h; y++) {
        if (y < 0) continue;
        int t = ((y - y0) * 256) / (y1 - y0);
        uint32_t c = 0;
        for (int sh = 24; sh >= 0; sh -= 8) {
            int a = (top >> sh) & 255, b = (bottom >> sh) & 255;
            c |= (uint32_t)(a + (((b - a) * t) >> 8)) << sh;
        }
        fill_px(dst->px + (size_t)y * dst->w, dst->w, c);
    }
}

static void put_px(GeImage *dst, int x, int y, uint32_t c, int flags) {
    if ((unsigned)x < (unsigned)dst->w && (unsigned)y < (unsigned)dst->h) {
        uint32_t *p = &dst->px[(size_t)y * dst->w + x];
        *p = px_blend(*p, c, flags);
    }
}

void ge_draw_line(GeImage *dst, int x0, int y0, int x1, int y1, int thick, uint32_t color) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int off = thick / 2;
    for (;;) {
        for (int j = 0; j < thick; j++)
            for (int i = 0; i < thick; i++) put_px(dst, x0 - off + i, y0 - off + j, color, 0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void ge_fill_circle(GeImage *dst, int cx, int cy, int r, uint32_t color, int flags) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) put_px(dst, cx + x, cy + y, color, flags);
}

/* Darken the screen edges with a few translucent strips: cheap enough for a
 * 268 MHz console, unlike a per-pixel radial falloff. */
void ge_vignette(GeImage *dst, int depth, int max_alpha) {
    if (depth <= 0 || depth * 2 > dst->h || depth * 2 > dst->w) return;
    for (int i = 0; i < depth; i++) {
        /* black at max_alpha on the very edge, fading quadratically inwards -> a brightness factor of 0..256 */
        int al = max_alpha * (depth - i) * (depth - i) / (depth * depth);
        uint32_t f = (uint32_t)(256 - (al + (al >> 7)));
        for (int pass = 0; pass < 2; pass++) {                       /* top row i, bottom row i */
            uint32_t *row = dst->px + (size_t)(pass ? dst->h - 1 - i : i) * dst->w;
            for (int x = 0; x < dst->w; x++) {
                uint32_t p = row[x];
                row[x] = (p & 0xFF000000u) | ((((p & 0x00FF00FFu) * f) >> 8) & 0x00FF00FFu) | ((((p & 0x0000FF00u) * f) >> 8) & 0x0000FF00u);
            }
        }
        for (int y = depth; y < dst->h - depth; y++) {                   /* left column i, right column i */
            uint32_t *row = dst->px + (size_t)y * dst->w;
            for (int pass = 0; pass < 2; pass++) {
                uint32_t *q = &row[pass ? dst->w - 1 - i : i], p = *q;
                *q = (p & 0xFF000000u) | ((((p & 0x00FF00FFu) * f) >> 8) & 0x00FF00FFu) | ((((p & 0x0000FF00u) * f) >> 8) & 0x0000FF00u);
            }
        }
    }
}

/* ---- blitting ---------------------------------------------------------- */

void ge_blit(GeImage *dst, const GeImage *src, GeIRect sr, int dx, int dy, int flags, uint32_t tint) {
    if (sr.x < 0 || sr.y < 0 || sr.x + sr.w > src->w || sr.y + sr.h > src->h) return;
    int x0 = dx < 0 ? 0 : dx, y0 = dy < 0 ? 0 : dy;
    int x1 = dx + sr.w > dst->w ? dst->w : dx + sr.w;
    int y1 = dy + sr.h > dst->h ? dst->h : dy + sr.h;
    if (x1 <= x0 || y1 <= y0) return;                       /* entirely off-screen */
    bool plain = tint == GE_WHITE;
    int flash = flags & GE_FLASH;
    if ((flags & GE_OPAQUE) && plain && !(flags & (GE_FLIP_X | GE_FLIP_Y | GE_ADDITIVE | GE_FLASH))) {
        for (int y = y0; y < y1; y++)
            memcpy(dst->px + (size_t)y * dst->w + x0, src->px + (size_t)(sr.y + y - dy) * src->w + sr.x + (x0 - dx),
                   (size_t)(x1 - x0) * sizeof(uint32_t));
        return;
    }
    for (int y = y0; y < y1; y++) {
        int sy = (flags & GE_FLIP_Y) ? sr.y + sr.h - 1 - (y - dy) : sr.y + (y - dy);
        const uint32_t *srow = src->px + (size_t)sy * src->w;
        uint32_t *drow = dst->px + (size_t)y * dst->w;
        int sx = (flags & GE_FLIP_X) ? sr.x + sr.w - 1 - (x0 - dx) : sr.x + (x0 - dx), step = (flags & GE_FLIP_X) ? -1 : 1;
        if (plain && !(flags & GE_ADDITIVE)) {                  /* the common case: most pixels are opaque or empty */
            for (int x = x0; x < x1; x++, sx += step) {
                uint32_t s = srow[sx], sa = s >> 24;
                if (sa == 255) drow[x] = s;
                else if (sa) drow[x] = px_blend(drow[x], s, 0);
            }
            continue;
        }
        for (int x = x0; x < x1; x++, sx += step) {
            uint32_t s = srow[sx];
            if (!(s >> 24)) continue;
            if (!plain) s = px_tint(s, tint, flash);
            drow[x] = px_blend(drow[x], s, flags);
        }
    }
}

void ge_blit_scaled(GeImage *dst, const GeImage *src, GeIRect sr, int dx, int dy, int scale, int flags, uint32_t tint) {
    if (scale <= 1) { ge_blit(dst, src, sr, dx, dy, flags, tint); return; }
    if (sr.x < 0 || sr.y < 0 || sr.x + sr.w > src->w || sr.y + sr.h > src->h) return;
    int w = sr.w * scale, h = sr.h * scale;
    int x0 = dx < 0 ? 0 : dx, y0 = dy < 0 ? 0 : dy;
    int x1 = dx + w > dst->w ? dst->w : dx + w, y1 = dy + h > dst->h ? dst->h : dy + h;
    if (x1 <= x0 || y1 <= y0) return;                       /* entirely off-screen */
    bool plain = tint == GE_WHITE;
    bool fast = plain && !(flags & (GE_ADDITIVE | GE_FLASH));
    bool opaque = (flags & GE_OPAQUE) != 0;
    int flash = flags & GE_FLASH;
    /* divisions once per call, not per row or pixel (the 3DS's ARM11 has no divide instruction) */
    int lx0 = (x0 - dx) / scale, sub0 = (x0 - dx) % scale;
    int ly = (y0 - dy) / scale, suby = (y0 - dy) % scale;
    int step = (flags & GE_FLIP_X) ? -1 : 1;
    if (scale == 2 && (opaque || fast)) {
        /* The duels' 2x zoom: each source pixel is read and tested once for its whole 2x2 block, and two
         * screen rows are written per pass where both are on screen. */
        int n = x1 - x0;
        for (int y = y0; y < y1; ) {
            bool pair = suby == 0 && y + 1 < y1;
            uint32_t *d0 = dst->px + (size_t)y * dst->w + x0, *d1 = d0 + dst->w;
            int sy = (flags & GE_FLIP_Y) ? sr.y + sr.h - 1 - ly : sr.y + ly;
            const uint32_t *sp = src->px + (size_t)sy * src->w + (step < 0 ? sr.x + sr.w - 1 - lx0 : sr.x + lx0);
            int i = 0;
#define PUT2(d, k, s) do { uint32_t sa_ = (s) >> 24;                                                       \
                           if (opaque || sa_ == 255) d[k] = (s); else if (sa_) d[k] = px_blend(d[k], (s), 0); } while (0)
            if (sub0) {                                     /* starting on the right half of a source pixel */
                uint32_t c = *sp; sp += step;
                PUT2(d0, 0, c);
                if (pair) PUT2(d1, 0, c);
                i = 1;
            }
            if (pair) {
                for (; i + 2 <= n; i += 2, sp += step) {
                    uint32_t c = *sp, sa = c >> 24;
                    if (opaque || sa == 255) { d0[i] = c; d0[i + 1] = c; d1[i] = c; d1[i + 1] = c; }
                    else if (sa) {
                        d0[i] = px_blend(d0[i], c, 0); d0[i + 1] = px_blend(d0[i + 1], c, 0);
                        d1[i] = px_blend(d1[i], c, 0); d1[i + 1] = px_blend(d1[i + 1], c, 0);
                    }
                }
                if (i < n) { uint32_t c = *sp; PUT2(d0, i, c); PUT2(d1, i, c); }
            } else {
                for (; i + 2 <= n; i += 2, sp += step) {
                    uint32_t c = *sp, sa = c >> 24;
                    if (opaque || sa == 255) { d0[i] = c; d0[i + 1] = c; }
                    else if (sa) { d0[i] = px_blend(d0[i], c, 0); d0[i + 1] = px_blend(d0[i + 1], c, 0); }
                }
                if (i < n) { uint32_t c = *sp; PUT2(d0, i, c); }
            }
#undef PUT2
            if (pair) { y += 2; ly++; }
            else { y++; if (++suby == 2) { suby = 0; ly++; } }
        }
        return;
    }
    const uint32_t *prev = NULL;                            /* the row above, if it came from the same source row */
    for (int y = y0; y < y1; y++) {
        uint32_t *drow = dst->px + (size_t)y * dst->w;
        if (opaque && prev) {                               /* opaque: rows from the same source row are identical */
            memcpy(drow + x0, prev + x0, sizeof(uint32_t) * (size_t)(x1 - x0));
            prev = drow;
            if (++suby == scale) { suby = 0; ly++; prev = NULL; }
            continue;
        }
        int sy = (flags & GE_FLIP_Y) ? sr.y + sr.h - 1 - ly : sr.y + ly;
        const uint32_t *sp = src->px + (size_t)sy * src->w + (step < 0 ? sr.x + sr.w - 1 - lx0 : sr.x + lx0);
        int sub = sub0;
        if (opaque) {
            for (int x = x0; x < x1; x++) {
                drow[x] = *sp;
                if (++sub == scale) { sub = 0; sp += step; }
            }
        } else if (fast) {
            for (int x = x0; x < x1; x++) {
                uint32_t s = *sp, sa = s >> 24;
                if (sa == 255) drow[x] = s;
                else if (sa) drow[x] = px_blend(drow[x], s, 0);
                if (++sub == scale) { sub = 0; sp += step; }
            }
        } else {
            for (int x = x0; x < x1; x++) {
                uint32_t s = *sp;
                if (s >> 24) {
                    if (!plain) s = px_tint(s, tint, flash);
                    drow[x] = px_blend(drow[x], s, flags & ~GE_OPAQUE);
                }
                if (++sub == scale) { sub = 0; sp += step; }
            }
        }
        prev = drow;
        if (++suby == scale) { suby = 0; ly++; prev = NULL; }
    }
}

void ge_blit_wrap_x(GeImage *dst, const GeImage *src, int scroll_x, int dy, int flags, uint32_t tint) {
    if (src->w <= 0) return;
    int off = scroll_x % src->w;
    if (off < 0) off += src->w;
    GeIRect all = { 0, 0, src->w, src->h };
    for (int x = -off; x < dst->w; x += src->w) ge_blit(dst, src, all, x, dy, flags, tint);
}

/* ---- spans ------------------------------------------------------------- */

void ge_image_scale_alpha(GeImage *img, int alpha) {
    if (alpha >= 255) return;
    size_t n = (size_t)img->w * img->h;
    for (size_t i = 0; i < n; i++) {
        uint32_t p = img->px[i];
        img->px[i] = (div255((p >> 24) * (uint32_t)alpha) << 24) | (p & 0x00FFFFFFu);
    }
}

int ge_spans_build(GeSpans *out, const GeImage *img) {
    memset(out, 0, sizeof *out);
    if (img->w > 65535 || img->w <= 0 || img->h <= 0) return -1;
    size_t cap = 64;
    out->spans = (GeSpan *)malloc(cap * sizeof(GeSpan));
    out->row_start = (uint32_t *)malloc(((size_t)img->h + 1) * sizeof(uint32_t));
    if (!out->spans || !out->row_start) { ge_spans_free(out); return -1; }
    size_t n = 0;
    for (int y = 0; y < img->h; y++) {
        out->row_start[y] = (uint32_t)n;
        const uint32_t *row = img->px + (size_t)y * img->w;
        int x = 0;
        while (x < img->w) {
            while (x < img->w && !(row[x] >> 24)) x++;          /* transparent gap */
            if (x >= img->w) break;
            int start = x;
            uint8_t opaque = (row[x] >> 24) == 255;
            while (x < img->w && (row[x] >> 24) && (((row[x] >> 24) == 255) == opaque)) x++;
            if (n == cap) {
                cap *= 2;
                GeSpan *g = (GeSpan *)realloc(out->spans, cap * sizeof(GeSpan));
                if (!g) { ge_spans_free(out); return -1; }
                out->spans = g;
            }
            out->spans[n++] = (GeSpan){ (uint16_t)start, (uint16_t)(x - start), opaque };
        }
    }
    out->row_start[img->h] = (uint32_t)n;
    out->rows = img->h;
    out->row_kind = (uint8_t *)malloc((size_t)img->h);   /* optional: without it the compositor isn't used */
    if (out->row_kind)
        for (int y = 0; y < img->h; y++) {
            int count = 0;
            for (uint32_t k = out->row_start[y]; k < out->row_start[y + 1]; k++)
                if (out->spans[k].opaque) count += out->spans[k].len;
            out->row_kind[y] = count == 0 ? GE_ROW_NONE : count == img->w ? GE_ROW_ALL : GE_ROW_SOME;
        }
    return 0;
}

void ge_spans_free(GeSpans *s) {
    free(s->spans); free(s->row_start); free(s->row_kind);
    memset(s, 0, sizeof *s);
}

void ge_blend_row(uint32_t *dst, const uint32_t *src, int n, int flags) {
    for (int i = 0; i < n; i++) dst[i] = px_blend(dst[i], src[i], flags);
}

void ge_spans_draw_wrap_x(GeImage *dst, const GeImage *src, const GeSpans *sp, int scroll_x, int dy, int flags) {
    ge_spans_draw_wrap_x_clip(dst, src, sp, scroll_x, dy, flags, dst->w);
}

void ge_spans_draw_wrap_x_clip(GeImage *dst, const GeImage *src, const GeSpans *sp, int scroll_x, int dy, int flags, int clip_x1) {
    if (src->w <= 0 || !sp->spans) return;
    int cw = clip_x1 < dst->w ? clip_x1 : dst->w;
    if (cw <= 0) return;
    int off = scroll_x % src->w;
    if (off < 0) off += src->w;
    int y0 = dy < 0 ? 0 : dy, y1 = dy + src->h > dst->h ? dst->h : dy + src->h;
    bool add = (flags & GE_ADDITIVE) != 0;
    for (int y = y0; y < y1; y++) {
        const uint32_t *srow = src->px + (size_t)(y - dy) * src->w;
        uint32_t *drow = dst->px + (size_t)y * dst->w;
        uint32_t first = sp->row_start[y - dy], last = sp->row_start[y - dy + 1];
        for (int base = -off; base < cw; base += src->w) {
            for (uint32_t k = first; k < last; k++) {
                const GeSpan *g = &sp->spans[k];
                int a = base + g->x, b = a + g->len;
                if (b <= 0 || a >= cw) continue;
                int from = a < 0 ? -a : 0;
                if (a < 0) a = 0;
                if (b > cw) b = cw;
                const uint32_t *s = srow + g->x + from;
                if (g->opaque && !add) memcpy(drow + a, s, (size_t)(b - a) * sizeof(uint32_t));
                else for (int x = a; x < b; x++, s++) drow[x] = px_blend(drow[x], *s, flags);
            }
        }
    }
}

/* ---- text -------------------------------------------------------------- */

/* one magnified glyph pixel: written straight into the image when opaque (no call, no blend) */
static inline void block(GeImage *dst, int x, int y, int scale, uint32_t color) {
    int x0 = x < 0 ? 0 : x, x1 = x + scale > dst->w ? dst->w : x + scale;
    int y0 = y < 0 ? 0 : y, y1 = y + scale > dst->h ? dst->h : y + scale;
    if ((color >> 24) == 0) return;
    bool opaque = (color >> 24) == 255;   /* fading text blends here directly, not through ge_fill_rect */
    for (int yy = y0; yy < y1; yy++) {
        uint32_t *row = dst->px + (size_t)yy * dst->w;
        if (opaque) for (int xx = x0; xx < x1; xx++) row[xx] = color;
        else for (int xx = x0; xx < x1; xx++) row[xx] = px_blend(row[xx], color, 0);
    }
}

/* Rows of a bitmap, bit (top >> col) = column col, each bit a scale x scale block of one opaque colour,
 * the whole thing on screen: plain stores, no clipping or blending (the per-pixel block() calls were most
 * of the cost of text). */
static void bits_direct(GeImage *dst, int x, int y, const uint8_t *rows, int nrows, int ncols, int top, int scale, uint32_t color) {
    for (int r = 0; r < nrows; r++) {
        int bits = rows[r];
        if (!bits) continue;
        uint32_t *row = dst->px + (size_t)(y + r * scale) * dst->w + x;
        for (int col = 0; col < ncols; ) {
            if (!(bits & (top >> col))) { col++; continue; }
            int c1 = col + 1;
            while (c1 < ncols && (bits & (top >> c1))) c1++;      /* a run of set bits: one fill per pixel row */
            for (int yy = 0; yy < scale; yy++) fill_px(row + (size_t)yy * dst->w + col * scale, (c1 - col) * scale, color);
            col = c1;
        }
    }
}

void ge_draw_bits(GeImage *dst, int x, int y, const uint8_t rows[7], int scale, uint32_t color) {
    if (x >= dst->w || y >= dst->h || x + 5 * scale <= 0 || y + 7 * scale <= 0) return;
    if (scale > 1 && (color >> 24) == 255 && x >= 0 && x + 5 * scale <= dst->w && y >= 0 && y + 7 * scale <= dst->h) {
        bits_direct(dst, x, y, rows, 7, 5, 0x10, scale, color);
        return;
    }
    if (scale == 1 && (color >> 24) == 255 && x >= 0 && x + 5 <= dst->w && y >= 0 && y + 7 <= dst->h) {
        /* the usual HUD case: plain stores, no clipping (one call per pixel was most of the cost) */
        uint32_t *row = dst->px + (size_t)y * dst->w + x;
        for (int r = 0; r < 7; r++, row += dst->w) {
            int bits = rows[r];
            if (!bits) continue;
            if (bits & 0x10) row[0] = color;
            if (bits & 0x08) row[1] = color;
            if (bits & 0x04) row[2] = color;
            if (bits & 0x02) row[3] = color;
            if (bits & 0x01) row[4] = color;
        }
        return;
    }
    for (int r = 0; r < 7; r++) {
        int bits = rows[r];
        if (!bits) continue;
        for (int col = 0; col < 5; col++)
            if (bits & (0x10 >> col)) block(dst, x + col * scale, y + r * scale, scale, color);
    }
}

/* the glyph grown by one pixel in every direction: the outline, drawn before the glyph itself */
void ge_draw_bits_outline(GeImage *dst, int x, int y, const uint8_t rows[7], int scale, uint32_t color) {
    if (x >= dst->w + scale || y >= dst->h + scale || x + 6 * scale <= 0 || y + 8 * scale <= 0) return;
    if (scale > 1 && (color >> 24) == 255 && x >= scale && x + 6 * scale <= dst->w && y >= scale && y + 8 * scale <= dst->h) {
        uint8_t grown[9];
        for (int r = -1; r <= 7; r++) {
            int bits = 0;
            for (int k = r - 1; k <= r + 1; k++)
                if (k >= 0 && k < 7) bits |= (rows[k] << 2) | (rows[k] << 1) | rows[k];
            grown[r + 1] = (uint8_t)bits;
        }
        bits_direct(dst, x - scale, y - scale, grown, 9, 7, 0x40, scale, color);
        return;
    }
    if (scale == 1 && (color >> 24) == 255 && x >= 1 && x + 6 <= dst->w && y >= 1 && y + 8 <= dst->h) {
        uint32_t *row = dst->px + (size_t)(y - 1) * dst->w + (x - 1);   /* as below, without the clipping */
        for (int r = -1; r <= 7; r++, row += dst->w) {
            int bits = 0;
            for (int k = r - 1; k <= r + 1; k++)
                if (k >= 0 && k < 7) bits |= (rows[k] << 2) | (rows[k] << 1) | rows[k];
            if (!bits) continue;
            for (int col = 0; col < 7; col++)
                if (bits & (0x40 >> col)) row[col] = color;
        }
        return;
    }
    for (int r = -1; r <= 7; r++) {
        int bits = 0;   /* 7 columns, -1 .. 5: bit 0x40 is column -1 */
        for (int k = r - 1; k <= r + 1; k++)
            if (k >= 0 && k < 7) bits |= (rows[k] << 2) | (rows[k] << 1) | rows[k];
        if (!bits) continue;
        for (int col = 0; col < 7; col++)
            if (bits & (0x40 >> col)) block(dst, x + (col - 1) * scale, y + r * scale, scale, color);
    }
}

/* ---- file formats ------------------------------------------------------ */

/* Netpbm PAM (P7, 4-channel). Trivial to export from GIMP/ImageMagick and
 * needs no third-party decoder, which keeps the 3DS build dependency-free. */
int ge_image_save_pam(const GeImage *img, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", img->w, img->h);
    unsigned char row[4 * 512];
    for (int y = 0; y < img->h; y++)
        for (int x0 = 0; x0 < img->w; x0 += 512) {
            int n = img->w - x0 < 512 ? img->w - x0 : 512;
            for (int i = 0; i < n; i++) {
                uint32_t p = img->px[y * img->w + x0 + i];
                row[i * 4] = (unsigned char)(p >> 16); row[i * 4 + 1] = (unsigned char)(p >> 8);
                row[i * 4 + 2] = (unsigned char)p; row[i * 4 + 3] = (unsigned char)(p >> 24);
            }
            fwrite(row, 4, (size_t)n, f);
        }
    return fclose(f) == 0 ? 0 : -2;
}

int ge_image_load_pam(GeImage *out, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char line[128];
    int w = 0, h = 0, depth = 0, maxval = 0, ok = 0;
    if (!fgets(line, sizeof line, f) || strncmp(line, "P7", 2) != 0) { fclose(f); return -2; }
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "ENDHDR", 6) == 0) { ok = 1; break; }
        sscanf(line, "WIDTH %d", &w);
        sscanf(line, "HEIGHT %d", &h);
        sscanf(line, "DEPTH %d", &depth);
        sscanf(line, "MAXVAL %d", &maxval);
    }
    if (!ok || w <= 0 || h <= 0 || depth != 4 || maxval != 255) { fclose(f); return -3; }
    GeImage img = ge_image_create(w, h);
    if (!img.px) { fclose(f); return -4; }
    for (size_t i = 0; i < (size_t)w * h; i++) {
        unsigned char c[4];
        if (fread(c, 1, 4, f) != 4) { ge_image_free(&img); fclose(f); return -5; }
        img.px[i] = GE_ARGB(c[3], c[0], c[1], c[2]);
    }
    fclose(f);
    *out = img;
    return 0;
}

static uint32_t crc_table[256];
static void crc_init(void) {
    if (crc_table[1]) return;
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc_update(uint32_t crc, const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; i++) crc = crc_table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return crc;
}
static void put_be32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void write_chunk(FILE *f, const char *type, const unsigned char *data, uint32_t len) {
    unsigned char hdr[8];
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t crc = crc_update(0xFFFFFFFFu, (const unsigned char *)type, 4);
    if (len) crc = crc_update(crc, data, len);
    unsigned char tail[4];
    put_be32(tail, crc ^ 0xFFFFFFFFu);
    fwrite(tail, 1, 4, f);
}

/* Uncompressed (stored-deflate) PNG: big files, but ~40 lines and no zlib. */
int ge_image_save_png(const GeImage *img, const char *path) {
    crc_init();
    size_t rowlen = (size_t)img->w * 3 + 1, rawlen = rowlen * (size_t)img->h;
    unsigned char *raw = (unsigned char *)malloc(rawlen);
    if (!raw) return -1;
    for (int y = 0; y < img->h; y++) {
        unsigned char *r = raw + (size_t)y * rowlen;
        *r++ = 0;
        for (int x = 0; x < img->w; x++) {
            uint32_t p = img->px[(size_t)y * img->w + x];
            *r++ = p >> 16; *r++ = p >> 8; *r++ = p;
        }
    }
    size_t nblocks = (rawlen + 65534) / 65535;
    size_t zlen = 2 + rawlen + nblocks * 5 + 4;
    unsigned char *z = (unsigned char *)malloc(zlen);
    if (!z) { free(raw); return -1; }
    size_t zp = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t off = 0; off < rawlen; off += 65535) {
        size_t n = rawlen - off > 65535 ? 65535 : rawlen - off;
        z[zp++] = (off + n >= rawlen) ? 1 : 0;
        z[zp++] = n & 255; z[zp++] = n >> 8; z[zp++] = ~n & 255; z[zp++] = (~n >> 8) & 255;
        memcpy(z + zp, raw + off, n);
        zp += n;
    }
    for (size_t i = 0; i < rawlen; i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    put_be32(z + zp, (b << 16) | a);
    zp += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return -2; }
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    fwrite(sig, 1, 8, f);
    unsigned char ihdr[13];
    put_be32(ihdr, (uint32_t)img->w);
    put_be32(ihdr + 4, (uint32_t)img->h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    write_chunk(f, "IHDR", ihdr, 13);
    write_chunk(f, "IDAT", z, (uint32_t)zp);
    write_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 0;
}
