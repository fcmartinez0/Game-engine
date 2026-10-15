#include "demo.h"

#include <stdlib.h>
#include <string.h>

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

/* ---- characters -------------------------------------------------------- */

typedef struct {
    uint32_t skin, hair, scarf, tunic, pants, boots, weapon, weapon_hi;
    int head_r, weapon_len, weapon_w;
    int ears;
} Look;

typedef struct {
    float bob, lean;
    int legs;           /* 0 walk, 1 tucked (jump), 2 spread (fall), 3 stand */
    float phase;
    float arm_f, arm_b; /* radians, 0 = right, +pi/2 = down */
    float sword;
    int arc;            /* draw a slash trail */
    float arc0, arc1;
    float scarf;
} Pose;

static void pt(float x, float y, float ang, float len, int *ox, int *oy) {
    *ox = (int)lroundf(x + cosf(ang) * len);
    *oy = (int)lroundf(y + sinf(ang) * len);
}

static void draw_figure(GeImage *im, int cx, int base, const Look *L, const Pose *P) {
    const uint32_t outline = GE_RGB(20, 14, 30);
    int hipx = cx + (int)lroundf(P->lean * 0.4f), hipy = base - 12 + (int)P->bob;
    int shx = cx + (int)lroundf(P->lean), shy = base - 21 + (int)P->bob;
    int hx = cx + (int)lroundf(P->lean * 1.25f), hy = base - 25 + (int)P->bob;

    float s = sinf(P->phase * PI2), c = cosf(P->phase * PI2);
    int f1x, f1y, f2x, f2y;
    switch (P->legs) {
    case 0: f1x = cx + (int)(s * 6); f1y = base - (int)(fmaxf(0, c) * 3);
            f2x = cx - (int)(s * 6); f2y = base - (int)(fmaxf(0, -c) * 3); break;
    case 1: f1x = cx + 4; f1y = base - 6; f2x = cx - 2; f2y = base - 4; break;
    case 2: f1x = cx + 5; f1y = base - 2; f2x = cx - 4; f2y = base - 1; break;
    default: f1x = cx + 3; f1y = base; f2x = cx - 3; f2y = base; break;
    }

    /* back leg + back arm */
    ge_draw_line(im, hipx, hipy, f2x, f2y, 3, L->pants);
    ge_fill_rect(im, f2x - 1, f2y - 1, 4, 2, L->boots, 0);
    int bax, bay;
    pt((float)shx, (float)shy, P->arm_b, 7, &bax, &bay);
    ge_draw_line(im, shx, shy, bax, bay, 2, mix(L->tunic, outline, 0.4f));

    /* scarf streaming behind the neck */
    int nx = hx - L->head_r + 1, ny = hy + L->head_r;
    ge_draw_line(im, nx, ny, nx - 6, ny + 1 + (int)P->scarf, 2, L->scarf);
    ge_draw_line(im, nx - 6, ny + 1 + (int)P->scarf, nx - 11, ny + 3 + (int)(P->scarf * 1.6f), 2, mix(L->scarf, outline, 0.3f));

    /* torso */
    ge_draw_line(im, hipx, hipy, shx, shy, 5, L->tunic);
    ge_fill_rect(im, hipx - 2, hipy - 1, 5, 2, outline, 0); /* belt */

    /* front leg */
    ge_draw_line(im, hipx, hipy, f1x, f1y, 3, L->pants);
    ge_fill_rect(im, f1x - 1, f1y - 1, 4, 2, L->boots, 0);

    /* head */
    ge_fill_circle(im, hx, hy, L->head_r + 1, outline, 0);
    ge_fill_circle(im, hx, hy, L->head_r, L->skin, 0);
    ge_fill_circle(im, hx - 1, hy - 2, L->head_r - 1, L->hair, 0);
    ge_fill_rect(im, hx - L->head_r, hy - 1, L->head_r - 1, 3, L->hair, 0);
    ge_fill_rect(im, hx + 2, hy, 1, 2, outline, 0); /* eye */
    if (L->ears) {
        ge_draw_line(im, hx - L->head_r, hy - 1, hx - L->head_r - 4, hy - 4, 1, L->skin);
        ge_draw_line(im, hx + L->head_r, hy - 1, hx + L->head_r + 3, hy - 4, 1, L->skin);
    }

    /* front arm + weapon */
    int fax, fay;
    pt((float)shx, (float)shy, P->arm_f, 8, &fax, &fay);
    ge_draw_line(im, shx, shy, fax, fay, 2, L->tunic);
    ge_fill_rect(im, fax - 1, fay - 1, 3, 3, L->skin, 0);
    if (L->weapon_len > 0) {
        int tx, ty;
        pt((float)fax, (float)fay, P->sword, (float)L->weapon_len, &tx, &ty);
        ge_draw_line(im, fax, fay, tx, ty, L->weapon_w, L->weapon);
        if (L->weapon_w >= 2) ge_draw_line(im, fax, fay, tx, ty, 1, L->weapon_hi);
        int gx, gy, g2x, g2y;
        pt((float)fax, (float)fay, P->sword + 1.5708f, 3, &gx, &gy);
        pt((float)fax, (float)fay, P->sword - 1.5708f, 3, &g2x, &g2y);
        ge_draw_line(im, gx, gy, g2x, g2y, 1, GE_RGB(240, 200, 90));
    }

    /* slash trail: a fading arc swept around the shoulder */
    if (P->arc) {
        float r = (float)L->weapon_len + 7.0f;
        int steps = 10;
        int px = 0, py = 0;
        for (int i = 0; i <= steps; i++) {
            float t = (float)i / steps;
            float a = P->arc0 + (P->arc1 - P->arc0) * t;
            int ax, ay;
            pt((float)shx, (float)shy, a, r, &ax, &ay);
            if (i > 0) {
                int alpha = 60 + (int)(190 * t);
                ge_draw_line(im, px, py, ax, ay, 3, GE_ARGB(alpha, 255, 250, 235));
            }
            px = ax; py = ay;
        }
    }
}

static void draw_fallen(GeImage *im, int cx, int base, const Look *L) {
    const uint32_t outline = GE_RGB(20, 14, 30);
    ge_draw_line(im, cx - 6, base - 3, cx + 5, base - 4, 5, L->tunic);
    ge_draw_line(im, cx - 6, base - 3, cx - 14, base - 2, 3, L->pants);
    ge_fill_circle(im, cx + 9, base - 4, L->head_r + 1, outline, 0);
    ge_fill_circle(im, cx + 9, base - 4, L->head_r, L->skin, 0);
    ge_fill_circle(im, cx + 8, base - 6, L->head_r - 1, L->hair, 0);
    if (L->weapon_len > 0)
        ge_draw_line(im, cx - 2, base - 1, cx + 12, base, L->weapon_w, L->weapon);
}

#define NCELL_COLS 8

static void cell_origin(const GeImage *im, int cell, int *ox, int *oy) {
    (void)im;
    *ox = (cell % NCELL_COLS) * 32;
    *oy = (cell / NCELL_COLS) * 32;
}

static void put(GeImage *im, int cell, const Look *L, Pose p) {
    int ox, oy;
    cell_origin(im, cell, &ox, &oy);
    /* draw into a scratch cell so clipping to the cell is automatic */
    GeImage tmp = ge_image_create(32, 32);
    draw_figure(&tmp, 16, 31, L, &p);
    GeIRect all = { 0, 0, 32, 32 };
    ge_blit(im, &tmp, all, ox, oy, 0, GE_WHITE);
    ge_image_free(&tmp);
}

static void put_fallen(GeImage *im, int cell, const Look *L) {
    int ox, oy;
    cell_origin(im, cell, &ox, &oy);
    GeImage tmp = ge_image_create(32, 32);
    draw_fallen(&tmp, 16, 31, L);
    GeIRect all = { 0, 0, 32, 32 };
    ge_blit(im, &tmp, all, ox, oy, 0, GE_WHITE);
    ge_image_free(&tmp);
}

static const char player_clips[] =
    "cell 32 32\n"
    "origin 16 31\n"
    "clip idle loop\n  frame 0 28\n  frame 1 28\n"
    "clip run loop\n  frame 2 5\n  frame 3 5\n  frame 4 5\n  frame 5 5\n"
    "clip jump once\n  frame 6 8\n"
    "clip fall once\n  frame 7 8\n"
    "# 3-hit combo: startup / active / recovery. Chain by pressing attack again from the cancel frame.\n"
    "clip attack1 once dmg=8 kb=2.2,-1.6 stop=4 stun=16 cancel=2 next=attack2 lunge=1.6\n"
    "  frame 8 3\n  frame 9 3 hit=2,-31,29,24\n  frame 10 6\n"
    "clip attack2 once dmg=9 kb=2.6,-1.8 stop=4 stun=16 cancel=2 next=attack3 lunge=1.8\n"
    "  frame 11 3\n  frame 12 3 hit=2,-33,30,28\n  frame 13 6\n"
    "clip attack3 once dmg=16 kb=5.2,-3.4 stop=9 stun=26 lunge=2.6\n"
    "  frame 14 5\n  frame 15 3 hit=0,-34,36,30\n  frame 16 3 hit=0,-30,38,28\n  frame 17 10\n"
    "clip hurt once\n  frame 18 99\n"
    "clip dash once\n  frame 19 99\n"
    "clip dead once\n  frame 20 999\n";

int art_make_player(GeSprite *s) {
    memset(s, 0, sizeof *s);
    s->sheet = ge_image_create(256, 96);
    Look L = { GE_RGB(240, 200, 160), GE_RGB(60, 40, 40), GE_RGB(220, 50, 70), GE_RGB(50, 90, 170),
               GE_RGB(40, 44, 70), GE_RGB(90, 60, 50), GE_RGB(205, 215, 235), GE_RGB(255, 255, 255), 4, 12, 2, 0 };
    GeImage *im = &s->sheet;
    put(im, 0, &L, (Pose){ 0, 0, 3, 0, 0.5f, 2.2f, -0.9f, 0, 0, 0, 0 });
    put(im, 1, &L, (Pose){ 1, 0, 3, 0, 0.55f, 2.3f, -0.85f, 0, 0, 0, 1 });
    for (int i = 0; i < 4; i++)
        put(im, 2 + i, &L, (Pose){ (float)(i & 1), 3, 0, i * 0.25f, 1.0f + 0.4f * sinf(i * 1.57f), 2.6f, -0.2f, 0, 0, 0, 1.5f });
    put(im, 6, &L, (Pose){ 0, 1, 1, 0, -1.2f, 0.3f, -1.4f, 0, 0, 0, -1 });
    put(im, 7, &L, (Pose){ 0, 1, 2, 0, -0.6f, 0.9f, -0.5f, 0, 0, 0, 2 });
    /* combo 1: horizontal cut */
    put(im, 8, &L, (Pose){ 0, -2, 3, 0, -2.3f, 2.4f, -2.7f, 0, 0, 0, 0 });
    put(im, 9, &L, (Pose){ 0, 3, 3, 0, 0.0f, 2.0f, 0.0f, 1, -2.1f, 0.5f, 1 });
    put(im, 10, &L, (Pose){ 0, 2, 3, 0, 0.4f, 2.0f, 0.7f, 0, 0, 0, 0 });
    /* combo 2: rising reverse cut */
    put(im, 11, &L, (Pose){ 0, -1, 3, 0, 1.5f, 2.4f, 2.0f, 0, 0, 0, 0 });
    put(im, 12, &L, (Pose){ 0, 3, 3, 0, -0.3f, 2.0f, -0.6f, 1, 1.6f, -1.5f, 1 });
    put(im, 13, &L, (Pose){ 0, 2, 3, 0, -0.8f, 2.0f, -1.1f, 0, 0, 0, 0 });
    /* combo 3: big overhead */
    put(im, 14, &L, (Pose){ 1, -3, 3, 0, -1.9f, 2.4f, -2.0f, 0, 0, 0, 0 });
    put(im, 15, &L, (Pose){ 0, 4, 3, 0, 0.3f, 2.0f, 0.5f, 1, -2.2f, 0.7f, 2 });
    put(im, 16, &L, (Pose){ 1, 5, 3, 0, 0.6f, 2.0f, 0.9f, 1, 0.3f, 1.4f, 2 });
    put(im, 17, &L, (Pose){ 1, 3, 3, 0, 0.9f, 2.0f, 1.2f, 0, 0, 0, 0 });
    put(im, 18, &L, (Pose){ 0, -4, 3, 0, -1.6f, -2.0f, 2.0f, 0, 0, 0, -1 });
    put(im, 19, &L, (Pose){ 1, 6, 0, 0.15f, -0.2f, 2.5f, -0.1f, 0, 0, 0, 3 });
    put_fallen(im, 20, &L);
    return ge_sprite_parse(s, player_clips);
}

static const char goblin_clips[] =
    "cell 32 32\n"
    "origin 16 31\n"
    "clip idle loop\n  frame 0 30\n  frame 1 30\n"
    "clip walk loop\n  frame 2 7\n  frame 3 7\n  frame 4 7\n  frame 5 7\n"
    "# the long wind-up is the player's cue to dodge, trade or hop lanes\n"
    "clip attack once dmg=6 kb=3.2,-2.2 stop=5 stun=18\n"
    "  frame 6 22\n  frame 7 4 hit=2,-26,24,22\n  frame 8 16\n"
    "clip hurt once\n  frame 9 99\n"
    "clip dead once\n  frame 10 999\n";

int art_make_goblin(GeSprite *s) {
    memset(s, 0, sizeof *s);
    s->sheet = ge_image_create(256, 64);
    Look L = { GE_RGB(110, 170, 80), GE_RGB(70, 100, 50), GE_RGB(150, 90, 40), GE_RGB(120, 80, 50),
               GE_RGB(70, 55, 45), GE_RGB(50, 40, 35), GE_RGB(120, 80, 45), GE_RGB(170, 125, 80), 5, 11, 3, 1 };
    GeImage *im = &s->sheet;
    put(im, 0, &L, (Pose){ 0, 1, 3, 0, 0.8f, 2.4f, 0.6f, 0, 0, 0, 0 });
    put(im, 1, &L, (Pose){ 1, 1, 3, 0, 0.8f, 2.4f, 0.65f, 0, 0, 0, 0 });
    for (int i = 0; i < 4; i++)
        put(im, 2 + i, &L, (Pose){ (float)(i & 1), 2, 0, i * 0.25f, 0.9f, 2.4f, 0.5f, 0, 0, 0, 0 });
    put(im, 6, &L, (Pose){ 0, -3, 3, 0, -2.2f, 2.4f, -2.2f, 0, 0, 0, 0 });
    put(im, 7, &L, (Pose){ 0, 4, 3, 0, 0.1f, 2.4f, 0.4f, 1, -2.0f, 0.6f, 1 });
    put(im, 8, &L, (Pose){ 1, 3, 3, 0, 0.5f, 2.4f, 1.0f, 0, 0, 0, 0 });
    put(im, 9, &L, (Pose){ 0, -3, 3, 0, -1.4f, -2.0f, 1.8f, 0, 0, 0, 0 });
    put_fallen(im, 10, &L);
    return ge_sprite_parse(s, goblin_clips);
}

/* ---- tiles ------------------------------------------------------------- */

void art_make_tileset(GeImage *tiles) {
    *tiles = ge_image_create(GE_TILE * 4, GE_TILE);
    GeRng r;
    ge_rng_seed(&r, 7);
    const uint32_t d0 = GE_RGB(86, 58, 56), d1 = GE_RGB(100, 68, 62), d2 = GE_RGB(64, 44, 48);
    for (int t = 0; t < 4; t++)
        for (int y = 0; y < GE_TILE; y++)
            for (int x = 0; x < GE_TILE; x++) {
                float n = ge_rng_float(&r);
                uint32_t c = n < 0.18f ? d2 : (n > 0.78f ? d1 : d0);
                if (t == 2) c = 0; /* plank is built below */
                tiles->px[y * tiles->w + t * GE_TILE + x] = c;
            }
    /* 0: grass-topped ground */
    for (int x = 0; x < GE_TILE; x++) {
        int blade = 3 + (int)(ge_rng_float(&r) * 3.0f);
        for (int y = 0; y < blade; y++)
            tiles->px[y * tiles->w + x] = y == 0 ? GE_RGB(120, 190, 100) : mix(GE_RGB(78, 140, 80), GE_RGB(48, 92, 66), (float)y / blade);
        if (x % 3 == 0) tiles->px[blade * tiles->w + x] = GE_RGB(44, 84, 62);
    }
    /* 1, 3: dirt with stones / roots */
    for (int i = 0; i < 4; i++) {
        int sx = (int)(ge_rng_float(&r) * 12), sy = 2 + (int)(ge_rng_float(&r) * 11);
        ge_fill_rect(tiles, GE_TILE + sx, sy, 3, 2, GE_RGB(120, 100, 104), 0);
    }
    ge_draw_line(tiles, 3 * GE_TILE + 2, 0, 3 * GE_TILE + 8, 9, 1, GE_RGB(60, 90, 60));
    /* 2: one-way wooden plank, only the top 6px */
    for (int x = 0; x < GE_TILE; x++)
        for (int y = 0; y < 6; y++) {
            uint32_t c = y == 0 ? GE_RGB(190, 140, 90) : (y == 5 ? GE_RGB(70, 46, 40) : GE_RGB(150, 104, 66));
            if (y > 0 && y < 5 && (x == 0 || x == 8)) c = GE_RGB(100, 68, 50);
            tiles->px[y * tiles->w + 2 * GE_TILE + x] = c;
        }
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
        draw_pine(&im, x, y, 20 + (int)(ge_rng_float(&r) * 16), mix(GE_RGB(32, 22, 62), GE_RGB(44, 28, 74), ge_rng_float(&r)));
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
    /* { image, factor_x, factor_y, auto_x, y, foreground, additive, opaque, optional, alpha } */
    ge_world_add_layer(w, (GeParallaxLayer){ make_sky(), 0.0f, 0.0f, 0.0f, 0, false, false, true, false, 255 });
    ge_world_add_layer(w, (GeParallaxLayer){ make_mountains(800, 150, GE_RGB(60, 38, 100), GE_RGB(120, 62, 110), 66, 24, 12, 5),
                                              0.07f, 0.0f, 0.0f, 62, false, false, false, false, 255 });
    ge_world_add_layer(w, (GeParallaxLayer){ make_clouds(800, 70), 0.12f, 0.0f, 0.05f, 30, false, true, false, true, 255 });
    ge_world_add_layer(w, (GeParallaxLayer){ make_hills(800, 130), 0.22f, 0.0f, 0.0f, 100, false, false, false, false, 255 });
    ge_world_add_layer(w, (GeParallaxLayer){ make_forest(640, 210), 0.45f, 0.0f, 0.0f, 30, false, false, false, false, 255 });
    ge_world_add_layer(w, (GeParallaxLayer){ make_shafts(480, 240), 0.35f, 0.0f, 0.0f, 0, false, true, false, true, 255 });
    ge_world_add_layer(w, (GeParallaxLayer){ make_mist(800, 80, 90), 0.65f, 0.0f, 0.12f, 140, false, false, false, true, 255 });
    /* foreground: passes in front of the action, moving faster than the world */
    ge_world_add_layer(w, (GeParallaxLayer){ make_grass(800, 34), 1.45f, 0.0f, 0.0f, 206, true, false, false, false, 255 });
    ge_world_add_layer(w, (GeParallaxLayer){ make_canopy(800, 40), 1.25f, 0.0f, 0.0f, 0, true, false, false, false, 255 });
    ge_world_add_layer(w, (GeParallaxLayer){ make_mist(800, 70, 70), 1.7f, 0.0f, 0.3f, 170, true, false, false, true, 255 });
}
