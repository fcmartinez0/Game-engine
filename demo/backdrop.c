/* The demo's backdrop: ten parallax layers, all drawn in code. */

#include "demo.h"

#define PI2 6.2831853f

/* ---- little helpers ---------------------------------------------------- */

static uint32_t mix(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int sh = 24; sh >= 0; sh -= 8) {
        float x = (float)((a >> sh) & 255), y = (float)((b >> sh) & 255);
        out |= (uint32_t)(x + (y - x) * t) << sh;
    }
    return out;
}

/* Everything that tiles horizontally is drawn three times (x-W, x, x+W) so
 * shapes crossing the edge wrap and parallax layers scroll without seams. */
static void wrap_circle(GeImage *im, int x, int y, int r, uint32_t c) {
    for (int k = -1; k <= 1; k++) ge_fill_circle(im, x + k * im->w, y, r, c, 0);
}
static void wrap_rect(GeImage *im, int x, int y, int w, int h, uint32_t c) {
    for (int k = -1; k <= 1; k++) ge_fill_rect(im, x + k * im->w, y, w, h, c, 0);
}

/* ---- parallax backdrop ------------------------------------------------- */

static GeImage make_sky(void) {
    GeImage im = ge_image_create(400, 240);
    ge_gradient_v(&im, 0, 110, GE_RGB(10, 8, 34), GE_RGB(58, 30, 94));
    ge_gradient_v(&im, 110, 190, GE_RGB(58, 30, 94), GE_RGB(236, 112, 100));
    ge_gradient_v(&im, 190, 240, GE_RGB(236, 112, 100), GE_RGB(255, 178, 110));
    GeRng r;
    ge_rng_seed(&r, 11);
    for (int i = 0; i < 110; i++) {
        int x = (int)(ge_rng_float(&r) * im.w), y = (int)(ge_rng_float(&r) * 120);
        int a = 90 + (int)(ge_rng_float(&r) * 165);
        ge_fill_rect(&im, x, y, 1 + (i % 9 == 0), 1 + (i % 9 == 0), GE_ARGB(a, 255, 245, 230), 0);
    }
    ge_fill_circle(&im, 310, 58, 36, GE_ARGB(16, 255, 220, 200), GE_ADDITIVE); /* moon glow */
    ge_fill_circle(&im, 310, 58, 27, GE_ARGB(22, 255, 220, 200), GE_ADDITIVE);
    ge_fill_circle(&im, 310, 58, 17, GE_RGB(252, 240, 222), 0);
    ge_fill_circle(&im, 304, 53, 3, GE_RGB(226, 208, 196), 0);
    ge_fill_circle(&im, 316, 63, 4, GE_RGB(230, 214, 200), 0);
    return im;
}

static float ridge(int x, int w, float a, float b, float c) {
    float u = (float)x / (float)w * PI2;
    return a * sinf(u + 0.3f) + b * sinf(3 * u + 1.1f) + c * sinf(7 * u + 2.0f);
}

static GeImage make_mountains(int w, int h, uint32_t top, uint32_t bottom, float base, float a, float b, float c) {
    GeImage im = ge_image_create(w, h);
    for (int x = 0; x < w; x++) {
        int top_y = (int)(base + ridge(x, w, a, b, c));
        for (int y = top_y < 0 ? 0 : top_y; y < h; y++)
            im.px[y * w + x] = mix(top, bottom, (float)(y - top_y) / (float)(h - top_y));
    }
    return im;
}

static void draw_pine(GeImage *im, int x, int base_y, int height, uint32_t c) {
    wrap_rect(im, x - 1, base_y - 4, 3, 5, c);
    for (int tier = 0; tier < 4; tier++) {
        int y = base_y - 4 - tier * (height / 4);
        int half = 8 - tier * 2;
        for (int row = 0; row < height / 3; row++) {
            int hw = half * (height / 3 - row) / (height / 3);
            wrap_rect(im, x - hw, y - row, hw * 2 + 1, 1, c);
        }
    }
}

static GeImage make_hills(int w, int h) {
    GeImage im = make_mountains(w, h, GE_RGB(46, 30, 78), GE_RGB(60, 36, 80), 60, 16, 7, 3);
    GeRng r;
    ge_rng_seed(&r, 21);
    for (int i = 0; i < 46; i++) {
        int x = (int)(ge_rng_float(&r) * w);
        int y = (int)(60 + ridge(x, w, 16, 7, 3)) + 4;
        /* (two random numbers in one argument list would be drawn in whatever order the compiler likes: gcc and clang
         * differ, and so did the trees. The colour is drawn first, which is the order the saved pictures were made in.) */
        uint32_t shade = mix(GE_RGB(32, 22, 62), GE_RGB(44, 28, 74), ge_rng_float(&r));
        draw_pine(&im, x, y, 20 + (int)(ge_rng_float(&r) * 16), shade);
    }
    return im;
}

static GeImage make_clouds(int w, int h) {
    GeImage im = ge_image_create(w, h);
    GeRng r;
    ge_rng_seed(&r, 5);
    for (int i = 0; i < 9; i++) {
        int cx = (int)(ge_rng_float(&r) * w), cy = 12 + (int)(ge_rng_float(&r) * (h - 28));
        for (int j = 0; j < 7; j++) {
            int ox = (int)(ge_rng_range(&r, -34, 34)), oy = (int)(ge_rng_range(&r, -4, 4));
            int rr = 6 + (int)(ge_rng_float(&r) * 8);
            for (int k = -1; k <= 1; k++)
                ge_fill_circle(&im, cx + ox + k * w, cy + oy, rr, GE_ARGB(34, 255, 190, 190), 0);
        }
    }
    return im;
}

static GeImage make_forest(int w, int h) {
    GeImage im = ge_image_create(w, h);
    GeRng r;
    ge_rng_seed(&r, 33);
    const uint32_t trunk = GE_RGB(24, 18, 44), leaf = GE_RGB(30, 24, 58), rim = GE_RGB(70, 46, 96);
    for (int i = 0; i < 15; i++) {
        int x = i * (w / 15) + (int)(ge_rng_float(&r) * 24);
        int tw = 8 + (int)(ge_rng_float(&r) * 8), top = 40 + (int)(ge_rng_float(&r) * 40);
        wrap_rect(&im, x, top, tw, h - top, trunk);
        wrap_rect(&im, x + tw - 2, top, 2, h - top, rim);  /* moonlit edge */
        int cr = 16 + (int)(ge_rng_float(&r) * 12);
        for (int j = 0; j < 5; j++) {
            int ox = (int)ge_rng_range(&r, -cr, cr), oy = (int)ge_rng_range(&r, -cr / 2, cr / 3);
            wrap_circle(&im, x + tw / 2 + ox, top + oy, cr * 3 / 4, leaf);
        }
    }
    return im;
}

static GeImage make_shafts(int w, int h) {
    GeImage im = ge_image_create(w, h);
    GeRng r;
    ge_rng_seed(&r, 47);
    for (int i = 0; i < 6; i++) {
        int x0 = (int)(ge_rng_float(&r) * w), sw = 10 + (int)(ge_rng_float(&r) * 16);
        int a = 12 + (int)(ge_rng_float(&r) * 14);
        for (int y = 0; y < h; y++) {
            int fade = a * (h - y) / h;
            wrap_rect(&im, x0 + y * 2 / 5, y, sw, 1, GE_ARGB(fade, 255, 190, 150));
        }
    }
    return im;
}

static GeImage make_mist(int w, int h, int peak_alpha) {
    GeImage im = ge_image_create(w, h);
    for (int x = 0; x < w; x++) {
        float u = (float)x / (float)w * PI2;
        float wob = 0.65f + 0.2f * sinf(2 * u) + 0.15f * sinf(5 * u + 1.0f);
        for (int y = 0; y < h; y++) {
            float t = (float)y / (float)(h - 1);
            float prof = sinf(t * 3.14159f);
            int a = (int)((float)peak_alpha * prof * prof * wob);
            if (a < 6) a = 0;   /* invisible at this alpha, but each one would still cost a blend */
            im.px[y * w + x] = GE_ARGB(a, 214, 170, 200);
        }
    }
    return im;
}

static GeImage make_grass(int w, int h) {
    GeImage im = ge_image_create(w, h);
    GeRng r;
    ge_rng_seed(&r, 61);
    for (int x = 0; x < w; x += 2) {
        int bh = 6 + (int)(ge_rng_float(&r) * (float)(h - 8));
        float lean = ge_rng_range(&r, -3, 3);
        uint32_t c = mix(GE_RGB(6, 8, 16), GE_RGB(16, 30, 30), ge_rng_float(&r));
        int px = x, py = h - 1;
        for (int i = 0; i < bh; i++) {
            float t = (float)i / bh;
            int bx = x + (int)(lean * t * t);
            for (int k = -1; k <= 1; k++) ge_fill_rect(&im, bx + k * w, h - 1 - i, 2, 1, c, 0);
            px = bx; py = h - 1 - i;
        }
        (void)px; (void)py;
    }
    return im;
}

static GeImage make_canopy(int w, int h) {
    GeImage im = ge_image_create(w, h);
    GeRng r;
    ge_rng_seed(&r, 71);
    const uint32_t c = GE_RGB(6, 5, 14);
    for (int i = 0; i < 26; i++) {
        int x = (int)(ge_rng_float(&r) * w), rr = 8 + (int)(ge_rng_float(&r) * 12);
        wrap_circle(&im, x, (int)(ge_rng_float(&r) * 8) - 4, rr, c);
    }
    for (int i = 0; i < 12; i++) { /* hanging vines */
        int x = (int)(ge_rng_float(&r) * w), len = 12 + (int)(ge_rng_float(&r) * (h - 14));
        wrap_rect(&im, x, 0, 1, len, c);
        wrap_circle(&im, x, len, 2, c);
    }
    return im;
}

void art_add_layers(GeWorld *w) {
#define LAYER(...) ge_world_add_layer(w, (GeParallaxLayer){ __VA_ARGS__ })
    LAYER(.img = make_sky(), .opaque = true);
    LAYER(.img = make_mountains(800, 150, GE_RGB(60, 38, 100), GE_RGB(120, 62, 110), 66, 24, 12, 5), .factor_x = 0.07f, .y = 62);
    LAYER(.img = make_clouds(800, 70), .factor_x = 0.12f, .auto_x = 0.05f, .y = 30, .additive = true, .optional = true);
    LAYER(.img = make_hills(800, 130), .factor_x = 0.22f, .y = 100);
    LAYER(.img = make_forest(640, 210), .factor_x = 0.45f, .y = 30);
    LAYER(.img = make_shafts(480, 240), .factor_x = 0.35f, .additive = true, .optional = true);
    LAYER(.img = make_mist(800, 80, 90), .factor_x = 0.65f, .auto_x = 0.12f, .y = 140, .optional = true);
    /* foreground: passes in front of the action, moving faster than the world */
    LAYER(.img = make_grass(800, 20), .factor_x = 1.45f, .y = 220, .foreground = true);
    LAYER(.img = make_canopy(800, 40), .factor_x = 1.25f, .foreground = true);
    LAYER(.img = make_mist(800, 70, 70), .factor_x = 1.7f, .auto_x = 0.3f, .y = 170, .foreground = true, .optional = true);
#undef LAYER
}

/* ---- the caves (placeholder art) ------------------------------------------------------------------
 * Under the forest: a dark, still backdrop with rock columns far away and teeth of stone hanging from
 * the dark above. Cheap on the 3DS on purpose: one solid back layer (it replaces the clear) and two
 * layers of solid silhouettes, so the compositor skips everything the columns hide. */

static GeImage make_cave_back(void) {
    GeImage im = ge_image_create(400, 240);
    ge_gradient_v(&im, 0, 140, GE_RGB(8, 6, 18), GE_RGB(22, 16, 40));
    ge_gradient_v(&im, 140, 240, GE_RGB(22, 16, 40), GE_RGB(36, 26, 52));
    GeRng r;
    ge_rng_seed(&r, 41);
    for (int i = 0; i < 40; i++) {   /* a few glints in the rock */
        int x = (int)(ge_rng_float(&r) * im.w), y = (int)(ge_rng_float(&r) * im.h);
        ge_fill_rect(&im, x, y, 1, 1, GE_ARGB(60 + (int)(ge_rng_float(&r) * 100), 150, 200, 255), 0);
    }
    return im;
}

static GeImage make_cave_columns(int w, int h, uint32_t c, int seed) {
    GeImage im = ge_image_create(w, h);
    GeRng r;
    ge_rng_seed(&r, (uint32_t)seed);
    for (int i = 0; i < 9; i++) {
        int x = (int)(ge_rng_float(&r) * w), cw = 14 + (int)(ge_rng_float(&r) * 26);
        for (int y = 0; y < h; y++) {   /* a column, a little narrower in the middle */
            float t = (float)y / (float)h, k = 1.0f - 0.35f * sinf(t * 3.14159f);
            int hw = (int)((float)cw * 0.5f * k);
            wrap_rect(&im, x - hw, y, hw * 2, 1, c);
        }
    }
    return im;
}

static GeImage make_stalactites(int w, int h, uint32_t c) {
    GeImage im = ge_image_create(w, h);
    GeRng r;
    ge_rng_seed(&r, 43);
    wrap_rect(&im, 0, 0, w, 6, c);
    for (int x = 0; x < w; x += 6 + (int)(ge_rng_float(&r) * 10)) {
        int len = 8 + (int)(ge_rng_float(&r) * (float)(h - 10)), hw = 2 + (int)(ge_rng_float(&r) * 5);
        for (int y = 0; y < len; y++) wrap_rect(&im, x - hw * (len - y) / len, y, 1 + 2 * hw * (len - y) / len, 1, c);
    }
    return im;
}

void art_add_cave_layers(GeWorld *w) {
#define LAYER(...) ge_world_add_layer(w, (GeParallaxLayer){ __VA_ARGS__ })
    LAYER(.img = make_cave_back(), .opaque = true);
    LAYER(.img = make_cave_columns(640, 260, GE_RGB(26, 20, 44), 47), .factor_x = 0.15f, .factor_y = 0.1f, .y = -10);
    LAYER(.img = make_cave_columns(800, 280, GE_RGB(34, 26, 54), 53), .factor_x = 0.35f, .factor_y = 0.2f, .y = -20);
    LAYER(.img = make_stalactites(800, 60, GE_RGB(18, 14, 30)), .factor_x = 0.5f, .factor_y = 0.3f, .y = 0);
#undef LAYER
}
