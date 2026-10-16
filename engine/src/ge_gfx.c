#include "ge/ge_gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GeImage ge_image_create(int w, int h) {
    GeImage img = { w, h, (uint32_t *)calloc((size_t)w * (size_t)h, sizeof(uint32_t)) };
    if (!img.px) img.w = img.h = 0;
    return img;
}

void ge_image_free(GeImage *img) {
    free(img->px);
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
        int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
        int r = dr + ((((int)((s >> 16) & 255) - dr) * (int)sa) / 255);
        int g = dg + ((((int)((s >> 8) & 255) - dg) * (int)sa) / 255);
        int b = db + ((((int)(s & 255) - db) * (int)sa) / 255);
        return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
    /* general "over" for textures with transparency (used when generating art) */
    uint32_t oa = sa + da * (255 - sa) / 255;
    if (oa == 0) return 0;
    uint32_t wd = da * (255 - sa) / 255;
    uint32_t r = (((s >> 16) & 255) * sa + ((d >> 16) & 255) * wd) / oa;
    uint32_t g = (((s >> 8) & 255) * sa + ((d >> 8) & 255) * wd) / oa;
    uint32_t b = ((s & 255) * sa + (d & 255) * wd) / oa;
    return (oa << 24) | (r << 16) | (g << 8) | b;
}

static inline uint32_t px_tint(uint32_t s, uint32_t t, int flash) {
    uint32_t a = ((s >> 24) * (t >> 24)) / 255;
    if (flash) return (a << 24) | (t & 0x00FFFFFFu);
    uint32_t r = (((s >> 16) & 255) * ((t >> 16) & 255)) / 255;
    uint32_t g = (((s >> 8) & 255) * ((t >> 8) & 255)) / 255;
    uint32_t b = ((s & 255) * (t & 255)) / 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

/* ---- primitives -------------------------------------------------------- */

void ge_clear(GeImage *dst, uint32_t color) {
    size_t n = (size_t)dst->w * (size_t)dst->h;
    for (size_t i = 0; i < n; i++) dst->px[i] = color;
}

void ge_fill_rect(GeImage *dst, int x, int y, int w, int h, uint32_t color, int flags) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > dst->w ? dst->w : x + w, y1 = y + h > dst->h ? dst->h : y + h;
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
        uint32_t *row = dst->px + (size_t)y * dst->w;
        for (int x = 0; x < dst->w; x++) row[x] = c;
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
    for (int i = 0; i < depth; i++) {
        int a = max_alpha * (depth - i) * (depth - i) / (depth * depth);
        uint32_t c = GE_ARGB(a, 0, 0, 0);
        ge_fill_rect(dst, 0, i, dst->w, 1, c, 0);
        ge_fill_rect(dst, 0, dst->h - 1 - i, dst->w, 1, c, 0);
        ge_fill_rect(dst, i, depth, 1, dst->h - 2 * depth, c, 0);
        ge_fill_rect(dst, dst->w - 1 - i, depth, 1, dst->h - 2 * depth, c, 0);
    }
}

/* ---- blitting ---------------------------------------------------------- */

void ge_blit(GeImage *dst, const GeImage *src, GeIRect sr, int dx, int dy, int flags, uint32_t tint) {
    if (sr.x < 0 || sr.y < 0 || sr.x + sr.w > src->w || sr.y + sr.h > src->h) return;
    int x0 = dx < 0 ? 0 : dx, y0 = dy < 0 ? 0 : dy;
    int x1 = dx + sr.w > dst->w ? dst->w : dx + sr.w;
    int y1 = dy + sr.h > dst->h ? dst->h : dy + sr.h;
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
        for (int x = x0; x < x1; x++) {
            int sx = (flags & GE_FLIP_X) ? sr.x + sr.w - 1 - (x - dx) : sr.x + (x - dx);
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
    bool plain = tint == GE_WHITE;
    int flash = flags & GE_FLASH;
    for (int y = y0; y < y1; y++) {
        int ly = (y - dy) / scale;
        int sy = (flags & GE_FLIP_Y) ? sr.y + sr.h - 1 - ly : sr.y + ly;
        const uint32_t *srow = src->px + (size_t)sy * src->w;
        uint32_t *drow = dst->px + (size_t)y * dst->w;
        for (int x = x0; x < x1; x++) {
            int lx = (x - dx) / scale;
            uint32_t s = srow[(flags & GE_FLIP_X) ? sr.x + sr.w - 1 - lx : sr.x + lx];
            if (!(s >> 24)) continue;
            if (!plain) s = px_tint(s, tint, flash);
            drow[x] = px_blend(drow[x], s, flags & ~GE_OPAQUE);
        }
    }
}

void ge_blit_wrap_x(GeImage *dst, const GeImage *src, int scroll_x, int dy, int flags, uint32_t tint) {
    if (src->w <= 0) return;
    int off = scroll_x % src->w;
    if (off < 0) off += src->w;
    GeIRect all = { 0, 0, src->w, src->h };
    for (int x = -off; x < dst->w; x += src->w) ge_blit(dst, src, all, x, dy, flags, tint);
}

/* ---- file formats ------------------------------------------------------ */

/* Netpbm PAM (P7, 4-channel). Trivial to export from GIMP/ImageMagick and
 * needs no third-party decoder, which keeps the 3DS build dependency-free. */
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
