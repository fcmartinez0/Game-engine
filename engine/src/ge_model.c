#include "ge/ge_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATTACK_WEIGHT 8.0f   /* one attack started is worth this many neutral samples (an attack lasts ~30 ticks, samples come every 4) */
#define PRIOR 0.3f

int ge_dist_bucket(float dx) {
    dx = dx < 0 ? -dx : dx;
    return dx < 32.0f ? GE_DIST_CLOSE : dx < 64.0f ? GE_DIST_NEAR : dx < 120.0f ? GE_DIST_MID : GE_DIST_FAR;
}

void ge_model_init(GePlayerModel *m) {
    memset(m, 0, sizeof *m);
    m->w.last_intent = -1;
}

/* every counter array, by name, for decay / save / load */
typedef struct { const char *name; float *p; int n; } Arr;
static int arrays(GePlayerModel *m, Arr *a) {
    int n = 0;
#define A(nm, ptr, cnt) a[n++] = (Arr){ nm, (float *)(ptr), cnt }
    A("attacks", m->attacks, GE_DIST_N * GE_INT_ATTACKS);
    A("neutral", m->neutral, GE_DIST_N * GE_INT_NEUTRALS);
    A("bigram", m->bigram, GE_INT_ATTACKS * GE_INT_ATTACKS);
    A("faced", m->faced, 4);
    A("blocked", m->blocked, 4);
    A("after_block", m->after_block, GE_RESP_COUNT);
    A("wakeup", m->wakeup, GE_RESP_COUNT);
    A("vs_jumpin", m->vs_jumpin, GE_RESP_COUNT);
    A("vs_shot", m->vs_shot, GE_RESP_COUNT);
    A("throws", &m->throws_faced, 3);          /* faced, teched, made: contiguous in the struct */
    A("hits", &m->hits_landed, 2);             /* landed, taken */
#undef A
    return n;
}

void ge_model_decay(GePlayerModel *m, float keep) {
    Arr a[16];
    int n = arrays(m, a);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < a[i].n; k++) a[i].p[k] *= keep;
}

/* ---- prediction -------------------------------------------------------- */

static float weight_total(const GePlayerModel *m) {
    float w = 0;
    for (int d = 0; d < GE_DIST_N; d++) {
        for (int i = 0; i < GE_INT_ATTACKS; i++) w += ATTACK_WEIGHT * m->attacks[d][i];
        for (int i = 0; i < GE_INT_NEUTRALS; i++) w += m->neutral[d][i];
    }
    return w;
}

float ge_model_confidence(const GePlayerModel *m) {
    float w = weight_total(m);
    return w / (w + 300.0f);
}

void ge_model_predict(const GePlayerModel *m, int dist, int last_intent, float p[GE_INT_COUNT]) {
    dist = dist < 0 ? 0 : dist >= GE_DIST_N ? GE_DIST_N - 1 : dist;
    for (int i = 0; i < GE_INT_ATTACKS; i++) {
        float v = PRIOR + ATTACK_WEIGHT * m->attacks[dist][i];
        if (dist > 0) v += 0.3f * ATTACK_WEIGHT * m->attacks[dist - 1][i];          /* neighbouring ranges lend a little evidence */
        if (dist < GE_DIST_N - 1) v += 0.3f * ATTACK_WEIGHT * m->attacks[dist + 1][i];
        p[i] = v;
    }
    for (int i = 0; i < GE_INT_NEUTRALS; i++) {
        float v = PRIOR + m->neutral[dist][i];
        if (dist > 0) v += 0.3f * m->neutral[dist - 1][i];
        if (dist < GE_DIST_N - 1) v += 0.3f * m->neutral[dist + 1][i];
        p[GE_INT_ATTACKS + i] = v;
    }
    if (last_intent >= 0 && last_intent < GE_INT_ATTACKS) {                       /* "after this, they usually do that" */
        float row = 0;
        for (int i = 0; i < GE_INT_ATTACKS; i++) row += m->bigram[last_intent][i];
        if (row > 0.5f)
            for (int i = 0; i < GE_INT_ATTACKS; i++) p[i] *= 1.0f + 2.5f * m->bigram[last_intent][i] / row;
    }
    float s = 0;
    for (int i = 0; i < GE_INT_COUNT; i++) s += p[i];
    for (int i = 0; i < GE_INT_COUNT; i++) p[i] /= s;
}

float ge_model_block_rate(const GePlayerModel *m, int guard) {
    if (guard < 0 || guard > 3 || m->faced[guard] < 1.0f) return -1.0f;
    return m->blocked[guard] / m->faced[guard];
}

/* ---- watching a fight -------------------------------------------------- */

static int resp_index(int flags) {
    if (flags & 1) return GE_RESP_ATTACK;
    if (flags & 2) return GE_RESP_JUMP;
    if (flags & 4) return GE_RESP_DASH;
    if (flags & 8) return GE_RESP_BLOCK;
    return GE_RESP_NOTHING;
}

static void close_window(GePlayerModel *m) {
    GeModelWatch *w = &m->w;
    float *dst = w->window == 1 ? m->after_block : w->window == 2 ? m->wakeup : w->window == 3 ? m->vs_jumpin : w->window == 4 ? m->vs_shot : NULL;
    if (dst) dst[resp_index(w->window_flags)] += 1.0f;
    w->window = 0;
    w->cooldown = 20;
}

static void open_window(GePlayerModel *m, int kind, int ticks) {
    GeModelWatch *w = &m->w;
    if (w->window || w->cooldown > 0) return;
    w->window = kind; w->window_timer = ticks; w->window_flags = 0;
}

void ge_model_on_move_start(GePlayerModel *m, const GeFight *f, int side) {
    const GeFighter *P = &f->f[side];
    const GeMove *mv = &P->def->moves[P->move];
    if (mv->is_throw) m->throws_made += 1.0f;
    int role = mv->ai;
    if (m->w.window) m->w.window_flags |= 1;
    if (role < 1 || role > GE_INT_ATTACKS) return;
    int intent = role - 1;
    float dx = f->world.ents[P->ent].pos.x - f->world.ents[f->f[1 - side].ent].pos.x;
    m->attacks[ge_dist_bucket(dx)][intent] += 1.0f;
    if (m->w.last_intent >= 0 && m->w.tick - m->w.last_tick <= 45) m->bigram[m->w.last_intent][intent] += 1.0f;
    m->w.last_intent = intent;
    m->w.last_tick = m->w.tick;
}

void ge_model_on_hit(GePlayerModel *m, const GeFight *f, int attacker, int guard, bool blocked, int observed) {
    (void)f;
    if (attacker != observed) {                      /* the observed player was attacked */
        if (guard < 0 || guard > 3) guard = 0;
        m->faced[guard] += 1.0f;
        if (blocked) m->blocked[guard] += 1.0f; else m->hits_taken += 1.0f;
    } else if (!blocked) {
        m->hits_landed += 1.0f;
    }
}

void ge_model_on_throw(GePlayerModel *m, int victim, bool teched, int observed) {
    if (victim != observed) return;
    m->throws_faced += 1.0f;
    if (teched) m->throws_teched += 1.0f;
}

void ge_model_tick(GePlayerModel *m, const GeFight *f, int side) {
    GeModelWatch *w = &m->w;
    if (f->phase != GE_FP_FIGHT) { w->prev_state = -1; return; }
    const GeFighter *P = &f->f[side], *O = &f->f[1 - side];
    const GeEntity *pe = &f->world.ents[P->ent], *oe = &f->world.ents[O->ent];
    w->tick++;
    if (w->cooldown > 0) w->cooldown--;
    float dx = pe->pos.x - oe->pos.x;
    int db = ge_dist_bucket(dx);

    if (P->state == GE_FS_MOVE && (w->prev_state != GE_FS_MOVE || P->move != w->prev_move)) ge_model_on_move_start(m, f, side);

    bool back = P->in.x * pe->facing < 0 || (P->in.held & GE_FB_BLOCK);
    bool opp_attacking = O->state == GE_FS_MOVE;
    int cls = -1;                                                                    /* neutral behaviour this tick */
    switch (P->state) {
    case GE_FS_AIR:    cls = GE_INT_JUMP; break;
    case GE_FS_DASH:   cls = GE_INT_DASH; break;
    case GE_FS_CROUCH: cls = (back && opp_attacking) ? GE_INT_BLOCK_LOW : GE_INT_CROUCH; break;
    case GE_FS_STAND:  cls = (back && opp_attacking) ? GE_INT_BLOCK : back ? GE_INT_WALK_B : P->in.x ? GE_INT_WALK_F : GE_INT_STAND; break;
    default: break;
    }
    if (cls >= 0 && (w->tick & 3) == 0) m->neutral[db][cls - GE_INT_ATTACKS] += 1.0f;

    /* open a "how do they answer this?" window on the right trigger */
    bool was_free = w->prev_state == GE_FS_STAND || w->prev_state == GE_FS_CROUCH;
    if (w->prev_state == GE_FS_BLOCKSTUN && (P->state == GE_FS_STAND || P->state == GE_FS_CROUCH)) open_window(m, 1, 24);
    else if (w->prev_state == GE_FS_DOWN && (P->state == GE_FS_STAND || P->state == GE_FS_CROUCH)) open_window(m, 2, 30);
    else if (O->state == GE_FS_AIR && was_free && db >= GE_DIST_NEAR && db <= GE_DIST_MID) open_window(m, 3, 28);
    else if (was_free) {
        for (int i = 0; i < GE_MAX_SHOTS; i++) {
            const GeShot *s = &f->world.shots[i];
            if (s->active && s->team != side && fabsf(s->pos.x - pe->pos.x) < 150.0f && (s->vel.x > 0) == (s->pos.x < pe->pos.x)) { open_window(m, 4, 30); break; }
        }
    }
    if (w->window) {
        if (P->state == GE_FS_AIR) w->window_flags |= 2;
        else if (P->state == GE_FS_DASH) w->window_flags |= 4;
        else if (back && (P->state == GE_FS_STAND || P->state == GE_FS_CROUCH || P->state == GE_FS_BLOCKSTUN)) w->window_flags |= 8;
        if (--w->window_timer <= 0) close_window(m);
    }

    w->prev_state = P->state;
    w->prev_move = P->move;
}

void ge_fight_observe(GeFight *f, GePlayerModel *m, int side) {
    f->model = m;
    f->model_side = side;
    if (m) { m->w.prev_state = -1; m->w.last_intent = -1; m->w.window = 0; m->w.tick = 0; }
}

/* ---- saving and loading ------------------------------------------------ */

int ge_model_save(const GePlayerModel *m, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    GePlayerModel *mm = (GePlayerModel *)m;      /* arrays() only takes addresses */
    Arr a[16];
    int n = arrays(mm, a);
    fprintf(f, "ge_model 1\n");
    fprintf(f, "journey %u %u %u %u %u %u\n", (unsigned)m->duels, (unsigned)m->rounds, (unsigned)m->wins, (unsigned)m->losses,
            (unsigned)m->absorbed, (unsigned)m->finals);
    fprintf(f, "overworld");
    for (int i = 0; i < GE_OW_COUNT; i++) fprintf(f, " %u", (unsigned)m->ow[i]);
    fputc('\n', f);
    for (int i = 0; i < n; i++) {
        fprintf(f, "%s %d", a[i].name, a[i].n);
        for (int k = 0; k < a[i].n; k++) fprintf(f, " %.3f", (double)a[i].p[k]);
        fputc('\n', f);
    }
    return fclose(f) == 0 ? 0 : -1;
}

int ge_model_load(GePlayerModel *m, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char head[32];
    if (!fgets(head, sizeof head, f) || strncmp(head, "ge_model 1", 10) != 0) { fclose(f); return -2; }
    GePlayerModel tmp;
    ge_model_init(&tmp);
    Arr a[16];
    int na = arrays(&tmp, a);
    static char line[8192];
    while (fgets(line, sizeof line, f)) {
        char name[32];
        int used = 0;
        if (sscanf(line, "%31s%n", name, &used) != 1) continue;
        const char *p = line + used;
        if (!strcmp(name, "journey")) {
            unsigned v[6] = { 0 };
            sscanf(p, "%u %u %u %u %u %u", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]);
            tmp.duels = v[0]; tmp.rounds = v[1]; tmp.wins = v[2]; tmp.losses = v[3]; tmp.absorbed = v[4]; tmp.finals = v[5];
            continue;
        }
        if (!strcmp(name, "overworld")) {
            for (int i = 0; i < GE_OW_COUNT; i++) { unsigned v = 0; int u = 0; if (sscanf(p, "%u%n", &v, &u) == 1) { tmp.ow[i] = v; p += u; } }
            continue;
        }
        for (int i = 0; i < na; i++) {
            if (strcmp(name, a[i].name) != 0) continue;
            int cnt = 0, u = 0;
            if (sscanf(p, "%d%n", &cnt, &u) != 1 || cnt != a[i].n) break;     /* a different layout: skip it rather than guess */
            p += u;
            for (int k = 0; k < cnt; k++) { float v = 0; if (sscanf(p, "%f%n", &v, &u) != 1) break; a[i].p[k] = v; p += u; }
            break;
        }
    }
    fclose(f);
    *m = tmp;
    return 0;
}

/* ---- what it learned, in words ----------------------------------------- */

typedef struct { float score; char text[72]; } Fact;

static void add(Fact *facts, int *n, float score, const char *fmt, ...) __attribute__((format(printf, 4, 5)));
#include <stdarg.h>
static void add(Fact *facts, int *n, float score, const char *fmt, ...) {
    if (*n >= 40) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(facts[*n].text, sizeof facts[*n].text, fmt, ap);
    va_end(ap);
    facts[*n].score = score;
    (*n)++;
}

static const char *RESP_WORD[GE_RESP_COUNT] = { "ATTACK", "BLOCK", "JUMP", "DASH", "DO NOTHING" };
static const char *ROLE_WORD[GE_INT_ATTACKS] = { "LIGHT POKES", "LOW ATTACKS", "OVERHEADS", "RISING ATTACKS", "PROJECTILES", "RUSHES", "HEAVY COMBOS", "SUPERS", "JUMPING ATTACKS", "THROWS" };
static const char *DIST_WORD[GE_DIST_N] = { "POINT-BLANK", "CLOSE", "MID", "LONG" };

static void resp_fact(Fact *f, int *n, const float *r, const char *fmt) {
    float tot = 0;
    for (int i = 0; i < GE_RESP_COUNT; i++) tot += r[i];
    if (tot < 3.0f) return;
    int best = 0;
    for (int i = 1; i < GE_RESP_COUNT; i++) if (r[i] > r[best]) best = i;
    float share = r[best] / tot;
    add(f, n, share * (tot < 12 ? 0.8f : 1.0f), fmt, RESP_WORD[best], (int)(share * 100.0f + 0.5f));
}

int ge_model_describe(const GePlayerModel *m, char out[][72], int max) {
    Fact facts[40];
    int n = 0;
    static const char *GUARD_WORD[3] = { "NORMAL", "LOW", "HIGH" };
    for (int g = 1; g <= 2; g++) {
        if (m->faced[g] < 4.0f) continue;
        float rate = m->blocked[g] / m->faced[g];
        add(facts, &n, 0.5f + (rate > 0.5f ? rate - 0.5f : 0.5f - rate) * 0.9f, "YOU BLOCK %s ATTACKS %d%% OF THE TIME", GUARD_WORD[g], (int)(rate * 100.0f + 0.5f));
    }
    resp_fact(facts, &n, m->vs_jumpin, "WHEN IT JUMPS AT YOU, YOU %s (%d%%)");
    resp_fact(facts, &n, m->after_block, "AFTER BLOCKING, YOU %s (%d%%)");
    resp_fact(facts, &n, m->wakeup, "WHEN YOU GET UP, YOU %s (%d%%)");
    resp_fact(facts, &n, m->vs_shot, "AGAINST PROJECTILES, YOU %s (%d%%)");
    if (m->throws_faced >= 3.0f) add(facts, &n, 0.55f + 0.3f * (1.0f - m->throws_teched / m->throws_faced), "YOU ESCAPE %d%% OF THROWS", (int)(100.0f * m->throws_teched / m->throws_faced + 0.5f));
    float dist_tot[GE_DIST_N] = { 0 }, all = 0, role_tot[GE_INT_ATTACKS] = { 0 };
    for (int d = 0; d < GE_DIST_N; d++)
        for (int i = 0; i < GE_INT_ATTACKS; i++) { dist_tot[d] += m->attacks[d][i]; role_tot[i] += m->attacks[d][i]; all += m->attacks[d][i]; }
    if (all >= 8.0f) {
        int bd = 0, br = 0;
        for (int d = 1; d < GE_DIST_N; d++) if (dist_tot[d] > dist_tot[bd]) bd = d;
        for (int i = 1; i < GE_INT_ATTACKS; i++) if (role_tot[i] > role_tot[br]) br = i;
        add(facts, &n, 0.5f + 0.4f * dist_tot[bd] / all, "YOU FIGHT AT %s RANGE (%d%%)", DIST_WORD[bd], (int)(100.0f * dist_tot[bd] / all + 0.5f));
        add(facts, &n, 0.5f + 0.4f * role_tot[br] / all, "YOUR FAVOURITE WEAPON: %s (%d%%)", ROLE_WORD[br], (int)(100.0f * role_tot[br] / all + 0.5f));
    }
    if (m->ow[GE_OW_NEEDLE] >= 3) add(facts, &n, 0.45f, "YOU THREW %u NEEDLES. IT COUNTED EVERY ONE", (unsigned)m->ow[GE_OW_NEEDLE]);
    if (m->ow[GE_OW_POGO] >= 3) add(facts, &n, 0.45f, "YOU BOUNCED OFF %u THINGS WITH A DOWN-SLASH", (unsigned)m->ow[GE_OW_POGO]);
    if (m->ow[GE_OW_DASH] >= 10) add(facts, &n, 0.4f, "YOU DASHED %u TIMES", (unsigned)m->ow[GE_OW_DASH]);
    if (m->ow[GE_OW_DEATH] >= 2) add(facts, &n, 0.5f, "YOU FELL %u TIMES. IT REMEMBERS EACH ONE", (unsigned)m->ow[GE_OW_DEATH]);
    if (m->duels >= 1) add(facts, &n, 0.35f, "%u %s WATCHED. YOU WON %u. IT LEARNED FROM ALL", (unsigned)m->duels, m->duels == 1 ? "DUEL" : "DUELS", (unsigned)m->wins);
    /* most distinctive first (simple selection sort; there are only a handful) */
    int count = 0;
    while (count < max && n > 0) {
        int best = 0;
        for (int i = 1; i < n; i++) if (facts[i].score > facts[best].score) best = i;
        snprintf(out[count++], 72, "%s", facts[best].text);
        facts[best] = facts[--n];
    }
    return count;
}
