/* Gloamwood: a Silksong-style platformer where some enemies challenge you to
 * a fighting-game duel. Everything game-specific lives in demo/; the engine
 * knows nothing about benches, masks or goblins. */

#include "demo.h"
#include "devlog.h"
#include "puzzle.h"
#include "ge/ge_engine.h"
#include "ge/ge_font.h"
#include "ge/ge_platform.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef DEMO_HEADLESS
#include "ge/ge_platform_null.h"
#endif
#ifdef __3DS__
#include <3ds.h>
#endif

#define DUEL_PRACTICE (-2)        /* g.duel for a practice duel: nobody in the room */

typedef enum { M_OVERWORLD, M_ALERT, M_TRANS_IN, M_BATTLE, M_TRANS_OUT, M_VICTORY, M_REPORT } Mode;
enum { B_SLEEP, B_INTRO, B_IDLE, B_SWING, B_CHARGE, B_LEAP, B_THROW, B_DEAD };

typedef struct {
    Sprites spr;
    Chars ch;
    GeWorld world;
    GeFight fight;
    bool fight_live;
    Mode mode;
    int mode_timer;

    World wd;                     /* every room, as data (the built-in world or mods/world.txt) */
    char world_problem[96];       /* why mods/world.txt wasn't used, if it wasn't */
    bool world_broken;            /* it failed to load: never save over it */
    bool to_paragon;              /* --paragon: start in the hall */
    const char *start_room;       /* --room */
    const char *export_world;     /* --export-world */
    const char *world_file;       /* --world: play this file instead of mods/world.txt */
    Puzzle pz;                    /* the room's levers, gates, crates... while it is played */
    RoomProgress progress[WORLD_MAX_ROOMS];   /* what the session remembers about each room */
    int keys;                     /* carried */
    int bonus_masks, bonus_ammo;  /* from relics */
    int door_fade, door_room;     /* going through a door: the fade, and where to */
    char door_to[24];
    GeParallaxLayer backdrops[BD_COUNT][GE_MAX_LAYERS];   /* each backdrop's layers, built once */
    int nbackdrop[BD_COUNT];
    int room;                     /* the one being played */
    int player;
    int ent_of[ROOM_MAX_THINGS];  /* entity per thing of the room, -1 = none or gone */
    bool beaten[WORLD_MAX_ROOMS][ROOM_MAX_THINGS];
    int duel;                     /* the thing being duelled, or DUEL_PRACTICE */
    bool first_strike;
    int duel_result;
    int duel_cd;
    int bench_room, bench_col, bench_row;   /* where you rested last */
    float safe_x, safe_y;
    int warp_fade;                /* spike respawn fade */
    bool hero_dying;
    int respawn_timer;

    /* the Paragon: what it has learned about the player, its brain, and this fight's def */
    GePlayerModel model;
    GeParagon brain;
    GeFighterDef paragon_def;
    int paragon_spawn;
    unsigned gifts;                       /* GIFT_* for every duelist and the boss beaten so far */
    int difficulty;                       /* -1 easy, 0 normal, +1 hard: masks, spikes, duelists, the boss (not the Paragon) */
    bool difficulty_set;                  /* given on the command line: don't load the saved one */
    char facts[6][72];
    int nfacts, learn_shown;
    bool fresh_profile, paragon_report, beat_all, hall_named, no_mods, no_hints;
    ModReport mods;
    bool practice;                        /* the current duel is a practice one: nothing is at stake */
    char paragon_say[2][72];              /* its two lines before the fight */
    bool bottom_dirty;                    /* the bottom screen needs redrawing */
    int tap_flash, bottom_frames;
    struct { char text[60]; uint32_t color; int count; } coach[4];   /* the duel coach's last lines, newest first */
    int coach_prev[9], coach_nfacts, coach_facts_tick;
    char coach_facts[5][72];
    bool coach_was_showing;
    int confirm_item, confirm_timer;      /* a pending "tap again to confirm" */

    int boss_spawn, boss_state, boss_timer, boss_combo;
    bool boss_engaged, boss_was_air;

    char msg[80];
    int msg_timer;

    bool autoplay, low_detail, god;
    const char *layers_path;
    int warp_col;
    GeInput bot_in;
    uint32_t bot_prev;
    int bot_climb;

    int kills, duels_won, duels_lost, deaths, heals;
    bool show_perf;                       /* frame-rate / CPU-time readout */
    uint64_t last_draw_us;
    float fps, update_ms, draw_ms;        /* smoothed */

    /* dev menu + play log */
    bool menu_open;
    int menu_sel, warp_sel;
    int detail_mode;                      /* 0 auto, 1 low, 2 full */
    bool auto_low;                        /* what "auto" picks on this console */
    bool log_wanted;
    const char *console;
    uint64_t start_us, now_us, next_sample_us;
    struct { int frames, slow; double dt_sum, dt_max, draw_sum, draw_max, update_sum, present_sum, bottom_sum, wait_sum; } win, tot, duel_perf;
    float present_ms;                     /* smoothed: sending the top screen (the 3DS's rotate copy) */
    int ticks_world, ticks_duel;
    uint32_t tick;
} Demo;

static Demo g;
static bool g_log;
/* events go to stderr with GLOAM_LOG=1, and to the play log file when it's on */
#define LOG(...) do { if (g_log) { fprintf(stderr, "[%6u] ", (unsigned)g.tick); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
                      devlog(__VA_ARGS__); } while (0)   /* written to the card at calm moments: see calm_flush */

static GeEntity *hero(void) { return &g.world.ents[g.player]; }

/* ---- the room being played, as data ---------------------------------------- */
static Room *room(void) { return &g.wd.rooms[g.room]; }
static Thing *thing(int i) { return &room()->things[i]; }
#define BEATEN(i) g.beaten[g.room][i]
static const Thing *find_thing(int kind) { int i = room_find(room(), kind, 0); return i >= 0 ? thing(i) : NULL; }
static float thing_x(const Thing *t) { return t->col * GE_TILE + 8.0f; }       /* its centre */
static float thing_feet(const Thing *t) { return (t->row + 1) * (float)GE_TILE; }   /* the floor it stands on */
/* the overworld entity kind a thing spawns, or -1 */
static int thing_entity_kind(int kind) {
    switch (kind) {
    case TH_CRAWLER: return EN_CRAWLER;
    case TH_BAT: return EN_BAT;
    case TH_SPITTER: return EN_SPITTER;
    case TH_GRUNT: return EN_GRUNT;
    case TH_DUELIST: return EN_DUELIST;
    case TH_BOSS: return EN_BOSS;
    default: return -1;
    }
}
/* a practice duel's opponent: Grobb at beginner strength, wherever you are */
static const Thing *practice_thing(void) {
    static Thing t;
    if (!t.name[0]) { t = thing_default(TH_DUELIST, 0, 0); t.style = STYLE_BRAWLER; t.level = 0; snprintf(t.name, sizeof t.name, "GROBB"); }
    return &t;
}
static const Thing *duel_thing(void) { return g.duel == DUEL_PRACTICE ? practice_thing() : thing(g.duel); }
static int duel_ent(void) { return g.duel >= 0 ? g.ent_of[g.duel] : -1; }
static int boss_ent(void) { return g.boss_spawn >= 0 ? g.ent_of[g.boss_spawn] : -1; }

/* Writing to the SD card stalls the game for tens of milliseconds on a 3DS, so the play log is written at
 * moments nobody is playing through: a black fade, a bench, the menu (and at least once a minute). */
static void calm_flush(void) { devlog_flush(); }

/* ---- difficulty ------------------------------------------------------------
 * One setting for the whole game (the dev menu's DIFFICULTY), saved between sessions. Easy is meant to be
 * genuinely easy for someone new to fighting games; the Paragon ignores it (it is unbeatable by design). */
static int hero_masks(void) { return (g.difficulty < 0 ? 8 : g.difficulty > 0 ? 4 : 6) + g.bonus_masks; }
static int boss_hp(void) { return g.difficulty < 0 ? 100 : g.difficulty > 0 ? 200 : 150; }
/* duelists' base levels: Grobb 0, Silke 1, Mosshide 2, Champion 3. Easy: all beginners but the Champion;
 * normal: one step gentler (Grobb is a beginner); hard: one step sharper. */
static int duel_cpu_level(int base) { return g.difficulty < 0 ? (base >= 3 ? 0 : -1) : ge_clampi(base + (g.difficulty > 0 ? 1 : -1), -1, 3); }
static float duel_cpu_damage(void) { return g.difficulty < 0 ? 0.5f : g.difficulty > 0 ? 1.1f : 0.75f; }
static float duel_player_damage(void) { return g.difficulty < 0 ? 1.4f : g.difficulty > 0 ? 1.0f : 1.15f; }
#ifdef __3DS__
#define SETTINGS_PATH "sdmc:/3ds/gloamwood/settings.txt"
#else
#define SETTINGS_PATH "gloamwood_settings.txt"
#endif
static void save_settings(void) {
    FILE *f = fopen(SETTINGS_PATH, "w");
    if (!f) return;
    fprintf(f, "difficulty %d\n", g.difficulty);
    fclose(f);
}
static void load_settings(void) {
    FILE *f = fopen(SETTINGS_PATH, "r");
    if (!f) return;
    int d;
    if (fscanf(f, "difficulty %d", &d) == 1) g.difficulty = ge_clampi(d, -1, 1);
    fclose(f);
}
static void say(const char *s, int ticks) { snprintf(g.msg, sizeof g.msg, "%s", s); g.msg_timer = ticks; }

/* ---- rooms ---------------------------------------------------------------- */

static const char *world_path(void) {
    static char p[192];
    snprintf(p, sizeof p, "%s/world.txt", mods_dir());
    return p;
}

/* The world to play: the built-in one, with each room in mods/world.txt (the editor saves there) laid over
 * it: a room of the same name is replaced, a new one is added. So later fixes to the built-in rooms still
 * reach you, and the file only holds what you made or changed. */
static void load_world(void) {
    world_builtin(&g.wd);
    const char *path = g.world_file ? g.world_file : world_path();
    if (g.no_mods && !g.world_file) return;
    FILE *f = fopen(path, "r");
    if (!f) return;
    fclose(f);
    static World file;                     /* (far too big for the 3DS's stack) */
    char err[120];
    int rc = world_load(&file, path, err, sizeof err);
    if (rc) {
        LOG("world: %s line %d: %s (playing the built-in world)", path, rc, err);
        snprintf(g.world_problem, sizeof g.world_problem, "WORLD.TXT LINE %d: %.70s", rc, err);
        g.world_broken = true;             /* the editor won't save over it */
        return;
    }
    int added = 0, replaced = world_overlay(&g.wd, &file, &added);
    LOG("world: %s: %d rooms replaced, %d added", path, replaced, added);
}

static const char *room_title(void) {
    static char t[32];
    if (!strcmp(room()->name, "gloamwood")) return "THE GLOAMWOOD";
    size_t i = 0;
    for (; room()->name[i] && i + 1 < sizeof t; i++) t[i] = (char)toupper((unsigned char)room()->name[i]);
    t[i] = 0;
    return t;
}

static void build_warps(void);
static void arena_set_exit(bool closed);

/* Make room r the one being played: its tilemap from its tiles; nothing spawned yet. */
static void room_build(int r) {
    g.room = r;
    room_build_tilemap(room(), &g.world.map);
    for (int i = 0; i < ROOM_MAX_THINGS; i++) g.ent_of[i] = -1;
    g.boss_spawn = g.paragon_spawn = -1;
    arena_set_exit(true);            /* the arena's far door is shut until its boss falls */
    build_warps();
}

/* ---- spawning ---------------------------------------------------------- */

/* the floor under a point (searching down from y), or near the bottom of the room if there is none */
static float ground_at(float x, float y) {
    float gy = ge_world_ground_y(&g.world, x, y);
    return gy < 0 ? (float)((room()->h - 3) * GE_TILE) : gy;
}
/* a column with no row to go by (--warp): the floor under the first open cell from the top, so a room with
 * a ceiling puts you inside it, not on its roof */
static float ground_in_column(float x) {
    int col = (int)floorf(x / GE_TILE), row = 0;
    while (row < g.world.map.h - 1 && ge_tilemap_get(&g.world.map, col, row) == GE_TILE_SOLID) row++;
    return ground_at(x, (float)(row * GE_TILE));
}

static void spawn_hero_at(float x, float y_from) {
    g.player = ge_world_spawn(&g.world, &g.ch.hero, &g.spr.hero, 0, x, ground_at(x, y_from), hero_masks());
    g.world.follow = g.player;
    g.pz.hero = g.player;
    g.safe_x = x;
    g.safe_y = hero()->pos.y;
    g.world.cam.x = x - SCREEN_W * 0.5f;
}
static void spawn_hero(float x) { spawn_hero_at(x, ground_in_column(x) - 1.0f); }

static GeActorDef g_duel_defs[3];
static int g_base_ammo;            /* the hero's needles as the character files say, before relics */
static void apply_relic_ammo(void) { g.ch.hero.ammo_max = g_base_ammo + g.bonus_ammo; }

static const GeSprite *duel_sprite(int variant) {
    return variant == STYLE_PARAGON ? &g.spr.paragon : variant == STYLE_FENCER ? &g.spr.fencer : &g.spr.brawler;
}

static bool ordinary_enemy(int kind) { return kind == EN_CRAWLER || kind == EN_BAT || kind == EN_SPITTER || kind == EN_GRUNT; }

/* Everything in the level. With `ordinary_only`, just the ordinary enemies are put back where they started,
 * alive and at full health, the way Hollow Knight refills a room when you rest: duelists you beat and the
 * boss stay beaten. */
static void spawn_level_part(bool ordinary_only) {
    for (int i = 0; i < room()->nthings; i++) {
        const Thing *s = thing(i);
        int kind = thing_entity_kind(s->kind);
        if (kind < 0) continue;
        if (ordinary_only) {
            if (!ordinary_enemy(kind)) continue;
            int old = g.ent_of[i];   /* (only if that slot still holds this enemy: slots get reused) */
            if (old >= 0 && old != g.player && g.world.ents[old].kind == kind) g.world.ents[old].active = false;
            g.ent_of[i] = -1;
            BEATEN(i) = false;
        }
        float x = thing_x(s), y = thing_feet(s);
        const GeActorDef *d = NULL;
        const GeSprite *sp = NULL;
        int hp = 10;
        switch (kind) {
        case EN_CRAWLER: d = &g.ch.crawler; sp = &g.spr.crawler; hp = 10; break;
        case EN_BAT:     d = &g.ch.bat; sp = &g.spr.bat; hp = 8; break;
        case EN_SPITTER: d = &g.ch.spitter; sp = &g.spr.spitter; hp = 12; break;
        case EN_GRUNT:   d = &g.ch.grunt; sp = &g.spr.brawler; hp = 22; break;
        case EN_DUELIST: d = &g_duel_defs[ge_clampi(s->style, 0, 2)]; sp = duel_sprite(s->style); hp = 999; if (s->style == STYLE_PARAGON) g.paragon_spawn = i; break;
        case EN_BOSS:    d = &g.ch.warden; sp = &g.spr.warden; hp = boss_hp(); g.boss_spawn = i; break;
        }
        g.ent_of[i] = BEATEN(i) ? -1 : ge_world_spawn(&g.world, d, sp, 1, x, y, hp);
        if (g.ent_of[i] < 0) continue;
        GeEntity *e = &g.world.ents[g.ent_of[i]];
        e->kind = kind;
        e->facing = -1;
        e->ai_x0 = x;
        e->ai_y0 = y;
        e->ai_state = -1;
        if (ordinary_only)   /* a puff where each one comes back, so a respawn can be seen */
            for (int k = 0; k < 10; k++)
                ge_world_emit(&g.world, x + ge_rng_range(&g.world.rng, -8, 8), y - ge_rng_range(&g.world.rng, 0, 14), ge_rng_range(&g.world.rng, -0.6f, 0.6f),
                              -0.5f, 0, 40, GE_ARGB(220, 200, 170, 255), 2, GE_P_ADD);
    }
}

static void spawn_level(void) { spawn_level_part(false); }
static void respawn_enemies(void) { spawn_level_part(true); LOG("enemies respawned"); }

/* The boss arena's two doors: a column of h tiles standing on the arena's floor row. */
static void arena_door(int col, bool closed) {
    const Thing *a = find_thing(TH_ARENA);
    if (!a) return;
    GeTilemap *m = &g.world.map;
    for (int y = a->row - a->h + 1; y <= a->row; y++)
        if (col >= 0 && col < m->w && y >= 0 && y < m->h) {
            m->type[y * m->w + col] = closed ? GE_TILE_SOLID : GE_TILE_EMPTY;
            m->vis[y * m->w + col] = TV_STONE;
        }
}
static void arena_set_gate(bool closed) { const Thing *a = find_thing(TH_ARENA); if (a) arena_door(arena_gate_col(a), closed); }
static void arena_set_exit(bool closed) { const Thing *a = find_thing(TH_ARENA); if (a) arena_door(arena_exit_col(a), closed); }
static bool boss_beaten(void) { return g.boss_spawn >= 0 && BEATEN(g.boss_spawn); }
static bool boss_beaten_anywhere(void) {
    for (int r = 0; r < g.wd.nrooms; r++)
        for (int i = 0; i < g.wd.rooms[r].nthings; i++) if (g.wd.rooms[r].things[i].kind == TH_BOSS && g.beaten[r][i]) return true;
    return false;
}

static void reset_boss(void) {
    g.boss_engaged = false;
    g.boss_state = B_SLEEP;
    g.world.cam.locked = false;
    arena_set_gate(false);
    arena_set_exit(!boss_beaten());
    int bi = boss_ent();
    if (bi >= 0 && g.world.ents[bi].active) {
        GeEntity *b = &g.world.ents[bi];
        b->hp = b->max_hp;
        b->pos = (GeVec2){ b->ai_x0, b->ai_y0 };
        b->vel = (GeVec2){ 0, 0 };
        b->state = GE_ST_IDLE;
    }
}

static void respawn_enemies(void);

/* the backdrop's layers into the world (they stay owned by g.backdrops) */
static void set_backdrop(int bd) {
    bd = ge_clampi(bd, 0, BD_COUNT - 1);
    if (g.nbackdrop[bd] == 0) bd = BD_FOREST;
    for (int i = 0; i < g.nbackdrop[bd]; i++) { g.world.layers[i] = g.backdrops[bd][i]; g.world.layers[i].borrowed = true; }
    g.world.nlayers = g.nbackdrop[bd];
}

static int count_relics(int r, bool found) {
    int n = 0;
    for (int i = 0; i < g.wd.rooms[r].nthings; i++)
        if (g.wd.rooms[r].things[i].kind == TH_RELIC && (!found || g.progress[r].done[i])) n++;
    return n;
}

/* Go into room r, at the door called `door` (NULL: its start, else its first bench). Everything but the
 * hero is cleared; the room's enemies not beaten come back; its puzzles are as the session left them. */
static void room_enter(int r, const char *door) {
    GeWorld *w = &g.world;
    for (int i = 0; i < GE_MAX_ENTS; i++) if (i != g.player) w->ents[i].active = false;
    for (int i = 0; i < GE_MAX_SHOTS; i++) w->shots[i].active = false;
    for (int i = 0; i < GE_MAX_PARTS; i++) w->parts[i].life = 0;
    g.boss_engaged = false;
    g.boss_state = B_SLEEP;
    w->cam.locked = false;
    room_build(r);
    set_backdrop(room()->backdrop);
    spawn_level();
    if (boss_beaten()) arena_set_exit(false);
    const Thing *at = NULL;
    for (int i = room_find(room(), TH_DOOR, 0); door && i >= 0 && !at; i = room_find(room(), TH_DOOR, i + 1))
        if (!strcmp(thing(i)->name, door)) at = thing(i);
    if (!at) at = find_thing(TH_START);
    if (!at) at = find_thing(TH_BENCH);
    GeEntity *p = hero();
    float x = at ? thing_x(at) : 2.5f * GE_TILE, y = at ? thing_feet(at) : (float)((room()->h - 1) * GE_TILE);
    p->pos = (GeVec2){ x, y };
    p->vel = (GeVec2){ 0, 0 };
    p->state = GE_ST_IDLE;
    g.safe_x = x; g.safe_y = y;
    w->cam.x = x - SCREEN_W * 0.5f;
    w->cam.y = y - SCREEN_H * 0.65f;
    puzzle_enter(&g.pz, room(), &g.progress[r], w, g.player, &g.keys);
    g.duel_cd = 90;                        /* a moment to look around before anyone challenges you */
    int relics = count_relics(r, false);
    char buf[64];
    if (relics) snprintf(buf, sizeof buf, "%s   RELICS %d/%d", room_title(), count_relics(r, true), relics);
    else snprintf(buf, sizeof buf, "%s", room_title());
    say(buf, 120);
    LOG("entered room %s%s%s", room()->name, door ? " by door " : "", door ? door : "");
}

/* Back to the last bench: its room as you first find it (crates home, timed levers off), what the session
 * remembers kept (pulled levers, broken stone, keys and relics). */
static void respawn_at_bench(void) {
    if (hero()->active) hero()->active = false;
    if (g.bench_room != g.room) room_enter(g.bench_room, NULL);
    else { room_build_tilemap(room(), &g.world.map); arena_set_exit(true); }
    reset_boss();
    respawn_enemies();
    spawn_hero_at(g.bench_col * GE_TILE + 8.0f, (float)((g.bench_row + 1) * GE_TILE - 20));
    hero()->invuln = 60;
    puzzle_enter(&g.pz, room(), &g.progress[g.room], &g.world, g.player, &g.keys);
}

/* ---- events ------------------------------------------------------------ */

static int spawn_of(int ent) {
    for (int i = 0; i < room()->nthings; i++) if (g.ent_of[i] == ent) return i;
    return -1;
}

static void start_duel_alert(int spawn, bool first_strike);

/* A practice duel from the dev menu: Grobb at beginner strength, nothing won or lost. */
static void start_practice(void) {
    if (g.mode != M_OVERWORLD || !hero()->active) { say("PRACTICE STARTS FROM THE OVERWORLD", 90); return; }
    g.menu_open = false;
    g.practice = true;
    start_duel_alert(DUEL_PRACTICE, false);
    LOG("practice duel");
}

static void start_duel_alert(int spawn, bool first_strike) {
    if (g.mode != M_OVERWORLD) return;
    g.mode = M_ALERT;
    g.mode_timer = 0;
    g.duel = spawn;
    g.first_strike = first_strike;
    g.msg_timer = 0;                       /* the alert line takes the message slot */
    memset(&g.world.intents[g.player], 0, sizeof(GeIntent));
    LOG("duel alert: %s%s (difficulty %s)", duel_thing()->name, first_strike ? " (first strike)" : "",
        g.difficulty < 0 ? "easy" : g.difficulty > 0 ? "hard" : "normal");
}

static void on_event(void *user, GeEventType ev, int a, int b) {
    (void)user;
    GeWorld *w = &g.world;
    if (ev == GE_EV_KILL) {
        if (b == g.player) {
            g.deaths++;
            g.model.ow[GE_OW_DEATH]++;
            int bi = boss_ent();
            LOG("hero killed at col %d by %s (boss hp %d)", (int)(w->ents[b].pos.x / GE_TILE), a >= 0 ? "a strike" : "contact/shot",
                bi >= 0 ? w->ents[bi].hp : -1);
            return;
        }
        if (w->ents[b].kind == EN_BOSS) {
            g.boss_state = B_DEAD;
            g.world.cam.locked = false;
            arena_set_gate(false);
            arena_set_exit(false);
            g.mode = M_VICTORY; g.mode_timer = 0;
            g.boss_engaged = false;
            LOG("boss defeated");
            if (g.boss_spawn >= 0) BEATEN(g.boss_spawn) = true;
            for (int i = 0; i < 40; i++)
                ge_world_emit(w, w->ents[b].pos.x, w->ents[b].pos.y - 30, ge_rng_range(&w->rng, -3, 3), ge_rng_range(&w->rng, -4, 0), 0.08f, 60,
                              GE_ARGB(255, 255, 200, 120), 3, GE_P_ADD);
        }
        g.kills++;
        int s = spawn_of(b);
        if (s >= 0) { BEATEN(s) = true; if (ordinary_enemy(thing_entity_kind(thing(s)->kind))) g.ent_of[s] = -1; }
    } else if (ev == GE_EV_HIT && b >= 0 && w->ents[b].kind == EN_DUELIST && a == g.player) {
        w->ents[b].hp = w->ents[b].max_hp;          /* you can't slash a duelist down: you get a head start */
        int s = spawn_of(b);
        if (s >= 0 && g.duel_cd == 0) start_duel_alert(s, true);
    } else if (ev == GE_EV_HIT && b == g.player) {
        g.model.ow[GE_OW_HURT]++;
        ge_world_shake(w, 3.0f);
    } else if (ev == GE_EV_POGO && a == g.player) {
        g.model.ow[GE_OW_POGO]++;
    } else if (ev == GE_EV_HAZARD && a == g.player) {
        GeEntity *p = hero();
        if (p->state == GE_ST_DEAD || g.warp_fade > 0) return;
        if (g.difficulty >= 0) p->hp -= 1;     /* on easy, spikes just send you back to safe ground */
        p->flash = 6;
        p->invuln = 30;                 /* one spike hit per contact, not one per tick */
        ge_world_shake(w, 4.0f);
        /* This runs inside the physics step, which sets the actor's state when it returns --
         * so a death is only flagged here and applied after the world update. */
        if (p->hp <= 0) { p->hp = 0; g.hero_dying = true; }
        else g.warp_fade = 40;
        LOG("spikes at col %d, masks left %d", (int)(p->pos.x / GE_TILE), p->hp);
    } else if (ev == GE_EV_SHOT_TILE) {
        puzzle_shot_tile(&g.pz, a, b);
    } else if (ev == GE_EV_FELL && a == g.player) {
        g.deaths++;
    } else if (ev == GE_EV_CHANNEL && a == g.player) {
        GeEntity *p = hero();
        p->hp = p->hp + 2 > p->max_hp ? p->max_hp : p->hp + 2;
        g.heals++;
        g.model.ow[GE_OW_HEAL]++;
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
                          spawn_of(idx) == g.paragon_spawn ? GE_ARGB(220, 255, 210, 110) : GE_ARGB(200, 190, 120, 255), 2, GE_P_ADD);
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
        const Thing *a = find_thing(TH_ARENA);    /* its arena, or (without one) close enough to see it */
        bool inside = a ? p->pos.x > (a->col + 3) * GE_TILE && p->pos.x < (a->col + a->w) * GE_TILE
                        : fabsf(p->pos.x - b->pos.x) < 150.0f && fabsf(p->pos.y - b->pos.y) < 60.0f;
        if (p->active && inside) {
            g.boss_engaged = true;
            g.boss_state = B_INTRO; g.boss_timer = 70;
            arena_set_gate(true);
            w->cam.locked = a != NULL;
            if (a) {
                w->cam.lock_x0 = (float)(a->col - 1) * GE_TILE;
                w->cam.lock_x1 = (float)(a->col + a->w) * GE_TILE;
            }
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
    if (g.warp_fade > 0 || g.door_fade > 0) return;
    it->move_x = ((in->down & GE_BTN_RIGHT) ? 1.0f : 0.0f) - ((in->down & GE_BTN_LEFT) ? 1.0f : 0.0f);
    it->move_y = ((in->down & GE_BTN_DOWN) ? 1.0f : 0.0f) - ((in->down & GE_BTN_UP) ? 1.0f : 0.0f);
    it->jump_pressed = (in->pressed & GE_BTN_B) != 0;
    it->jump_held = (in->down & GE_BTN_B) != 0;
    it->attack_pressed = (in->pressed & GE_BTN_Y) != 0;
    it->dash_pressed = (in->pressed & GE_BTN_R) != 0;
    it->tool_pressed = (in->pressed & GE_BTN_X) != 0;
    it->skill_pressed = (in->pressed & GE_BTN_A) != 0;
    it->channel_held = (in->down & GE_BTN_L) && p->hp < p->max_hp;
    g.model.ow[GE_OW_SLASH] += it->attack_pressed;
    g.model.ow[GE_OW_NEEDLE] += it->tool_pressed;
    g.model.ow[GE_OW_JUMP] += it->jump_pressed;
    g.model.ow[GE_OW_DASH] += it->dash_pressed;

    /* rest at a bench: full health, and it becomes your respawn point */
    for (int i = room_find(room(), TH_BENCH, 0); i >= 0; i = room_find(room(), TH_BENCH, i + 1)) {
        const Thing *t = thing(i);
        float bx = thing_x(t);
        if (fabsf(p->pos.y - thing_feet(t)) > 20.0f) continue;
        if (fabsf(p->pos.x - bx) < 18.0f && p->on_ground && (in->pressed & GE_BTN_UP)) {
            p->hp = p->max_hp;
            g.bench_col = t->col;
            g.bench_row = t->row;
            g.bench_room = g.room;
            say("RESTED HERE. THE FOREST'S CREATURES RETURN", 150);
            for (int r = 0; r < g.wd.nrooms; r++)          /* every room's creatures, the way Hollow Knight does it */
                for (int k = 0; k < g.wd.rooms[r].nthings; k++)
                    if (ordinary_enemy(thing_entity_kind(g.wd.rooms[r].things[k].kind))) g.beaten[r][k] = false;
            respawn_enemies();
            LOG("rested at the bench in column %d", g.bench_col);
            calm_flush();
        } else if (fabsf(p->pos.x - bx) < 18.0f && p->on_ground && (in->pressed & GE_BTN_DOWN)) {
            start_practice();                       /* DOWN at a bench: a no-stakes practice duel */
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
        GeEntity *bs = &w->ents[boss_ent() >= 0 ? boss_ent() : g.player];
        float dx = bs->pos.x - p->pos.x;
        int toward = dx > 0 ? 1 : -1;
        b = 0;
        bool telegraph = bs->state == GE_ST_ATTACK && bs->anim.frame == 0;
        if (p->hp <= 2 && p->meter >= 33 && fabsf(dx) > 120.0f) return GE_BTN_L;             /* heal */
        if (telegraph && fabsf(dx) < 90.0f && (g.tick & 1)) b |= GE_BTN_R | (toward > 0 ? GE_BTN_RIGHT : GE_BTN_LEFT); /* dash through */
        else if (fabsf(dx) > 46.0f) b |= toward > 0 ? GE_BTN_RIGHT : GE_BTN_LEFT;
        else if (fabsf(dx) < 26.0f) b |= toward > 0 ? GE_BTN_LEFT : GE_BTN_RIGHT;
        if (fabsf(dx) < 52.0f && (g.tick % 6) < 2) b |= GE_BTN_Y;
        if (p->ammo > 0 && fabsf(dx) < 150.0f && (g.tick % 40) == 0) b |= GE_BTN_X | (toward > 0 ? GE_BTN_RIGHT : GE_BTN_LEFT);
        if (p->meter >= 66 && fabsf(dx) < 40.0f) b |= GE_BTN_A;
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
    for (int i = room_find(room(), TH_BENCH, 0); i >= 0; i = room_find(room(), TH_BENCH, i + 1))
        if (fabsf(p->pos.x - thing_x(thing(i))) < 10.0f && p->hp < p->max_hp && (g.tick & 8)) b |= GE_BTN_UP;

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
            else if (!p->air_dashed && p->dash_cd == 0) b |= GE_BTN_R;
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
        for (int k = -2; k <= 2; k++)   /* (gates, crates and breakable stone can vanish: not safe ground) */
            solid = solid && ge_tilemap_get(&g.world.map, tx + k, ty) == GE_TILE_SOLID && !puzzle_owns_cell(&g.pz, tx + k, ty);
        if (solid && !ge_world_touches_tile(&g.world, around, GE_TILE_SPIKE)) {
            g.safe_x = p->pos.x; g.safe_y = p->pos.y;
        }
    }
    if (g.warp_fade > 0 && --g.warp_fade == 20) {   /* the screen is black */
        calm_flush();
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
    for (int i = 0; i < room()->nthings; i++) {
        if (thing(i)->kind != TH_DUELIST || g.ent_of[i] < 0) continue;
        GeEntity *e = &g.world.ents[g.ent_of[i]];
        if (!e->active) continue;
        float dx = p->pos.x - e->pos.x, dy = fabsf(p->pos.y - e->pos.y);
        bool touch = ge_rect_overlap(ge_entity_hurtbox(p), ge_entity_hurtbox(e));
        bool sight = fabsf(dx) < 110.0f && dy < 40.0f && (dx * (float)e->facing > 0 || fabsf(dx) < 40.0f) &&
                     clear_line(e->pos.x, e->pos.y - 16, p->pos.x, p->pos.y - 12);
        if (touch || sight) { e->facing = dx > 0 ? 1 : -1; start_duel_alert(i, false); return; }
    }
}

static void coach_reset(void);

/* What the Paragon has taken so far: one technique per duelist (and the boss) beaten. */
static unsigned earned_gifts(void) {
    static const struct { const char *name; unsigned gift; } T[] = {
        { "GROBB", GIFT_GROBB }, { "SILKE", GIFT_SILKE }, { "MOSSHIDE", GIFT_MOSSHIDE }, { "THE CHAMPION", GIFT_CHAMPION }, { "THORNWARDEN", GIFT_THORNWARDEN } };
    unsigned gifts = 0;
    for (int r = 0; r < g.wd.nrooms; r++)          /* whichever room they were in */
        for (int i = 0; i < g.wd.rooms[r].nthings; i++) {
            const Thing *t = &g.wd.rooms[r].things[i];
            if (g.beaten[r][i] && (t->kind == TH_DUELIST || t->kind == TH_BOSS))
                for (unsigned k = 0; k < sizeof T / sizeof T[0]; k++) if (!strcmp(t->name, T[k].name)) gifts |= T[k].gift;
        }
    return gifts;
}

static bool paragon_duel(void) { return duel_thing()->kind == TH_DUELIST && duel_thing()->style == STYLE_PARAGON; }

static void begin_battle(void) {
    const Thing *s = duel_thing();
    bool fencer = s->style == STYLE_FENCER;
    bool final = paragon_duel();
    const GeFighterDef *opp = fencer ? &g.ch.f_fencer : &g.ch.f_brawler;
    if (final) {
        g.gifts = earned_gifts();
        paragon_build_def(&g.ch, &g.spr, g.gifts, &g.paragon_def);
        opp = &g.paragon_def;
    }
    ge_fight_init(&g.fight, &g.ch.f_hero, &g.spr.hero, opp, duel_sprite(s->style), &g.world, 1000 + g.tick);
    g.fight.names[0] = "WREN";
    g.fight.names[1] = s->name;
    if (final) g.fight.world.backdrop_pattern = encounter_pattern(s->name);   /* only the Paragon fights inside its pattern */
    if (!final && room()->backdrop == BD_CAVE) {     /* a cave's duels stand on stone, not grass */
        GeTilemap *am = &g.fight.world.map;
        for (int i = 0; i < am->w * am->h; i++)
            if (am->type[i] == GE_TILE_SOLID) am->vis[i] = (i >= am->w && am->type[i - am->w] == GE_TILE_SOLID) ? TV_STONE : TV_STONE_TOP;
    }
    g.fight.rounds_to_win = s->rounds;
    ge_fight_observe(&g.fight, &g.model, 0);          /* every duel teaches the Paragon */
    if (!final) { g.fight.damage_scale[1] = duel_cpu_damage(); g.fight.damage_scale[0] = duel_player_damage(); }
    if (g.practice) { g.fight.damage_scale[1] = 0.5f; g.fight.damage_scale[0] = 1.4f; g.fight.names[1] = "GROBB (PRACTICE)"; }
    g.fight.forgiving_guard = g.difficulty <= 0;      /* easy and normal: L also guards in the air and after your own swing */
    g.fight.forgiving_tech = g.difficulty <= 0 ? 1u : 0u;   /* ...and a tap of L breaks a throw (logs: 0% of throws escaped) */
    g.fight.hints = 1;                                 /* teaching aids for the player's side */
    coach_reset();
    if (final) {
        int learn = paragon_learn_level(&g.model, g.gifts);
        ge_paragon_init(&g.brain, &g.model);
        ge_paragon_set_learn(&g.brain, learn, (g.gifts & GIFT_CHAMPION) ? 1 : 0);
        g.brain.decay_rate = 0.03f;                    /* in this fight it forgets old habits fast: change your style and it follows */
        g.fight.infinite_clock = true;
        g.fight.finisher_tick = PARAGON_CLOCK_TICKS;
        if (g.gifts & GIFT_CHAMPION) g.fight.f[1].meter = GE_FIGHT_METER;
        g.learn_shown = learn;
        g.fight.intro_ticks = 230;                     /* time to hear what it says */
        /* what it says before the fight: drawn from what it has learned. [story hook: its real voice] */
        char facts[3][72];
        int nf = ge_model_describe(&g.model, facts, 3);
        if (g.model.finals == 0 && g.model.duels == 0) snprintf(g.paragon_say[0], sizeof g.paragon_say[0], "I HAVE BEEN WAITING FOR YOU.");
        else if (g.model.finals == 0 && g.model.duels == 1) snprintf(g.paragon_say[0], sizeof g.paragon_say[0], "I WATCHED YOUR ONE DUEL.");
        else if (g.model.finals == 0) snprintf(g.paragon_say[0], sizeof g.paragon_say[0], "I HAVE WATCHED ALL %u OF YOUR DUELS.", (unsigned)g.model.duels);
        else snprintf(g.paragon_say[0], sizeof g.paragon_say[0], "AGAIN. THAT IS %u TIMES NOW.", (unsigned)g.model.finals + 1);
        if (nf > 0) snprintf(g.paragon_say[1], sizeof g.paragon_say[1], "%s.", facts[g.model.finals % (unsigned)nf]);
        else snprintf(g.paragon_say[1], sizeof g.paragon_say[1], "SHOW ME HOW YOU FIGHT.");
        GeTilemap *am = &g.fight.world.map;            /* its arena floor is the hall's dressed stone */
        for (int x = 0; x < am->w; x++)
            for (int y = 0; y < am->h; y++)
                if (am->type[y * am->w + x] == GE_TILE_SOLID) am->vis[y * am->w + x] = (y > 0 && am->type[(y - 1) * am->w + x] == GE_TILE_SOLID) ? TV_STONE : TV_STONE_TOP;
        LOG("PARAGON fight begins: learn %d, react %d ticks, gifts 0x%x, model confidence %.2f, duels seen %u",
            learn, g.brain.react, g.gifts, ge_model_confidence(&g.model), (unsigned)g.model.duels);
    } else if (g.first_strike) {
        g.fight.start_hp[1] = opp->max_hp * 85 / 100;
    }
    g.fight_live = true;
    memset(&g.duel_perf, 0, sizeof g.duel_perf);
}

static GeFightInput fight_input(const GeInput *in) {
    GeFightInput f = { 0, 0, 0, 0, -1 };
    f.x = ((in->down & GE_BTN_RIGHT) ? 1 : 0) - ((in->down & GE_BTN_LEFT) ? 1 : 0);
    f.y = ((in->down & GE_BTN_DOWN) ? 1 : 0) - ((in->down & GE_BTN_UP) ? 1 : 0);
    /* fighting layout: Y light, X medium, A heavy, B sword thrust; L blocks (held), R dashes; H+K together = super */
    static const uint32_t map[6][2] = { { GE_BTN_Y, GE_FB_L }, { GE_BTN_X, GE_FB_M }, { GE_BTN_A, GE_FB_H }, { GE_BTN_B, GE_FB_K },
                                        { GE_BTN_L, GE_FB_BLOCK }, { GE_BTN_R, GE_FB_DASH } };
    for (int i = 0; i < 6; i++) {
        if (in->pressed & map[i][0]) f.pressed |= (uint8_t)map[i][1];
        if (in->down & map[i][0]) f.held |= (uint8_t)map[i][1];
    }
    return f;
}

static void end_battle(void) {
    GeEntity *p = hero();
    int ent = duel_ent();
    LOG("duel over: %s vs WREN -> %s (rounds %d-%d), duel ran at %.1f fps avg, worst frame %.1f ms",
        duel_thing()->name, g.duel_result == 0 ? "WON" : "LOST", g.fight.f[0].wins, g.fight.f[1].wins,
        g.duel_perf.dt_sum > 0 ? g.duel_perf.frames / (g.duel_perf.dt_sum / 1000.0) : 0.0, g.duel_perf.dt_max);
    LOG("  your defence: blocked %d, hit at the wrong height while blocking %d, hit while not blocking %d, thrown %d",
        g.fight.stat_blocked[0], g.fight.stat_wrong_height[0], g.fight.stat_hit[0], g.fight.stat_thrown[0]);
    LOG("  those hits came while you were: attacking %d, in the air %d, already in a combo %d, standing open %d, holding L but busy %d",
        g.fight.stat_hit_attacking[0], g.fight.stat_hit_air[0], g.fight.stat_hit_combo[0], g.fight.stat_hit_open[0], g.fight.stat_hit_guard_busy[0]);
    g.model.duels++;
    g.model.rounds += g.fight.f[0].wins + g.fight.f[1].wins;
    if (g.duel_result == 0) g.model.wins++; else g.model.losses++;
    g.model.absorbed = (uint32_t)__builtin_popcount(earned_gifts());
    if (g.practice) {                                    /* nothing at stake: back where you were */
        g.practice = false;
        say(g.duel_result == 0 ? "PRACTICE WON. TRY IT ON HARD WHEN YOU'RE READY" : "PRACTICE OVER. NOTHING LOST", 150);
        g.duel_cd = 120;
        p->invuln = 60;
        paragon_profile_save(&g.model);
        return;
    }
    if (paragon_duel()) {
        g.model.finals++;
        g.duels_lost++;
        g.nfacts = ge_model_describe(&g.model, g.facts, 6);
        LOG("PARAGON duel over: %s, finisher %s, you lasted %d s. It learned:", g.duel_result == 1 ? "LOST as designed" : "WON??",
            g.fight.finisher_seen ? "played" : "not seen", g.fight.fight_ticks / 60);
        for (int i = 0; i < g.nfacts; i++) LOG("  - %s", g.facts[i]);
        g.paragon_report = true;
        int near = -1;                                    /* wake at the bench nearest to it: the rematch is a few steps away */
        for (int i = room_find(room(), TH_BENCH, 0); i >= 0; i = room_find(room(), TH_BENCH, i + 1))
            if (near < 0 || abs(thing(i)->col - duel_thing()->col) < abs(thing(near)->col - duel_thing()->col)) near = i;
        if (near >= 0) { g.bench_col = thing(near)->col; g.bench_row = thing(near)->row; g.bench_room = g.room; }
        say("", 1);
        respawn_at_bench();
    } else if (g.duel_result == 0) {
        g.duels_won++;
        if (g.duel >= 0) BEATEN(g.duel) = true;
        if (ent >= 0) {
            GeEntity *e = &g.world.ents[ent];
            for (int i = 0; i < 24; i++)
                ge_world_emit(&g.world, e->pos.x, e->pos.y - 12, ge_rng_range(&g.world.rng, -2, 2), ge_rng_range(&g.world.rng, -3, 0), 0.05f, 50,
                              GE_ARGB(255, 200, 140, 255), 2, GE_P_ADD);
            e->active = false;
            if (g.duel >= 0) g.ent_of[g.duel] = -1;
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
    paragon_profile_save(&g.model);
}

/* ---- game callbacks ---------------------------------------------------- */

static void draw_props(void *user, GeImage *fb, int camx, int camy) {
    (void)user;
    const Thing *h = find_thing(TH_HALL);
    if (h) paragon_draw_hall(fb, camx, camy, g.tick, h->col, h->w, h->row - h->h + 1, h->row + 1);
    for (int i = room_find(room(), TH_BENCH, 0); i >= 0; i = room_find(room(), TH_BENCH, i + 1))
        art_draw_bench(fb, (int)thing_x(thing(i)) - camx, (int)thing_feet(thing(i)) - camy);
    puzzle_draw(&g.pz, fb, camx, camy, g.tick);
}

/* Every character: the built-in art and clips, then whatever the mods folder replaces, then the defs. */
static int load_characters(void) {
    art_make_sheets(&g.spr);
    if (chars_parse(&g.spr) != 0) return 1;
    memset(&g.mods, 0, sizeof g.mods);
    if (!g.no_mods) mods_apply_sprites(&g.spr, &g.mods);
    if (chars_build(&g.ch, &g.spr) != 0) return 1;
    if (!g.no_mods) mods_apply_moves(&g.ch, &g.spr, &g.mods);
    g_base_ammo = g.ch.hero.ammo_max;
    apply_relic_ammo();                                 /* relics found this session */
    for (int v = 0; v < 3; v++) {
        const GeSprite *sp = duel_sprite(v);
        g_duel_defs[v] = g.ch.duelist;
        g_duel_defs[v].clip_idle = g_duel_defs[v].clip_jump = g_duel_defs[v].clip_fall = ge_sprite_find_clip(sp, "idle");
        g_duel_defs[v].clip_run = ge_sprite_find_clip(sp, "run");
        g_duel_defs[v].clip_hurt = ge_sprite_find_clip(sp, "hurt");
        g_duel_defs[v].clip_dead = ge_sprite_find_clip(sp, "dead");
    }
    paragon_forget_base();
    return 0;
}

static void log_mods(void) {
    if (g.mods.nlines == 0) { LOG("mods: none in %s", mods_dir()); return; }
    LOG("mods from %s: %d loaded, %d with problems", mods_dir(), g.mods.loaded, g.mods.errors);
    for (int i = 0; i < g.mods.nlines; i++) LOG("  %s", g.mods.lines[i]);
}

static void reload_mods(void) {
    if (g.mode != M_OVERWORLD && g.mode != M_VICTORY) { say("RELOAD MODS OUTSIDE A DUEL", 90); return; }
    art_free(&g.spr);
    if (load_characters() != 0) { say("RELOAD FAILED: SEE THE LOG", 120); return; }
    for (int i = 0; i < GE_MAX_ENTS; i++) {           /* nothing may keep playing a clip that is gone */
        GeEntity *e = &g.world.ents[i];
        if (!e->active || !e->def) continue;
        e->anim.clip = e->def->clip_idle >= 0 ? e->def->clip_idle : 0;
        e->anim.frame = e->anim.timer = 0; e->anim.done = false;
        if (e->state == GE_ST_ATTACK) e->state = GE_ST_IDLE;
    }
    for (int i = 0; i < GE_MAX_SHOTS; i++) g.world.shots[i].active = false;
    log_mods();
    char buf[64];
    snprintf(buf, sizeof buf, "MODS RELOADED: %d LOADED, %d PROBLEMS", g.mods.loaded, g.mods.errors);
    say(buf, 150);
}

static int demo_init(void *user) {
    (void)user;
    if (load_characters() != 0) return 1;
    ge_model_init(&g.model);
    if (!g.fresh_profile) paragon_profile_load(&g.model);
    if (!g.fresh_profile && !g.difficulty_set) load_settings();
    GeWorld *w = &g.world;
    ge_world_init(w, SCREEN_W, SCREEN_H, 1234);
    w->on_event = on_event;
    w->draw_props = draw_props;
    w->user = &g;
    w->vignette = 26;
    w->low_detail = g.low_detail;
    art_make_tileset(&w->tileset);
    /* each backdrop's layers are built once into a scratch world and lent to the world as rooms change */
    static GeWorld scratch;
    for (int bd = 0; bd < BD_COUNT; bd++) {
        ge_world_init(&scratch, SCREEN_W, SCREEN_H, 1);
        if (bd == BD_FOREST && g.layers_path) {
            int line = 0, rc = ge_world_load_layers(&scratch, g.layers_path, &line);
            if (rc != GE_LAYERS_OK) {
                fprintf(stderr, "%s: %s (line %d)\n", g.layers_path,
                        rc == GE_LAYERS_EOPEN ? "cannot open" : rc == GE_LAYERS_EIMAGE ? "cannot load image" :
                        rc == GE_LAYERS_EFULL ? "too many layers" : "syntax error", line);
                return 1;
            }
        } else if (bd == BD_FOREST) art_add_layers(&scratch);
        else art_add_cave_layers(&scratch);
        memcpy(g.backdrops[bd], scratch.layers, sizeof(GeParallaxLayer) * (size_t)scratch.nlayers);
        g.nbackdrop[bd] = scratch.nlayers;
    }
    load_world();
    if (g.to_paragon) { int h = room_find(&g.wd.rooms[0], TH_HALL, 0); g.warp_col = h >= 0 ? g.wd.rooms[0].things[h].col + 10 : 1; }   /* (in its column the first open cell is inside the hall) */
    room_build(0);
    set_backdrop(room()->backdrop);
    if (g.beat_all)                                   /* dev: pretend every duelist and the boss are already beaten */
        for (int i = 0; i < room()->nthings; i++)
            if ((thing(i)->kind == TH_DUELIST && thing(i)->style != STYLE_PARAGON) || thing(i)->kind == TH_BOSS) BEATEN(i) = true;
    spawn_level();
    const Thing *hall = find_thing(TH_HALL), *start = find_thing(TH_START);
    int bench = room_find(room(), TH_BENCH, 0);
    if ((hall && g.warp_col >= hall->col) || g.beat_all) arena_set_exit(false);
    g.bench_room = 0;
    g.bench_col = bench >= 0 ? thing(bench)->col : 0;
    g.bench_row = bench >= 0 ? thing(bench)->row : room()->h - 2;
    spawn_hero((g.warp_col > 0 ? g.warp_col : start ? start->col : 1) * GE_TILE + 8.0f);
    puzzle_enter(&g.pz, room(), &g.progress[0], w, g.player, &g.keys);
    if (!hall || g.warp_col < hall->col) say(room_title(), 120);
    if (g.start_room) {
        int r = world_find_room(&g.wd, g.start_room);
        if (r < 0) { fprintf(stderr, "no room called %s\n", g.start_room); return 1; }
        if (r != 0) room_enter(r, NULL);
    }
    return 0;
}

static void demo_update_inner(const GeInput *in);

/* ---- dev menu ------------------------------------------------------------ */

/* Dev warps: the landmarks of the room you are in, from its things. */
typedef struct { char name[28]; int col, row; } Warp;   /* row: where to look down for the floor from */
static Warp g_warps[32];
static int g_nwarps;
static void build_warps(void) {
    g_nwarps = 0;
    int benches = 0;
    for (int i = 0; i < room()->nthings && g_nwarps < 32; i++) {
        const Thing *t = thing(i);
        Warp *wp = &g_warps[g_nwarps];
        switch (t->kind) {
        case TH_START:   snprintf(wp->name, sizeof wp->name, "START"); wp->col = t->col; break;
        case TH_BENCH:   snprintf(wp->name, sizeof wp->name, "BENCH %d", ++benches); wp->col = t->col + 1; break;
        case TH_DUELIST: snprintf(wp->name, sizeof wp->name, "%s", t->name); wp->col = t->col - 7; break;
        case TH_ARENA:   snprintf(wp->name, sizeof wp->name, "BOSS ARENA"); wp->col = t->col - 4; break;
        case TH_HALL:    snprintf(wp->name, sizeof wp->name, "THE HALL"); wp->col = t->col + 5; break;
        case TH_DOOR:    snprintf(wp->name, sizeof wp->name, "DOOR %s", t->name); wp->col = t->col; break;
        case TH_MARK:    snprintf(wp->name, sizeof wp->name, "%s", t->name); wp->col = t->col; break;
        default: continue;
        }
        if (wp->col < 1) wp->col = 1;
        wp->row = t->row;
        g_nwarps++;
    }
    for (int i = 1; i < g_nwarps; i++)                 /* left to right */
        for (int j = i; j > 0 && g_warps[j].col < g_warps[j - 1].col; j--) { Warp t = g_warps[j]; g_warps[j] = g_warps[j - 1]; g_warps[j - 1] = t; }
    if (g_nwarps == 0) { snprintf(g_warps[0].name, sizeof g_warps[0].name, "LEFT EDGE"); g_warps[0].col = 1; g_warps[0].row = -1; g_nwarps = 1; }
    if (g.warp_sel >= g_nwarps) g.warp_sel = 0;
}
#define NWARPS g_nwarps
#define WARPS g_warps
enum { MI_PERF, MI_DETAIL, MI_LOG, MI_GOD, MI_DUEL, MI_WARP, MI_PRACTICE, MI_MARK, MI_MODS, MI_EXPORT, MI_RESET, MI_CLOSE, MI_COUNT };

static void apply_detail(void) {
    bool low = g.detail_mode == 0 ? g.auto_low : g.detail_mode == 1;
    g.low_detail = low;
    g.world.low_detail = low;
    if (g.fight_live) g.fight.world.low_detail = low;
}

static void set_logging(bool on) {
    g.log_wanted = on;
    if (on && !devlog_is_open() && devlog_open()) {
        devlog("session start: Gloamwood, built %s %s, console %s, detail %s, difficulty %s", __DATE__, __TIME__, g.console,
               g.low_detail ? "low" : "full", g.difficulty < 0 ? "easy" : g.difficulty > 0 ? "hard" : "normal");
        devlog("a perf line every 10 s: fps avg/worst, share of frames slower than 60 fps, update/draw/present ms (present = sending the picture to the screen; vsync wait = waiting for the screen's next refresh: time to spare when the frame was on time)");
        devlog_flush();
    } else if (!on && devlog_is_open()) {
        devlog("play log switched off from the dev menu");
        devlog_close();
    }
}

static void warp_to(int col, int row) {   /* row < 0: the first floor from the top of the column */
    GeEntity *p = hero();
    if (!p->active || g.mode != M_OVERWORLD) return;
    const Thing *a = find_thing(TH_ARENA), *h = find_thing(TH_HALL);
    if (g.boss_engaged && a && col < a->col) reset_boss();
    if (h && col >= h->col) arena_set_exit(false);
    float x = col * GE_TILE + 8.0f;
    p->pos = (GeVec2){ x, row < 0 ? ground_in_column(x) : ground_at(x, (float)(row * GE_TILE)) };
    p->vel = (GeVec2){ 0, 0 };
    p->state = GE_ST_IDLE;
    g.safe_x = x; g.safe_y = p->pos.y;
    g.world.cam.x = x - SCREEN_W * 0.5f;
    g.duel_cd = 120;
    LOG("dev: warped to column %d", col);
}

/* What the room's puzzles did this tick: words on screen, the play log, rewards. */
static void puzzle_events(void) {
    for (int k = 0; k < g.pz.nev; k++) {
        const PuzzleEvent *e = &g.pz.ev[k];
        const Thing *t = e->thing >= 0 ? thing(e->thing) : NULL;
        char buf[72];
        switch (e->kind) {
        case PZ_KEY: say("A KEY. IT OPENS A LOCKED GATE", 120); LOG("key taken in %s (carrying %d)", room()->name, g.keys); break;
        case PZ_UNLOCK: say("UNLOCKED", 90); LOG("locked gate opened in %s", room()->name); break;
        case PZ_SECRET: say("A WAY THROUGH", 90); LOG("breakable stone broken in %s", room()->name); break;
        case PZ_LEVER: if (t) LOG("lever (wire %d, %s) %s in %s", t->ch, t->mode == LEVER_ONCE ? "once" : t->mode == LEVER_TOGGLE ? "toggle" : "timed",
                                 g.pz.p[e->thing].on ? "on" : "off", room()->name); break;
        case PZ_RELIC: {
            GeEntity *p = hero();
            const char *gift = "";
            if (t && t->gives == GIVE_MASK) { g.bonus_masks++; p->max_hp++; p->hp++; gift = ": ONE MORE MASK"; }
            else if (t && t->gives == GIVE_NEEDLE) { g.bonus_ammo++; apply_relic_ammo(); p->ammo++; gift = ": ONE MORE NEEDLE"; }
            else if (t && t->gives == GIVE_SILK) { p->meter = p->def->meter_max; gift = ": SILK FULL"; }
            snprintf(buf, sizeof buf, "FOUND %.24s%s", t ? t->name : "A RELIC", gift);
            say(buf, 180);
            LOG("relic found in %s: %s (%d of %d here)", room()->name, t ? t->name : "?", count_relics(g.room, true), count_relics(g.room, false));
            break;
        }
        case PZ_DOOR:
            if (t && g.door_fade == 0) {
                char room_name[32] = "", door_name[24] = "";
                const char *colon = strchr(t->target, ':');
                size_t n = colon ? (size_t)(colon - t->target) : strlen(t->target);
                if (n >= sizeof room_name) n = sizeof room_name - 1;
                memcpy(room_name, t->target, n); room_name[n] = 0;
                if (colon) snprintf(door_name, sizeof door_name, "%s", colon + 1);
                int r = world_find_room(&g.wd, room_name);
                if (r < 0) { say(t->target[0] ? "THIS DOOR LEADS NOWHERE YET" : "THIS DOOR ISN'T LINKED (EDITOR: LINK)", 120); break; }
                g.door_room = r;
                snprintf(g.door_to, sizeof g.door_to, "%s", door_name);
                g.door_fade = 40;
            }
            break;
        default: break;
        }
    }
}

/* Going through a door: fade to black, change room in the dark, fade back in. */
static void door_update(void) {
    if (g.door_fade <= 0) return;
    if (--g.door_fade == 20) { calm_flush(); room_enter(g.door_room, g.door_to[0] ? g.door_to : NULL); }
}

/* Things that are hard to undo (switching the log off, wiping the Paragon's memory) need a second tap or A
 * press within two seconds: a stray touch on the bottom screen should never cost a play log. */
static bool confirmed(int item) {
    if (g.confirm_item == item && g.confirm_timer > 0) { g.confirm_item = -1; g.confirm_timer = 0; return true; }
    g.confirm_item = item;
    g.confirm_timer = 120;
    return false;
}

/* A difficulty change applies at once: masks (gained ones come full), and the boss if it isn't mid-fight. */
static void apply_difficulty(void) {
    GeEntity *p = hero();
    int before = p->max_hp;
    p->max_hp = hero_masks();
    if (p->max_hp > before) p->hp += p->max_hp - before;
    if (p->hp > p->max_hp) p->hp = p->max_hp;
    int bi = boss_ent();
    if (bi >= 0 && !g.boss_engaged) g.world.ents[bi].hp = g.world.ents[bi].max_hp = boss_hp();
    LOG("dev: difficulty now %s", g.difficulty < 0 ? "easy" : g.difficulty > 0 ? "hard" : "normal");
}

/* One place that changes a menu item, for the D-pad (menu open, SELECT) and for taps on the bottom screen alike.
 * dir: -1 / +1 to step a value, act: the A button or a tap on the middle of the row. */
static void menu_apply(int item, int dir, bool act) {
    g.bottom_dirty = true;
    switch (item) {
    case MI_PERF:   if (act || dir) g.show_perf = !g.show_perf; break;
    case MI_DETAIL: if (act || dir) { g.detail_mode = (g.detail_mode + (dir < 0 ? 2 : 1)) % 3; apply_detail(); LOG("dev: detail now %s", g.low_detail ? "low" : "full"); } break;
    case MI_LOG:
        if (!(act || dir)) break;
        if (g.log_wanted && !confirmed(MI_LOG)) break;                     /* turning it off takes a second tap */
        set_logging(!g.log_wanted);
        break;
    case MI_GOD:    if (act || dir) { g.god = !g.god; LOG("dev: god mode %s", g.god ? "on" : "off"); } break;
    case MI_DUEL:
        if (act && !dir) dir = g.difficulty >= 1 ? -2 : 1;                   /* a tap cycles EASY > NORMAL > HARD */
        if (dir) { g.difficulty = ge_clampi(g.difficulty + dir, -1, 1); apply_difficulty(); save_settings(); }
        break;
    case MI_RESET:
        if (act && confirmed(MI_RESET)) { ge_model_init(&g.model); paragon_profile_save(&g.model); LOG("dev: the Paragon's memory was wiped"); say("PARAGON MEMORY WIPED", 90); g.menu_open = false; }
        break;
    case MI_WARP:
        if (dir) g.warp_sel = (g.warp_sel + NWARPS + dir) % NWARPS;
        if (act) { warp_to(WARPS[g.warp_sel].col, WARPS[g.warp_sel].row); g.menu_open = false; }
        break;
    case MI_MARK:
        if (act) { LOG("--- marked by the player (something worth looking at happened here) ---"); devlog_flush(); say("LOG MARKED", 60); g.menu_open = false; }
        break;
    case MI_PRACTICE: if (act) start_practice(); break;
    case MI_MODS:     if (act) { reload_mods(); g.menu_open = false; } break;
    case MI_EXPORT:
        if (act) {
            int n = mods_export(&g.spr, &g.ch);
            char buf[72];
            snprintf(buf, sizeof buf, n ? "EXPORTED %d FILES TO MODS/_EXPORT" : "EXPORT FAILED (CAN'T WRITE THE FOLDER)", n);
            say(buf, 180);
            LOG("dev: exported %d character files to %s/_export", n, mods_dir());
        }
        break;
    case MI_CLOSE:  if (act || dir) g.menu_open = !g.menu_open; break;
    }
}

static void menu_input(const GeInput *in) {
    if (in->pressed & (GE_BTN_SELECT | GE_BTN_B)) { g.menu_open = false; g.bottom_dirty = true; return; }
    if (in->pressed & GE_BTN_UP) { g.menu_sel = (g.menu_sel + MI_COUNT - 1) % MI_COUNT; g.bottom_dirty = true; }
    if (in->pressed & GE_BTN_DOWN) { g.menu_sel = (g.menu_sel + 1) % MI_COUNT; g.bottom_dirty = true; }
    int dir = (in->pressed & GE_BTN_RIGHT) ? 1 : (in->pressed & GE_BTN_LEFT) ? -1 : 0;
    bool act = (in->pressed & GE_BTN_A) != 0;
    if (dir || act) menu_apply(g.menu_sel, dir, act);
}

/* ---- the bottom screen: the dev menu lives here --------------------------
 * On a 3DS this is the touch screen; on a PC it is the panel under the game,
 * and the mouse taps it. It is always showing and always tappable; SELECT also
 * pauses the game and lets the D-pad drive it. */

#define BM_ROW_Y 26
#define BM_ROW_H 13
#define BM_ARROW_W 30

static bool item_steps(int i) { return i == MI_DETAIL || i == MI_DUEL || i == MI_WARP; }

static bool coach_showing(void);

static void bottom_touch(const GeInput *in) {
    if (!in->touch_pressed || coach_showing()) return;   /* during a duel the bottom screen is the coach: no stray taps */
    int row = (in->touch_y - BM_ROW_Y) / BM_ROW_H;
    if (in->touch_y < BM_ROW_Y || row < 0 || row >= MI_COUNT) return;
    int dir = 0;
    if (item_steps(row) && in->touch_x < BM_ARROW_W) dir = -1;
    else if (item_steps(row) && in->touch_x >= GE_BOTTOM_W - BM_ARROW_W) dir = 1;
    g.menu_sel = row;
    g.tap_flash = 8;
    menu_apply(row, dir, dir == 0);
}

/* ---- the duel coach: in duels the bottom screen shows the moves and explains what just happened ------ */

static bool coach_showing(void) { return !g.menu_open && (g.mode == M_BATTLE || (g.mode == M_TRANS_OUT && g.fight_live)); }

static void coach_say(const char *msg, uint32_t col) {
    if (!strncmp(g.coach[0].text, msg, strlen(msg))) {   /* the same thing again: count it instead of repeating */
        g.coach[0].count++;
        snprintf(g.coach[0].text, sizeof g.coach[0].text, "%s X%d", msg, g.coach[0].count);
        g.bottom_dirty = true;
        return;
    }
    memmove(&g.coach[1], &g.coach[0], sizeof g.coach[0] * 3);
    g.coach[0].count = 1;
    snprintf(g.coach[0].text, sizeof g.coach[0].text, "%s", msg);
    g.coach[0].color = col;
    g.bottom_dirty = true;
}

/* compare the fight's counters with last tick's and turn each change into a line of advice */
static void coach_update(void) {
    if (!g.fight_live) return;
    const GeFight *f = &g.fight;
    int now[9] = { f->stat_blocked[0], f->stat_wrong_height[0], f->stat_hit_attacking[0], f->stat_hit_air[0], f->stat_hit_open[0],
                   f->stat_hit_guard_busy[0], f->stat_thrown[0], f->stat_hit[1] + f->stat_wrong_height[1], f->stat_blocked[1] };
    const uint32_t good = GE_RGB(140, 230, 160), bad = GE_RGB(255, 170, 120), info = GE_RGB(200, 200, 230);
    if (now[0] > g.coach_prev[0]) coach_say("BLOCKED! NOW HIT BACK WHILE THEY RECOVER", good);
    if (now[1] > g.coach_prev[1]) coach_say("WRONG HEIGHT. HOLD L: IT BLOCKS EVERY HEIGHT", bad);
    if (now[2] > g.coach_prev[2]) coach_say("HIT MID-SWING. BLOCK FIRST, THEN HIT BACK", bad);
    if (now[3] > g.coach_prev[3])
        coach_say(g.fight.forgiving_guard ? "HIT IN THE AIR. HOLD L WHILE JUMPING" : "HIT IN THE AIR. DON'T JUMP AT THEM", bad);
    if (now[4] > g.coach_prev[4]) coach_say("HIT OPEN. HOLD L WHEN THEY START A SWING", bad);
    if (now[5] > g.coach_prev[5]) coach_say("L HELD, BUT YOU WERE BUSY (ATTACK OR DASH)", bad);
    if (now[6] > g.coach_prev[6]) coach_say(g.fight.forgiving_tech & 1u ? "GRABBED! TAP L RIGHT AWAY TO BREAK IT" : "GRABBED! PRESS Y+X AT ONCE TO BREAK IT", bad);
    if (now[7] > g.coach_prev[7]) coach_say("YOUR HIT LANDED", good);
    if (now[8] > g.coach_prev[8]) coach_say("THEY BLOCKED. TRY DOWN+B (LOW) OR Y+X (THROW)", info);
    memcpy(g.coach_prev, now, sizeof now);
}

static void coach_reset(void) {
    memset(g.coach, 0, sizeof g.coach);
    memset(g.coach_prev, 0, sizeof g.coach_prev);
    g.coach_facts_tick = 0;
    coach_say(g.difficulty <= 0 ? "TIP: HOLD L TO GUARD. BLOCKS LOW AND HIGH" : "TIP: HOLD BACK OR L TO BLOCK", GE_RGB(200, 200, 230));
}

static void draw_coach(GeImage *fb) {
    const uint32_t ink = GE_RGB(20, 14, 30), dim = GE_RGB(150, 140, 180), hi = GE_RGB(255, 236, 160);
    bool paragon = paragon_duel();
    ge_clear(fb, GE_RGB(16, 10, 30));
    ge_fill_rect(fb, 0, 0, fb->w, 20, paragon ? GE_RGB(46, 36, 20) : GE_RGB(34, 22, 58), 0);
    ge_fill_rect(fb, 0, 20, fb->w, 1, paragon ? GE_RGB(240, 196, 90) : GE_RGB(190, 120, 255), 0);
    char buf[96];
    snprintf(buf, sizeof buf, "WREN VS %s", g.fight.names[1] ? g.fight.names[1] : "?");
    ge_text_outlined(fb, 6, 6, buf, paragon ? GE_RGB(255, 220, 140) : GE_RGB(220, 190, 255), ink, 1);
    ge_text(fb, fb->w - 6 - ge_text_width("SELECT: DEV MENU", 1), 7, "SELECT: DEV MENU", dim, 1);
    int y = 26;
    if (paragon) {   /* what it believes about you, right now */
        ge_text(fb, 6, y, "IT IS WATCHING. WHAT IT BELIEVES:", GE_RGB(255, 214, 120), 1);
        y += 12;
        for (int i = 0; i < g.coach_nfacts && i < 5; i++, y += 11) ge_text(fb, 10, y, g.coach_facts[i], GE_RGB(240, 230, 200), 1);
        if (g.coach_nfacts == 0) { ge_text(fb, 10, y, "NOTHING YET. IT IS STILL LEARNING YOU.", GE_RGB(240, 230, 200), 1); y += 11; }
        y += 4;
        ge_text(fb, 6, y, "IT CANNOT BE BEATEN. SURVIVE AND SEE WHAT IT DOES.", dim, 1);
        y += 14;
    } else {
        static const char *const MOVES[] = {
            "Y LIGHT    X MEDIUM    A HEAVY",
            "B THRUST: SLOW, LONGEST REACH (DOWN+B: LOW)",
            "HOLD L: GUARD. GRABBED? TAP L TO BREAK IT",
            "R DASH (BACK+R: BACKDASH)   UP: JUMP",
            "Y+X: THROW, UP CLOSE (ALSO BREAKS A THROW)",
            "A+B: SUPER, WHEN THE BLUE BAR IS FULL",
            "DOWN, DOWN-FWD, FWD + ATTACK: SHOT",
            "FWD, DOWN, DOWN-FWD + ATTACK: RISING CUT",
            "DOWN, DOWN-BACK, BACK + X OR A: RUSH",
        };
        ge_text(fb, 6, y, "YOUR MOVES", hi, 1);
        y += 12;
        for (unsigned i = 0; i < sizeof MOVES / sizeof MOVES[0]; i++, y += 10) {
            const char *line = MOVES[i];
            if (i == 2 && !(g.fight.forgiving_tech & 1u)) line = "HOLD L: GUARD (STOPS ALL BUT THROWS)";   /* hard: Y+X only */
            ge_text(fb, 10, y, line, GE_RGB(220, 214, 236), 1);
        }
        y += 4;
    }
    ge_fill_rect(fb, 0, y, fb->w, 1, GE_RGB(60, 46, 90), 0);
    y += 5;
    ge_text(fb, 6, y, "WHAT JUST HAPPENED", hi, 1);
    y += 12;
    for (int i = 0; i < 4 && y < fb->h - 8; i++, y += 11)
        if (g.coach[i].text[0]) ge_text(fb, 10, y, g.coach[i].text, i == 0 ? g.coach[i].color : dim, 1);
}

static bool draw_bottom(void *user, GeImage *fb) {
    (void)user;
    if (coach_showing()) {
        coach_update();
        if (paragon_duel() && g.tick >= (uint32_t)g.coach_facts_tick) {   /* refresh its beliefs every two seconds */
            g.coach_nfacts = ge_model_describe(&g.model, g.coach_facts, 5);
            g.coach_facts_tick = (int)g.tick + 120;
            g.bottom_dirty = true;
        }
        if (!g.bottom_dirty && (g.bottom_frames++ % 30) != 0) return false;
        g.bottom_dirty = false;
        draw_coach(fb);
        g.coach_was_showing = true;
        return true;
    }
    if (g.coach_was_showing) { g.coach_was_showing = false; g.bottom_dirty = true; }   /* back to the dev menu */
    if (g.tap_flash > 0 && --g.tap_flash == 0) g.bottom_dirty = true;
    if (g.confirm_timer > 0 && --g.confirm_timer == 0) { g.confirm_item = -1; g.bottom_dirty = true; }
    /* only send it when something changed, plus a refresh twice a second for the live numbers */
    if (!g.bottom_dirty && (g.bottom_frames++ % 30) != 0) return false;
    g.bottom_dirty = false;
    char buf[96];
    const uint32_t ink = GE_RGB(20, 14, 30), dim = GE_RGB(150, 140, 180), hi = GE_RGB(255, 236, 160);
    ge_clear(fb, GE_RGB(16, 10, 30));
    ge_fill_rect(fb, 0, 0, fb->w, 22, GE_RGB(34, 22, 58), 0);
    ge_fill_rect(fb, 0, 22, fb->w, 1, GE_RGB(190, 120, 255), 0);
    ge_text_outlined(fb, 6, 4, "DEV MENU", GE_RGB(220, 190, 255), ink, 2);
    const char *state = g.menu_open ? "PAUSED (SELECT)" : "TAP A ROW";
    ge_text(fb, fb->w - 6 - ge_text_width(state, 1), 9, state, g.menu_open ? hi : dim, 1);

    const char *detail = g.detail_mode == 0 ? (g.auto_low ? "AUTO (LOW)" : "AUTO (FULL)") : g.detail_mode == 1 ? "LOW" : "FULL";
    const char *val[MI_COUNT] = { g.show_perf ? "ON" : "OFF", detail, devlog_is_open() ? "ON" : (g.log_wanted ? "NO SD?" : "OFF"),
                                  g.god ? "ON" : "OFF", g.difficulty < 0 ? "EASY" : g.difficulty > 0 ? "HARD" : "NORMAL",
                                  WARPS[g.warp_sel].name, "TAP", "TAP", "TAP", "TAP",
                                  g.confirm_item == MI_RESET ? "TAP AGAIN TO WIPE" : "TAP", g.menu_open ? "RESUME" : "PAUSE" };
    if (g.confirm_item == MI_LOG) val[MI_LOG] = "TAP AGAIN: OFF";
    const char *label[MI_COUNT] = { "SPEED READOUT", "DETAIL", "PLAY LOG", "GOD MODE", "DIFFICULTY", "WARP (TAP TO GO)", "PRACTICE DUEL",
                                    "MARK THIS MOMENT", "RELOAD MODS", "EXPORT CHARACTERS", "WIPE PARAGON MEMORY", "GAME" };
    for (int i = 0; i < MI_COUNT; i++) {
        int y = BM_ROW_Y + i * BM_ROW_H;
        bool sel = i == g.menu_sel && (g.menu_open || g.tap_flash > 0);
        ge_fill_rect(fb, 2, y, fb->w - 4, BM_ROW_H - 2, sel ? GE_RGB(70, 46, 110) : (i & 1) ? GE_RGB(24, 16, 42) : GE_RGB(30, 20, 50), 0);
        uint32_t c = sel ? hi : GE_RGB(220, 214, 236);
        int tx = 10;
        if (item_steps(i)) {
            ge_fill_rect(fb, 2, y, BM_ARROW_W - 4, BM_ROW_H - 2, GE_RGB(46, 32, 76), 0);
            ge_fill_rect(fb, fb->w - BM_ARROW_W + 2, y, BM_ARROW_W - 4, BM_ROW_H - 2, GE_RGB(46, 32, 76), 0);
            ge_text(fb, 12, y + 3, "<", c, 1);
            ge_text(fb, fb->w - 18, y + 3, ">", c, 1);
            tx = BM_ARROW_W + 4;
        }
        ge_text(fb, tx, y + 3, label[i], c, 1);
        int vx = (item_steps(i) ? fb->w - BM_ARROW_W - 4 : fb->w - 10) - ge_text_width(val[i], 1);
        ge_text(fb, vx, y + 3, val[i], i == MI_GOD && g.god ? GE_RGB(255, 140, 120) : c, 1);
    }
    int y = BM_ROW_Y + MI_COUNT * BM_ROW_H + 3;
    uint64_t secs = (g.now_us - g.start_us) / 1000000u;
    snprintf(buf, sizeof buf, "PLAY TIME %s   %s", devlog_hms(secs), g.console);
    ge_text(fb, 6, y, buf, GE_RGB(160, 230, 170), 1);
    snprintf(buf, sizeof buf, "FPS %d  UPDATE %d.%d  DRAW %d.%d  PRESENT %d.%d MS", (int)(g.fps + 0.5f), (int)g.update_ms,
             (int)(g.update_ms * 10) % 10, (int)g.draw_ms, (int)(g.draw_ms * 10) % 10, (int)g.present_ms, (int)(g.present_ms * 10) % 10);
    ge_text(fb, 6, y + 11, buf, GE_RGB(120, 255, 140), 1);
    if (g.mods.nlines == 0) snprintf(buf, sizeof buf, "MODS: NONE IN %s", mods_dir());
    else snprintf(buf, sizeof buf, "MODS: %d LOADED, %d PROBLEMS (DETAILS IN THE LOG)", g.mods.loaded, g.mods.errors);
    ge_text(fb, 6, y + 22, buf, g.mods.errors ? GE_RGB(255, 130, 110) : dim, 1);
    if (devlog_is_open()) snprintf(buf, sizeof buf, "LOG WRITING TO %s", devlog_path());
    else snprintf(buf, sizeof buf, g.log_wanted ? "LOG NOT WRITING: CAN'T OPEN THE FILE" : "LOG OFF");
    ge_text(fb, 6, y + 33, buf, devlog_is_open() ? GE_RGB(160, 230, 170) : GE_RGB(255, 130, 110), 1);
    snprintf(buf, sizeof buf, "PARAGON KNOWS %d%% OF YOU (%u DUELS)", (int)(ge_model_confidence(&g.model) * 100.0f), (unsigned)g.model.duels);
    ge_text(fb, 6, y + 44, buf, GE_RGB(255, 214, 120), 1);
    return true;
}

static void demo_update(void *user, const GeInput *in) {
    (void)user;
    uint64_t t0 = ge_plat_time_us();
    bottom_touch(in);                                      /* the bottom screen works paused or not */
    if (g.menu_open) menu_input(in);                       /* the game is paused underneath */
    else if (in->pressed & GE_BTN_SELECT) { g.menu_open = true; g.menu_sel = 0; g.bottom_dirty = true; calm_flush(); }
    else {
        demo_update_inner(in);
        if (g.mode == M_BATTLE) g.ticks_duel++; else g.ticks_world++;
    }
    g.update_ms += ((float)(ge_plat_time_us() - t0) / 1000.0f - g.update_ms) * 0.1f;
}

static void demo_update_inner(const GeInput *in) {
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
        /* the victory words show for a few seconds, then it is the overworld again (duels, the Paragon's hall) */
        if (g.mode == M_VICTORY && g.mode_timer >= 300) { g.mode = M_OVERWORLD; g.mode_timer = 0; }
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
        puzzle_pre_update(&g.pz, &w->intents[g.player]);
        for (int i = 0; i < GE_MAX_ENTS; i++) {
            if (!w->ents[i].active || w->ents[i].team != 1) continue;
            if (w->ents[i].kind == EN_BOSS) boss_ai(i);
            else enemy_ai(i);
        }
        const Thing *hall = find_thing(TH_HALL);
        bool near_hall = hall && p->pos.x < (hall->col + hall->w + 2) * GE_TILE && fabsf(p->pos.y - thing_feet(hall)) < 6 * GE_TILE;
        bool in_hall = near_hall && p->pos.x >= (hall->col - 2) * GE_TILE;
        w->foreground_off = near_hall && p->pos.x >= (hall->col - 12) * GE_TILE;   /* indoors: no forest grass in front */
        /* inside the hall its walls, ceiling and floor cover the whole view: don't paint the forest behind them. (The
         * hall runs to the room's right edge and fills the screen's height there; elsewhere this is off.) */
        bool hall_fills = hall && hall->col + hall->w + 2 >= room()->w && room()->h <= ROOM_MIN_H;
        int open_x = hall_fills ? hall->col * GE_TILE - (int)w->cam.x : SCREEN_W;   /* screen column where the hall's solid art begins */
        w->background_off = open_x <= 0;
        w->background_clip_x = open_x > 0 && open_x < SCREEN_W ? open_x : 0;
        if (in_hall && !g.hall_named) { g.hall_named = true; say("THE STILL HALL", 150); }
        if (w->tick % 9 == 0 && ge_world_particle_count(w) < 90)   /* fireflies; in the hall, slow gold dust */
            ge_world_emit(w, w->cam.x + ge_rng_range(&w->rng, 0, SCREEN_W), w->cam.y + ge_rng_range(&w->rng, in_hall ? 100 : 70, 200), 0,
                          in_hall ? 0.05f : 0.0f, 0, 260 + (int)ge_rng_range(&w->rng, 0, 160),
                          in_hall ? GE_ARGB(255, 255, 214, 120) : GE_ARGB(255, 190, 255, 140), in_hall ? 1 : 2, GE_P_ADD | GE_P_WANDER | GE_P_TWINKLE);
        ge_world_update(w);
        hero_bookkeeping();
        puzzle_update(&g.pz, in);
        puzzle_events();
        door_update();
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
        if (paragon_duel()) ge_paragon_think(&g.brain, &g.fight, 1, &fin[1]);
        else ge_fight_cpu(&g.fight, 1, g.practice ? -1 : duel_cpu_level(duel_thing()->level), &fin[1]);
        if (g.god && g.fight.phase == GE_FP_FIGHT) {     /* dev: you can't be beaten down (the Paragon's finisher still lands) */
            GeEntity *h = &g.fight.world.ents[g.fight.f[0].ent];
            h->hp = g.fight.f[0].def->max_hp;
        }
        ge_fight_update(&g.fight, fin);
        if (g.fight.over) { g.duel_result = g.fight.winner; g.mode = M_TRANS_OUT; g.mode_timer = 0; }
        break;
    }
    case M_TRANS_OUT:
        if (g.mode_timer == 24) { ge_fight_free(&g.fight); g.fight_live = false; end_battle(); calm_flush(); }   /* the screen is black */
        if (g.mode_timer >= 48) {
            if (g.paragon_report) { g.paragon_report = false; g.mode = M_REPORT; g.mode_timer = 0; }
            else { g.mode = M_OVERWORLD; g.mode_timer = 0; }
        }
        break;
    case M_REPORT:
        if (g.mode_timer > 150 && ((in->pressed & (GE_BTN_A | GE_BTN_START)) || (g.autoplay && g.mode_timer > 420))) {
            g.mode = M_OVERWORLD; g.mode_timer = 0;
            say("THE PARAGON REMEMBERS", 150);
        }
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
    for (int i = 0; i < p->max_hp; i++) draw_mask(fb, 30 + i * 13, 8, p->active && i < p->hp);
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
    if (g.boss_engaged && g.boss_state != B_DEAD && boss_ent() >= 0) {
        GeEntity *b = &w->ents[boss_ent()];
        ge_fill_rect(fb, 49, 221, 302, 7, GE_ARGB(220, 10, 8, 20), 0);
        ge_fill_rect(fb, 50, 222, 300 * b->hp / b->max_hp, 5, GE_RGB(200, 60, 50), 0);
        ge_text_outlined(fb, 50, 211, thing(g.boss_spawn)->name, GE_RGB(255, 210, 190), GE_RGB(20, 14, 30), 1);
    }
    /* prompts */
    for (int i = room_find(room(), TH_BENCH, 0); i >= 0; i = room_find(room(), TH_BENCH, i + 1))
        if (p->active && fabsf(p->pos.x - thing_x(thing(i))) < 18.0f && fabsf(p->pos.y - thing_feet(thing(i))) < 20.0f && g.msg_timer == 0)
            ge_text_outlined(fb, (int)(p->pos.x - w->cam.x) - ge_text_width("UP: REST   DOWN: PRACTICE", 1) / 2, (int)(p->pos.y - w->cam.y) - 44, "UP: REST   DOWN: PRACTICE",
                             GE_WHITE, GE_RGB(20, 14, 30), 1);
    if (p->active && g.msg_timer == 0 && g.pz.door_near >= 0 && g.door_fade == 0) {
        const char *s = "UP: GO THROUGH";
        ge_text_outlined(fb, (int)(p->pos.x - w->cam.x) - ge_text_width(s, 1) / 2, (int)(p->pos.y - w->cam.y) - 44, s, GE_WHITE, GE_RGB(20, 14, 30), 1);
    }
    if (p->active && g.pz.sign_near >= 0) {                     /* a sign's words, in a box */
        const char *s = thing(g.pz.sign_near)->text;
        int tw = ge_text_width(s, 1), x = (SCREEN_W - tw) / 2;
        ge_fill_rect(fb, x - 6, 36, tw + 12, 15, GE_ARGB(220, 12, 8, 22), 0);
        ge_fill_rect(fb, x - 6, 36, tw + 12, 1, GE_RGB(150, 140, 190), 0);
        ge_text(fb, x, 40, s, GE_RGB(236, 230, 250), 1);
    }
    puzzle_draw_hints(&g.pz, fb, (int)w->cam.x, (int)w->cam.y, g.tick);
    if (g.keys > 0) {                                             /* keys carried */
        for (int k = 0; k < g.keys && k < 5; k++) {
            int kx = 92 + k * 12;
            ge_fill_circle(fb, kx + 2, 25, 2, GE_RGB(255, 214, 110), 0);
            ge_fill_rect(fb, kx + 4, 24, 5, 2, GE_RGB(255, 214, 110), 0);
        }
    }
    if (g.tick < 420 && !g.no_hints && g.mode == M_OVERWORLD && !g.menu_open) {   /* controls, for the first few seconds */
        const char *l1 = "B JUMP (AGAIN IN THE AIR)   Y SLASH (+UP / +DOWN)   R DASH";
        const char *l2 = "X THROW NEEDLE   A SILK SPIN   HOLD L TO HEAL   UP AT A BENCH";
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
    int ent = duel_ent();
    if (ent >= 0 && !g.practice) {                     /* the "!" over their head */
        GeEntity *e = &g.world.ents[ent];
        int x = (int)(e->pos.x - g.world.cam.x), y = (int)(e->pos.y - g.world.cam.y) - 46;
        int pop = g.mode_timer < 6 ? g.mode_timer : 6;
        ge_fill_rect(fb, x - 6, y - pop, 13, 16, GE_RGB(20, 14, 30), 0);
        ge_fill_rect(fb, x - 5, y - pop + 1, 11, 14, GE_WHITE, 0);
        ge_text(fb, x - 2, y - pop + 4, "!", GE_RGB(220, 40, 60), 1);
    }
    const Thing *s = duel_thing();
    char buf[64];
    if (s->style == STYLE_PARAGON) snprintf(buf, sizeof buf, "THE PARAGON HAS BEEN WATCHING YOU");
    else if (g.practice) snprintf(buf, sizeof buf, "PRACTICE DUEL: %s WILL GO EASY ON YOU", s->name);
    else snprintf(buf, sizeof buf, "%s WANTS TO DUEL!", s->name);
    ge_text_outlined(fb, (SCREEN_W - ge_text_width(buf, 1)) / 2, 186, buf, GE_RGB(255, 220, 160), GE_RGB(20, 14, 30), 1);
}

/* Pokemon-style: flash, flash, flash, then the duelist's own pattern floods out from where they stand. */
static void draw_trans_in(GeImage *fb, int t) {
    int ent = g.practice ? -1 : duel_ent();
    int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
    if (ent >= 0) {
        const GeEntity *e = &g.world.ents[ent];
        cx = (int)(e->pos.x - g.world.cam.x); cy = (int)(e->pos.y - g.world.cam.y) - 16;
    }
    encounter_draw_transition(fb, encounter_pattern(duel_thing()->name), t, cx, cy, g.tick);
}

/* The Paragon speaks: its two lines while the round is introduced, and one word at the finisher. Each line
 * types itself out a letter at a time, in a box with its gold trim. */
static void draw_paragon_voice(GeImage *fb) {
    const GeFight *f = &g.fight;
    const char *lines[2] = { NULL, NULL };
    int t = f->phase_timer;
    if (f->phase == GE_FP_INTRO) { lines[0] = g.paragon_say[0]; lines[1] = t > 40 ? g.paragon_say[1] : NULL; }
    else if (f->phase == GE_FP_FINISHER && t < 44) lines[0] = "ENOUGH.";
    else return;
    int y = 40;
    for (int i = 0; i < 2 && lines[i]; i++) {
        char buf[72];
        int shown = (i == 0 ? t : t - 40) * 2;   /* two letters a tick */
        snprintf(buf, sizeof buf, "%.*s", shown < 0 ? 0 : shown, lines[i]);
        int w = ge_text_width(lines[i], 1);
        int x = (fb->w - w) / 2;
        ge_fill_rect(fb, x - 6, y - 4, w + 12, 15, GE_ARGB(220, 8, 4, 14), 0);
        ge_fill_rect(fb, x - 6, y - 4, w + 12, 1, GE_RGB(200, 160, 80), 0);
        ge_fill_rect(fb, x - 6, y + 10, w + 12, 1, GE_RGB(200, 160, 80), 0);
        ge_text(fb, x, y, buf, GE_RGB(255, 228, 160), 1);
        y += 18;
    }
}

static void demo_draw_inner(GeImage *fb);

static void demo_draw(void *user, GeImage *fb) {
    (void)user;
    uint64_t t0 = ge_plat_time_us();
    if (!g.start_us) {
        g.start_us = g.next_sample_us = t0;
        if (g.log_wanted) set_logging(true);
        log_mods();
    }
    g.now_us = t0;
    devlog_set_clock(g.start_us, t0);
    double dt_ms = g.last_draw_us && t0 > g.last_draw_us ? (double)(t0 - g.last_draw_us) / 1000.0 : 0.0;
    if (dt_ms > 0) g.fps += (1000.0f / (float)dt_ms - g.fps) * 0.1f;
    g.last_draw_us = t0;
    demo_draw_inner(fb);
    if (g.menu_open) {
        const char *m = "PAUSED - DEV MENU ON THE BOTTOM SCREEN";
        ge_fill_rect(fb, 0, 0, fb->w, 13, GE_ARGB(210, 16, 10, 30), 0);
        ge_text(fb, (fb->w - ge_text_width(m, 1)) / 2, 3, m, GE_RGB(255, 236, 160), 1);
    }
    double draw = (double)(ge_plat_time_us() - t0) / 1000.0;
    g.draw_ms += ((float)draw - g.draw_ms) * 0.1f;
    g.present_ms += (ge_frame_times().present_ms - g.present_ms) * 0.1f;

    /* frame statistics for the play log (menu time excluded) */
    if (dt_ms > 0 && dt_ms < 1000 && !g.menu_open) {
        bool slow = dt_ms > 1000.0 / 60.0 + 1.5;
        g.win.frames++; g.tot.frames++;
        g.win.slow += slow; g.tot.slow += slow;
        g.win.dt_sum += dt_ms; g.tot.dt_sum += dt_ms;
        if (dt_ms > g.win.dt_max) g.win.dt_max = dt_ms;
        if (dt_ms > g.tot.dt_max) g.tot.dt_max = dt_ms;
        g.win.draw_sum += draw;
        if (draw > g.win.draw_max) g.win.draw_max = draw;
        g.win.update_sum += g.update_ms;
        GeFrameTimes ft = ge_frame_times();   /* the previous frame's: this one hasn't been sent yet */
        g.win.present_sum += ft.present_ms; g.win.bottom_sum += ft.bottom_ms; g.win.wait_sum += ft.wait_ms;
        if (g.mode == M_BATTLE) {
            g.duel_perf.frames++; g.duel_perf.dt_sum += dt_ms;
            if (dt_ms > g.duel_perf.dt_max) g.duel_perf.dt_max = dt_ms;
        }
    }
    if (t0 >= g.next_sample_us + 10000000u) {
        g.next_sample_us = t0;
        if (g.win.frames > 0 && devlog_is_open()) {
            const char *where = g.mode == M_BATTLE ? "duel" : (g.boss_engaged ? "boss fight" : "exploring");
            devlog("perf: %.1f fps avg, worst frame %.1f ms, %d%% of frames below 60 fps | update %.1f ms, draw %.1f ms (max %.1f), "
                   "present %.1f ms, bottom screen %.1f ms, vsync wait %.1f ms | %s, column %d, masks %d | detail %s",
                   g.win.frames / (g.win.dt_sum / 1000.0), g.win.dt_max, g.win.slow * 100 / g.win.frames,
                   g.win.update_sum / g.win.frames, g.win.draw_sum / g.win.frames, g.win.draw_max,
                   g.win.present_sum / g.win.frames, g.win.bottom_sum / g.win.frames, g.win.wait_sum / g.win.frames, where,
                   (int)(hero()->pos.x / GE_TILE), hero()->active ? hero()->hp : 0, g.low_detail ? "low" : "full");
            bool fighting = g.mode == M_ALERT || g.mode == M_TRANS_IN || g.mode == M_BATTLE || g.mode == M_TRANS_OUT || g.boss_engaged;
            if (!fighting) devlog_flush_if_due(60000000u);   /* a backstop, never mid-fight: the fade after a duel writes */
        }
        memset(&g.win, 0, sizeof g.win);
    }
    if (g.show_perf) {
        char buf[80];
        snprintf(buf, sizeof buf, "FPS %d  UPDATE %d.%d  DRAW %d.%d  PRESENT %d.%d MS  %s", (int)(g.fps + 0.5f),
                 (int)g.update_ms, (int)(g.update_ms * 10) % 10, (int)g.draw_ms, (int)(g.draw_ms * 10) % 10,
                 (int)g.present_ms, (int)(g.present_ms * 10) % 10, g.low_detail ? "LOW" : "FULL");
        ge_fill_rect(fb, 0, 230, fb->w, 10, GE_ARGB(200, 0, 0, 0), 0);
        ge_text(fb, 3, 231, buf, GE_RGB(120, 255, 140), 1);
    }
}

static void demo_draw_inner(GeImage *fb) {
    if (g.mode == M_BATTLE || (g.mode == M_TRANS_OUT && g.fight_live)) {
        ge_fight_draw(&g.fight, fb);
        if (paragon_duel() && g.fight_live) draw_paragon_voice(fb);
        if (g.mode == M_BATTLE && g.mode_timer < 24) {   /* the pattern breaks up and the duel shows through */
            if (paragon_duel()) ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB((24 - g.mode_timer) * 10, 255, 255, 255), 0);
            else encounter_draw_reveal(fb, encounter_pattern(duel_thing()->name), g.mode_timer, g.tick);
        }
        if (g.mode == M_BATTLE) {
            const char *hint = "Y LIGHT  X MEDIUM  A HEAVY  B THRUST  L BLOCK  R DASH  A+B SUPER";
            if (g.fight.phase == GE_FP_INTRO) ge_text_outlined(fb, (SCREEN_W - ge_text_width(hint, 1)) / 2, 200, hint, GE_RGB(220, 210, 240), GE_RGB(20, 14, 30), 1);
        }
    } else if (g.mode == M_REPORT && g.mode_timer >= 30) {
        paragon_draw_report(fb, g.facts, g.nfacts, &g.model, g.learn_shown, g.gifts, g.mode_timer);   /* opaque: no world under it */
    } else {
        ge_world_draw(&g.world, fb);
        draw_hud(fb);
        if (g.mode == M_ALERT) draw_alert(fb);
        if (g.mode == M_TRANS_IN) { draw_alert(fb); draw_trans_in(fb, g.mode_timer); }
        if (g.mode == M_REPORT) paragon_draw_report(fb, g.facts, g.nfacts, &g.model, g.learn_shown, g.gifts, g.mode_timer);
        if (g.mode == M_VICTORY && g.mode_timer > 60) {
            const char *a = "THORNWARDEN FELLED", *b = "THE FAR DOOR HAS OPENED";
            ge_text_outlined(fb, (SCREEN_W - ge_text_width(a, 2)) / 2, 90, a, GE_RGB(255, 220, 160), GE_RGB(20, 14, 30), 2);
            ge_text_outlined(fb, (SCREEN_W - ge_text_width(b, 1)) / 2, 112, b, GE_RGB(230, 220, 250), GE_RGB(20, 14, 30), 1);
        }
    }
    if (g.mode == M_TRANS_OUT) {   /* fade through black */
        int t = g.mode_timer, a = t < 24 ? t * 255 / 24 : (48 - t) * 255 / 24;
        ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(ge_clampi(a, 0, 255), 10, 4, 18), 0);
    }
    if (g.warp_fade > 0 || g.door_fade > 0) {
        int f = g.warp_fade > 0 ? g.warp_fade : g.door_fade;
        int t = 40 - f, a = t < 20 ? t * 255 / 20 : (40 - t) * 255 / 20;
        ge_fill_rect(fb, 0, 0, fb->w, fb->h, GE_ARGB(ge_clampi(a, 0, 255), 0, 0, 0), 0);
    }
}

static void demo_shutdown(void *user) {
    (void)user;
    if (devlog_is_open()) {
        uint64_t secs = (g.now_us - g.start_us) / 1000000u;
        devlog("session end. play time %s (exploring %s, duelling %s)", devlog_hms(secs), devlog_hms((uint64_t)g.ticks_world / 60),
               devlog_hms((uint64_t)g.ticks_duel / 60));
        devlog("  duels won %d, lost %d | deaths %d | enemies defeated %d | heals %d | boss %s",
               g.duels_won, g.duels_lost, g.deaths, g.kills, g.heals,
               boss_beaten_anywhere() ? "defeated" : (g.boss_engaged ? "fought, not beaten" : "not reached"));
        devlog("  overall %.1f fps avg, worst frame %.1f ms, %d%% of frames below 60 fps",
               g.tot.dt_sum > 0 ? g.tot.frames / (g.tot.dt_sum / 1000.0) : 0.0, g.tot.dt_max,
               g.tot.frames ? g.tot.slow * 100 / g.tot.frames : 0);
        devlog_close();
    }
#ifdef DEMO_HEADLESS
    GeEntity *p = hero();
    printf("mode=%d x=%.0f (col %d) masks=%d kills=%d duels_won=%d duels_lost=%d deaths=%d heals=%d boss=%s checksum=%08x\n",
           g.mode, p->pos.x, (int)(p->pos.x / GE_TILE), p->active ? p->hp : 0, g.kills, g.duels_won, g.duels_lost, g.deaths, g.heals,
           boss_beaten_anywhere() ? "defeated" : (g.boss_engaged ? "fighting" : "waiting"), ge_null_last_checksum());
#endif
    paragon_profile_save(&g.model);
    encounter_free();
    if (g.fight_live) ge_fight_free(&g.fight);
    ge_world_free(&g.world);
    art_free(&g.spr);
}

#ifdef DEMO_HEADLESS
/* --press SELECT@300,DOWN@310: scripted one-frame button taps for headless UI checks */
/* --press SELECT@300,RIGHT@10-90: buttons held on one frame, or over a range of frames */
static struct { uint32_t btn; int frame, last; } g_taps[64];
static int g_ntaps;
static uint32_t tap_script(void *user, int frame) {
    (void)user;
    uint32_t b = 0;
    for (int i = 0; i < g_ntaps; i++) if (frame >= g_taps[i].frame && frame <= g_taps[i].last) b |= g_taps[i].btn;
    return b;
}
/* --touch 160,100@300: a scripted tap on the bottom screen */
static struct { int x, y, frame; } g_touches[16];
static int g_ntouches;
static int touch_script(void *user, int frame, int *x, int *y) {
    (void)user;
    for (int i = 0; i < g_ntouches; i++)
        if (g_touches[i].frame == frame) { *x = g_touches[i].x; *y = g_touches[i].y; return 1; }
    return 0;
}
static void parse_touches(const char *s) {
    int x, y, frame, used;
    while (g_ntouches < 16 && sscanf(s, "%d,%d@%d%n", &x, &y, &frame, &used) == 3) {
        g_touches[g_ntouches].x = x; g_touches[g_ntouches].y = y; g_touches[g_ntouches++].frame = frame;
        s += used;
        if (*s == ';' || *s == ' ') s++;
    }
}
static void parse_taps(const char *s) {
    static const struct { const char *n; uint32_t b; } names[] = {
        { "LEFT", GE_BTN_LEFT }, { "RIGHT", GE_BTN_RIGHT }, { "UP", GE_BTN_UP }, { "DOWN", GE_BTN_DOWN }, { "A", GE_BTN_A }, { "B", GE_BTN_B },
        { "X", GE_BTN_X }, { "Y", GE_BTN_Y }, { "L", GE_BTN_L }, { "R", GE_BTN_R }, { "START", GE_BTN_START }, { "SELECT", GE_BTN_SELECT } };
    char name[16];
    int frame, used;
    while (g_ntaps < 64 && sscanf(s, "%15[A-Z]@%d%n", name, &frame, &used) == 2) {
        s += used;
        int last = frame;
        if (*s == '-' && sscanf(s, "-%d%n", &last, &used) == 1) s += used;
        for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
            if (!strcmp(names[i].n, name)) { g_taps[g_ntaps].btn = names[i].b; g_taps[g_ntaps].frame = frame; g_taps[g_ntaps++].last = last; }
        if (*s == ',') s++;
    }
}
#endif

int main(int argc, char **argv) {
    int frames = 1800, dump_every = 0;
    g.difficulty = -1;   /* new players start on easy; the setting is saved once changed */
    g_log = getenv("GLOAM_LOG") != NULL;
    const char *dump_dir = ".";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--autoplay")) g.autoplay = true;
        else if (!strcmp(argv[i], "--low-detail")) g.low_detail = true;
        else if (!strcmp(argv[i], "--god")) g.god = true;        /* dev: the hero can't be hurt */
        else if (!strcmp(argv[i], "--log")) g.log_wanted = true; /* write gloamwood_log.txt */
#ifdef DEMO_HEADLESS
        else if (!strcmp(argv[i], "--press") && i + 1 < argc) parse_taps(argv[++i]);
        else if (!strcmp(argv[i], "--touch") && i + 1 < argc) parse_touches(argv[++i]);
#endif
        else if (!strcmp(argv[i], "--beat-all")) g.beat_all = true;                               /* dev: Paragon starts with every technique */
        else if (!strcmp(argv[i], "--difficulty") && i + 1 < argc) { g.difficulty = ge_clampi(atoi(argv[++i]), -1, 1); g.difficulty_set = true; }
        else if (!strcmp(argv[i], "--no-mods")) g.no_mods = true;                                  /* ignore the mods folder */
        else if (!strcmp(argv[i], "--no-hints")) g.no_hints = true;                                /* benchmarks: skip the opening controls text */
        else if (!strcmp(argv[i], "--fresh")) g.fresh_profile = true;                              /* ignore the saved Paragon memory */
        else if (!strcmp(argv[i], "--profile") && i + 1 < argc) paragon_set_profile_path(argv[++i]);
        else if (!strcmp(argv[i], "--paragon")) g.to_paragon = true;                              /* straight to its hall */
        else if (!strcmp(argv[i], "--world") && i + 1 < argc) g.world_file = argv[++i];           /* play this world file */
        else if (!strcmp(argv[i], "--room") && i + 1 < argc) g.start_room = argv[++i];            /* start in this room */
        else if (!strcmp(argv[i], "--export-world") && i + 1 < argc) g.export_world = argv[++i];  /* write the world out and stop */
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
    g.console = is_new3ds ? "NEW 3DS" : "ORIGINAL 3DS";
    g.log_wanted = true;       /* playtest builds keep a play log on the SD card */
#elif defined(DEMO_HEADLESS)
    g.console = "HEADLESS";
#else
    g.console = "DESKTOP";
#endif
    g.auto_low = g.low_detail;
#ifdef DEMO_HEADLESS
    ge_null_configure(frames, dump_every, dump_dir, g_ntaps ? tap_script : NULL, NULL);
    if (g_ntouches) ge_null_set_touch(touch_script);
#else
    (void)frames; (void)dump_every; (void)dump_dir;
#endif
    if (g.export_world) {                  /* the whole world as it would be played, as a file; no game */
        load_world();
        int rc = world_save(&g.wd, g.export_world, false);
        fprintf(stderr, rc == 0 ? "world written to %s\n" : "can't write %s\n", g.export_world);
        return rc == 0 ? 0 : 1;
    }
    GeGame game = { .user = &g, .init = demo_init, .update = demo_update, .draw = demo_draw, .shutdown = demo_shutdown,
                    .draw_bottom = draw_bottom };
    GePlatformConfig cfg = { .title = "Gloamwood", .width = SCREEN_W, .height = SCREEN_H, .scale = 3 };
    return ge_run(&game, &cfg);
}
