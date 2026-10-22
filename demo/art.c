/* Procedural sprite sheets: a posable stick-figure "puppet" drawn into each
 * cell, plus a few creatures and projectiles. Crude on purpose -- it's a
 * stand-in until real art is dropped in through tools/asset-prep. */

#include "demo.h"

#include <stdlib.h>
#include <string.h>

#define PI 3.14159265f

static uint32_t mix(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int sh = 24; sh >= 0; sh -= 8) {
        float x = (float)((a >> sh) & 255), y = (float)((b >> sh) & 255);
        out |= (uint32_t)(x + (y - x) * t) << sh;
    }
    return out;
}

static void pt(float x, float y, float ang, float len, int *ox, int *oy) {
    *ox = (int)lroundf(x + cosf(ang) * len);
    *oy = (int)lroundf(y + sinf(ang) * len);
}

/* ---- the puppet -------------------------------------------------------- */

enum { HEAD_HUMAN, HEAD_MASK, HEAD_GOBLIN, HEAD_MOTH, HEAD_HELM, HEAD_PARAGON };

typedef struct {
    uint32_t skin, hair, cloak, tunic, pants, boots, weapon, weapon_hi;
    int head, head_r, weapon_len, weapon_w;
    uint32_t eye;                       /* optional glow colour for the mask's eye (0 = plain) */
    uint32_t halo;                      /* optional ring behind the head (0 = none) */
    int cloak_len;                      /* extra cloak length in pixels */
    uint32_t rim;                       /* outline colour (0 = near-black) */
} Look;

enum { L_WALK, L_TUCK, L_SPREAD, L_STAND, L_CROUCH, L_KICK, L_SWEEP, L_KNEEL };

typedef struct {
    float bob, lean;
    int legs;
    float phase;
    float arm_f, arm_b; /* radians: 0 = forward, +pi/2 = down, -pi/2 = up */
    float sword;
    int arc;            /* slash trail */
    float arc0, arc1;
    float cloak;        /* cloak flutter */
} Pose;

/* `far` = the leg on the far side of the body: drawn darker, so near and far limbs read as depth.
 * The boot always has a long toe in front and a short heel behind: the clearest facing cue at the feet. */
static void leg(GeImage *im, int hx, int hy, int kx, int ky, int fx, int fy, const Look *L, bool knee, bool far) {
    const uint32_t ink = GE_RGB(20, 14, 30);
    uint32_t pants = far ? mix(L->pants, ink, 0.45f) : L->pants, boots = far ? mix(L->boots, ink, 0.4f) : L->boots;
    if (knee) { ge_draw_line(im, hx, hy, kx, ky, 3, pants); ge_draw_line(im, kx, ky, fx, fy, 3, pants); }
    else ge_draw_line(im, hx, hy, fx, fy, 3, pants);
    ge_fill_rect(im, fx - 2, fy - 1, 3, 2, boots, 0);                 /* heel */
    ge_fill_rect(im, fx + 1, fy - 1, 4, 2, boots, 0);                 /* toe, pointing forward */
    ge_fill_rect(im, fx + 3, fy - 1, 2, 1, mix(boots, GE_RGB(255, 255, 255), 0.3f), 0);   /* toe-cap highlight */
}

static void draw_head(GeImage *im, int hx, int hy, const Look *L) {
    const uint32_t ink = GE_RGB(20, 14, 30);
    int r = L->head_r;
    /* Every head has a FACE on its front side (an eye near the front edge and a protruding nose or snout)
     * and everything else -- horns, ears, antennae, plume -- sweeps BACK. Read that way round the sprite
     * can't be mistaken for facing the other direction. */
    switch (L->head) {
    case HEAD_MASK:
        ge_draw_line(im, hx - 1, hy - r, hx - 8, hy - r - 1, 2, L->skin);          /* horns sweep back */
        ge_draw_line(im, hx + 1, hy - r, hx - 4, hy - r - 2, 2, L->skin);
        ge_fill_circle(im, hx, hy, r + 1, ink, 0);
        ge_fill_circle(im, hx, hy, r, L->skin, 0);
        ge_fill_rect(im, hx + r - 1, hy + 1, 3, 2, L->skin, 0);                    /* small snout */
        ge_fill_rect(im, hx + r + 1, hy + 1, 1, 2, ink, 0);
        ge_fill_rect(im, hx + 1, hy - 2, 3, 4, L->eye ? L->eye : ink, 0);          /* big front eye */
        if (L->eye) ge_fill_rect(im, hx + 2, hy - 1, 1, 2, GE_RGB(255, 255, 255), 0);
        ge_fill_rect(im, hx - 3, hy - 1, 2, 2, mix(L->skin, ink, 0.25f), 0);       /* shading at the back of the head */
        break;
    case HEAD_GOBLIN:
        ge_draw_line(im, hx - r, hy - 1, hx - r - 6, hy - 5, 1, L->skin);          /* long ears swept back */
        ge_draw_line(im, hx - r, hy, hx - r - 5, hy - 2, 1, mix(L->skin, ink, 0.3f));
        ge_fill_circle(im, hx, hy, r + 1, ink, 0);
        ge_fill_circle(im, hx, hy, r, L->skin, 0);
        ge_draw_line(im, hx + r - 1, hy + 1, hx + r + 4, hy + 3, 2, L->skin);      /* long pointed nose */
        ge_fill_rect(im, hx + r + 3, hy + 3, 2, 1, ink, 0);
        ge_fill_rect(im, hx + 1, hy - 2, 4, 3, GE_RGB(255, 235, 90), 0);           /* big yellow eye */
        ge_fill_rect(im, hx + 3, hy - 1, 2, 2, ink, 0);                            /* pupil looks forward */
        ge_fill_rect(im, hx + 1, hy + 3, 3, 1, ink, 0);                            /* mouth with a fang */
        ge_fill_rect(im, hx + 3, hy + 2, 1, 1, GE_RGB(240, 240, 230), 0);
        ge_fill_rect(im, hx - 3, hy, 2, 3, mix(L->skin, ink, 0.3f), 0);            /* shaded back of head */
        break;
    case HEAD_MOTH:
        ge_draw_line(im, hx - 1, hy - r, hx - 9, hy - r - 1, 1, L->hair);          /* antennae sweep back */
        ge_draw_line(im, hx + 1, hy - r, hx - 6, hy - r - 2, 1, L->hair);
        ge_fill_circle(im, hx, hy, r + 1, ink, 0);
        ge_fill_circle(im, hx, hy, r, L->skin, 0);
        ge_fill_rect(im, hx + r - 2, hy - 1, 6, 4, ink, 0);                        /* long snout outline */
        ge_fill_rect(im, hx + r - 2, hy, 5, 3, L->skin, 0);
        ge_fill_rect(im, hx + 1, hy - 2, 4, 3, GE_RGB(90, 235, 255), 0);          /* glowing cyan eye */
        ge_fill_rect(im, hx + 3, hy - 1, 2, 2, GE_RGB(20, 60, 90), 0);
        ge_fill_rect(im, hx - 3, hy - 1, 2, 3, L->hair, 0);                        /* dark hair at the back */
        break;
    case HEAD_HELM:
        ge_draw_line(im, hx - 1, hy - r, hx - 8, hy - r - 1, 2, L->hair);          /* plume streams back */
        ge_fill_circle(im, hx, hy, r + 1, ink, 0);
        ge_fill_circle(im, hx, hy, r, L->skin, 0);
        ge_fill_rect(im, hx - 4, hy - 4, 3, 8, mix(L->skin, ink, 0.5f), 0);       /* dark, plain back of the helm */
        ge_fill_rect(im, hx + r - 1, hy - 1, 5, 4, ink, 0);                        /* pointed front of the helm */
        ge_fill_rect(im, hx + r, hy, 3, 2, L->skin, 0);
        ge_fill_rect(im, hx + 1, hy - 2, r - 1, 3, GE_RGB(255, 140, 60), 0);       /* glowing visor: front half only */
        ge_fill_rect(im, hx + 3, hy - 1, 2, 1, GE_RGB(255, 240, 160), 0);          /* hot core of the visor */
        break;
    case HEAD_PARAGON:
        /* A smooth, featureless oval with one vertical slit of an eye at the front, three thin spines swept
         * back like a crown, and a ring hanging behind. Nothing else in the game has a halo. */
        if (L->halo) {
            for (int i = 0; i < 20; i++) {
                float a0 = (float)i * PI / 10.0f, a1 = (float)(i + 1) * PI / 10.0f;
                ge_draw_line(im, hx - 4 + (int)lroundf(cosf(a0) * 5.0f), hy - 1 + (int)lroundf(sinf(a0) * 6.0f),
                             hx - 4 + (int)lroundf(cosf(a1) * 5.0f), hy - 1 + (int)lroundf(sinf(a1) * 6.0f), 1, L->halo);
            }
        }
        for (int i = 0; i < 3; i++) ge_draw_line(im, hx - 1, hy - r + i * 2, hx - 9 + i, hy - r - 3 + i * 3, 1, L->hair);
        ge_fill_circle(im, hx, hy, r + 1, ink, 0);
        ge_fill_circle(im, hx, hy, r, L->skin, 0);
        ge_fill_rect(im, hx + r - 1, hy - 2, 2, 5, L->skin, 0);                    /* long chin forward */
        ge_fill_rect(im, hx + 2, hy - 3, 2, 5, ink, 0);                            /* the slit */
        ge_fill_rect(im, hx + 2, hy - 2, 1, 3, L->eye ? L->eye : GE_RGB(255, 210, 80), 0);
        ge_fill_rect(im, hx - 3, hy - 2, 2, 4, mix(L->skin, ink, 0.35f), 0);
        break;
    default:
        ge_fill_circle(im, hx, hy, r + 1, ink, 0);
        ge_fill_circle(im, hx, hy, r, L->skin, 0);
        ge_fill_circle(im, hx - 1, hy - 2, r - 1, L->hair, 0);
        ge_fill_rect(im, hx + 2, hy, 1, 2, ink, 0);
        break;
    }
}

static void draw_figure(GeImage *im, int cx, int base, const Look *L, const Pose *P) {
    static const int hip_h[] = { 12, 12, 12, 12, 7, 10, 6, 7 };
    int hipx = cx + (int)lroundf(P->lean * 0.4f), hipy = base - hip_h[P->legs] + (int)P->bob;
    int shx = cx + (int)lroundf(P->lean), shy = hipy - 9;
    int hx = cx + (int)lroundf(P->lean * 1.25f), hy = shy - 4;

    /* cloak behind everything */
    int cl = L->cloak_len;
    ge_draw_line(im, shx - 1, shy, hipx - 6 - cl / 2, hipy + 3 + cl + (int)P->cloak, 4, L->cloak);
    ge_draw_line(im, shx - 2, shy + 1, hipx - 9 - cl, hipy + 1 + cl + (int)(P->cloak * 1.5f), 2, mix(L->cloak, GE_RGB(20, 14, 30), 0.3f));

    float s = sinf(P->phase * 2 * PI), c = cosf(P->phase * 2 * PI);
    switch (P->legs) {   /* back leg */
    case L_WALK:   leg(im, hipx, hipy, 0, 0, cx - (int)(s * 6), base - (int)(fmaxf(0, -c) * 3), L, false, true); break;
    case L_TUCK:   leg(im, hipx, hipy, 0, 0, cx - 2, base - 4, L, false, true); break;
    case L_SPREAD: leg(im, hipx, hipy, 0, 0, cx - 4, base - 1, L, false, true); break;
    case L_CROUCH: leg(im, hipx, hipy, cx - 3, base - 4, cx - 5, base, L, true, true); break;
    case L_KICK:   leg(im, hipx, hipy, 0, 0, cx - 3, base, L, false, true); break;
    case L_SWEEP:  leg(im, hipx, hipy, cx - 3, base - 4, cx - 7, base, L, true, true); break;
    case L_KNEEL:  leg(im, hipx, hipy, cx - 3, base, cx - 9, base, L, true, true); break;
    default:       leg(im, hipx, hipy, 0, 0, cx - 3, base, L, false, true); break;
    }
    int bax, bay;
    pt((float)shx, (float)shy, P->arm_b, 7, &bax, &bay);
    ge_draw_line(im, shx, shy, bax, bay, 2, mix(L->tunic, GE_RGB(20, 14, 30), 0.4f));

    ge_draw_line(im, hipx, hipy, shx, shy, 5, L->tunic);
    ge_draw_line(im, hipx + 2, hipy - 2, shx + 2, shy + 1, 1, mix(L->tunic, GE_RGB(255, 255, 255), 0.28f));   /* lit chest, front side */
    ge_draw_line(im, hipx - 2, hipy - 2, shx - 2, shy + 1, 1, mix(L->tunic, GE_RGB(20, 14, 30), 0.45f));      /* shaded back */
    ge_fill_rect(im, hipx - 2, hipy - 1, 5, 2, GE_RGB(20, 14, 30), 0);
    ge_fill_rect(im, hipx + 1, hipy - 1, 2, 2, GE_RGB(240, 200, 90), 0);                                      /* buckle faces front */

    switch (P->legs) {   /* front leg */
    case L_WALK:   leg(im, hipx, hipy, 0, 0, cx + (int)(s * 6), base - (int)(fmaxf(0, c) * 3), L, false, false); break;
    case L_TUCK:   leg(im, hipx, hipy, 0, 0, cx + 4, base - 6, L, false, false); break;
    case L_SPREAD: leg(im, hipx, hipy, 0, 0, cx + 5, base - 2, L, false, false); break;
    case L_CROUCH: leg(im, hipx, hipy, cx + 6, base - 5, cx + 5, base, L, true, false); break;
    case L_KICK:   leg(im, hipx, hipy, 0, 0, cx + 13, base - 6, L, false, false); break;
    case L_SWEEP:  leg(im, hipx, hipy, 0, 0, cx + 14, base - 1, L, false, false); break;
    case L_KNEEL:  leg(im, hipx, hipy, cx + 5, base - 6, cx + 6, base, L, true, false); break;
    default:       leg(im, hipx, hipy, 0, 0, cx + 3, base, L, false, false); break;
    }

    draw_head(im, hx, hy, L);

    int fax, fay;
    pt((float)shx, (float)shy, P->arm_f, 9, &fax, &fay);
    ge_fill_circle(im, shx, shy, 2, mix(L->tunic, GE_RGB(255, 255, 255), 0.15f), 0);   /* shoulder */
    ge_draw_line(im, shx, shy, fax, fay, 2, L->tunic);
    ge_fill_rect(im, fax - 1, fay - 1, 3, 3, L->skin, 0);
    if (L->weapon_len > 0) {
        int tx, ty, g0x, g0y, g1x, g1y;
        pt((float)fax, (float)fay, P->sword, (float)L->weapon_len, &tx, &ty);
        ge_draw_line(im, fax, fay, tx, ty, L->weapon_w, L->weapon);
        if (L->weapon_w >= 2) ge_draw_line(im, fax, fay, tx, ty, 1, L->weapon_hi);
        pt((float)fax, (float)fay, P->sword + PI * 0.5f, 2.5f, &g0x, &g0y);         /* cross-guard: reads as "sword" */
        pt((float)fax, (float)fay, P->sword - PI * 0.5f, 2.5f, &g1x, &g1y);
        ge_draw_line(im, g0x, g0y, g1x, g1y, 1, mix(L->weapon, GE_RGB(20, 14, 30), 0.35f));
    }
    if (P->arc) {
        float r = (float)L->weapon_len + 10.0f;   /* the arc follows the blade tip */
        int px = 0, py = 0;
        for (int i = 0; i <= 10; i++) {
            float t = (float)i / 10.0f, a = P->arc0 + (P->arc1 - P->arc0) * t;
            int ax, ay;
            pt((float)shx, (float)shy, a, r, &ax, &ay);
            if (i > 0) ge_draw_line(im, px, py, ax, ay, 3, GE_ARGB(60 + (int)(190 * t), 255, 250, 235));
            px = ax; py = ay;
        }
    }
}

static void draw_fallen(GeImage *im, int cx, int base, const Look *L) {
    ge_draw_line(im, cx - 9, base - 2, cx - 4, base - 5, 4, L->cloak);
    ge_draw_line(im, cx - 6, base - 3, cx + 5, base - 4, 5, L->tunic);
    ge_draw_line(im, cx - 6, base - 3, cx - 14, base - 2, 3, L->pants);
    draw_head(im, cx + 9, base - 5, L);
    if (L->weapon_len > 0) ge_draw_line(im, cx - 2, base - 1, cx + 12, base, L->weapon_w, L->weapon);
}

/* Each cell is drawn into a scratch image so clipping to the cell is automatic. */
/* A one-pixel dark rim around everything solid: the single cheapest way to make a small sprite read
 * against a busy background (slash trails, which are see-through, get no rim). */
static void outline(GeImage *im, uint32_t rim) {
    const uint32_t ink = rim ? rim : GE_RGB(14, 10, 22);
    uint8_t *solid = calloc((size_t)(im->w * im->h), 1);
    for (int i = 0; i < im->w * im->h; i++) solid[i] = (im->px[i] >> 24) >= 200;
    for (int y = 0; y < im->h; y++)
        for (int x = 0; x < im->w; x++) {
            int i = y * im->w + x;
            if ((im->px[i] >> 24) != 0) continue;
            bool edge = (x > 0 && solid[i - 1]) || (x + 1 < im->w && solid[i + 1]) || (y > 0 && solid[i - im->w]) || (y + 1 < im->h && solid[i + im->w]);
            if (edge) im->px[i] = ink;
        }
    free(solid);
}

static void put_cell(GeImage *sheet, int cw, int ch, int cell, const Look *L, const Pose *p, bool fallen) {
    GeImage tmp = ge_image_create(cw, ch);
    if (fallen) draw_fallen(&tmp, HUM_BODY_X, ch - 1, L);
    else draw_figure(&tmp, HUM_BODY_X, ch - 1, L, p);
    outline(&tmp, L->rim);
    int cols = sheet->w / cw;
    GeIRect all = { 0, 0, cw, ch };
    ge_blit(sheet, &tmp, all, (cell % cols) * cw, (cell / cols) * ch, 0, GE_WHITE);
    ge_image_free(&tmp);
}

static GeImage humanoid_sheet(const Look *L) {
    GeImage im = ge_image_create(HUM_CELL_W * 8, HUM_CELL_H * 7);
    Pose P[H_COUNT];
    memset(P, 0, sizeof P);
#define POSE(i, ...) P[i] = (Pose){ __VA_ARGS__ }
    POSE(H_IDLE0, .legs = L_STAND, .arm_f = 0.6f, .arm_b = 2.2f, .sword = -0.9f);
    POSE(H_IDLE1, .bob = 1, .legs = L_STAND, .arm_f = 0.65f, .arm_b = 2.3f, .sword = -0.85f, .cloak = 1);
    for (int i = 0; i < 4; i++)
        POSE(H_RUN0 + i, .bob = (float)(i & 1), .lean = 3, .legs = L_WALK, .phase = (float)i * 0.25f,
             .arm_f = 1.0f + 0.4f * sinf((float)i * 1.57f), .arm_b = 2.6f, .sword = 2.8f, .cloak = 3);
    POSE(H_JUMP, .lean = 1, .legs = L_TUCK, .arm_f = -1.2f, .arm_b = 0.3f, .sword = -1.4f, .cloak = 3);
    POSE(H_FALL, .lean = 1, .legs = L_SPREAD, .arm_f = -0.6f, .arm_b = 0.9f, .sword = -0.5f, .cloak = -2);
    /* side slashes */
    POSE(H_S1A, .lean = -2, .legs = L_STAND, .arm_f = -2.3f, .arm_b = 2.4f, .sword = -2.7f);
    POSE(H_S1B, .lean = 3, .legs = L_STAND, .arm_f = 0.0f, .arm_b = 2.0f, .sword = 0.0f, .arc = 1, .arc0 = -2.1f, .arc1 = 0.5f, .cloak = 2);
    POSE(H_S1C, .lean = 2, .legs = L_STAND, .arm_f = 0.4f, .arm_b = 2.0f, .sword = 0.7f);
    POSE(H_S2A, .lean = -1, .legs = L_STAND, .arm_f = 1.5f, .arm_b = 2.4f, .sword = 2.0f);
    POSE(H_S2B, .lean = 3, .legs = L_STAND, .arm_f = -0.3f, .arm_b = 2.0f, .sword = -0.6f, .arc = 1, .arc0 = 1.6f, .arc1 = -1.5f, .cloak = 2);
    POSE(H_S2C, .lean = 2, .legs = L_STAND, .arm_f = -0.8f, .arm_b = 2.0f, .sword = -1.1f);
    /* up / down slashes */
    POSE(H_UPA, .legs = L_STAND, .arm_f = 0.8f, .arm_b = 2.2f, .sword = 1.4f);
    POSE(H_UPB, .legs = L_STAND, .arm_f = -1.5f, .arm_b = 2.0f, .sword = -1.6f, .arc = 1, .arc0 = 0.3f, .arc1 = -2.9f);
    POSE(H_UPC, .legs = L_STAND, .arm_f = -1.9f, .arm_b = 2.0f, .sword = -2.2f);
    POSE(H_DNA, .legs = L_TUCK, .arm_f = -1.0f, .arm_b = 0.4f, .sword = -1.2f, .cloak = -2);
    POSE(H_DNB, .legs = L_TUCK, .arm_f = 1.6f, .arm_b = 0.4f, .sword = 1.57f, .arc = 1, .arc0 = -0.2f, .arc1 = 2.8f, .cloak = -3);
    POSE(H_DNC, .legs = L_SPREAD, .arm_f = 1.2f, .arm_b = 0.4f, .sword = 1.3f, .cloak = -2);
    POSE(H_WALL, .lean = -2, .legs = L_TUCK, .arm_f = -1.8f, .arm_b = -2.4f, .sword = 2.6f, .cloak = -3);
    POSE(H_DASH, .bob = 1, .lean = 6, .legs = L_WALK, .phase = 0.15f, .arm_f = -0.2f, .arm_b = 2.5f, .sword = 2.9f, .cloak = 4);
    POSE(H_HURT, .lean = -4, .legs = L_STAND, .arm_f = -1.6f, .arm_b = -2.0f, .sword = 2.0f, .cloak = -1);
    /* tool throw */
    POSE(H_THA, .lean = -2, .legs = L_STAND, .arm_f = -2.6f, .arm_b = 0.8f, .sword = 2.6f);
    POSE(H_THB, .lean = 3, .legs = L_STAND, .arm_f = 0.1f, .arm_b = 2.3f, .sword = 2.6f, .cloak = 2);
    POSE(H_THC, .lean = 2, .legs = L_STAND, .arm_f = 0.6f, .arm_b = 2.3f, .sword = 2.6f);
    /* spin (silk skill / super) */
    for (int i = 0; i < 4; i++) {
        float a = (float)i * 1.5708f;
        POSE(H_SP0 + i, .legs = L_TUCK, .arm_f = a + 1.2f, .arm_b = a + 2.8f, .sword = a + 1.3f, .arc = 1, .arc0 = a - 0.4f, .arc1 = a + 1.4f,
             .cloak = (float)(i * 2 - 3));
    }
    POSE(H_HEAL0, .legs = L_KNEEL, .lean = 1, .arm_f = 1.2f, .arm_b = 1.8f, .sword = 1.9f);
    POSE(H_HEAL1, .bob = 1, .legs = L_KNEEL, .lean = 1, .arm_f = 1.25f, .arm_b = 1.9f, .sword = 1.9f, .cloak = 1);
    /* fighting game */
    POSE(H_CROUCH, .lean = 2, .legs = L_CROUCH, .arm_f = 0.5f, .arm_b = 2.2f, .sword = -0.5f);
    POSE(H_BLOCK, .lean = -1, .legs = L_STAND, .arm_f = -1.1f, .arm_b = 2.2f, .sword = -1.4f);
    POSE(H_BLOCKLO, .lean = 0, .legs = L_CROUCH, .arm_f = -0.9f, .arm_b = 2.2f, .sword = -1.4f);
    POSE(H_JABA, .lean = -1, .legs = L_STAND, .arm_f = 2.6f, .arm_b = 2.2f, .sword = 0.2f);
    POSE(H_JABB, .lean = 4, .legs = L_STAND, .arm_f = 0.0f, .arm_b = 2.4f, .sword = -0.05f, .cloak = 2);
    POSE(H_HVA, .bob = 1, .lean = -3, .legs = L_STAND, .arm_f = -1.9f, .arm_b = 2.4f, .sword = -2.0f);
    POSE(H_HVB, .lean = 4, .legs = L_STAND, .arm_f = 0.3f, .arm_b = 2.0f, .sword = 0.5f, .arc = 1, .arc0 = -2.2f, .arc1 = 0.7f, .cloak = 3);
    POSE(H_HVC, .bob = 1, .lean = 5, .legs = L_STAND, .arm_f = 0.6f, .arm_b = 2.0f, .sword = 0.9f, .arc = 1, .arc0 = 0.3f, .arc1 = 1.4f, .cloak = 3);
    POSE(H_HVD, .bob = 1, .lean = 3, .legs = L_STAND, .arm_f = 0.9f, .arm_b = 2.0f, .sword = 1.2f);
    POSE(H_KICK, .lean = -3, .legs = L_KICK, .arm_f = -0.8f, .arm_b = 2.6f, .sword = -0.8f, .cloak = -1);
    POSE(H_CSA, .lean = 0, .legs = L_CROUCH, .arm_f = 2.2f, .arm_b = 2.2f, .sword = 2.6f);
    POSE(H_CSB, .lean = 3, .legs = L_CROUCH, .arm_f = 0.5f, .arm_b = 2.2f, .sword = 0.3f, .arc = 1, .arc0 = 2.0f, .arc1 = 0.4f, .cloak = 2);
    POSE(H_SWA, .lean = -2, .legs = L_CROUCH, .arm_f = 2.4f, .arm_b = 1.0f, .sword = 2.8f);
    POSE(H_SWB, .lean = 1, .legs = L_SWEEP, .arm_f = 1.0f, .arm_b = 2.6f, .sword = 1.2f, .cloak = 3);
    POSE(H_RISE, .lean = 1, .legs = L_TUCK, .arm_f = -1.5f, .arm_b = 0.4f, .sword = -1.6f, .arc = 1, .arc0 = 0.8f, .arc1 = -2.2f, .cloak = -3);
    POSE(H_GRAB, .lean = 3, .legs = L_STAND, .arm_f = 0.0f, .arm_b = 0.2f, .sword = 2.6f);
    POSE(H_WIN, .legs = L_STAND, .arm_f = -1.3f, .arm_b = 2.4f, .sword = -1.57f, .cloak = 2);
    POSE(H_HURTAIR, .lean = -5, .legs = L_SPREAD, .arm_f = -2.4f, .arm_b = -2.0f, .sword = 2.4f, .cloak = 3);
    POSE(H_JH, .lean = 3, .legs = L_TUCK, .arm_f = 0.9f, .arm_b = 0.4f, .sword = 0.9f, .arc = 1, .arc0 = -1.2f, .arc1 = 1.1f, .cloak = -2);
    /* the long thrust (K): draw back, then the whole arm and blade straight out */
    POSE(H_TRA, .bob = 1, .lean = -3, .legs = L_SPREAD, .arm_f = 2.9f, .arm_b = 1.0f, .sword = 0.05f, .cloak = -1);
    POSE(H_TRB, .lean = 6, .legs = L_SPREAD, .arm_f = 0.02f, .arm_b = 2.8f, .sword = 0.0f, .cloak = 4);
    POSE(H_TRC, .lean = 4, .legs = L_SPREAD, .arm_f = 0.3f, .arm_b = 2.6f, .sword = 0.25f, .cloak = 2);
    POSE(H_JK, .lean = 4, .legs = L_TUCK, .arm_f = 0.5f, .arm_b = 2.6f, .sword = 0.55f, .arc = 1, .arc0 = -0.4f, .arc1 = 0.9f, .cloak = -3);
#undef POSE
    for (int i = 0; i < H_COUNT; i++) put_cell(&im, HUM_CELL_W, HUM_CELL_H, i, L, &P[i], i == H_DEAD);
    return im;
}

static GeImage upscale2(const GeImage *src) {
    GeImage out = ge_image_create(src->w * 2, src->h * 2);
    for (int y = 0; y < out.h; y++)
        for (int x = 0; x < out.w; x++) out.px[y * out.w + x] = src->px[(y / 2) * src->w + x / 2];
    return out;
}

/* ---- creatures --------------------------------------------------------- */

static void cell_xy(const GeImage *sheet, int cw, int ch, int cell, int *x, int *y) {
    int cols = sheet->w / cw;
    *x = (cell % cols) * cw;
    *y = (cell / cols) * ch;
}

static void crawler_cell(GeImage *im, int cell, int frame, bool hurt, bool dead) {
    int ox, oy;
    cell_xy(im, 24, 16, cell, &ox, &oy);
    uint32_t shell = hurt ? GE_RGB(255, 255, 255) : GE_RGB(70, 110, 150), rim = GE_RGB(20, 14, 30);
    int base = oy + 15, cx = ox + 12;
    if (dead) {
        ge_fill_rect(im, cx - 8, base - 3, 16, 3, rim, 0);
        ge_fill_rect(im, cx - 7, base - 3, 14, 2, GE_RGB(60, 80, 100), 0);
        return;
    }
    for (int i = 0; i < 3; i++) { /* legs */
        int lx = cx - 6 + i * 6, off = ((i + frame) & 1) ? 2 : -1;
        ge_draw_line(im, lx, base - 4, lx + off, base, 1, rim);
    }
    for (int y = 0; y < 8; y++) {  /* dome shell */
        int hw = (int)(9.0f * sqrtf(1.0f - (float)(y * y) / 64.0f));
        ge_fill_rect(im, cx - hw, base - 4 - y, hw * 2, 1, y == 7 ? rim : mix(shell, GE_RGB(150, 200, 230), (float)y / 12.0f), 0);
    }
    ge_fill_rect(im, cx - 9, base - 4, 18, 1, rim, 0);
    ge_fill_rect(im, cx + 6, base - 7, 2, 2, GE_RGB(255, 230, 120), 0); /* eye */
}

static void bat_cell(GeImage *im, int cell, int frame, bool hurt, bool dead) {
    int ox, oy;
    cell_xy(im, 24, 16, cell, &ox, &oy);
    uint32_t body = hurt ? GE_RGB(255, 255, 255) : GE_RGB(110, 70, 130), wing = GE_RGB(60, 36, 80);
    int cx = ox + 12, cy = oy + 9;
    if (dead) { ge_fill_circle(im, cx, oy + 13, 3, body, 0); ge_draw_line(im, cx - 6, oy + 15, cx + 6, oy + 14, 1, wing); return; }
    int wy = frame ? 5 : -5;
    ge_draw_line(im, cx - 2, cy, cx - 10, cy + wy, 2, wing);
    ge_draw_line(im, cx - 10, cy + wy, cx - 6, cy + 2, 1, wing);
    ge_draw_line(im, cx + 2, cy, cx + 10, cy + wy, 2, wing);
    ge_draw_line(im, cx + 10, cy + wy, cx + 6, cy + 2, 1, wing);
    ge_fill_circle(im, cx, cy, 4, body, 0);
    ge_fill_rect(im, cx + 1, cy - 1, 1, 1, GE_RGB(255, 90, 90), 0);
    ge_fill_rect(im, cx + 3, cy - 1, 1, 1, GE_RGB(255, 90, 90), 0);
}

static void spitter_cell(GeImage *im, int cell, int frame, bool hurt, bool dead) {
    int ox, oy;
    cell_xy(im, 24, 24, cell, &ox, &oy);
    int cx = ox + 12, base = oy + 23;
    uint32_t stem = GE_RGB(60, 110, 70), head = hurt ? GE_RGB(255, 255, 255) : GE_RGB(170, 70, 110);
    if (dead) { ge_draw_line(im, cx - 6, base, cx + 6, base - 2, 3, stem); return; }
    int sway = frame == 1 ? 1 : 0;
    ge_draw_line(im, cx, base, cx + sway, base - 10, 3, stem);
    ge_draw_line(im, cx, base - 3, cx - 5, base - 6, 2, stem);
    ge_draw_line(im, cx, base - 5, cx + 5, base - 8, 2, stem);
    ge_fill_circle(im, cx + sway, base - 14, 6, head, 0);
    if (frame == 2) ge_fill_circle(im, cx + sway + 2, base - 15, 3, GE_RGB(30, 10, 20), 0); /* open mouth */
    else ge_fill_rect(im, cx + sway, base - 15, 5, 1, GE_RGB(30, 10, 20), 0);
}

static void fx_cell(GeImage *im, int cell) {
    int ox, oy;
    cell_xy(im, 16, 16, cell, &ox, &oy);
    int cx = ox + 8, cy = oy + 8;
    switch (cell) {
    case FX_NEEDLE0: case FX_NEEDLE1:
        ge_draw_line(im, cx - 6, cy, cx + 6, cy, 1, GE_RGB(230, 236, 255));
        ge_draw_line(im, cx - 7, cy + (cell == FX_NEEDLE1), cx - 12, cy + (cell == FX_NEEDLE1 ? 2 : -1), 1, GE_RGB(220, 60, 70)); /* thread */
        ge_fill_rect(im, cx + 5, cy - 1, 2, 3, GE_RGB(255, 255, 255), 0);
        break;
    case FX_GLOB0: case FX_GLOB1:
        ge_fill_circle(im, cx, cy, cell == FX_GLOB0 ? 4 : 3, GE_RGB(190, 230, 90), 0);
        ge_fill_rect(im, cx - 1, cy - 2, 2, 1, GE_RGB(240, 255, 200), 0);
        break;
    case FX_ROCK:
        ge_fill_circle(im, cx, cy, 4, GE_RGB(20, 14, 30), 0);
        ge_fill_circle(im, cx, cy, 3, GE_RGB(130, 110, 100), 0);
        break;
    case FX_WAVE0: case FX_WAVE1:
        for (int i = 0; i < 4; i++)
            ge_draw_line(im, cx - 4 + i * 2, oy + 15, cx - 2 + i * 2, oy + 15 - (cell == FX_WAVE0 ? 10 : 8) + i, 2, GE_ARGB(220 - i * 40, 255, 170, 90));
        break;
    case FX_ORB0: case FX_ORB1:
        ge_fill_circle(im, cx, cy, cell == FX_ORB0 ? 6 : 5, GE_ARGB(110, 255, 210, 230), 0);
        ge_fill_circle(im, cx, cy, 3, GE_RGB(255, 240, 250), 0);
        break;
    case FX_SPEAR:
        ge_draw_line(im, cx - 7, cy, cx + 5, cy, 2, GE_RGB(110, 80, 60));
        ge_draw_line(im, cx + 4, cy - 2, cx + 7, cy, 1, GE_RGB(220, 220, 230));
        ge_draw_line(im, cx + 4, cy + 2, cx + 7, cy, 1, GE_RGB(220, 220, 230));
        break;
    case FX_DUST:
        for (int i = 0; i < 6; i++) ge_fill_circle(im, cx - 4 + i * 2, cy + (i & 1 ? 2 : -2), 2, GE_ARGB(170, 220, 210, 240), 0);
        break;
    }
}

void art_make_sheets(Sprites *s) {
    memset(s, 0, sizeof *s);
    Look hero = { GE_RGB(242, 236, 228), GE_RGB(40, 30, 40), GE_RGB(200, 40, 60), GE_RGB(150, 30, 50), GE_RGB(60, 40, 60),
                  GE_RGB(40, 30, 40), GE_RGB(220, 226, 240), GE_RGB(255, 255, 255), HEAD_MASK, 4, 17, 2, 0, 0, 0, 0 };
    Look brawler = { GE_RGB(110, 170, 80), GE_RGB(70, 100, 50), GE_RGB(120, 70, 40), GE_RGB(120, 80, 50), GE_RGB(70, 55, 45),
                     GE_RGB(50, 40, 35), GE_RGB(120, 80, 45), GE_RGB(170, 125, 80), HEAD_GOBLIN, 5, 14, 3, 0, 0, 0, 0 };
    Look fencer = { GE_RGB(210, 205, 220), GE_RGB(90, 80, 110), GE_RGB(180, 170, 210), GE_RGB(100, 90, 140), GE_RGB(60, 55, 80),
                    GE_RGB(40, 36, 56), GE_RGB(200, 210, 240), GE_RGB(255, 255, 255), HEAD_MOTH, 4, 21, 1, 0, 0, 0, 0 };
    Look warden = { GE_RGB(70, 66, 80), GE_RGB(180, 50, 40), GE_RGB(90, 30, 30), GE_RGB(60, 58, 72), GE_RGB(40, 38, 50),
                    GE_RGB(30, 28, 36), GE_RGB(150, 140, 130), GE_RGB(210, 200, 190), HEAD_HELM, 5, 17, 3, 0, 0, 0, 0 };
    /* the Paragon: the hero's own silhouette, drained of colour, with a gold eye. The same shape is the point. */
    /* The Paragon: a porcelain mask with one gold slit, a halo, a body the colour of the space between stars,
     * a cloak that trails to the floor, and the longest blade in the game. */
    Look paragon = { GE_RGB(226, 218, 200), GE_RGB(200, 170, 90), GE_RGB(26, 20, 44), GE_RGB(44, 36, 72), GE_RGB(24, 20, 38),
                     GE_RGB(16, 12, 26), GE_RGB(255, 228, 150), GE_RGB(255, 255, 240), HEAD_PARAGON, 4, 21, 1,
                     GE_RGB(255, 200, 60), GE_RGB(240, 196, 90), 5, GE_RGB(110, 86, 44) };
    s->hero.sheet = humanoid_sheet(&hero);
    s->paragon.sheet = humanoid_sheet(&paragon);
    s->brawler.sheet = humanoid_sheet(&brawler);
    s->fencer.sheet = humanoid_sheet(&fencer);
    GeImage w = humanoid_sheet(&warden);
    s->warden.sheet = upscale2(&w);   /* the boss is the same puppet at double size */
    ge_image_free(&w);

    s->crawler.sheet = ge_image_create(96, 16);
    crawler_cell(&s->crawler.sheet, 0, 0, false, false);
    crawler_cell(&s->crawler.sheet, 1, 1, false, false);
    crawler_cell(&s->crawler.sheet, 2, 0, true, false);
    crawler_cell(&s->crawler.sheet, 3, 0, false, true);
    s->bat.sheet = ge_image_create(96, 16);
    bat_cell(&s->bat.sheet, 0, 0, false, false);
    bat_cell(&s->bat.sheet, 1, 1, false, false);
    bat_cell(&s->bat.sheet, 2, 0, true, false);
    bat_cell(&s->bat.sheet, 3, 0, false, true);
    s->spitter.sheet = ge_image_create(120, 24);
    spitter_cell(&s->spitter.sheet, 0, 0, false, false);
    spitter_cell(&s->spitter.sheet, 1, 1, false, false);
    spitter_cell(&s->spitter.sheet, 2, 2, false, false);
    spitter_cell(&s->spitter.sheet, 3, 0, true, false);
    spitter_cell(&s->spitter.sheet, 4, 0, false, true);
    s->fx.sheet = ge_image_create(176, 16);
    for (int i = 0; i <= FX_DUST; i++) fx_cell(&s->fx.sheet, i);
}

void art_free(Sprites *s) {
    GeSprite *all[] = { &s->hero, &s->paragon, &s->brawler, &s->fencer, &s->warden, &s->crawler, &s->bat, &s->spitter, &s->fx };
    for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) ge_sprite_free(all[i]);
}

/* ---- tiles ------------------------------------------------------------- */

void art_make_tileset(GeImage *tiles) {
    *tiles = ge_image_create(GE_TILE * TV_COUNT, GE_TILE);
    GeRng r;
    ge_rng_seed(&r, 7);
    const uint32_t d0 = GE_RGB(86, 58, 56), d1 = GE_RGB(100, 68, 62), d2 = GE_RGB(64, 44, 48);
    const uint32_t s0 = GE_RGB(70, 66, 92), s1 = GE_RGB(84, 80, 108), s2 = GE_RGB(52, 48, 72);
    for (int t = 0; t < TV_FIRST_PUZZLE; t++)
        for (int y = 0; y < GE_TILE; y++)
            for (int x = 0; x < GE_TILE; x++) {
                float n = ge_rng_float(&r);
                uint32_t c = n < 0.18f ? d2 : (n > 0.78f ? d1 : d0);
                if (t == TV_STONE || t == TV_STONE_TOP) {
                    c = n < 0.15f ? s2 : (n > 0.8f ? s1 : s0);
                    if (y % 8 == 7 || (x + (y / 8) * 8) % 16 == 0) c = s2;   /* brick joints */
                }
                if (t == TV_PLANK || t == TV_SPIKE) c = 0;
                tiles->px[y * tiles->w + t * GE_TILE + x] = c;
            }
    for (int x = 0; x < GE_TILE; x++) {   /* grass tops */
        int blade = 3 + (int)(ge_rng_float(&r) * 3.0f);
        for (int y = 0; y < blade; y++)
            tiles->px[y * tiles->w + TV_GRASS * GE_TILE + x] = y == 0 ? GE_RGB(120, 190, 100) : mix(GE_RGB(78, 140, 80), GE_RGB(48, 92, 66), (float)y / blade);
        tiles->px[TV_STONE_TOP * GE_TILE + x] = GE_RGB(120, 116, 140);
        tiles->px[tiles->w + TV_STONE_TOP * GE_TILE + x] = GE_RGB(98, 94, 120);
    }
    for (int i = 0; i < 4; i++) {
        int sx = (int)(ge_rng_float(&r) * 12), sy = 2 + (int)(ge_rng_float(&r) * 11);
        ge_fill_rect(tiles, TV_DIRT * GE_TILE + sx, sy, 3, 2, GE_RGB(120, 100, 104), 0);
    }
    ge_draw_line(tiles, TV_DIRT2 * GE_TILE + 2, 0, TV_DIRT2 * GE_TILE + 8, 9, 1, GE_RGB(60, 90, 60));
    for (int x = 0; x < GE_TILE; x++)     /* one-way plank, top 6px */
        for (int y = 0; y < 6; y++) {
            uint32_t c = y == 0 ? GE_RGB(190, 140, 90) : (y == 5 ? GE_RGB(70, 46, 40) : GE_RGB(150, 104, 66));
            if (y > 0 && y < 5 && (x == 0 || x == 8)) c = GE_RGB(100, 68, 50);
            tiles->px[y * tiles->w + TV_PLANK * GE_TILE + x] = c;
        }
    for (int k = 0; k < 4; k++)           /* spikes */
        for (int y = 0; y < 12; y++) {
            int hw = (y + 1) / 4, x0 = TV_SPIKE * GE_TILE + k * 4 + 2 - hw;
            ge_fill_rect(tiles, x0, 4 + y, hw * 2 + 1, 1, y < 2 ? GE_RGB(240, 240, 255) : mix(GE_RGB(190, 190, 210), GE_RGB(80, 76, 100), (float)y / 12), 0);
        }
    art_puzzle_tiles(tiles);
}

/* The puzzle cells (placeholder art): drawn after the others with their own random numbers, so adding
 * them left every older cell exactly as it was. */
void art_puzzle_tiles(GeImage *tiles) {
    GeRng r;
    ge_rng_seed(&r, 31);
    const uint32_t s0 = GE_RGB(70, 66, 92), s1 = GE_RGB(84, 80, 108), s2 = GE_RGB(52, 48, 72);
    int cx = TV_CRACK * GE_TILE, gx = TV_GATE * GE_TILE, bx = TV_CRATE * GE_TILE, lx = TV_LOCK * GE_TILE;
    for (int y = 0; y < GE_TILE; y++)
        for (int x = 0; x < GE_TILE; x++) {
            float n = ge_rng_float(&r);
            uint32_t c = n < 0.15f ? s2 : (n > 0.8f ? s1 : s0);              /* cracked stone: stone, lighter, */
            if (y % 8 == 7 || (x + (y / 8) * 8) % 16 == 0) c = s2;
            tiles->px[y * tiles->w + cx + x] = mix(c, GE_RGB(120, 110, 130), 0.25f);
            tiles->px[y * tiles->w + gx + x] = 0;                            /* gate: see-through between bars */
            uint32_t wood = (x + y) % 5 == 0 ? GE_RGB(132, 90, 56) : GE_RGB(150, 104, 66);   /* crate */
            if (x == 0 || y == 0 || x == GE_TILE - 1 || y == GE_TILE - 1) wood = GE_RGB(70, 46, 40);
            else if (x == y || x == GE_TILE - 1 - y) wood = GE_RGB(100, 68, 50);
            tiles->px[y * tiles->w + bx + x] = wood;
            uint32_t door = (x % 5 == 0) ? GE_RGB(54, 38, 34) : GE_RGB(80, 56, 46);       /* lock: heavy planks, */
            if (y == 2 || y == 13) door = GE_RGB(110, 108, 124);                           /* iron bands */
            tiles->px[y * tiles->w + lx + x] = door;
        }
    static const int crack[][2] = { { 3, 0 }, { 4, 3 }, { 7, 5 }, { 6, 8 }, { 9, 11 }, { 8, 15 } };
    for (int i = 0; i + 1 < 6; i++)
        ge_draw_line(tiles, cx + crack[i][0], crack[i][1], cx + crack[i + 1][0], crack[i + 1][1], 1, GE_RGB(24, 20, 34));
    ge_draw_line(tiles, cx + 7, 5, cx + 12, 4, 1, GE_RGB(24, 20, 34));
    for (int k = 0; k < 3; k++)                                                    /* gate bars */
        for (int y = 0; y < GE_TILE; y++) {
            tiles->px[y * tiles->w + gx + 2 + k * 5] = GE_RGB(46, 44, 58);
            tiles->px[y * tiles->w + gx + 3 + k * 5] = GE_RGB(120, 118, 140);
        }
    for (int x = 0; x < GE_TILE; x++) tiles->px[4 * tiles->w + gx + x] = tiles->px[12 * tiles->w + gx + x] = GE_RGB(80, 78, 96);
    ge_fill_rect(tiles, lx + 6, 6, 4, 4, GE_RGB(220, 180, 80), 0);               /* the keyhole plate */
    ge_fill_rect(tiles, lx + 7, 7, 2, 3, GE_RGB(20, 14, 20), 0);
}

void art_draw_bench(GeImage *fb, int x, int y) {
    const uint32_t wood = GE_RGB(150, 104, 66), dark = GE_RGB(70, 46, 40);
    ge_fill_rect(fb, x - 14, y - 9, 28, 3, wood, 0);
    ge_fill_rect(fb, x - 14, y - 6, 28, 1, dark, 0);
    ge_fill_rect(fb, x - 12, y - 6, 2, 6, dark, 0);
    ge_fill_rect(fb, x + 10, y - 6, 2, 6, dark, 0);
    ge_fill_rect(fb, x - 14, y - 17, 28, 2, wood, 0);
    ge_fill_rect(fb, x - 12, y - 15, 2, 6, dark, 0);
    ge_fill_rect(fb, x + 10, y - 15, 2, 6, dark, 0);
    ge_fill_circle(fb, x, y - 22, 4, GE_ARGB(60, 255, 220, 150), GE_ADDITIVE); /* lantern glow */
    ge_fill_rect(fb, x - 1, y - 24, 3, 4, GE_RGB(255, 220, 140), 0);
}
