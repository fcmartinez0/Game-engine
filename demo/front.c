/* The title, the pause menu, the story and the credits (front.h). */

#include "front.h"

#include "ge/ge_font.h"
#include "ge/ge_math.h"

#include <stdio.h>
#include <string.h>

static const uint32_t INK = GE_RGB(20, 14, 30);
#define GOLD GE_RGB(255, 214, 120)
#define SOFT GE_RGB(220, 210, 240)
#define DIM GE_RGB(150, 140, 180)

static void centred(GeImage *fb, int y, const char *s, uint32_t c, int scale) {
    ge_text_outlined(fb, (fb->w - ge_text_width(s, scale)) / 2, y, s, c, INK, scale);
}

/* ---- the menus ------------------------------------------------------------------ */

void front_menu_open(FrontMenu *m, const FrontItem *items, int n) {
    memset(m, 0, sizeof *m);
    m->n = n > 6 ? 6 : n;
    for (int i = 0; i < m->n; i++) m->items[i] = (uint8_t)items[i];
    m->confirm = -1;
    m->dirty = true;
}

static bool is_setting(int item) { return item == FI_DIFFICULTY; }

static int choose(FrontMenu *m, int row, bool (*needs_confirm)(int item)) {
    int item = m->items[row];
    if (needs_confirm && needs_confirm(item) && m->confirm != row) { m->confirm = row; m->confirm_t = 180; m->dirty = true; return -1; }
    m->confirm = -1;
    m->dirty = true;
    return item;
}

int front_menu_input(FrontMenu *m, const GeInput *in, bool (*needs_confirm)(int item), int *step) {
    *step = 0;
    m->t++;
    if (m->confirm_t > 0 && --m->confirm_t == 0) { m->confirm = -1; m->dirty = true; }
    if (m->t < 8 || m->n == 0) return -1;                  /* (the press that opened it isn't a choice) */
    int was = m->sel;
    if (in->pressed & GE_BTN_UP) m->sel = (m->sel + m->n - 1) % m->n;
    if (in->pressed & GE_BTN_DOWN) m->sel = (m->sel + 1) % m->n;
    if (m->sel != was) { m->confirm = -1; m->dirty = true; return -1; }
    int item = m->items[m->sel];
    if (is_setting(item) && (in->pressed & (GE_BTN_LEFT | GE_BTN_RIGHT))) {
        *step = (in->pressed & GE_BTN_RIGHT) ? 1 : -1;
        m->dirty = true;
        return item;
    }
    if (in->pressed & (GE_BTN_A | GE_BTN_B | GE_BTN_START)) {
        if (is_setting(item)) { *step = 1; m->dirty = true; return item; }
        return choose(m, m->sel, needs_confirm);
    }
    return -1;
}

/* the bottom screen's buttons */
#define BTN_X 24
#define BTN_W 272
#define BTN_H 34
#define BTN_Y0 44
#define BTN_GAP 42

int front_menu_touch(FrontMenu *m, int x, int y, bool (*needs_confirm)(int item)) {
    if (m->t < 8) return -1;
    for (int i = 0; i < m->n; i++) {
        int by = BTN_Y0 + i * BTN_GAP;
        if (x < BTN_X || x >= BTN_X + BTN_W || y < by || y >= by + BTN_H) continue;
        if (m->sel != i) m->confirm = -1;
        m->sel = i;
        m->dirty = true;
        return choose(m, i, needs_confirm);   /* (a setting row cycles: main.c steps it) */
    }
    return -1;
}

static const char *item_label(const FrontMenu *m, int row, const char *difficulty, char *buf, size_t n) {
    bool asking = m->confirm == row;
    switch (m->items[row]) {
    case FI_CONTINUE:   return "CONTINUE";
    case FI_NEW:        return asking ? "NEW GAME: AGAIN TO ERASE YOUR SAVE" : "NEW GAME";
    case FI_DIFFICULTY: snprintf(buf, n, "DIFFICULTY: < %s >", difficulty); return buf;
    case FI_QUIT:       return "QUIT";
    case FI_RESUME:     return "RESUME";
    case FI_EDITOR:     return "WORLD EDITOR";
    case FI_TITLE:      return "SAVE AND QUIT TO TITLE";
    default:            return "?";
    }
}

static void draw_items(GeImage *fb, const FrontMenu *m, const char *difficulty, int y0, int gap, uint32_t tick) {
    char buf[48];
    for (int i = 0; i < m->n; i++) {
        const char *s = item_label(m, i, difficulty, buf, sizeof buf);
        bool sel = i == m->sel;
        int scale = strlen(s) > 22 ? 1 : 2, w = ge_text_width(s, scale), x = (fb->w - w) / 2, y = y0 + i * gap + (scale == 1 ? 4 : 0);
        uint32_t c = m->confirm == i ? GE_RGB(255, 150, 120) : sel ? GOLD : SOFT;
        ge_text_outlined(fb, x, y, s, c, INK, scale);
        if (sel) {                                       /* a needle points at the choice */
            int nx = x - 22 + (int)((tick / 8) % 4 == 3);
            ge_fill_rect(fb, nx, y + 6, 14, 2, GE_RGB(230, 236, 255), 0);
            ge_fill_rect(fb, nx + 14, y + 5, 2, 4, GE_RGB(230, 236, 255), 0);
            ge_fill_rect(fb, nx - 2, y + 5, 3, 4, GE_RGB(220, 60, 70), 0);
        }
    }
}

void front_draw_title(GeImage *fb, const FrontMenu *m, const char *difficulty, const char *note, uint32_t tick) {
    for (int y = 0; y < fb->h; y++) {                    /* darker at the top, where the name is */
        int a = y < 100 ? 190 - y : 90;
        ge_fill_rect(fb, 0, y, fb->w, 1, GE_ARGB(a, 8, 4, 18), 0);
    }
    int fade = m->t < 40 ? m->t * 255 / 40 : 255;        /* the name comes up out of the dark */
    const char *name = "FRAN'S DEMO";
    int w = ge_text_width(name, 4), x = (fb->w - w) / 2;
    ge_text_outlined(fb, x, 30, name, GE_ARGB(fade, 255, 214, 120), GE_ARGB(fade, 20, 14, 30), 4);
    ge_fill_rect(fb, x, 70, w, 1, GE_ARGB(fade * 3 / 4, 190, 120, 255), 0);   /* a thread under it, with a needle's eye */
    ge_fill_rect(fb, fb->w / 2 - 2, 68, 5, 5, GE_ARGB(fade, 230, 236, 255), 0);
    ge_fill_rect(fb, fb->w / 2 - 1, 69, 3, 3, GE_ARGB(fade, 30, 20, 50), 0);
    if (m->t < 8) return;                                /* (the choices show from the moment they can be chosen) */
    draw_items(fb, m, difficulty, 104, 22, tick);
    if (note && note[0]) ge_text_outlined(fb, (fb->w - ge_text_width(note, 1)) / 2, 196, note, GE_RGB(255, 150, 120), INK, 1);
    const char *hint = "UP / DOWN: CHOOSE    A OR START: OK";
    ge_text(fb, (fb->w - ge_text_width(hint, 1)) / 2, 222, hint, DIM, 1);
    ge_text(fb, 6, 230, "V1.0", GE_ARGB(150, 150, 140, 180), 1);
}

void front_draw_pause(GeImage *fb, const FrontMenu *m, const char *difficulty, const char *where) {
    ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(170, 8, 4, 18), 0);
    centred(fb, 34, "PAUSED", GOLD, 3);
    if (where && where[0]) centred(fb, 64, where, DIM, 1);
    draw_items(fb, m, difficulty, 92, 24, 0);
    const char *hint = "START: BACK TO THE GAME";
    ge_text(fb, (fb->w - ge_text_width(hint, 1)) / 2, 222, hint, DIM, 1);
}

void front_draw_menu_bottom(GeImage *fb, const FrontMenu *m, const char *difficulty, bool title) {
    ge_clear(fb, GE_RGB(16, 10, 30));
    ge_fill_rect(fb, 0, 0, fb->w, 30, GE_RGB(34, 22, 58), 0);
    ge_fill_rect(fb, 0, 30, fb->w, 1, GE_RGB(190, 120, 255), 0);
    const char *head = title ? "FRAN'S DEMO" : "PAUSED";
    ge_text_outlined(fb, (fb->w - ge_text_width(head, 2)) / 2, 8, head, GOLD, INK, 2);
    char buf[48];
    for (int i = 0; i < m->n; i++) {
        int by = BTN_Y0 + i * BTN_GAP;
        bool sel = i == m->sel;
        ge_fill_rect(fb, BTN_X, by, BTN_W, BTN_H, m->confirm == i ? GE_RGB(110, 40, 50) : sel ? GE_RGB(70, 46, 110) : GE_RGB(36, 24, 60), 0);
        ge_fill_rect(fb, BTN_X, by + BTN_H - 2, BTN_W, 2, GE_RGB(20, 12, 36), 0);
        const char *s = item_label(m, i, difficulty, buf, sizeof buf);
        int scale = ge_text_width(s, 2) <= BTN_W - 12 ? 2 : 1;
        ge_text_outlined(fb, BTN_X + (BTN_W - ge_text_width(s, scale)) / 2, by + (BTN_H - 7 * scale) / 2, s, sel ? GOLD : SOFT, INK, scale);
    }
    ge_text(fb, (fb->w - ge_text_width("TAP A BUTTON", 1)) / 2, fb->h - 12, "TAP A BUTTON", DIM, 1);
}

/* ---- the story ------------------------------------------------------------------ */

/* a note from Fran (there is no story yet); the last line is drawn in gold */
static const char *const STORY[] = {
    "I DON'T HAVE A STORY YET,",
    "SO NOT SURE WHAT TO WRITE HERE.",
    "",
    "THERE'S A CAT BOSS IN HERE SOMEWHERE.",
    "",
    "IF YOU CAN BEAT PARAGON, THAT WOULD BE COOL.",
    "",
    "- FRAN",
};
#define STORY_LINES ((int)(sizeof STORY / sizeof STORY[0]))
#define STORY_STEP 60

void front_draw_story(GeImage *fb, int t) {
    ge_clear(fb, GE_RGB(6, 3, 12));
    for (int i = 0; i < 18; i++) {                       /* a few fireflies drifting */
        int fx = (i * 97 + t / 3 + (i % 3) * t / 7) % fb->w, fy = 30 + (i * 53 + (t / 5) * (i % 2 ? 1 : -1) + 400) % 180;
        int a = 60 + ((t + i * 20) / 6) % 80;
        ge_fill_rect(fb, fx, fy, 2, 2, GE_ARGB(a, 190, 255, 140), 0);
    }
    int top = (fb->h - STORY_LINES * 16) / 2 - 8;
    for (int i = 0; i < STORY_LINES; i++) {
        int since = t - 20 - i * STORY_STEP;
        if (since < 0 || !STORY[i][0]) continue;
        int a = since < 30 ? since * 255 / 30 : 255;
        bool last = i == STORY_LINES - 1;
        ge_text_outlined(fb, (fb->w - ge_text_width(STORY[i], 1)) / 2, top + i * 16, STORY[i],
                         last ? GE_ARGB(a, 255, 214, 120) : GE_ARGB(a, 230, 222, 250), GE_ARGB(a, 20, 14, 30), 1);
    }
    if (t > 60 && t < FRONT_STORY_TICKS - 40) ge_text(fb, fb->w - 6 - ge_text_width("A: SKIP", 1), fb->h - 12, "A: SKIP", DIM, 1);
    int out = FRONT_STORY_TICKS - t;                     /* fades to the forest at the end */
    if (out < 40) ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(ge_clampi((40 - out) * 255 / 40, 0, 255), 0, 0, 0), 0);
}

/* ---- the credits ------------------------------------------------------------------ */

typedef struct { const char *text; int scale; uint32_t color; } Line;

static void hms(uint32_t ticks, char *out, size_t n) {
    unsigned s = ticks / 60, h = s / 3600, m = (s / 60) % 60;
    if (h) snprintf(out, n, "%u:%02u:%02u", h, m, s % 60);
    else snprintf(out, n, "%u:%02u", m, s % 60);
}

void front_draw_credits(GeImage *fb, const FrontStats *st, int t) {
    ge_clear(fb, GE_RGB(8, 4, 16));
    char time[24], l_time[48], l_deaths[48], l_duels[48], l_kills[48], l_relics[48], l_paragon[64];
    hms(st->play_ticks, time, sizeof time);
    snprintf(l_time, sizeof l_time, "TIME PLAYED  %s", time);
    snprintf(l_deaths, sizeof l_deaths, "TIMES FALLEN  %d", st->deaths);
    snprintf(l_duels, sizeof l_duels, "DUELS WON  %d", st->duels_won);
    snprintf(l_kills, sizeof l_kills, "CREATURES DEFEATED  %d", st->kills);
    snprintf(l_relics, sizeof l_relics, "RELICS FOUND  %d / %d", st->relics_found, st->relics);
    snprintf(l_paragon, sizeof l_paragon, "THE PARAGON KNOWS %d%% OF YOU", st->paragon_level);
    const uint32_t hd = GE_RGB(190, 150, 255), tx = GE_RGB(236, 230, 250), gold = GE_RGB(255, 214, 120);
    const Line L[] = {
        { "FRAN'S DEMO", 3, gold }, { "", 1, 0 }, { "THE END OF THE DEMO", 1, tx }, { "", 1, 0 }, { "", 1, 0 },
        { "STARRING", 1, hd }, { "MIKOSHI", 2, tx }, { "AVERY THE CAT", 1, tx }, { "THE PARAGON", 1, tx },
        { "GROBB   SILKE   MOSSHIDE   THE CHAMPION", 1, tx }, { "", 1, 0 },
        { "THE OPENING PICTURE", 1, hd }, { "A CAT IN A CHICKEN COSTUME", 1, tx }, { "", 1, 0 },
        { "A GAME BY", 1, hd }, { "FRAN", 2, tx }, { "", 1, 0 },
        { "MADE WITH GE, A SMALL HOMEMADE ENGINE,", 1, tx }, { "FOR NINTENDO 3DS, WINDOWS, MAC AND LINUX", 1, tx }, { "", 1, 0 }, { "", 1, 0 },
        { "YOUR JOURNEY", 1, hd }, { l_time, 1, tx }, { l_deaths, 1, tx }, { l_duels, 1, tx }, { l_kills, 1, tx },
        { l_relics, 1, tx }, { l_paragon, 1, tx }, { "", 1, 0 }, { "", 1, 0 },
        { "IT WILL REMEMBER YOU.", 1, gold },
    };
    const int n = (int)(sizeof L / sizeof L[0]);
    int height = 0;
    for (int i = 0; i < n; i++) height += 8 * L[i].scale + 6;
    /* roll from below the screen until the last line has gone past the middle */
    int travel = fb->h + height - fb->h / 2, y = fb->h - (int)((long)t * travel / FRONT_CREDITS_ROLL);
    for (int i = 0; i < n; i++) {
        int h = 8 * L[i].scale + 6;
        if (L[i].text[0] && y > -h && y < fb->h) centred(fb, y, L[i].text, L[i].color, L[i].scale);
        y += h;
    }
    if (t > FRONT_CREDITS_ROLL - 200) {                  /* the last words stay */
        int a = ge_clampi((t - (FRONT_CREDITS_ROLL - 200)) * 255 / 120, 0, 255);
        const char *thanks = "THANK YOU FOR PLAYING";
        ge_text_outlined(fb, (fb->w - ge_text_width(thanks, 2)) / 2, fb->h - 70, thanks, GE_ARGB(a, 255, 236, 200), GE_ARGB(a, 20, 14, 30), 2);
    }
    if (t > FRONT_CREDITS_ROLL && (t / 24) % 2 == 0) centred(fb, fb->h - 30, "PRESS A", GE_WHITE, 1);
    else if (t > 60 && t <= FRONT_CREDITS_ROLL) ge_text(fb, fb->w - 6 - ge_text_width("START: SKIP", 1), fb->h - 12, "START: SKIP", DIM, 1);
}
