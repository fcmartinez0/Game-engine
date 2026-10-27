/* The puzzle pieces of the room being played. See puzzle.h. */

#include "puzzle.h"
#include "demo.h"

#include <math.h>
#include <string.h>

#define T GE_TILE

static const uint32_t WIRE_COLORS[10] = {
    GE_RGB(150, 150, 160), GE_RGB(255, 90, 90), GE_RGB(90, 170, 255), GE_RGB(110, 230, 120), GE_RGB(255, 220, 80),
    GE_RGB(200, 120, 255), GE_RGB(255, 160, 60), GE_RGB(90, 230, 230), GE_RGB(255, 130, 200), GE_RGB(240, 240, 240),
};

uint32_t puzzle_wire_color(int ch) { return WIRE_COLORS[ch >= 0 && ch <= 9 ? ch : 0]; }

/* ---- cells ------------------------------------------------------------------ */

static void set_cell(Puzzle *pz, int col, int row, uint8_t type, uint8_t vis) {
    GeTilemap *m = &pz->w->map;
    if (col < 0 || row < 0 || col >= m->w || row >= m->h) return;
    m->type[row * m->w + col] = type;
    m->vis[row * m->w + col] = vis;
}

static int cell_type(const Puzzle *pz, int col, int row) { return ge_tilemap_get(&pz->w->map, col, row); }

static GeRect cell_rect(int col, int row) { return (GeRect){ (float)(col * T), (float)(row * T), (float)T, (float)T }; }

static GeEntity *hero(const Puzzle *pz) { return &pz->w->ents[pz->hero]; }

static GeRect body(const GeEntity *e) {
    return (GeRect){ e->pos.x - e->def->body_w * 0.5f, e->pos.y - e->def->body_h, e->def->body_w, e->def->body_h };
}

static bool hero_ok(const Puzzle *pz) { return hero(pz)->active && hero(pz)->state != GE_ST_DEAD; }

/* Something that a closing gate or a falling crate must not crush: the hero, an enemy, a crate. */
static bool cell_occupied(const Puzzle *pz, int col, int row, int except) {
    GeRect c = cell_rect(col, row);
    c.x += 1; c.y += 1; c.w -= 2; c.h -= 2;
    for (int i = 0; i < GE_MAX_ENTS; i++) {
        const GeEntity *e = &pz->w->ents[i];
        if (e->active && e->def && e->state != GE_ST_DEAD && ge_rect_overlap(body(e), c)) return true;
    }
    for (int i = 0; i < pz->room->nthings; i++)
        if (i != except && pz->room->things[i].kind == TH_CRATE && pz->p[i].col == col && pz->p[i].row == row) return true;
    return false;
}

/* Nothing solid between two points, other than the target cell and breakable stone. Slashes reach through
 * thin walls, so without this a lever behind a wall could be hit from the wrong side. */
static bool clear_view(const Puzzle *pz, float x0, float y0, float x1, float y1, int tcol, int trow) {
    for (int i = 1; i < 16; i++) {
        float t = (float)i / 16.0f;
        int tx = (int)floorf((x0 + (x1 - x0) * t) / T), ty = (int)floorf((y0 + (y1 - y0) * t) / T);
        if (tx == tcol && ty == trow) continue;
        if (cell_type(pz, tx, ty) != GE_TILE_SOLID) continue;
        char c = room_tile(pz->room, tx, ty);
        if (c == 'C' || c == 'H') continue;
        return false;
    }
    return true;
}

static void push_event(Puzzle *pz, PuzzleEventKind k, int thing) {
    if (pz->nev < (int)(sizeof pz->ev / sizeof pz->ev[0])) pz->ev[pz->nev++] = (PuzzleEvent){ k, thing };
}

static void sparks(Puzzle *pz, float x, float y, uint32_t c, int n) {
    for (int k = 0; k < n; k++) {
        float vy = ge_rng_range(&pz->w->rng, -2.0f, 0.5f), vx = ge_rng_range(&pz->w->rng, -1.5f, 1.5f);
        ge_world_emit(pz->w, x, y, vx, vy, 0.08f, 24, c, 2, GE_P_ADD);
    }
}

/* the hero's slash this tick, if it can reach the rect (and whether it is a down-slash: those bounce) */
static bool slash_hits(const Puzzle *pz, GeRect target, int tcol, int trow, bool *down) {
    if (!hero_ok(pz)) return false;
    const GeEntity *h = hero(pz);
    GeRect hb;
    if (!ge_entity_hitbox(h, &hb) || !ge_rect_overlap(hb, target)) return false;
    if (!clear_view(pz, h->pos.x, h->pos.y - h->def->body_h * 0.5f, target.x + target.w * 0.5f, target.y + target.h * 0.5f, tcol, trow)) return false;
    if (down) *down = h->anim.clip == h->def->clip_attack_down;
    return true;
}

static float pogo_speed(const GeEntity *h) { return h->anim.spr->clips[h->anim.clip].pogo > 0 ? h->anim.spr->clips[h->anim.clip].pogo : 5.0f; }

/* a needle of the hero's passing through the rect this tick */
static bool needle_hits(const Puzzle *pz, GeRect target) {
    for (int i = 0; i < GE_MAX_SHOTS; i++) {
        const GeShot *s = &pz->w->shots[i];
        if (!s->active || s->team != 0) continue;
        GeRect r = { s->pos.x - s->def->w * 0.5f, s->pos.y - s->def->h * 0.5f, s->def->w, s->def->h };
        if (ge_rect_overlap(r, target)) return true;
    }
    return false;
}

/* ---- gates ------------------------------------------------------------------ */

static int gate_top(const Thing *t) { return t->row - t->h + 1; }

static void restore_cell(Puzzle *pz, int col, int row) {   /* back to the room as authored */
    uint8_t type, vis;
    room_cell(pz->room, col, row, &type, &vis);
    set_cell(pz, col, row, type, vis);
}

static void gate_apply(Puzzle *pz, int i) {
    const Thing *t = &pz->room->things[i];
    const Piece *g = &pz->p[i];
    for (int r = 0; r < t->h; r++) {
        bool open = r >= t->h - g->open_rows;                 /* the bottom open_rows rows are up out of the way */
        uint8_t type = t->plank ? GE_TILE_ONEWAY : GE_TILE_SOLID;
        uint8_t vis = t->plank ? TV_PLANK : (t->lock && !pz->prog->done[i]) ? TV_LOCK : TV_GATE;
        for (int c = 0; c < t->w; c++) {
            if (open) restore_cell(pz, t->col + c, gate_top(t) + r);
            else set_cell(pz, t->col + c, gate_top(t) + r, type, vis);
        }
    }
}

static bool wire_live(const Puzzle *pz, int ch, int need) {
    if (ch <= 0 || ch > 9) return false;
    if (need == 0) return pz->wire_inputs[ch] > 0 && pz->wire_on[ch] == pz->wire_inputs[ch];   /* ALL */
    return pz->wire_on[ch] >= need;                                                            /* ANY (or N) */
}

static bool gate_wants_open(const Puzzle *pz, int i) {
    const Thing *t = &pz->room->things[i];
    if (t->lock || pz->prog->done[i]) return t->lock ? pz->prog->done[i] : true;   /* unlocked, or latched open */
    bool on = wire_live(pz, t->ch, t->need);
    return t->invert ? !on : on;
}

/* is any of the rect in the camera's view? */
static bool in_view(const Puzzle *pz, GeRect r) {
    const GeCamera *c = &pz->w->cam;
    return r.x + r.w > c->x && r.x < c->x + c->vw && r.y + r.h > c->y && r.y < c->y + c->vh;
}

static void gate_update(Puzzle *pz, int i) {
    const Thing *t = &pz->room->things[i];
    Piece *g = &pz->p[i];
    bool want = gate_wants_open(pz, i);
    if (want != g->want) {                                                             /* its wire changed */
        g->want = want;
        GeRect r = { (float)(t->col * T), (float)(gate_top(t) * T), (float)(t->w * T), (float)(t->h * T) };
        if (!in_view(pz, r)) g->alert = 90;                                            /* out of sight: point at it */
    }
    if (g->alert > 0) g->alert--;
    if (want && g->open_rows < t->h) {
        if (--g->step <= 0) {                                                          /* opens quickly */
            g->open_rows++; g->step = 2; gate_apply(pz, i);
            if (g->open_rows == t->h) push_event(pz, PZ_GATE, i);
        }
    } else if (!want && g->open_rows > 0) {
        int y = gate_top(t) + (t->h - g->open_rows);                                   /* the next row down to close */
        bool blocked = false;
        for (int c = 0; c < t->w && !blocked; c++) blocked = cell_occupied(pz, t->col + c, y, -1);
        if (blocked) g->step = 6;                                                     /* never onto anyone: it waits */
        else if (--g->step <= 0) { g->open_rows--; g->step = 6; gate_apply(pz, i); }  /* closes slowly, top first */
    } else {
        g->step = want ? 2 : 6;
    }
}

/* ---- crates ----------------------------------------------------------------- */

static bool crate_can_enter(const Puzzle *pz, int i, int col, int row) {
    const GeTilemap *m = &pz->w->map;
    if (col < 0 || col >= m->w || row < 0) return false;
    if (row >= m->h) return true;                                  /* off the bottom: it will come back */
    return cell_type(pz, col, row) == GE_TILE_EMPTY && !cell_occupied(pz, col, row, i);
}

static void crate_place(Puzzle *pz, int i, int col, int row) {
    Piece *c = &pz->p[i];
    c->col = (int16_t)col; c->row = (int16_t)row;
    c->dx = c->dy = c->px = 0;
    set_cell(pz, col, row, GE_TILE_SOLID, TV_CRATE);
}

static bool plate_at(const Puzzle *pz, int col, int row) {
    for (int k = 0; k < pz->room->nthings; k++)
        if (pz->room->things[k].kind == TH_PLATE && pz->room->things[k].col == col && pz->room->things[k].row == row) return true;
    return false;
}

/* where a crate starts: on the last plate it came to rest on, else where it was built */
static void crate_home(const Puzzle *pz, int i, int *col, int *row) {
    const Thing *t = &pz->room->things[i];
    *col = t->col; *row = t->row;
    if (pz->prog->crate_homed[i]) { *col = pz->prog->crate_home[i][0]; *row = pz->prog->crate_home[i][1]; }
}

/* put a crate back at its home (entering the room, or after it fell out of it); false if something is there */
static bool crate_go_home(Puzzle *pz, int i) {
    int col, row;
    crate_home(pz, i, &col, &row);
    if (cell_type(pz, col, row) != GE_TILE_EMPTY || cell_occupied(pz, col, row, i)) {   /* the plate is taken: as built */
        col = pz->room->things[i].col; row = pz->room->things[i].row;
        if (cell_type(pz, col, row) != GE_TILE_EMPTY || cell_occupied(pz, col, row, i)) return false;
    }
    crate_place(pz, i, col, row);
    return true;
}

static void crate_move(Puzzle *pz, int i, int dx, int dy, int speed) {
    Piece *c = &pz->p[i];
    restore_cell(pz, c->col, c->row);
    c->col = (int16_t)(c->col + dx); c->row = (int16_t)(c->row + dy);
    set_cell(pz, c->col, c->row, GE_TILE_SOLID, TV_BLANK);         /* solid at once, drawn sliding into place */
    c->dx = dx; c->dy = dy; c->px = 0; c->speed = speed;
}

static int g_push_dir;   /* the hero's walking direction this tick (from the intent) */

static void crate_update(Puzzle *pz, int i) {
    const Thing *t = &pz->room->things[i];
    Piece *c = &pz->p[i];
    if (c->lockout > 0) c->lockout--;
    if (c->dx || c->dy) {                                           /* moving into its new cell */
        c->px += c->speed;
        if (c->px < T) return;
        if (c->row >= pz->w->map.h) {                               /* fell out of the room: back home */
            c->col = -100; c->row = -100;                           /* (nowhere, while looking for room at home) */
            if (!crate_go_home(pz, i)) { c->dx = c->dy = 0; c->px = 0; c->col = t->col; c->row = t->row; }
            return;
        }
        crate_place(pz, i, c->col, c->row);
        if (!crate_can_enter(pz, i, c->col, c->row + 1) && plate_at(pz, c->col, c->row)) {   /* a plate catches it: */
            c->push = 0;                                            /* it stops there a moment, even if you keep pushing */
            pz->prog->crate_homed[i] = true;
            pz->prog->crate_home[i][0] = c->col; pz->prog->crate_home[i][1] = c->row;
        }
    }
    if (crate_can_enter(pz, i, c->col, c->row + 1)) { crate_move(pz, i, 0, 1, 2); return; }   /* falls */
    if (!hero_ok(pz)) return;
    const GeEntity *h = hero(pz);
    /* walked into: 0.2 s of pushing on the ground, then it slides a cell at the hero's pace */
    int dir = g_push_dir, feet_row = (int)floorf((h->pos.y - 1.0f) / T);
    float edge = h->pos.x + (float)dir * h->def->body_w * 0.5f, face = (float)(dir > 0 ? c->col * T : (c->col + 1) * T);
    bool pushing = dir != 0 && h->on_ground && feet_row == c->row && fabsf(edge - face) <= 2.5f;
    if (!pushing) c->push = 0;
    else if (++c->push >= 12 && crate_can_enter(pz, i, c->col + dir, c->row)) { crate_move(pz, i, dir, 0, 1); return; }
    /* knocked by a slash or a needle: one cell away from the hero */
    bool down = false;
    if (c->lockout == 0 && slash_hits(pz, cell_rect(c->col, c->row), c->col, c->row, &down)) {
        c->lockout = 20;
        if (down) { ge_entity_pogo(pz->w, pz->hero, pogo_speed(h)); return; }
        int away = (c->col * T + T / 2) > h->pos.x ? 1 : -1;
        if (crate_can_enter(pz, i, c->col + away, c->row)) crate_move(pz, i, away, 0, 4);
    }
}

/* ---- cracked stone ---------------------------------------------------------- */

static bool is_crack(const Puzzle *pz, int col, int row) {
    char ch = room_tile(pz->room, col, row);
    return (ch == 'C' || ch == 'H') && cell_type(pz, col, row) == GE_TILE_SOLID;
}

/* the 4-connected stretch of breakable stone around a cell */
static int crack_group(const Puzzle *pz, int col, int row, int16_t out[][2], int max) {
    int n = 0;
    if (!is_crack(pz, col, row)) return 0;
    out[n][0] = (int16_t)col; out[n][1] = (int16_t)row; n++;
    for (int k = 0; k < n; k++) {
        static const int d[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
        for (int j = 0; j < 4; j++) {
            int x = out[k][0] + d[j][0], y = out[k][1] + d[j][1];
            if (!is_crack(pz, x, y)) continue;
            bool seen = false;
            for (int q = 0; q < n && !seen; q++) seen = out[q][0] == x && out[q][1] == y;
            if (!seen && n < max) { out[n][0] = (int16_t)x; out[n][1] = (int16_t)y; n++; }
        }
    }
    return n;
}

/* a cell under stone that just broke is open to the air now: give it the top edge the room would have built */
static void redress_below(Puzzle *pz, int col, int row) {
    GeTilemap *m = &pz->w->map;
    int y = row + 1;
    if (col < 0 || y < 0 || col >= m->w || y >= m->h || m->type[y * m->w + col] != GE_TILE_SOLID || puzzle_owns_cell(pz, col, y)) return;
    char c = room_tile(pz->room, col, y);
    if (c == '#') m->vis[y * m->w + col] = TV_GRASS;
    else if (c == 'S') m->vis[y * m->w + col] = TV_STONE_TOP;
}

static void break_cell(Puzzle *pz, int col, int row) {
    set_cell(pz, col, row, GE_TILE_EMPTY, 0);
    redress_below(pz, col, row);
}

static void crack_hit(Puzzle *pz, int col, int row, bool down) {
    int16_t grp[64][2];
    int n = crack_group(pz, col, row, grp, 64);
    if (n == 0) return;
    int key = 0;                                                    /* the group is known by its first cell */
    for (int k = 1; k < n; k++) if (grp[k][1] < grp[key][1] || (grp[k][1] == grp[key][1] && grp[k][0] < grp[key][0])) key = k;
    int e = -1;
    for (int k = 0; k < pz->ncracks; k++) if (pz->cracks[k].col == grp[key][0] && pz->cracks[k].row == grp[key][1]) e = k;
    if (e < 0) {
        if (pz->ncracks >= PZ_MAX_CRACKS) return;
        e = pz->ncracks++;
        pz->cracks[e].col = grp[key][0]; pz->cracks[e].row = grp[key][1]; pz->cracks[e].hits = 0; pz->cracks[e].lockout = 0;
    }
    if (pz->cracks[e].lockout > 0) return;
    pz->cracks[e].lockout = 20;
    if (down) ge_entity_pogo(pz->w, pz->hero, pogo_speed(hero(pz)));
    float cx = col * T + T * 0.5f, cy = row * T + T * 0.5f;
    if (++pz->cracks[e].hits < 2) {                                 /* the first hit spreads the crack */
        for (int k = 0; k < n; k++) set_cell(pz, grp[k][0], grp[k][1], GE_TILE_SOLID, TV_CRACK);
        sparks(pz, cx, cy, GE_ARGB(255, 230, 220, 255), 8);
        ge_world_shake(pz->w, 2.0f);
        push_event(pz, PZ_CRACK, -1);
        return;
    }
    for (int k = 0; k < n; k++) {                                   /* the second breaks it all */
        break_cell(pz, grp[k][0], grp[k][1]);
        if (pz->prog->nbroken < ROOM_MAX_BROKEN) {
            pz->prog->broken[pz->prog->nbroken][0] = grp[k][0];
            pz->prog->broken[pz->prog->nbroken][1] = grp[k][1];
            pz->prog->nbroken++;
        }
        for (int q = 0; q < 4; q++) {
            float vy = ge_rng_range(&pz->w->rng, -3, 0), vx = ge_rng_range(&pz->w->rng, -2, 2);
            ge_world_emit(pz->w, grp[k][0] * T + 8.0f, grp[k][1] * T + 8.0f, vx, vy, 0.15f, 40, GE_RGB(110, 104, 130), 3, 0);
        }
    }
    pz->cracks[e] = pz->cracks[--pz->ncracks];
    ge_world_shake(pz->w, 4.0f);
    push_event(pz, PZ_SECRET, -1);
}

static void cracks_update(Puzzle *pz) {
    for (int k = 0; k < pz->ncracks; k++) if (pz->cracks[k].lockout > 0) pz->cracks[k].lockout--;
    if (!hero_ok(pz)) return;
    GeRect hb;
    if (!ge_entity_hitbox(hero(pz), &hb)) return;
    int x0 = (int)floorf(hb.x / T), x1 = (int)floorf((hb.x + hb.w) / T), y0 = (int)floorf(hb.y / T), y1 = (int)floorf((hb.y + hb.h) / T);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            bool down = false;
            if (is_crack(pz, x, y) && slash_hits(pz, cell_rect(x, y), x, y, &down)) { crack_hit(pz, x, y, down); return; }
        }
}

/* (from inside the world update: noted now, acted on in puzzle_update, so the map only changes at one point) */
void puzzle_shot_tile(Puzzle *pz, int owner, int cell) {
    if (owner != pz->hero || cell < 0 || pz->nshot_cells >= 8) return;
    pz->shot_cells[pz->nshot_cells++] = cell;
}

static void shot_cell(Puzzle *pz, int cell) {
    if (pz->w->map.w <= 0 || cell < 0 || cell >= pz->w->map.w * pz->w->map.h) return;
    int col = cell % pz->w->map.w, row = cell / pz->w->map.w;
    if (is_crack(pz, col, row)) { crack_hit(pz, col, row, false); return; }
    for (int i = 0; i < pz->room->nthings; i++) {                   /* a needle knocks a crate one cell on */
        Piece *c = &pz->p[i];
        if (pz->room->things[i].kind != TH_CRATE || c->col != col || c->row != row || c->dx || c->dy || c->lockout) continue;
        int away = (col * T + T / 2) > hero(pz)->pos.x ? 1 : -1;
        c->lockout = 20;
        if (crate_can_enter(pz, i, col + away, row)) crate_move(pz, i, away, 0, 4);
    }
}

/* ---- the room --------------------------------------------------------------- */

static GeRect thing_rect(const Thing *t) {
    switch (t->kind) {
    case TH_LEVER: return (GeRect){ (float)(t->col * T + 3), (float)(t->row * T + 1), 10, 15 };
    case TH_KEY: case TH_RELIC: return (GeRect){ (float)(t->col * T + 2), (float)(t->row * T + 2), 12, 12 };
    case TH_GATE: return (GeRect){ (float)(t->col * T), (float)(gate_top(t) * T), (float)(t->w * T), (float)(t->h * T) };
    default: return cell_rect(t->col, t->row);
    }
}

static void wires_update(Puzzle *pz) {
    memset(pz->wire_on, 0, sizeof pz->wire_on);
    memset(pz->wire_inputs, 0, sizeof pz->wire_inputs);
    for (int i = 0; i < pz->room->nthings; i++) {
        const Thing *t = &pz->room->things[i];
        if ((t->kind != TH_LEVER && t->kind != TH_PLATE) || t->ch <= 0 || t->ch > 9) continue;
        pz->wire_inputs[t->ch]++;
        if (pz->p[i].on) pz->wire_on[t->ch]++;
    }
}

void puzzle_enter(Puzzle *pz, Room *r, RoomProgress *prog, GeWorld *w, int hero_idx, int *keys) {
    memset(pz, 0, sizeof *pz);
    pz->room = r; pz->prog = prog; pz->w = w; pz->hero = hero_idx; pz->keys = keys;
    pz->sign_near = pz->door_near = -1;
    for (int i = 0; i < r->nthings; i++) { pz->p[i].col = -100; pz->p[i].row = -100; }   /* (no crate anywhere yet) */
    for (int k = 0; k < prog->nbroken; k++) {                /* stone broken before (only where it still is breakable stone: */
        char t = room_tile(r, prog->broken[k][0], prog->broken[k][1]);   /* the room may have been redrawn since it was saved) */
        if (t == 'C' || t == 'H') break_cell(pz, prog->broken[k][0], prog->broken[k][1]);
    }
    for (int i = 0; i < r->nthings; i++) {
        const Thing *t = &r->things[i];
        Piece *p = &pz->p[i];
        if (t->kind == TH_LEVER) p->on = t->mode == LEVER_ONCE && prog->done[i];   /* pulled for good */
        if (t->kind == TH_CRATE && !crate_go_home(pz, i)) { p->col = t->col; p->row = t->row; }
    }
    wires_update(pz);
    for (int i = 0; i < r->nthings; i++)                           /* gates start where their wires say, no animation */
        if (r->things[i].kind == TH_GATE) {
            pz->p[i].want = gate_wants_open(pz, i);
            pz->p[i].open_rows = pz->p[i].want ? r->things[i].h : 0;
            gate_apply(pz, i);
        }
}

void puzzle_pre_update(Puzzle *pz, GeIntent *it) {
    g_push_dir = it->move_x > 0.1f ? 1 : it->move_x < -0.1f ? -1 : 0;
    if (!hero_ok(pz)) return;
    const GeEntity *h = hero(pz);
    for (int i = 0; i < pz->room->nthings; i++) {                   /* pushing a sliding crate: walk at its pace */
        const Piece *c = &pz->p[i];
        if (pz->room->things[i].kind != TH_CRATE || c->dx == 0 || c->dx != g_push_dir || c->speed != 1) continue;
        float rear = c->dx > 0 ? (float)(c->col * T) - (float)(T - c->px) : (float)((c->col + 1) * T) + (float)(T - c->px);
        if (fabsf(h->pos.x + (float)c->dx * h->def->body_w * 0.5f - rear) < 6.0f && h->def->run_speed > 0)
            it->move_x = (float)c->dx * (1.0f / h->def->run_speed);
    }
}

void puzzle_update(Puzzle *pz, const GeInput *in) {
    pz->nev = 0;
    Room *r = pz->room;
    const GeEntity *h = hero(pz);
    GeRect hb = body(h);
    pz->sign_near = pz->door_near = -1;
    /* inputs */
    for (int i = 0; i < r->nthings; i++) {
        const Thing *t = &r->things[i];
        Piece *p = &pz->p[i];
        if (t->kind == TH_LEVER) {
            if (p->lockout > 0) p->lockout--;
            if (t->mode == LEVER_TIMED && p->on && --p->timer <= 0) p->on = false;
            bool down = false, hit = slash_hits(pz, thing_rect(t), t->col, t->row, &down) || needle_hits(pz, thing_rect(t));
            if (!hit || p->lockout > 0) continue;
            p->lockout = 30;                                        /* one swing (or a spin, or a pierce) is one hit */
            if (down) ge_entity_pogo(pz->w, pz->hero, pogo_speed(h));   /* a lever is a pogo post too */
            bool was = p->on;
            if (t->mode == LEVER_ONCE) { p->on = true; pz->prog->done[i] = true; }
            else if (t->mode == LEVER_TOGGLE) p->on = !p->on;
            else { p->on = true; p->timer = t->time > 0 ? t->time : 180; }    /* hitting a timed lever again restarts it */
            sparks(pz, t->col * T + 8.0f, t->row * T + 6.0f, puzzle_wire_color(t->ch), 6);
            if (p->on != was || t->mode == LEVER_TIMED) push_event(pz, PZ_LEVER, i);
        } else if (t->kind == TH_PLATE) {
            bool on = false;
            if (hero_ok(pz) && h->on_ground && fabsf(h->pos.y - (float)((t->row + 1) * T)) < 3.0f &&
                h->pos.x >= t->col * T - 2.0f && h->pos.x <= (t->col + 1) * T + 2.0f) on = true;
            for (int k = 0; k < r->nthings && !on; k++)
                if (r->things[k].kind == TH_CRATE && pz->p[k].col == t->col && pz->p[k].row == t->row && pz->p[k].dy == 0) on = true;
            p->on = on;
        }
    }
    wires_update(pz);
    /* pieces */
    for (int i = 0; i < r->nthings; i++) {
        const Thing *t = &r->things[i];
        bool done = pz->prog->done[i];
        switch (t->kind) {
        case TH_GATE:
            if (t->lock && !done && hero_ok(pz) && *pz->keys > 0) {   /* walk into it with a key */
                GeRect near = { hb.x - 3, hb.y, hb.w + 6, hb.h };
                if (ge_rect_overlap(near, thing_rect(t))) { (*pz->keys)--; pz->prog->done[i] = true; push_event(pz, PZ_UNLOCK, i); gate_apply(pz, i); }
            }
            gate_update(pz, i);
            break;
        case TH_CRATE: crate_update(pz, i); break;
        case TH_KEY: case TH_RELIC:
            if (!done && hero_ok(pz) && ge_rect_overlap(hb, thing_rect(t))) {
                pz->prog->done[i] = true;
                if (t->kind == TH_KEY) (*pz->keys)++;
                sparks(pz, t->col * T + 8.0f, t->row * T + 8.0f, t->kind == TH_KEY ? GE_RGB(255, 220, 120) : GE_RGB(200, 230, 255), 14);
                push_event(pz, t->kind == TH_KEY ? PZ_KEY : PZ_RELIC, i);
            }
            break;
        case TH_SIGN:
            if (hero_ok(pz) && fabsf(h->pos.x - (t->col * T + 8.0f)) < 22.0f && fabsf(h->pos.y - (float)((t->row + 1) * T)) < 24.0f) pz->sign_near = i;
            break;
        case TH_DOOR:
            if (hero_ok(pz) && h->on_ground && fabsf(h->pos.x - (t->col * T + 8.0f)) < 10.0f && fabsf(h->pos.y - (float)((t->row + 1) * T)) < 4.0f) {
                pz->door_near = i;
                if ((in->pressed & GE_BTN_UP) && !(in->down & GE_BTN_Y)) push_event(pz, PZ_DOOR, i);   /* not an up-slash */
            }
            break;
        default: break;
        }
    }
    cracks_update(pz);
    for (int k = 0; k < pz->nshot_cells; k++) shot_cell(pz, pz->shot_cells[k]);
    pz->nshot_cells = 0;
}

bool puzzle_owns_cell(const Puzzle *pz, int col, int row) {
    for (int i = 0; i < pz->room->nthings; i++) {
        const Thing *t = &pz->room->things[i];
        if (t->kind == TH_GATE && col >= t->col && col < t->col + t->w && row >= gate_top(t) && row <= t->row) return true;
        if (t->kind == TH_CRATE && pz->p[i].col == col && pz->p[i].row == row) return true;
    }
    char c = room_tile(pz->room, col, row);
    return c == 'C' || c == 'H';
}

/* ---- drawing ---------------------------------------------------------------- */

void puzzle_draw_hints(const Puzzle *pz, GeImage *fb, int camx, int camy, uint32_t tick) {
    for (int i = 0; i < pz->room->nthings; i++) {
        const Thing *t = &pz->room->things[i];
        const Piece *g = &pz->p[i];
        if (t->kind != TH_GATE || g->alert <= 0 || ((tick / 8) & 1)) continue;   /* (it blinks) */
        float gx = (float)(t->col * T) + t->w * T * 0.5f - (float)camx, gy = (float)(gate_top(t) * T) + t->h * T * 0.5f - (float)camy;
        float cx = fb->w * 0.5f, cy = fb->h * 0.5f, dx = gx - cx, dy = gy - cy;
        float sx = fabsf(dx) > 1e-3f ? (cx - 14.0f) / fabsf(dx) : 1e9f, sy = fabsf(dy) > 1e-3f ? (cy - 14.0f) / fabsf(dy) : 1e9f;
        float k = sx < sy ? sx : sy;
        if (k >= 1.0f) continue;                                                 /* on screen after all */
        int ax = (int)(cx + dx * k), ay = (int)(cy + dy * k);
        float len = sqrtf(dx * dx + dy * dy), ux = dx / len, uy = dy / len;       /* pointing at it */
        uint32_t c = puzzle_wire_color(t->ch), ink = GE_RGB(20, 14, 30);
        int tx = ax + (int)(ux * 9), ty = ay + (int)(uy * 9);
        int bx = ax - (int)(ux * 5), by = ay - (int)(uy * 5);
        int px = (int)(-uy * 6), py = (int)(ux * 6);
        ge_draw_line(fb, bx + px, by + py, tx, ty, 3, ink);
        ge_draw_line(fb, bx - px, by - py, tx, ty, 3, ink);
        ge_draw_line(fb, bx + px, by + py, tx, ty, 1, c);
        ge_draw_line(fb, bx - px, by - py, tx, ty, 1, c);
        ge_fill_circle(fb, ax, ay, 3, ink, 0);
        ge_fill_circle(fb, ax, ay, 2, c, 0);
    }
}

static void gem(GeImage *fb, int x, int y, uint32_t c) {
    ge_fill_rect(fb, x - 2, y - 2, 5, 5, GE_RGB(20, 14, 30), 0);
    ge_fill_rect(fb, x - 1, y - 1, 3, 3, c, 0);
}

void puzzle_draw(const Puzzle *pz, GeImage *fb, int camx, int camy, uint32_t tick) {
    const Room *r = pz->room;
    const GeImage *ts = &pz->w->tileset;
    for (int i = 0; i < r->nthings; i++) {
        const Thing *t = &r->things[i];
        const Piece *p = &pz->p[i];
        int x = t->col * T - camx, y = t->row * T - camy;
        if (x < -64 || x > fb->w + 64 || y < -64 || y > fb->h + 64) continue;
        bool done = pz->prog->done[i];
        uint32_t wc = puzzle_wire_color(t->ch);
        switch (t->kind) {
        case TH_LEVER: {
            /* under a ceiling with nothing to stand on, it hangs from the ceiling */
            bool hang = cell_type(pz, t->col, t->row - 1) == GE_TILE_SOLID && cell_type(pz, t->col, t->row + 1) != GE_TILE_SOLID &&
                        cell_type(pz, t->col, t->row + 1) != GE_TILE_ONEWAY;
            int base = hang ? y : y + 12, pivot = hang ? y + 4 : y + 12;
            ge_fill_rect(fb, x + 2, base, 12, 4, GE_RGB(84, 80, 108), 0);             /* the base */
            ge_fill_rect(fb, x + 2, hang ? base + 3 : base, 12, 1, GE_RGB(120, 116, 140), 0);
            bool blink = t->mode == LEVER_TIMED && p->on && p->timer < 60 && (tick & 4);
            int hx = x + 8 + (p->on ? 6 : -6), hy = hang ? y + 13 : y + 3;          /* the handle leans the way it was pushed */
            ge_draw_line(fb, x + 8, pivot, hx, hy, 2, GE_RGB(60, 56, 70));
            ge_fill_circle(fb, hx, hy, 3, blink ? GE_WHITE : (p->on ? wc : GE_RGB(70, 66, 80)), 0);
            gem(fb, x + 8, hang ? y + 2 : y + 14, wc);
            break;
        }
        case TH_PLATE: {
            int ph = p->on ? 1 : 3;
            ge_fill_rect(fb, x + 1, y + T - ph, 14, ph, p->on ? wc : GE_RGB(140, 134, 160), 0);
            ge_fill_rect(fb, x + 1, y + T - ph, 14, 1, GE_RGB(200, 196, 220), 0);
            break;
        }
        case TH_GATE:
            if (!t->lock && t->ch > 0 && p->open_rows < t->h) {                        /* its wire's colour on the top bar */
                int gy = (gate_top(t) + 0) * T - camy;
                if (t->invert) { ge_fill_rect(fb, x + 6, gy + 2, 5, 5, wc, 0); ge_fill_rect(fb, x + 7, gy + 3, 3, 3, GE_RGB(20, 14, 30), 0); }
                else gem(fb, x + 8, gy + 4, wc);
            }
            break;
        case TH_CRATE:
            if (p->dx || p->dy) {                                                      /* sliding or falling into its cell */
                int cx = p->col * T - p->dx * (T - p->px) - camx, cy = p->row * T - p->dy * (T - p->px) - camy;
                ge_blit(fb, ts, (GeIRect){ TV_CRATE * T, 0, T, T }, cx, cy, GE_OPAQUE, GE_WHITE);
            }
            break;
        case TH_KEY:
            if (!done) {
                int by = y + 4 + (int)(sinf((float)tick * 0.08f + (float)i) * 2.0f);
                ge_fill_circle(fb, x + 5, by + 3, 3, GE_RGB(255, 214, 110), 0);
                ge_fill_circle(fb, x + 5, by + 3, 1, GE_RGB(60, 40, 20), 0);
                ge_fill_rect(fb, x + 8, by + 2, 6, 2, GE_RGB(255, 214, 110), 0);
                ge_fill_rect(fb, x + 11, by + 4, 1, 2, GE_RGB(255, 214, 110), 0);
                ge_fill_rect(fb, x + 13, by + 4, 1, 3, GE_RGB(255, 214, 110), 0);
            }
            break;
        case TH_RELIC:
            if (!done) {
                int by = y + 7 + (int)(sinf((float)tick * 0.06f + (float)i) * 2.0f);
                ge_fill_circle(fb, x + 8, by, 7, GE_ARGB(70, 160, 200, 255), GE_ADDITIVE);
                ge_fill_circle(fb, x + 8, by, 3, GE_RGB(220, 236, 255), 0);
                if ((tick / 6 + (uint32_t)i) % 8 == 0) ge_fill_rect(fb, x + 8, by - 6, 1, 3, GE_WHITE, 0);
            }
            break;
        case TH_SIGN:
            ge_fill_rect(fb, x + 2, y + 2, 12, 14, GE_RGB(70, 66, 92), 0);
            ge_fill_rect(fb, x + 2, y + 2, 12, 2, GE_RGB(120, 116, 140), 0);
            ge_fill_rect(fb, x + 5, y + 7, 6, 1, GE_RGB(170, 160, 200), 0);
            ge_fill_rect(fb, x + 5, y + 10, 4, 1, GE_RGB(170, 160, 200), 0);
            break;
        case TH_DOOR:
            ge_fill_rect(fb, x - 1, y - T - 2, T + 2, 2 * T + 2, GE_RGB(52, 48, 72), 0);   /* a stone frame */
            ge_fill_rect(fb, x + 1, y - T, T - 2, 2 * T, GE_RGB(10, 6, 16), 0);
            ge_fill_rect(fb, x + 1, y - T, T - 2, 2, GE_RGB(120, 116, 140), 0);
            ge_fill_circle(fb, x + 8, y - T + 5, 2, GE_ARGB(120, 255, 220, 150), GE_ADDITIVE);
            break;
        default: break;
        }
    }
}
