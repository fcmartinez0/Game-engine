/* Gloamwood: a Silksong-style platformer where some enemies challenge you to
 * a fighting-game duel. Everything game-specific lives in demo/; the engine
 * knows nothing about benches, masks or goblins. */

#include "demo.h"
#include "ge/ge_engine.h"
#include "ge/ge_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEMO_HEADLESS
#include "ge/ge_platform_null.h"
#endif
#ifdef __3DS__
#include <3ds.h>
#endif

#define HERO_MASKS 5
#define MAX_SPAWNS 32

typedef enum { M_OVERWORLD, M_ALERT, M_TRANS_IN, M_BATTLE, M_TRANS_OUT, M_VICTORY } Mode;
enum { B_SLEEP, B_INTRO, B_IDLE, B_SWING, B_CHARGE, B_LEAP, B_THROW, B_DEAD };

typedef struct {
    Sprites spr;
    Chars ch;
    GeWorld world;
    GeFight fight;
    bool fight_live;
    Mode mode;
    int mode_timer;

    int player;
    int ent_of[MAX_SPAWNS];       /* entity per level spawn, -1 = gone */
    bool beaten[MAX_SPAWNS];
    int duel;                     /* spawn index being duelled */
    bool first_strike;
    int duel_result;
    int duel_cd;
    int bench_col;
    float safe_x, safe_y;
    int warp_fade;                /* spike respawn fade */
    bool hero_dying;
    int respawn_timer;

    int boss_spawn, boss_state, boss_timer, boss_combo;
    bool boss_engaged, boss_was_air;

    char msg[64];
    int msg_timer;

    bool autoplay, low_detail, god;
    const char *layers_path;
    int warp_col;
    GeInput bot_in;
    uint32_t bot_prev;
    int bot_climb;

    int kills, duels_won, duels_lost, deaths, heals;
    uint32_t tick;
} Demo;

static Demo g;
static bool g_log;
#define LOG(...) do { if (g_log) { fprintf(stderr, "[%6u] ", (unsigned)g.tick); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static GeEntity *hero(void) { return &g.world.ents[g.player]; }
static void say(const char *s, int ticks) { snprintf(g.msg, sizeof g.msg, "%s", s); g.msg_timer = ticks; }

/* ---- spawning ---------------------------------------------------------- */

static float ground_at(float x, float y) { float gy = ge_world_ground_y(&g.world, x, y); return gy < 0 ? (float)(GROUND_ROW * GE_TILE) : gy; }

static void spawn_hero(float x) {
    g.player = ge_world_spawn(&g.world, &g.ch.hero, &g.spr.hero, 0, x, ground_at(x, 0), HERO_MASKS);
    g.world.follow = g.player;
    g.safe_x = x;
    g.safe_y = hero()->pos.y;
    g.world.cam.x = x - SCREEN_W * 0.5f;
}

static GeActorDef g_duel_defs[2];

static void spawn_level(void) {
    for (int i = 0; i < level_nspawns && i < MAX_SPAWNS; i++) {
        const Spawn *s = &level_spawns[i];
        float x = s->col * GE_TILE + 8.0f, y = s->row * GE_TILE;
        const GeActorDef *d = NULL;
        const GeSprite *sp = NULL;
        int hp = 10;
        switch (s->kind) {
        case EN_CRAWLER: d = &g.ch.crawler; sp = &g.spr.crawler; hp = 10; break;
        case EN_BAT:     d = &g.ch.bat; sp = &g.spr.bat; hp = 8; break;
        case EN_SPITTER: d = &g.ch.spitter; sp = &g.spr.spitter; hp = 12; break;
        case EN_GRUNT:   d = &g.ch.grunt; sp = &g.spr.brawler; hp = 22; break;
        case EN_DUELIST: d = &g_duel_defs[s->variant]; sp = s->variant == DUEL_FENCER ? &g.spr.fencer : &g.spr.brawler; hp = 999; break;
        case EN_BOSS:    d = &g.ch.warden; sp = &g.spr.warden; hp = 180; g.boss_spawn = i; break;
        }
        g.ent_of[i] = g.beaten[i] ? -1 : ge_world_spawn(&g.world, d, sp, 1, x, y, hp);
        if (g.ent_of[i] < 0) continue;
        GeEntity *e = &g.world.ents[g.ent_of[i]];
        e->kind = s->kind;
        e->facing = -1;
        e->ai_x0 = x;
        e->ai_y0 = y;
        e->ai_state = -1;
    }
}

static void reset_boss(void) {
    g.boss_engaged = false;
    g.boss_state = B_SLEEP;
    g.world.cam.locked = false;
    level_set_gate(&g.world, false);
    int bi = g.ent_of[g.boss_spawn];
    if (bi >= 0 && g.world.ents[bi].active) {
        GeEntity *b = &g.world.ents[bi];
        b->hp = b->max_hp;
        b->pos = (GeVec2){ b->ai_x0, b->ai_y0 };
        b->vel = (GeVec2){ 0, 0 };
        b->state = GE_ST_IDLE;
    }
}

static void respawn_at_bench(void) {
    if (hero()->active) hero()->active = false;
    reset_boss();
    spawn_hero(g.bench_col * GE_TILE + 8.0f);
    hero()->invuln = 60;
}

/* ---- events ------------------------------------------------------------ */

static int spawn_of(int ent) {
    for (int i = 0; i < level_nspawns; i++) if (g.ent_of[i] == ent) return i;
    return -1;
}

static void start_duel_alert(int spawn, bool first_strike) {
    if (g.mode != M_OVERWORLD) return;
    g.mode = M_ALERT;
    g.mode_timer = 0;
    g.duel = spawn;
    g.first_strike = first_strike;
    memset(&g.world.intents[g.player], 0, sizeof(GeIntent));
    LOG("duel alert: %s%s", level_spawns[spawn].name, first_strike ? " (first strike)" : "");
}

static void on_event(void *user, GeEventType ev, int a, int b) {
    (void)user;
    GeWorld *w = &g.world;
    if (ev == GE_EV_KILL) {
        if (b == g.player) {
            g.deaths++;
            int bi = g.ent_of[g.boss_spawn];
            LOG("hero killed at col %d by %s (boss hp %d)", (int)(w->ents[b].pos.x / GE_TILE), a >= 0 ? "a strike" : "contact/shot",
                bi >= 0 ? w->ents[bi].hp : -1);
            return;
        }
        if (w->ents[b].kind == EN_BOSS) {
            g.boss_state = B_DEAD;
            g.world.cam.locked = false;
            level_set_gate(w, false);
            g.mode = M_VICTORY; g.mode_timer = 0;
            LOG("boss defeated");
            g.beaten[g.boss_spawn] = true;
            for (int i = 0; i < 40; i++)
                ge_world_emit(w, w->ents[b].pos.x, w->ents[b].pos.y - 30, ge_rng_range(&w->rng, -3, 3), ge_rng_range(&w->rng, -4, 0), 0.08f, 60,
                              GE_ARGB(255, 255, 200, 120), 3, GE_P_ADD);
        }
        g.kills++;
        int s = spawn_of(b);
        if (s >= 0) g.beaten[s] = true;
    } else if (ev == GE_EV_HIT && b >= 0 && w->ents[b].kind == EN_DUELIST && a == g.player) {
        w->ents[b].hp = w->ents[b].max_hp;          /* you can't slash a duelist down: you get a head start */
        int s = spawn_of(b);
        if (s >= 0 && g.duel_cd == 0) start_duel_alert(s, true);
    } else if (ev == GE_EV_HIT && b == g.player) {
        ge_world_shake(w, 3.0f);
    } else if (ev == GE_EV_HAZARD && a == g.player) {
        GeEntity *p = hero();
        if (p->state == GE_ST_DEAD || g.warp_fade > 0) return;
        p->hp -= 1;
        p->flash = 6;
        p->invuln = 30;                 /* one spike hit per contact, not one per tick */
        ge_world_shake(w, 4.0f);
        /* This runs inside the physics step, which sets the actor's state when it returns --
         * so a death is only flagged here and applied after the world update. */
        if (p->hp <= 0) { p->hp = 0; g.hero_dying = true; }
        else g.warp_fade = 40;
        LOG("spikes at col %d, masks left %d", (int)(p->pos.x / GE_TILE), p->hp);
    } else if (ev == GE_EV_FELL && a == g.player) {
        g.deaths++;
    } else if (ev == GE_EV_CHANNEL && a == g.player) {
        GeEntity *p = hero();
        p->hp = p->hp + 2 > p->max_hp ? p->max_hp : p->hp + 2;
        g.heals++;
        for (int i = 0; i < 16; i++)
            ge_world_emit(w, p->pos.x, p->pos.y - 12, ge_rng_range(&w->rng, -1.5f, 1.5f), ge_rng_range(&w->rng, -2, 0), 0, 30, GE_ARGB(255, 255, 255, 255), 2, GE_P_ADD);
    }
}

/* ---- enemy AI ---------------------------------------------------------- */

static bool floor_ahead(const GeEntity *e, float dist) {
    float x = e->pos.x + (float)e->facing * dist;
    float gy = ge_world_ground_y(&g.world, x, e->pos.y - 2.0f);
    if (gy < 0 || gy > e->pos.y + 20.0f) return false;
    return ge_tilemap_get(&g.world.map, (int)floorf(x / GE_TILE), (int)floorf((e->pos.y - 2.0f) / GE_TILE)) != GE_TILE_SPIKE;
}

static bool clear_line(float x0, float y0, float x1, float y1) {
    for (int i = 1; i < 16; i++) {
        float t = (float)i / 16.0f;
        int tx = (int)floorf((x0 + (x1 - x0) * t) / GE_TILE), ty = (int)floorf((y0 + (y1 - y0) * t) / GE_TILE);
        if (ge_tilemap_get(&g.world.map, tx, ty) == GE_TILE_SOLID) return false;
    }
    return true;
}

static void walker_ai(GeEntity *e, GeIntent *it, float speed) {
    if (e->ai_state == -1) e->ai_state = e->facing;
    if (!floor_ahead(e, 10.0f) || (e->state == GE_ST_RUN && fabsf(e->vel.x) < 0.01f && e->ai_timer++ > 4)) {
        e->ai_state = -e->ai_state;
        e->ai_timer = 0;
    }
    e->facing = e->ai_state;
    it->move_x = speed * (float)e->ai_state;
}

static void enemy_ai(int idx) {
    GeWorld *w = &g.world;
    GeEntity *e = &w->ents[idx];
    GeIntent *it = &w->intents[idx];
    memset(it, 0, sizeof *it);
    if (e->state == GE_ST_DEAD || e->state == GE_ST_HURT) return;
    const GeEntity *p = hero()->active && hero()->state != GE_ST_DEAD ? hero() : NULL;
    float dx = p ? p->pos.x - e->pos.x : 9999.0f, dy = p ? p->pos.y - e->pos.y : 9999.0f;
    if (e->ai_cd > 0) e->ai_cd--;

    switch (e->kind) {
    case EN_CRAWLER:
        walker_ai(e, it, 1.0f);
        break;
    case EN_BAT: {
        float tx = e->ai_x0 + sinf((float)(w->tick + idx * 37) * 0.02f) * 30.0f, ty = e->ai_y0 + sinf((float)w->tick * 0.05f) * 8.0f;
        if (p && fabsf(dx) < 110.0f && fabsf(dy) < 90.0f && e->ai_cd == 0) { e->ai_timer = 50; e->ai_cd = 140; }
        if (e->ai_timer > 0 && p) { e->ai_timer--; tx = p->pos.x; ty = p->pos.y - 10.0f; }   /* swoop */
        it->move_x = ge_clampf((tx - e->pos.x) * 0.05f, -1, 1);
        it->move_y = ge_clampf((ty - e->pos.y) * 0.05f, -1, 1);
        break;
    }
    case EN_SPITTER:
        if (p && fabsf(dx) < 170.0f && fabsf(dy) < 100.0f) {
            e->facing = dx > 0 ? 1 : -1;
            if (e->ai_cd == 0 && e->state != GE_ST_ATTACK) { it->tool_pressed = true; e->ai_cd = 150; }
        }
        break;
    case EN_GRUNT:
        if (p && fabsf(dx) < 140.0f && fabsf(dy) < 40.0f) {
            e->facing = dx > 0 ? 1 : -1;
            if (fabsf(dx) > 26.0f) { if (floor_ahead(e, 12.0f)) it->move_x = dx > 0 ? 1.0f : -1.0f; }
            else if (e->ai_cd == 0 && e->on_ground) { it->attack_pressed = true; e->ai_cd = 80 + (int)(ge_rng_float(&w->rng) * 40.0f); }
        } else {
            walker_ai(e, it, 0.5f);
        }
        break;
    case EN_DUELIST:
        /* turns to look around, like a trainer waiting for a challenger */
        if (++e->ai_timer > 150) { e->ai_timer = 0; e->facing = -e->facing; }
        if (p && fabsf(dx) < 60.0f) e->facing = dx > 0 ? 1 : -1;
        if ((w->tick + idx * 5) % 7 == 0)
            ge_world_emit(w, e->pos.x + ge_rng_range(&w->rng, -8, 8), e->pos.y - ge_rng_range(&w->rng, 0, 20), 0, -0.4f, 0, 40,
                          GE_ARGB(200, 190, 120, 255), 2, GE_P_ADD);
        break;
    default: break;
    }
}

/* ---- the boss: Thornwarden ---------------------------------------------- */

static void boss_ai(int idx) {
    GeWorld *w = &g.world;
    GeEntity *b = &w->ents[idx];
    GeIntent *it = &w->intents[idx];
    memset(it, 0, sizeof *it);
    if (b->state == GE_ST_DEAD || g.boss_state == B_DEAD) return;
    GeEntity *p = hero();
    if (!g.boss_engaged) {
        if (p->active && p->pos.x > (BOSS_COL0 + 3) * GE_TILE) {
            g.boss_engaged = true;
            g.boss_state = B_INTRO; g.boss_timer = 70;
            level_set_gate(w, true);
            w->cam.locked = true;
            w->cam.lock_x0 = (float)(BOSS_COL0 - 1) * GE_TILE;
            w->cam.lock_x1 = (float)(BOSS_COL1 + 1) * GE_TILE;
            say("THORNWARDEN", 150);
            LOG("boss engaged");
            ge_world_shake(w, 6.0f);
        }
        return;
    }
    if (!p->active || p->state == GE_ST_DEAD) return;
    float dx = p->pos.x - b->pos.x, dist = fabsf(dx);
    bool enraged = b->hp < b->max_hp / 2;
    if (g.boss_timer > 0) g.boss_timer--;

    switch (g.boss_state) {
    case B_INTRO:
        if (g.boss_timer == 0) { g.boss_state = B_IDLE; g.boss_timer = 40; }
        break;
    case B_IDLE:
        b->facing = dx > 0 ? 1 : -1;
        if (dist > 60.0f) it->move_x = (float)b->facing * 0.6f;
        if (g.boss_timer == 0) {
            float r = ge_rng_float(&w->rng);
            if (dist < 70.0f && r < 0.6f) { g.boss_state = B_SWING; it->attack_pressed = true; }
            else if (r < 0.35f) { g.boss_state = B_CHARGE; it->attack_pressed = true; it->move_y = -1; }
            else if (r < 0.7f) { g.boss_state = B_LEAP; it->jump_pressed = it->jump_held = true; b->vel.x = ge_clampf(dx / 40.0f, -3.5f, 3.5f); g.boss_was_air = false; }
            else { g.boss_state = B_THROW; g.boss_combo = enraged ? 3 : 2; it->tool_pressed = true; }
            g.boss_timer = 10;
        }
        break;
    case B_SWING:
        if (b->state != GE_ST_ATTACK && g.boss_timer == 0) { g.boss_state = B_IDLE; g.boss_timer = enraged ? 25 : 45; }
        break;
    case B_CHARGE:
        /* the charge's long active frame: barrel across the arena */
        if (b->state == GE_ST_ATTACK && b->anim.frame == 1) b->vel.x = (float)b->facing * (enraged ? 6.5f : 5.5f);
        if (b->state != GE_ST_ATTACK && g.boss_timer == 0) { g.boss_state = B_IDLE; g.boss_timer = enraged ? 30 : 50; }
        break;
    case B_LEAP:
        it->jump_held = true;
        if (!b->on_ground) g.boss_was_air = true;
        else if (g.boss_was_air) {   /* landed: shockwaves both ways */
            for (int d = -1; d <= 1; d += 2) ge_world_shoot(w, 1, idx, &g.ch.shockwave, b->pos.x + d * 18.0f, b->pos.y - 7.0f, d);
            ge_world_shake(w, 6.0f);
            g.boss_state = B_IDLE; g.boss_timer = enraged ? 30 : 50;
        } else if (g.boss_timer == 0) { g.boss_state = B_IDLE; g.boss_timer = 20; }
        break;
    case B_THROW:
        b->facing = dx > 0 ? 1 : -1;
        if (b->state != GE_ST_ATTACK && g.boss_timer == 0) {
            if (--g.boss_combo > 0) { it->tool_pressed = true; g.boss_timer = 8; }
            else { g.boss_state = B_IDLE; g.boss_timer = 40; }
        }
        break;
    default: break;
    }
}

/* ---- the hero ---------------------------------------------------------- */

static void hero_intent(const GeInput *in) {
    GeEntity *p = hero();
    GeIntent *it = &g.world.intents[g.player];
    memset(it, 0, sizeof *it);
    if (g.warp_fade > 0) return;
    it->move_x = ((in->down & GE_BTN_RIGHT) ? 1.0f : 0.0f) - ((in->down & GE_BTN_LEFT) ? 1.0f : 0.0f);
    it->move_y = ((in->down & GE_BTN_DOWN) ? 1.0f : 0.0f) - ((in->down & GE_BTN_UP) ? 1.0f : 0.0f);
    it->jump_pressed = (in->pressed & GE_BTN_B) != 0;
    it->jump_held = (in->down & GE_BTN_B) != 0;
    it->attack_pressed = (in->pressed & GE_BTN_Y) != 0;
    it->dash_pressed = (in->pressed & GE_BTN_A) != 0;
    it->tool_pressed = (in->pressed & GE_BTN_X) != 0;
    it->skill_pressed = (in->pressed & GE_BTN_R) != 0;
    it->channel_held = (in->down & GE_BTN_L) && p->hp < p->max_hp;

    /* rest at a bench: full health, and it becomes your respawn point */
    for (int i = 0; i < level_nbenches; i++) {
        float bx = level_benches[i] * GE_TILE + 8.0f;
        if (fabsf(p->pos.x - bx) < 18.0f && p->on_ground && (in->pressed & GE_BTN_UP)) {
            p->hp = p->max_hp;
            g.bench_col = level_benches[i];
            say("RESTED. THIS BENCH IS YOUR RESPAWN POINT", 120);
        }
    }
}

/* A scripted player for --autoplay: runs right, clears hazards, climbs the
 * shaft, fights what's in the way. Fights are handed to the fighting-game CPU. */
static uint32_t bot_buttons(void) {
    GeWorld *w = &g.world;
    GeEntity *p = hero();
    if (!p->active || p->state == GE_ST_DEAD) return 0;
    uint32_t b = GE_BTN_RIGHT;
    bool want_jump = false;

    /* the nearest threat ahead */
    int tgt = -1;
    float best = 1e9f;
    for (int i = 0; i < GE_MAX_ENTS; i++) {
        const GeEntity *e = &w->ents[i];
        if (!e->active || e->team != 1 || e->state == GE_ST_DEAD || e->kind == EN_DUELIST) continue;
        float dx = e->pos.x - p->pos.x, dy = e->pos.y - p->pos.y;
        if (dx > -30.0f && fabsf(dx) < best && fabsf(dy) < 70.0f) { best = fabsf(dx); tgt = i; }
    }
    if (g.boss_engaged && g.boss_state != B_DEAD) {
        GeEntity *bs = &w->ents[g.ent_of[g.boss_spawn]];
        float dx = bs->pos.x - p->pos.x;
        int toward = dx > 0 ? 1 : -1;
        b = 0;
        bool telegraph = bs->state == GE_ST_ATTACK && bs->anim.frame == 0;
        if (p->hp <= 2 && p->meter >= 33 && fabsf(dx) > 120.0f) return GE_BTN_L;             /* heal */
        if (telegraph && fabsf(dx) < 90.0f && (g.tick & 1)) b |= GE_BTN_A | (toward > 0 ? GE_BTN_RIGHT : GE_BTN_LEFT); /* dash through */
        else if (fabsf(dx) > 46.0f) b |= toward > 0 ? GE_BTN_RIGHT : GE_BTN_LEFT;
        else if (fabsf(dx) < 26.0f) b |= toward > 0 ? GE_BTN_LEFT : GE_BTN_RIGHT;
        if (fabsf(dx) < 52.0f && (g.tick % 6) < 2) b |= GE_BTN_Y;
        if (p->ammo > 0 && fabsf(dx) < 150.0f && (g.tick % 40) == 0) b |= GE_BTN_X | (toward > 0 ? GE_BTN_RIGHT : GE_BTN_LEFT);
        if (p->meter >= 66 && fabsf(dx) < 40.0f) b |= GE_BTN_R;
        return b;
    }
    if (tgt >= 0 && best >= 44.0f && best < 130.0f && p->ammo > 0 && p->on_ground && (g.tick % 30) == 0 &&
        fabsf(w->ents[tgt].pos.y - p->pos.y) < 24.0f)
        b |= GE_BTN_X;                                   /* needle anything waiting across a gap */
    if (tgt >= 0 && best < 44.0f) {
        const GeEntity *e = &w->ents[tgt];
        float dy = e->pos.y - p->pos.y;
        if (best < 28.0f) b &= ~GE_BTN_RIGHT;
        if ((g.tick % 6) < 2) {
            b |= GE_BTN_Y;
            if (dy < -20.0f) b |= GE_BTN_UP;
            if (dy > 12.0f && !p->on_ground) b |= GE_BTN_DOWN;
        }
    }
    if (p->hp <= 2 && p->meter >= 33 && (tgt < 0 || best > 120.0f) && p->on_ground) return GE_BTN_L;
    for (int i = 0; i < level_nbenches; i++)
        if (fabsf(p->pos.x - (level_benches[i] * GE_TILE + 8.0f)) < 10.0f && p->hp < p->max_hp && (g.tick & 8)) b |= GE_BTN_UP;

    /* walls: jump low ones, wall-jump up tall ones */
    float hw = p->def->body_w * 0.5f;
    int front_tx = (int)floorf((p->pos.x + hw + 4.0f) / GE_TILE);
    int ty_body = (int)floorf((p->pos.y - 8.0f) / GE_TILE);
    bool wall_front = ge_tilemap_get(&w->map, front_tx, ty_body) == GE_TILE_SOLID ||
                      ge_tilemap_get(&w->map, front_tx, (int)floorf((p->pos.y - 20.0f) / GE_TILE)) == GE_TILE_SOLID;
    bool tall = wall_front && ge_tilemap_get(&w->map, front_tx, ty_body - 3) == GE_TILE_SOLID;
    if (tall || g.bot_climb) {
        g.bot_climb = 1;
        if (p->on_ground && !wall_front) { b = GE_BTN_RIGHT; }
        if (p->wall_dir == 1) { b = GE_BTN_LEFT; want_jump = true; }           /* off the right wall, to the left */
        else if (p->wall_dir == -1) { b = GE_BTN_RIGHT; want_jump = true; }    /* and back */
        else if (p->on_ground) { b = GE_BTN_RIGHT; want_jump = true; }
        else b = p->vel.x < 0 ? GE_BTN_LEFT : GE_BTN_RIGHT;
        if (!p->on_ground && p->wall_lock == 0 && p->wall_dir == 0) b = (p->vel.x < -0.5f) ? GE_BTN_LEFT : GE_BTN_RIGHT;
        if (p->on_ground && !wall_front && p->pos.y < (GROUND_ROW - 2) * GE_TILE) g.bot_climb = 0; /* made it up */
        if (want_jump && !(g.bot_prev & GE_BTN_B)) b |= GE_BTN_B;
        else if (!p->on_ground && p->vel.y < 0) b |= GE_BTN_B;
        return b;
    } else if (wall_front && p->on_ground) want_jump = true;

    /* hazards: pits and spikes ahead */
    float look = p->pos.x + hw + 14.0f;
    float gy = ge_world_ground_y(w, look, p->pos.y - 2.0f);
    bool spikes = ge_tilemap_get(&w->map, (int)floorf(look / GE_TILE), (int)floorf((p->pos.y - 2.0f) / GE_TILE)) == GE_TILE_SPIKE ||
                  ge_tilemap_get(&w->map, (int)floorf(look / GE_TILE), (int)floorf((p->pos.y + 2.0f) / GE_TILE)) == GE_TILE_SPIKE;
    bool pit = gy < 0 || gy > p->pos.y + 24.0f || spikes;
    if (p->on_ground && pit) want_jump = true;
    if (!p->on_ground) {
        float below = ge_world_ground_y(w, p->pos.x + 10.0f, p->pos.y);
        bool danger = below < 0 || ge_world_touches_tile(w, (GeRect){ p->pos.x - 4, p->pos.y, 32, 64 }, GE_TILE_SPIKE);
        if (danger && p->vel.y > 0.5f) {
            if (p->air_jumps_left > 0) want_jump = true;
            else if (!p->air_dashed && p->dash_cd == 0) b |= GE_BTN_A;
        }
        if (p->vel.y < 0) b |= GE_BTN_B;   /* hold for full height */
    }
    if (want_jump && !(g.bot_prev & GE_BTN_B)) b |= GE_BTN_B;
    return b;
}

static void hero_bookkeeping(void) {
    GeEntity *p = hero();
    if (!p->active) return;
    if (g.hero_dying) {
        g.hero_dying = false;
        p->state = GE_ST_DEAD;
        p->dead_timer = p->def->dead_ticks;
        p->vel = (GeVec2){ 0, -2.0f };
        ge_anim_play(&p->anim, p->def->clip_dead, true);
        g.deaths++;
        LOG("hero died on spikes");
        return;
    }
    /* remember the last safe footing for spike respawns */
    if (p->on_ground && p->state != GE_ST_HURT && g.warp_fade == 0) {
        GeRect around = { p->pos.x - 40, p->pos.y - 18, 80, 60 };   /* no spikes nearby, above or below */
        int tx = (int)floorf(p->pos.x / GE_TILE), ty = (int)floorf(p->pos.y / GE_TILE);
        bool solid = true;
        for (int k = -2; k <= 2; k++) solid = solid && ge_tilemap_get(&g.world.map, tx + k, ty) == GE_TILE_SOLID;
        if (solid && !ge_world_touches_tile(&g.world, around, GE_TILE_SPIKE)) {
            g.safe_x = p->pos.x; g.safe_y = p->pos.y;
        }
    }
    if (g.warp_fade > 0 && --g.warp_fade == 20) {
        p->pos = (GeVec2){ g.safe_x, g.safe_y };
        p->vel = (GeVec2){ 0, 0 };
        p->state = GE_ST_IDLE;
        p->invuln = 70;
    }
}

/* ---- duels ------------------------------------------------------------- */

static void check_duelists(void) {
    GeEntity *p = hero();
    if (!p->active || p->state == GE_ST_DEAD || g.duel_cd > 0 || g.warp_fade) return;
    for (int i = 0; i < level_nspawns; i++) {
        if (level_spawns[i].kind != EN_DUELIST || g.ent_of[i] < 0) continue;
        GeEntity *e = &g.world.ents[g.ent_of[i]];
        if (!e->active) continue;
        float dx = p->pos.x - e->pos.x, dy = fabsf(p->pos.y - e->pos.y);
        bool touch = ge_rect_overlap(ge_entity_hurtbox(p), ge_entity_hurtbox(e));
        bool sight = fabsf(dx) < 110.0f && dy < 40.0f && (dx * (float)e->facing > 0 || fabsf(dx) < 40.0f) &&
                     clear_line(e->pos.x, e->pos.y - 16, p->pos.x, p->pos.y - 12);
        if (touch || sight) { e->facing = dx > 0 ? 1 : -1; start_duel_alert(i, false); return; }
    }
}

static void begin_battle(void) {
    const Spawn *s = &level_spawns[g.duel];
    bool fencer = s->variant == DUEL_FENCER;
    const GeFighterDef *opp = fencer ? &g.ch.f_fencer : &g.ch.f_brawler;
    ge_fight_init(&g.fight, &g.ch.f_hero, &g.spr.hero, opp, fencer ? &g.spr.fencer : &g.spr.brawler, &g.world, 1000 + g.tick);
    g.fight.names[0] = "WREN";
    g.fight.names[1] = s->name;
    g.fight.rounds_to_win = s->rounds;
    if (g.first_strike) g.fight.start_hp[1] = opp->max_hp * 85 / 100;
    g.fight_live = true;
}

static GeFightInput fight_input(const GeInput *in) {
    GeFightInput f = { 0, 0, 0, 0, -1 };
    f.x = ((in->down & GE_BTN_RIGHT) ? 1 : 0) - ((in->down & GE_BTN_LEFT) ? 1 : 0);
    f.y = ((in->down & GE_BTN_DOWN) ? 1 : 0) - ((in->down & GE_BTN_UP) ? 1 : 0);
    /* fighting layout: Y light, X medium, A heavy, B super */
    static const uint32_t map[4][2] = { { GE_BTN_Y, GE_FB_L }, { GE_BTN_X, GE_FB_M }, { GE_BTN_A, GE_FB_H }, { GE_BTN_B, GE_FB_S } };
    for (int i = 0; i < 4; i++) {
        if (in->pressed & map[i][0]) f.pressed |= (uint8_t)map[i][1];
        if (in->down & map[i][0]) f.held |= (uint8_t)map[i][1];
    }
    return f;
}

static void end_battle(void) {
    GeEntity *p = hero();
    int ent = g.ent_of[g.duel];
    LOG("duel over: %s vs WREN -> %s (rounds %d-%d)", level_spawns[g.duel].name, g.duel_result == 0 ? "WON" : "LOST",
        g.fight.f[0].wins, g.fight.f[1].wins);
    if (g.duel_result == 0) {
        g.duels_won++;
        g.beaten[g.duel] = true;
        if (ent >= 0) {
            GeEntity *e = &g.world.ents[ent];
            for (int i = 0; i < 24; i++)
                ge_world_emit(&g.world, e->pos.x, e->pos.y - 12, ge_rng_range(&g.world.rng, -2, 2), ge_rng_range(&g.world.rng, -3, 0), 0.05f, 50,
                              GE_ARGB(255, 200, 140, 255), 2, GE_P_ADD);
            e->active = false;
            g.ent_of[g.duel] = -1;
        }
        p->hp = p->max_hp;
        p->meter = p->def->meter_max;
        say("DUEL WON. MASKS RESTORED, SILK FULL", 150);
    } else {
        g.duels_lost++;
        say("DEFEATED... BACK TO THE BENCH", 150);
        respawn_at_bench();
    }
    g.duel_cd = 120;
    p = hero();
    p->invuln = 60;
}

/* ---- game callbacks ---------------------------------------------------- */

static void draw_props(void *user, GeImage *fb, int camx, int camy) {
    (void)user;
    for (int i = 0; i < level_nbenches; i++)
        art_draw_bench(fb, level_benches[i] * GE_TILE + 8 - camx, GROUND_ROW * GE_TILE - camy);
}

static int demo_init(void *user) {
    (void)user;
    art_make_sheets(&g.spr);
    if (chars_init(&g.ch, &g.spr) != 0) return 1;
    for (int v = 0; v < 2; v++) {
        const GeSprite *sp = v == DUEL_FENCER ? &g.spr.fencer : &g.spr.brawler;
        g_duel_defs[v] = g.ch.duelist;
        g_duel_defs[v].clip_idle = g_duel_defs[v].clip_jump = g_duel_defs[v].clip_fall = ge_sprite_find_clip(sp, "idle");
        g_duel_defs[v].clip_run = ge_sprite_find_clip(sp, "run");
        g_duel_defs[v].clip_hurt = ge_sprite_find_clip(sp, "hurt");
        g_duel_defs[v].clip_dead = ge_sprite_find_clip(sp, "dead");
    }
    GeWorld *w = &g.world;
    ge_world_init(w, SCREEN_W, SCREEN_H, 1234);
    w->on_event = on_event;
    w->draw_props = draw_props;
    w->user = &g;
    w->vignette = 26;
    w->low_detail = g.low_detail;
    art_make_tileset(&w->tileset);
    if (g.layers_path) {
        int line = 0, rc = ge_world_load_layers(w, g.layers_path, &line);
        if (rc != GE_LAYERS_OK) {
            fprintf(stderr, "%s: %s (line %d)\n", g.layers_path,
                    rc == GE_LAYERS_EOPEN ? "cannot open" : rc == GE_LAYERS_EIMAGE ? "cannot load image" :
                    rc == GE_LAYERS_EFULL ? "too many layers" : "syntax error", line);
            return 1;
        }
    } else {
        art_add_layers(w);
    }
    level_build(w);
    spawn_level();
    g.bench_col = level_benches[0];
    spawn_hero((g.warp_col > 0 ? g.warp_col : level_benches[0] + 1) * GE_TILE + 8.0f);
    say("THE GLOAMWOOD", 120);
    return 0;
}

static void demo_update(void *user, const GeInput *in) {
    (void)user;
    g.tick++;
    GeWorld *w = &g.world;
    GeInput bot;
    if (g.autoplay) {
        uint32_t b = g.mode == M_OVERWORLD ? bot_buttons() : 0;
        ge_input_update(&g.bot_in, b);
        g.bot_prev = b;
        bot = g.bot_in;
        in = &bot;
    }
    if (g.msg_timer > 0) g.msg_timer--;
    g.mode_timer++;

    switch (g.mode) {
    case M_OVERWORLD:
    case M_VICTORY: {
        if (g.duel_cd > 0) g.duel_cd--;
        GeEntity *p = hero();
        if (g.god && p->active) p->invuln = 2;
        if (p->active && p->state != GE_ST_DEAD) {
            hero_intent(in);
            g.respawn_timer = 0;
        } else if (++g.respawn_timer > 100) {
            say("...", 60);
            respawn_at_bench();
        }
        for (int i = 0; i < GE_MAX_ENTS; i++) {
            if (!w->ents[i].active || w->ents[i].team != 1) continue;
            if (w->ents[i].kind == EN_BOSS) boss_ai(i);
            else enemy_ai(i);
        }
        if (w->tick % 9 == 0 && ge_world_particle_count(w) < 90)   /* fireflies */
            ge_world_emit(w, w->cam.x + ge_rng_range(&w->rng, 0, SCREEN_W), w->cam.y + ge_rng_range(&w->rng, 70, 200), 0, 0, 0,
                          260 + (int)ge_rng_range(&w->rng, 0, 160), GE_ARGB(255, 190, 255, 140), 2, GE_P_ADD | GE_P_WANDER | GE_P_TWINKLE);
        ge_world_update(w);
        hero_bookkeeping();
        if (g.mode == M_OVERWORLD) check_duelists();
        break;
    }
    case M_ALERT:
        if (g.mode_timer >= 45) { g.mode = M_TRANS_IN; g.mode_timer = 0; }
        break;
    case M_TRANS_IN:
        if (g.mode_timer >= 54) { begin_battle(); g.mode = M_BATTLE; g.mode_timer = 0; }
        break;
    case M_BATTLE: {
        GeFightInput fin[2];
        if (g.autoplay) ge_fight_cpu(&g.fight, 0, 3, &fin[0]);
        else fin[0] = fight_input(in);
        ge_fight_cpu(&g.fight, 1, level_spawns[g.duel].cpu_level, &fin[1]);
        ge_fight_update(&g.fight, fin);
        if (g.fight.over) { g.duel_result = g.fight.winner; g.mode = M_TRANS_OUT; g.mode_timer = 0; }
        break;
    }
    case M_TRANS_OUT:
        if (g.mode_timer == 24) { ge_fight_free(&g.fight); g.fight_live = false; end_battle(); }
        if (g.mode_timer >= 48) { g.mode = M_OVERWORLD; g.mode_timer = 0; }
        break;
    }
}

/* ---- drawing ----------------------------------------------------------- */

static void draw_mask(GeImage *fb, int x, int y, bool full) {
    uint32_t c = full ? GE_RGB(240, 236, 228) : GE_ARGB(160, 40, 34, 56);
    ge_fill_rect(fb, x + 1, y, 8, 11, GE_RGB(20, 14, 30), 0);
    ge_fill_rect(fb, x, y + 1, 10, 9, GE_RGB(20, 14, 30), 0);
    ge_fill_rect(fb, x + 2, y + 1, 6, 9, c, 0);
    ge_fill_rect(fb, x + 1, y + 2, 8, 7, c, 0);
    if (full) { ge_fill_rect(fb, x + 3, y + 4, 1, 3, GE_RGB(20, 14, 30), 0); ge_fill_rect(fb, x + 6, y + 4, 1, 3, GE_RGB(20, 14, 30), 0); }
}

static void draw_hud(GeImage *fb) {
    GeWorld *w = &g.world;
    GeEntity *p = hero();
    for (int i = 0; i < HERO_MASKS; i++) draw_mask(fb, 30 + i * 13, 8, p->active && i < p->hp);
    /* silk spool: three segments, each pays for a heal or a skill */
    ge_fill_circle(fb, 16, 14, 10, GE_RGB(20, 14, 30), 0);
    int seg = p->meter / 33;
    ge_fill_circle(fb, 16, 14, 8, GE_RGB(60, 50, 80), 0);
    if (p->meter > 0) ge_fill_circle(fb, 16, 14, 2 + p->meter * 6 / 99, seg >= 1 ? GE_RGB(250, 245, 255) : GE_RGB(180, 170, 210), 0);
    for (int i = 0; i < 3; i++) ge_fill_rect(fb, 30 + i * 10, 24, 8, 3, i < seg ? GE_RGB(250, 245, 255) : GE_ARGB(160, 40, 34, 56), 0);
    for (int i = 0; i < p->def->ammo_max; i++) {   /* tool charges */
        uint32_t c = i < p->ammo ? GE_RGB(230, 236, 255) : GE_ARGB(150, 60, 56, 80);
        ge_fill_rect(fb, 66 + i * 6, 23, 1, 6, c, 0);
        ge_fill_rect(fb, 66 + i * 6, 22, 2, 1, GE_RGB(220, 60, 70), 0);
    }
    char buf[48];
    snprintf(buf, sizeof buf, "DUELS %d", g.duels_won);
    ge_text_outlined(fb, SCREEN_W - 8 - ge_text_width(buf, 1), 8, buf, GE_RGB(200, 170, 255), GE_RGB(20, 14, 30), 1);

    /* boss bar */
    if (g.boss_engaged && g.boss_state != B_DEAD && g.ent_of[g.boss_spawn] >= 0) {
        GeEntity *b = &w->ents[g.ent_of[g.boss_spawn]];
        ge_fill_rect(fb, 49, 221, 302, 7, GE_ARGB(220, 10, 8, 20), 0);
        ge_fill_rect(fb, 50, 222, 300 * b->hp / b->max_hp, 5, GE_RGB(200, 60, 50), 0);
        ge_text_outlined(fb, 50, 211, "THORNWARDEN", GE_RGB(255, 210, 190), GE_RGB(20, 14, 30), 1);
    }
    /* prompts */
    for (int i = 0; i < level_nbenches; i++)
        if (p->active && fabsf(p->pos.x - (level_benches[i] * GE_TILE + 8.0f)) < 18.0f && g.msg_timer == 0)
            ge_text_outlined(fb, (int)(p->pos.x - w->cam.x) - ge_text_width("UP: REST", 1) / 2, (int)(p->pos.y - w->cam.y) - 44, "UP: REST",
                             GE_WHITE, GE_RGB(20, 14, 30), 1);
    if (g.tick < 420 && g.mode == M_OVERWORLD) {   /* controls, for the first few seconds */
        const char *l1 = "B JUMP (AGAIN IN THE AIR)   Y SLASH (+UP / +DOWN)   A DASH";
        const char *l2 = "X THROW NEEDLE   R SILK SPIN   HOLD L TO HEAL   UP AT A BENCH";
        int a = g.tick > 360 ? (420 - (int)g.tick) * 255 / 60 : 255;
        ge_text_outlined(fb, (SCREEN_W - ge_text_width(l1, 1)) / 2, 44, l1, GE_ARGB(a, 230, 220, 250), GE_ARGB(a, 20, 14, 30), 1);
        ge_text_outlined(fb, (SCREEN_W - ge_text_width(l2, 1)) / 2, 55, l2, GE_ARGB(a, 230, 220, 250), GE_ARGB(a, 20, 14, 30), 1);
    }
    if (g.msg_timer > 0) {
        int scale = strlen(g.msg) < 16 ? 2 : 1;
        ge_text_outlined(fb, (SCREEN_W - ge_text_width(g.msg, scale)) / 2, 186, g.msg, GE_RGB(255, 236, 200), GE_RGB(20, 14, 30), scale);
    }
}

static void draw_alert(GeImage *fb) {
    int ent = g.ent_of[g.duel];
    if (ent < 0) return;
    GeEntity *e = &g.world.ents[ent];
    int x = (int)(e->pos.x - g.world.cam.x), y = (int)(e->pos.y - g.world.cam.y) - 46;
    int pop = g.mode_timer < 6 ? g.mode_timer : 6;
    ge_fill_rect(fb, x - 6, y - pop, 13, 16, GE_RGB(20, 14, 30), 0);
    ge_fill_rect(fb, x - 5, y - pop + 1, 11, 14, GE_WHITE, 0);
    ge_text(fb, x - 2, y - pop + 4, "!", GE_RGB(220, 40, 60), 1);
    const Spawn *s = &level_spawns[g.duel];
    char buf[64];
    snprintf(buf, sizeof buf, "%s WANTS TO DUEL!", s->name);
    ge_text_outlined(fb, (SCREEN_W - ge_text_width(buf, 1)) / 2, 186, buf, GE_RGB(255, 220, 160), GE_RGB(20, 14, 30), 1);
}

/* Pokemon-style: flash, flash, flash, then the screen is shuttered by bars. */
static void draw_trans_in(GeImage *fb, int t) {
    if (t < 24) {
        if ((t / 4) & 1) ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(220, 255, 255, 255), 0);
        else ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(170, 10, 0, 20), 0);
        return;
    }
    float p = (float)(t - 24) / 30.0f;
    int bands = 12, bh = fb->h / bands + 1;
    for (int i = 0; i < bands; i++) {
        float q = ge_clampf(p * 1.6f - (float)i * 0.05f, 0.0f, 1.0f);
        int wpx = (int)(q * (float)fb->w);
        ge_fill_rect(fb, (i & 1) ? fb->w - wpx : 0, i * bh, wpx, bh, GE_RGB(10, 4, 18), 0);
    }
}

static void demo_draw(void *user, GeImage *fb) {
    (void)user;
    if (g.mode == M_BATTLE || (g.mode == M_TRANS_OUT && g.fight_live)) {
        ge_fight_draw(&g.fight, fb);
        if (g.mode == M_BATTLE && g.mode_timer < 30) {   /* the shutters open again */
            float p = (float)g.mode_timer / 30.0f;
            int bands = 12, bh = fb->h / bands + 1;
            for (int i = 0; i < bands; i++) {
                int wpx = (int)((1.0f - p) * (float)fb->w);
                ge_fill_rect(fb, (i & 1) ? fb->w - wpx : 0, i * bh, wpx, bh, GE_RGB(10, 4, 18), 0);
            }
        }
        if (g.mode == M_BATTLE) {
            const char *hint = "Y LIGHT  X MEDIUM  A HEAVY  B SUPER  HOLD BACK TO BLOCK";
            if (g.fight.phase == GE_FP_INTRO) ge_text_outlined(fb, (SCREEN_W - ge_text_width(hint, 1)) / 2, 200, hint, GE_RGB(220, 210, 240), GE_RGB(20, 14, 30), 1);
        }
    } else {
        ge_world_draw(&g.world, fb);
        draw_hud(fb);
        if (g.mode == M_ALERT) draw_alert(fb);
        if (g.mode == M_TRANS_IN) { draw_alert(fb); draw_trans_in(fb, g.mode_timer); }
        if (g.mode == M_VICTORY && g.mode_timer > 60) {
            const char *a = "THORNWARDEN FELLED", *b = "THE GLOAMWOOD IS QUIET AGAIN";
            ge_text_outlined(fb, (SCREEN_W - ge_text_width(a, 2)) / 2, 90, a, GE_RGB(255, 220, 160), GE_RGB(20, 14, 30), 2);
            ge_text_outlined(fb, (SCREEN_W - ge_text_width(b, 1)) / 2, 112, b, GE_RGB(230, 220, 250), GE_RGB(20, 14, 30), 1);
        }
    }
    if (g.mode == M_TRANS_OUT) {   /* fade through black */
        int t = g.mode_timer, a = t < 24 ? t * 255 / 24 : (48 - t) * 255 / 24;
        ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(ge_clampi(a, 0, 255), 10, 4, 18), 0);
    }
    if (g.warp_fade > 0) {
        int t = 40 - g.warp_fade, a = t < 20 ? t * 255 / 20 : (40 - t) * 255 / 20;
        ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(ge_clampi(a, 0, 255), 0, 0, 0), 0);
    }
}

static void demo_shutdown(void *user) {
    (void)user;
#ifdef DEMO_HEADLESS
    GeEntity *p = hero();
    printf("mode=%d x=%.0f (col %d) masks=%d kills=%d duels_won=%d duels_lost=%d deaths=%d heals=%d boss=%s checksum=%08x\n",
           g.mode, p->pos.x, (int)(p->pos.x / GE_TILE), p->active ? p->hp : 0, g.kills, g.duels_won, g.duels_lost, g.deaths, g.heals,
           g.beaten[g.boss_spawn] ? "defeated" : (g.boss_engaged ? "fighting" : "waiting"), ge_null_last_checksum());
#endif
    if (g.fight_live) ge_fight_free(&g.fight);
    ge_world_free(&g.world);
    art_free(&g.spr);
}

int main(int argc, char **argv) {
    int frames = 1800, dump_every = 0;
    g_log = getenv("GLOAM_LOG") != NULL;
    const char *dump_dir = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--autoplay")) g.autoplay = true;
        else if (!strcmp(argv[i], "--low-detail")) g.low_detail = true;
        else if (!strcmp(argv[i], "--god")) g.god = true;        /* dev: the hero can't be hurt */
        else if (!strcmp(argv[i], "--layers") && i + 1 < argc) g.layers_path = argv[++i];
        else if (!strcmp(argv[i], "--warp") && i + 1 < argc) g.warp_col = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-every") && i + 1 < argc) dump_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-dir") && i + 1 < argc) dump_dir = argv[++i];
    }
#ifdef __3DS__
    bool is_new3ds = false;
    APT_CheckNew3DS(&is_new3ds);
    g.low_detail = !is_new3ds; /* original 3DS: 268 MHz, drop the decorative layers */
#endif
#ifdef DEMO_HEADLESS
    ge_null_configure(frames, dump_every, dump_dir, NULL, NULL);
#else
    (void)frames; (void)dump_every; (void)dump_dir;
#endif
    GeGame game = { &g, demo_init, demo_update, demo_draw, demo_shutdown };
    GePlatformConfig cfg = { "Gloamwood", SCREEN_W, SCREEN_H, 3 };
    return ge_run(&game, &cfg);
}
