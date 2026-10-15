#include "ge/ge_anim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ge_sprite_find_clip(const GeSprite *s, const char *name) {
    for (int i = 0; i < s->nclips; i++)
        if (strcmp(s->clips[i].name, name) == 0) return i;
    return -1;
}

void ge_sprite_free(GeSprite *s) {
    ge_image_free(&s->sheet);
    memset(s, 0, sizeof *s);
}

static int parse_floats(const char *s, float *out, int n) {
    for (int i = 0; i < n; i++) {
        char *end;
        out[i] = strtof(s, &end);
        if (end == s) return 0;
        s = end;
        if (i < n - 1) { if (*s != ',') return 0; s++; }
    }
    return 1;
}

static int parse_clip_opt(GeClip *c, const char *tok) {
    const char *eq = strchr(tok, '=');
    if (!eq) return 0;
    size_t klen = (size_t)(eq - tok);
    const char *v = eq + 1;
    float f[2];
    if (klen == 3 && !strncmp(tok, "dmg", 3)) { c->damage = atoi(v); c->is_attack = true; }
    else if (klen == 2 && !strncmp(tok, "kb", 2)) { if (!parse_floats(v, f, 2)) return 0; c->kb_x = f[0]; c->kb_y = f[1]; }
    else if (klen == 4 && !strncmp(tok, "stop", 4)) c->hitstop = atoi(v);
    else if (klen == 4 && !strncmp(tok, "stun", 4)) c->stun = atoi(v);
    else if (klen == 6 && !strncmp(tok, "cancel", 6)) c->cancel_from = atoi(v);
    else if (klen == 5 && !strncmp(tok, "lunge", 5)) c->lunge = (float)atof(v);
    else if (klen == 4 && !strncmp(tok, "next", 4)) { strncpy(c->next_name, v, GE_NAME_LEN - 1); }
    else return 0;
    return 1;
}

int ge_sprite_parse(GeSprite *s, const char *text) {
    char line[256];
    int lineno = 0;
    GeClip *cur = NULL;
    s->nclips = 0;
    if (s->cell_w == 0) { s->cell_w = 32; s->cell_h = 32; }

    for (const char *p = text; *p;) {
        size_t n = 0;
        while (*p && *p != '\n') { if (n < sizeof line - 1) line[n++] = *p; p++; }
        if (*p == '\n') p++;
        line[n] = 0;
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;

        char *tok[12];
        int nt = 0;
        for (char *q = line; *q && nt < 12;) {
            while (*q == ' ' || *q == '\t' || *q == '\r') *q++ = 0;
            if (!*q) break;
            tok[nt++] = q;
            while (*q && *q != ' ' && *q != '\t' && *q != '\r') q++;
        }
        if (nt == 0) continue;

        if (!strcmp(tok[0], "cell") && nt == 3) {
            s->cell_w = atoi(tok[1]); s->cell_h = atoi(tok[2]);
        } else if (!strcmp(tok[0], "origin") && nt == 3) {
            s->origin_x = atoi(tok[1]); s->origin_y = atoi(tok[2]);
        } else if (!strcmp(tok[0], "clip") && nt >= 3) {
            if (s->nclips >= GE_SPRITE_MAX_CLIPS) return -lineno;
            cur = &s->clips[s->nclips++];
            memset(cur, 0, sizeof *cur);
            strncpy(cur->name, tok[1], GE_NAME_LEN - 1);
            cur->next = -1;
            cur->cancel_from = -1;
            if (!strcmp(tok[2], "loop")) cur->loop = true;
            else if (strcmp(tok[2], "once") != 0) return -lineno;
            for (int i = 3; i < nt; i++)
                if (!parse_clip_opt(cur, tok[i])) return -lineno;
        } else if (!strcmp(tok[0], "frame") && nt >= 3 && cur) {
            if (cur->nframes >= GE_ANIM_MAX_FRAMES) return -lineno;
            GeAnimFrame *f = &cur->frames[cur->nframes++];
            memset(f, 0, sizeof *f);
            f->cell = atoi(tok[1]);
            f->ticks = atoi(tok[2]);
            if (f->ticks <= 0) return -lineno;
            for (int i = 3; i < nt; i++) {
                float r[4];
                if (!strncmp(tok[i], "hit=", 4) && parse_floats(tok[i] + 4, r, 4)) {
                    f->has_hit = true;
                    f->hit = (GeRect){ r[0], r[1], r[2], r[3] };
                } else return -lineno;
            }
        } else {
            return -lineno;
        }
    }

    s->cols = s->sheet.w > 0 ? s->sheet.w / s->cell_w : 0;
    for (int i = 0; i < s->nclips; i++) {
        GeClip *c = &s->clips[i];
        if (c->nframes == 0) return -lineno;
        if (c->next_name[0]) {
            c->next = ge_sprite_find_clip(s, c->next_name);
            if (c->next < 0) return -lineno;
        }
    }
    return 0;
}

int ge_sprite_load(GeSprite *s, const char *pam_path, const char *anim_path) {
    memset(s, 0, sizeof *s);
    if (ge_image_load_pam(&s->sheet, pam_path) != 0) return -1;
    FILE *f = fopen(anim_path, "rb");
    if (!f) { ge_image_free(&s->sheet); return -2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = n >= 0 ? (char *)malloc((size_t)n + 1) : NULL;
    if (!text || fread(text, 1, (size_t)n, f) != (size_t)n) { free(text); fclose(f); ge_image_free(&s->sheet); return -2; }
    text[n] = 0;
    fclose(f);
    int rc = ge_sprite_parse(s, text);
    free(text);
    if (rc != 0) { fprintf(stderr, "%s: syntax error on line %d\n", anim_path, -rc); ge_image_free(&s->sheet); return -3; }
    return 0;
}

void ge_anim_play(GeAnimator *a, int clip, bool restart) {
    if (a->clip == clip && !restart) return;
    a->clip = clip; a->frame = 0; a->timer = 0; a->done = false;
}

void ge_anim_update(GeAnimator *a) {
    const GeClip *c = &a->spr->clips[a->clip];
    if (a->done || c->nframes == 0) return;
    if (++a->timer >= c->frames[a->frame].ticks) {
        a->timer = 0;
        if (a->frame + 1 < c->nframes) a->frame++;
        else if (c->loop) a->frame = 0;
        else a->done = true;
    }
}

const GeAnimFrame *ge_anim_frame(const GeAnimator *a) {
    return &a->spr->clips[a->clip].frames[a->frame];
}

void ge_anim_draw(const GeAnimator *a, GeImage *dst, int x, int y, bool flip, int flags, uint32_t tint) {
    const GeSprite *s = a->spr;
    int cell = ge_anim_frame(a)->cell;
    GeIRect sr = { (cell % s->cols) * s->cell_w, (cell / s->cols) * s->cell_h, s->cell_w, s->cell_h };
    /* the mirrored origin pixel is cell_w-1-origin_x */
    int dx = flip ? x - (s->cell_w - 1 - s->origin_x) : x - s->origin_x;
    ge_blit(dst, &s->sheet, sr, dx, y - s->origin_y, flags | (flip ? GE_FLIP_X : 0), tint);
}
