/* Gloamwood: a small side-scrolling brawler built on the engine.
 * Everything game-specific lives here; the engine knows nothing about goblins. */

#include "demo.h"
#include "ge/ge_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEMO_HEADLESS
#include "ge/ge_platform_null.h"
#endif
#ifdef __3DS__
#include <3ds.h>
#endif

typedef struct {
    GeWorld world;
    GeSprite player_spr, goblin_spr;
    GeActorDef player_def, goblin_def;
    int player;
    int kills, deaths;
    int respawn_timer;
    float safe_x;
    bool autoplay, low_detail;
    const char *layers_path; /* --layers FILE: backdrop from disk instead of the procedural one */
    uint32_t ap_tick;
} Demo;

/* ---- actor definitions ------------------------------------------------- */

static void make_defs(Demo *g) {
    const GeSprite *p = &g->player_spr, *e = &g->goblin_spr;
    g->player_def = (GeActorDef){
        .run_speed = 1.7f, .accel = 0.22f, .friction = 0.28f, .air_accel = 0.14f,
        .gravity = 0.38f, .max_fall = 8.0f, .jump_vel = 6.7f, .jump_cut_vel = 2.2f,
        .coyote_ticks = 6, .jump_buffer_ticks = 6,
        .dash_speed = 4.6f, .dash_ticks = 11, .dash_cooldown = 30,
        .invuln_ticks = 50, .dead_ticks = 90, .body_w = 12, .body_h = 24,
        .clip_idle = ge_sprite_find_clip(p, "idle"), .clip_run = ge_sprite_find_clip(p, "run"),
        .clip_jump = ge_sprite_find_clip(p, "jump"), .clip_fall = ge_sprite_find_clip(p, "fall"),
        .clip_hurt = ge_sprite_find_clip(p, "hurt"), .clip_dead = ge_sprite_find_clip(p, "dead"),
        .clip_dash = ge_sprite_find_clip(p, "dash"), .clip_attack = ge_sprite_find_clip(p, "attack1"),
    };
    g->goblin_def = (GeActorDef){
        .run_speed = 0.75f, .accel = 0.1f, .friction = 0.2f, .air_accel = 0.08f,
        .gravity = 0.38f, .max_fall = 8.0f,
        .invuln_ticks = 0, .dead_ticks = 45, .body_w = 14, .body_h = 22,
        .clip_idle = ge_sprite_find_clip(e, "idle"), .clip_run = ge_sprite_find_clip(e, "walk"),
        .clip_jump = ge_sprite_find_clip(e, "idle"), .clip_fall = ge_sprite_find_clip(e, "idle"),
        .clip_hurt = ge_sprite_find_clip(e, "hurt"), .clip_dead = ge_sprite_find_clip(e, "dead"),
        .clip_dash = -1, .clip_attack = ge_sprite_find_clip(e, "attack"),
    };
}

/* ---- gameplay ---------------------------------------------------------- */

static void spawn_player(Demo *g, float x, float y) {
    g->player = ge_world_spawn(&g->world, &g->player_def, &g->player_spr, 0, x, y, 100);
    g->world.follow = g->player;
}

static void on_event(void *user, GeEventType ev, int a, int b) {
    Demo *g = (Demo *)user;
    (void)a;
    if (ev == GE_EV_KILL) {
        if (g->world.ents[b].team == 1) g->kills++;
        else g->deaths++;
    } else if (ev == GE_EV_FELL && b == g->player) {
        g->deaths++;
    }
}

static bool ground_ahead(const GeWorld *w, const GeEntity *e, float dist) {
    return ge_world_ground_y(w, e->pos.x + (float)e->facing * dist, e->pos.y - 1.0f) >= 0.0f;
}

static void goblin_ai(Demo *g, int i) {
    GeWorld *w = &g->world;
    GeEntity *e = &w->ents[i];
    GeIntent *it = &w->intents[i];
    memset(it, 0, sizeof *it);
    if (e->state == GE_ST_DEAD || e->state == GE_ST_HURT || e->state == GE_ST_ATTACK) return;
    if (e->ai_x0 == 0.0f) { e->ai_x0 = e->pos.x; e->ai_state = 1; }
    if (e->ai_timer > 0) e->ai_timer--;

    const GeEntity *p = g->world.ents[g->player].active ? &g->world.ents[g->player] : NULL;
    float dx = p ? p->pos.x - e->pos.x : 0.0f;
    bool sees = p && fabsf(dx) < 150.0f && fabsf(p->pos.y - e->pos.y) < 60.0f;

    if (sees) {
        e->facing = dx > 0 ? 1 : -1;
        if (e->lane != p->lane && e->lane_cd == 0) it->lane_shift = p->lane > e->lane ? 1 : -1; /* chase across depth lanes */
        if (fabsf(dx) > 26.0f) {
            if (ground_ahead(w, e, 14.0f)) it->move_x = dx > 0 ? 1.0f : -1.0f;
        } else if (e->lane == p->lane && e->on_ground && e->ai_timer == 0) {
            it->attack_pressed = true;
            e->ai_timer = 80 + (int)(ge_rng_float(&w->rng) * 40.0f);
        }
    } else {
        if (fabsf(e->pos.x - e->ai_x0) > 40.0f && (e->pos.x - e->ai_x0) * (float)e->ai_state > 0) e->ai_state = -e->ai_state;
        e->facing = e->ai_state;
        if (!ground_ahead(w, e, 14.0f)) e->ai_state = -e->ai_state;
        it->move_x = 0.5f * (float)e->ai_state;
    }
}

/* A scripted player so `--autoplay` / the headless run can show a full fight. */
static uint32_t autoplay_mask(Demo *g) {
    const GeWorld *w = &g->world;
    const GeEntity *p = &w->ents[g->player];
    if (!p->active || p->state == GE_ST_DEAD) return 0;
    uint32_t b = GE_BTN_RIGHT;
    int tgt = -1;
    float best = 1e9f;
    for (int i = 0; i < GE_MAX_ENTS; i++) {
        const GeEntity *e = &w->ents[i];
        if (!e->active || e->team != 1 || e->state == GE_ST_DEAD) continue;
        float dx = e->pos.x - p->pos.x;
        if (dx > -20.0f && dx < best && fabsf(e->pos.y - p->pos.y) < 40.0f) { best = dx; tgt = i; }
    }
    bool fighting = false;
    if (tgt >= 0 && best < 70.0f) {
        const GeEntity *e = &w->ents[tgt];
        fighting = true;
        if (e->lane < p->lane) b |= GE_BTN_LANE_BACK;
        else if (e->lane > p->lane) b |= GE_BTN_LANE_FRONT;
        else if (best < 30.0f) { if (g->ap_tick % 7 < 2) b |= GE_BTN_ATTACK; }
        if (best < 30.0f) b &= ~GE_BTN_RIGHT;
    }
    (void)fighting;
    int tx = (int)floorf((p->pos.x + 14.0f) / GE_TILE), ty = (int)floorf((p->pos.y - 8.0f) / GE_TILE);
    if (ge_tilemap_get(&w->map, tx, ty) == GE_TILE_SOLID) b |= GE_BTN_JUMP;       /* wall ahead */
    if (p->on_ground && ge_world_ground_y(w, p->pos.x + 14.0f, p->pos.y - 1.0f) < 0) b |= GE_BTN_JUMP; /* pit ahead */
    if (!p->on_ground && p->vel.y > 0.0f && ge_world_ground_y(w, p->pos.x + 8.0f, p->pos.y - 1.0f) < 0) b |= GE_BTN_DASH;
    if (p->vel.y < 0 && !p->on_ground) b |= GE_BTN_JUMP;                            /* hold for full height */
    return b;
}

static void player_intent(Demo *g, const GeInput *in) {
    GeIntent *it = &g->world.intents[g->player];
    memset(it, 0, sizeof *it);
    it->move_x = ((in->down & GE_BTN_RIGHT) ? 1.0f : 0.0f) - ((in->down & GE_BTN_LEFT) ? 1.0f : 0.0f);
    it->jump_pressed = (in->pressed & GE_BTN_JUMP) != 0;
    it->jump_held = (in->down & GE_BTN_JUMP) != 0;
    it->attack_pressed = (in->pressed & GE_BTN_ATTACK) != 0;
    it->dash_pressed = (in->pressed & GE_BTN_DASH) != 0;
    it->drop_held = (in->down & GE_BTN_DOWN) != 0;
    it->lane_shift = ((in->pressed & GE_BTN_LANE_FRONT) ? 1 : 0) - ((in->pressed & GE_BTN_LANE_BACK) ? 1 : 0);
}

static int demo_init(void *user) {
    Demo *g = (Demo *)user;
    if (art_make_player(&g->player_spr) != 0 || art_make_goblin(&g->goblin_spr) != 0) {
        fprintf(stderr, "sprite clip parse failed\n");
        return 1;
    }
    make_defs(g);
    ge_world_init(&g->world, GE_SCREEN_W, GE_SCREEN_H, 1234);
    g->world.on_event = on_event;
    g->world.user = g;
    g->world.vignette = 26;
    g->world.low_detail = g->low_detail;
    art_make_tileset(&g->world.tileset);
    if (g->layers_path) {
        int line = 0, rc = ge_world_load_layers(&g->world, g->layers_path, &line);
        if (rc != GE_LAYERS_OK) {
            fprintf(stderr, "%s: %s (line %d)\n", g->layers_path,
                    rc == GE_LAYERS_EOPEN ? "cannot open" : rc == GE_LAYERS_EIMAGE ? "cannot load image" :
                    rc == GE_LAYERS_EFULL ? "too many layers" : "syntax error", line);
            return 1;
        }
    } else {
        art_add_layers(&g->world);
    }
    level_build(&g->world);

    spawn_player(g, 3 * GE_TILE + 8, 12 * GE_TILE);
    g->safe_x = g->world.ents[g->player].pos.x;
    g->world.cam.x = g->safe_x - GE_SCREEN_W / 2.0f;
    for (int i = 0; i < level_enemy_count; i++) {
        int e = ge_world_spawn(&g->world, &g->goblin_def, &g->goblin_spr, 1,
                               level_enemy_cols[i] * GE_TILE + 8, 12 * GE_TILE, 24);
        if (e < 0) continue;
        g->world.ents[e].lane = (int)(ge_rng_next(&g->world.rng) % 3) - 1;
        g->world.ents[e].facing = -1;
    }
    return 0;
}

static void demo_update(void *user, const GeInput *in) {
    Demo *g = (Demo *)user;
    GeWorld *w = &g->world;

    GeInput ap;
    if (g->autoplay) {
        static GeInput state;
        g->ap_tick++;
        ge_input_update(&state, autoplay_mask(g));
        ap = state;
        in = &ap;
    }

    GeEntity *p = &w->ents[g->player];
    if (p->active) {
        player_intent(g, in);
        if (p->on_ground && p->state != GE_ST_HURT && (w->tick % 30) == 0 &&
            ge_world_ground_y(w, p->pos.x - 10.0f, p->pos.y - 1.0f) >= 0 && ge_world_ground_y(w, p->pos.x + 10.0f, p->pos.y - 1.0f) >= 0)
            g->safe_x = p->pos.x;
        g->respawn_timer = 0;
    } else if (++g->respawn_timer > 60) {
        spawn_player(g, g->safe_x - 8.0f, 12 * GE_TILE - 40.0f);
        g->world.ents[g->player].invuln = 90;
        g->respawn_timer = 0;
    }

    for (int i = 0; i < GE_MAX_ENTS; i++)
        if (w->ents[i].active && w->ents[i].team == 1) goblin_ai(g, i);

    /* drifting fireflies keep the scene alive */
    if (w->tick % 9 == 0 && ge_world_particle_count(w) < 70)
        ge_world_emit(w, w->cam.x + ge_rng_range(&w->rng, 0, GE_SCREEN_W), w->cam.y + ge_rng_range(&w->rng, 70, 200),
                      0, 0, 0, 260 + (int)ge_rng_range(&w->rng, 0, 160), GE_ARGB(255, 190, 255, 140), 2,
                      GE_P_ADD | GE_P_WANDER | GE_P_TWINKLE);

    ge_world_update(w);
}

/* ---- HUD --------------------------------------------------------------- */

static void draw_bar(GeImage *fb, int x, int y, int w, int h, int val, int max, uint32_t col) {
    ge_fill_rect(fb, x - 1, y - 1, w + 2, h + 2, GE_ARGB(200, 10, 8, 20), 0);
    ge_fill_rect(fb, x, y, w, h, GE_RGB(50, 20, 30), 0);
    if (max > 0 && val > 0) ge_fill_rect(fb, x, y, w * val / max, h, col, 0);
}

static void demo_draw(void *user, GeImage *fb) {
    Demo *g = (Demo *)user;
    GeWorld *w = &g->world;
    ge_world_draw(w, fb);

    const GeEntity *p = &w->ents[g->player];
    draw_bar(fb, 8, 8, 84, 7, p->active ? p->hp : 0, p->max_hp, GE_RGB(220, 70, 80));
    for (int i = 0; i < p->combo && p->active; i++) /* combo pips */
        ge_fill_rect(fb, 8 + i * 8, 19, 6, 3, GE_RGB(255, 210, 90), 0);
    for (int i = 0; i < g->kills && i < 20; i++)
        ge_fill_rect(fb, 8 + i * 4, 26, 3, 3, GE_RGB(150, 200, 120), 0);

    for (int i = 0; i < GE_MAX_ENTS; i++) {
        const GeEntity *e = &w->ents[i];
        if (!e->active || e->team != 1 || e->hp >= e->max_hp || e->state == GE_ST_DEAD) continue;
        int sx = (int)(e->pos.x - w->cam.x) - 9, sy = (int)(e->pos.y - w->cam.y) - 34 + e->lane * 5;
        draw_bar(fb, sx, sy, 18, 2, e->hp, e->max_hp, GE_RGB(240, 120, 60));
    }
}

static void demo_shutdown(void *user) {
    Demo *g = (Demo *)user;
#ifdef DEMO_HEADLESS
    printf("kills=%d deaths=%d player_hp=%d x=%.0f checksum=%08x\n", g->kills, g->deaths,
           g->world.ents[g->player].hp, g->world.ents[g->player].pos.x, ge_null_last_checksum());
#endif
    ge_world_free(&g->world);
    ge_sprite_free(&g->player_spr);
    ge_sprite_free(&g->goblin_spr);
}

int main(int argc, char **argv) {
    static Demo g;
    int frames = 1800, dump_every = 0;
    const char *dump_dir = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--autoplay")) g.autoplay = true;
        else if (!strcmp(argv[i], "--low-detail")) g.low_detail = true;
        else if (!strcmp(argv[i], "--layers") && i + 1 < argc) g.layers_path = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-every") && i + 1 < argc) dump_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump-dir") && i + 1 < argc) dump_dir = argv[++i];
    }
#ifdef __3DS__
    g.low_detail = !APT_CheckNew3DS(); /* original 3DS: 268 MHz, drop the decorative layers */
#endif
#ifdef DEMO_HEADLESS
    ge_null_configure(frames, dump_every, dump_dir, NULL, NULL);
#else
    (void)frames; (void)dump_every; (void)dump_dir;
#endif
    GeGame game = { &g, demo_init, demo_update, demo_draw, demo_shutdown };
    GePlatformConfig cfg = { "Gloamwood - ge demo", GE_SCREEN_W, GE_SCREEN_H, 3 };
    return ge_run(&game, &cfg);
}
