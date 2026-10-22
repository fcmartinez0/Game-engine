/* The mods folder: change the game's art, animations, attack frame data and move lists without rebuilding.
 *
 *   SD:/3ds/gloamwood/mods/   (on a PC: ./mods/)
 *     hero.anim      clips and frame data (hitboxes, damage, stun, guard...)   - the asset tool edits these
 *     hero.pam       the sprite sheet (optional: without it the built-in sheet is used with your clips)
 *     hero.moves     the duel move list and tuning (which input does which clip)
 *   the same for brawler (Grobb, Mosshide), fencer (Silke, the Champion), paragon, warden (the boss), fx.
 *
 * "EXPORT DEFAULTS" in the dev menu writes the built-in versions of every file to mods/_export/ to start from.
 * Analogy: the game keeps its own costumes, but checks the dressing room first. */

#include "demo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#else
#define mkdir_one(p) mkdir(p, 0777)
#endif

#ifdef __3DS__
#define MODS_DIR "sdmc:/3ds/gloamwood/mods"
#else
#define MODS_DIR "mods"
#endif

const char *mods_dir(void) { return MODS_DIR; }
void mods_make_dir(void) { mkdir_one(MODS_DIR); }

typedef struct { const char *name; GeSprite *spr; GeFighterDef *fighter; } Target;

static int targets(Sprites *s, Chars *c, Target t[8]) {
    int n = 0;
    t[n++] = (Target){ "hero", &s->hero, c ? &c->f_hero : NULL };
    t[n++] = (Target){ "brawler", &s->brawler, c ? &c->f_brawler : NULL };
    t[n++] = (Target){ "fencer", &s->fencer, c ? &c->f_fencer : NULL };
    t[n++] = (Target){ "paragon", &s->paragon, c ? &c->f_paragon : NULL };
    t[n++] = (Target){ "warden", &s->warden, NULL };
    t[n++] = (Target){ "fx", &s->fx, NULL };
    return n;
}

static bool exists(const char *path) { FILE *f = fopen(path, "rb"); if (!f) return false; fclose(f); return true; }

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
    if (buf) buf[n] = 0;
    fclose(f);
    return buf;
}

static void note(ModReport *r, const char *fmt, const char *a, const char *b, int n) {
    if (r->nlines >= MOD_REPORT_LINES) return;
    char *line = r->lines[r->nlines++];
    snprintf(line, sizeof r->lines[0], fmt, a, b, n);
    for (char *p = line; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);   /* the font is capitals */
}

/* ---- art and clips ---------------------------------------------------------------------------------- */

void mods_apply_sprites(Sprites *s, ModReport *r) {
    Target t[8];
    int n = targets(s, NULL, t);
    for (int i = 0; i < n; i++) {
        char anim[160], pam[160];
        snprintf(anim, sizeof anim, "%s/%s.anim", MODS_DIR, t[i].name);
        snprintf(pam, sizeof pam, "%s/%s.pam", MODS_DIR, t[i].name);
        bool has_anim = exists(anim), has_pam = exists(pam);
        if (!has_anim && !has_pam) continue;
        static GeSprite tmp;            /* big: keep it off the stack */
        memset(&tmp, 0, sizeof tmp);
        bool own_sheet = false;
        if (has_pam) {
            if (ge_image_load_pam(&tmp.sheet, pam) != 0) { note(r, "%s.PAM: CAN'T READ IT (NEEDS A P7 RGBA .PAM)", t[i].name, "", 0); r->errors++; continue; }
            own_sheet = true;
        } else {
            tmp.sheet = t[i].spr->sheet;  /* borrowed: only the clips change */
        }
        char *text = has_anim ? slurp(anim) : NULL;
        int rc = 0;
        if (has_anim) {
            rc = text ? ge_sprite_parse(&tmp, text) : 1;
        } else {                         /* a new sheet with the built-in clips */
            int cols = tmp.sheet.w / (t[i].spr->cell_w ? t[i].spr->cell_w : 1);
            memcpy(tmp.clips, t[i].spr->clips, sizeof tmp.clips);
            tmp.nclips = t[i].spr->nclips; tmp.cell_w = t[i].spr->cell_w; tmp.cell_h = t[i].spr->cell_h;
            tmp.origin_x = t[i].spr->origin_x; tmp.origin_y = t[i].spr->origin_y; tmp.cols = cols;
        }
        free(text);
        if (rc != 0) {
            note(r, "%s.ANIM LINE %.0s%d: CAN'T READ IT", t[i].name, "", rc < 0 ? -rc : 0);
            r->errors++;
            if (own_sheet) ge_image_free(&tmp.sheet);
            continue;
        }
        /* every clip the game uses by name must still be there */
        const char *missing = NULL;
        for (int k = 0; k < t[i].spr->nclips && !missing; k++)
            if (ge_sprite_find_clip(&tmp, t[i].spr->clips[k].name) < 0) missing = t[i].spr->clips[k].name;
        /* and every frame must point at a cell inside the sheet */
        int cells = tmp.cols * (tmp.cell_h > 0 ? tmp.sheet.h / tmp.cell_h : 0), bad_cell = -1;
        for (int k = 0; k < tmp.nclips && bad_cell < 0; k++)
            for (int f = 0; f < tmp.clips[k].nframes; f++)
                if (tmp.clips[k].frames[f].cell < 0 || tmp.clips[k].frames[f].cell >= cells) { bad_cell = tmp.clips[k].frames[f].cell; break; }
        if (missing || bad_cell >= 0) {
            if (missing) note(r, "%s.ANIM: CLIP %s IS MISSING (KEEP EVERY NAME)", t[i].name, missing, 0);
            else note(r, "%s.ANIM: CELL %.0s%d IS OUTSIDE THE SHEET", t[i].name, "", bad_cell);
            r->errors++;
            if (own_sheet) ge_image_free(&tmp.sheet);
            continue;
        }
        if (own_sheet) { ge_image_free(&t[i].spr->sheet); t[i].spr->sheet = tmp.sheet; }
        GeImage keep = t[i].spr->sheet;
        *t[i].spr = tmp;
        t[i].spr->sheet = keep;
        note(r, "%s: %s", t[i].name, has_anim && has_pam ? "NEW ART AND CLIPS" : has_anim ? "NEW CLIPS" : "NEW ART", 0);
        r->loaded++;
    }
}

/* ---- move lists ------------------------------------------------------------------------------------- */

static const char *const ROLES[] = { "none", "poke", "low", "overhead", "antiair", "zone", "rush", "combo", "super", "air", "throw" };
#define NROLES ((int)(sizeof ROLES / sizeof ROLES[0]))

static const GeShotDef *shot_named(const Chars *c, const char *n) {
    if (!strcmp(n, "needle")) return &c->f_needle;
    if (!strcmp(n, "rock")) return &c->f_rock;
    if (!strcmp(n, "dust")) return &c->f_dust;
    if (!strcmp(n, "orb")) return &c->f_orb;
    return NULL;
}
static const char *shot_name(const Chars *c, const GeShotDef *s) {
    return s == &c->f_needle ? "needle" : s == &c->f_rock ? "rock" : s == &c->f_dust ? "dust" : s == &c->f_orb ? "orb" : NULL;
}

/* GeMove keeps string pointers, so parsed names live here, per fighter */
static char g_pool[8][GE_FIGHT_MAX_MOVES][2][GE_NAME_LEN];

static int parse_moves(Chars *c, int who, GeFighterDef *d, const GeSprite *spr, char *text, int *bad_line) {
    GeFighterDef out = *d;
    out.nmoves = 0;
    int lineno = 0;
    for (char *line = text, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next) *next++ = 0;
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *tok[12];
        int nt = 0;
        for (char *p = line; *p && nt < 12;) {                 /* split on spaces, tabs and \r */
            while (*p == ' ' || *p == '\t' || *p == '\r') *p++ = 0;
            if (!*p) break;
            tok[nt++] = p;
            while (*p && *p != ' ' && *p != '\t' && *p != '\r') p++;
        }
        if (nt == 0) continue;
        if (!strcmp(tok[0], "tuning")) {
            for (int i = 1; i < nt; i++) {
                char *eq = strchr(tok[i], '=');
                if (!eq) { *bad_line = lineno; return -1; }
                *eq = 0;
                float v = (float)atof(eq + 1);
                if (!strcmp(tok[i], "walk")) out.body.run_speed = v;
                else if (!strcmp(tok[i], "back")) out.walk_back = v;
                else if (!strcmp(tok[i], "jump")) out.body.jump_vel = v;
                else if (!strcmp(tok[i], "jumpx")) out.jump_vx = v;
                else if (!strcmp(tok[i], "dash")) out.dash_speed = v;
                else if (!strcmp(tok[i], "dashticks")) out.dash_ticks = (int)v;
                else if (!strcmp(tok[i], "hp")) out.max_hp = (int)v;
                else if (!strcmp(tok[i], "throw")) out.throw_damage = (int)v;
                else if (!strcmp(tok[i], "range")) out.throw_range = v;
                else { *bad_line = lineno; return -1; }
            }
        } else if (!strcmp(tok[0], "move") && nt >= 5) {
            if (out.nmoves >= GE_FIGHT_MAX_MOVES) { *bad_line = lineno; return -1; }
            GeMove *m = &out.moves[out.nmoves];
            memset(m, 0, sizeof *m);
            char *in = g_pool[who][out.nmoves][0], *cl = g_pool[who][out.nmoves][1];
            snprintf(in, GE_NAME_LEN, "%s", tok[1]);
            snprintf(cl, GE_NAME_LEN, "%s", tok[2]);
            m->input = in; m->clip = cl;
            m->level = atoi(tok[3]);
            m->ai = -1;
            for (int r = 0; r < NROLES; r++) if (!strcmp(tok[4], ROLES[r])) m->ai = r;
            if (m->ai < 0) { *bad_line = lineno; return -1; }
            for (int i = 5; i < nt; i++) {
                if (!strncmp(tok[i], "meter=", 6)) m->meter_cost = atoi(tok[i] + 6);
                else if (!strncmp(tok[i], "shot=", 5)) { m->shot = shot_named(c, tok[i] + 5); if (!m->shot) { *bad_line = lineno; return -1; } }
                else { *bad_line = lineno; return -1; }
            }
            out.nmoves++;
        } else {
            *bad_line = lineno; return -1;
        }
    }
    int rc = ge_fighter_def_finish(&out, spr);
    if (rc != 0) { *bad_line = -rc; return -2; }   /* -(1+index) of the bad move */
    *d = out;
    return 0;
}

void mods_apply_moves(Chars *c, Sprites *s, ModReport *r) {
    Target t[8];
    int n = targets(s, c, t);
    for (int i = 0; i < n; i++) {
        if (!t[i].fighter) continue;
        char path[160];
        snprintf(path, sizeof path, "%s/%s.moves", MODS_DIR, t[i].name);
        char *text = slurp(path);
        if (!text) continue;
        int bad = 0;
        int rc = parse_moves(c, i, t[i].fighter, t[i].spr, text, &bad);
        free(text);
        if (rc == -1) { note(r, "%s.MOVES LINE %.0s%d: CAN'T READ IT", t[i].name, "", bad); r->errors++; }
        else if (rc == -2) { note(r, "%s.MOVES MOVE %.0s%d: UNKNOWN CLIP OR INPUT", t[i].name, "", bad); r->errors++; }
        else { note(r, "%s: NEW MOVE LIST%.0s", t[i].name, "", 0); r->loaded++; }
    }
}

/* ---- export the built-in versions ----------------------------------------------------------------- */

static void write_moves(const Chars *c, const GeFighterDef *d, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# Duel move list. One move per line; the first one whose input matches wins, so keep supers and\n"
               "# specials first and command normals (like 6M) before plain ones (5M).\n"
               "#   move INPUT CLIP LEVEL ROLE [meter=N] [shot=needle|rock|dust|orb]\n"
               "# INPUT: numpad notation facing right (5 neutral, 6 forward, 2 down, 236 = down, down-forward, forward)\n"
               "#        then buttons: L light (Y), M medium (X), H heavy (A), K thrust (B), S special. 'j.' = in the air.\n"
               "# CLIP: a clip name in this character's .anim (its frame data says damage, hitboxes, guard...).\n"
               "# LEVEL: cancel strength 0 light .. 4 super: on a hit you can cancel into a higher level.\n"
               "# ROLE (what the computer uses it for): none poke low overhead antiair zone rush combo super air throw\n");
    fprintf(f, "tuning walk=%g back=%g jump=%g jumpx=%g dash=%g dashticks=%d hp=%d throw=%d range=%g\n",
            (double)d->body.run_speed, (double)d->walk_back, (double)d->body.jump_vel, (double)d->jump_vx, (double)d->dash_speed,
            d->dash_ticks, d->max_hp, d->throw_damage, (double)d->throw_range);
    for (int i = 0; i < d->nmoves; i++) {
        const GeMove *m = &d->moves[i];
        fprintf(f, "move %-6s %-10s %d %s", m->input, m->clip, m->level, m->ai >= 0 && m->ai < NROLES ? ROLES[m->ai] : "none");
        if (m->meter_cost) fprintf(f, " meter=%d", m->meter_cost);
        const char *sn = shot_name(c, m->shot);
        if (sn) fprintf(f, " shot=%s", sn);
        fputc('\n', f);
    }
    fclose(f);
}

int mods_export(Sprites *s, Chars *c) {
    char dir[160];
    mkdir_one(MODS_DIR);
    snprintf(dir, sizeof dir, "%s/_export", MODS_DIR);
    mkdir_one(dir);
    Target t[8];
    int n = targets(s, c, t), written = 0;
    for (int i = 0; i < n; i++) {
        char path[200];
        snprintf(path, sizeof path, "%s/%s.pam", dir, t[i].name);
        if (ge_image_save_pam(&t[i].spr->sheet, path) == 0) written++;
        snprintf(path, sizeof path, "%s/%s.anim", dir, t[i].name);
        if (ge_sprite_save_anim(t[i].spr, path) == 0) written++;
        if (t[i].fighter) { snprintf(path, sizeof path, "%s/%s.moves", dir, t[i].name); write_moves(c, t[i].fighter, path); written++; }
    }
    char path[200];
    snprintf(path, sizeof path, "%s/README.txt", dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("These are the game's built-in characters. To change one, copy its files up one folder (into mods/)\n"
              "and edit them; the game uses mods/ first and falls back to the built-in versions.\n\n"
              "  NAME.anim   animation clips + attack frame data. Open NAME.pam and NAME.anim together in\n"
              "              tools/asset-prep/index.html to edit cells, timing and hitboxes visually.\n"
              "  NAME.pam    the sprite sheet (any paint program that saves PAM, or the asset tool).\n"
              "              You can leave it out: then your .anim uses the built-in sheet.\n"
              "  NAME.moves  duel move list and tuning (fighters only); the comments in it explain the format.\n\n"
              "Keep every clip name the built-in .anim has (the game looks them up by name); you can add more.\n"
              "hero = Mikoshi, brawler = Grobb and Mosshide, fencer = Silke and the Champion, paragon, warden = the boss.\n"
              "Then use RELOAD MODS in the dev menu (bottom screen). Problems are listed there and in the play log.\n", f);
        fclose(f);
    }
    return written;
}
