/* Loader for .layers files: a list of parallax layers, back to front. */

#include "ge/ge_world.h"
#include "ge_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_num(const char *v, float *out) {
    char *end;
    *out = strtof(v, &end);
    return end != v && *end == 0;
}

static bool image_is_opaque(const GeImage *img) {
    size_t n = (size_t)img->w * img->h;
    for (size_t i = 0; i < n; i++)
        if ((img->px[i] >> 24) != 255) return false;
    return true;
}

static bool is_absolute(const char *p) {
    return p[0] == '/' || p[0] == '\\' || strchr(p, ':') != NULL; /* also "C:\..." and "romfs:/..." */
}

static void undo(GeWorld *w, int first) {
    for (int i = first; i < w->nlayers; i++) { ge_image_free(&w->layers[i].img); ge_spans_free(&w->layers[i].spans); }
    w->nlayers = first;
}

int ge_world_parse_layers(GeWorld *w, const char *text, const char *base_dir, int *err_line) {
    const int first = w->nlayers;
    char line[512], path[1024];
    int lineno = 0, rc = GE_LAYERS_OK;

    for (const char *p = text; ge_next_line(&p, line, sizeof line);) {
        lineno++;
        char *tok[16];
        int nt = ge_tokenize(line, tok, 16);
        if (nt == 0) continue;
        if (strcmp(tok[0], "layer") != 0 || nt < 2) { rc = GE_LAYERS_ESYNTAX; break; }

        GeParallaxLayer l;
        memset(&l, 0, sizeof l);
        l.factor_x = 1.0f;
        l.alpha = 255;
        for (int i = 2; i < nt && rc == GE_LAYERS_OK; i++) {
            char *k = tok[i], *eq = strchr(k, '=');
            float v = 0;
            if (!eq) {
                if (!strcmp(k, "front")) l.foreground = true;
                else if (!strcmp(k, "additive")) l.additive = true;
                else if (!strcmp(k, "optional")) l.optional = true;
                else rc = GE_LAYERS_ESYNTAX;
                continue;
            }
            *eq = 0;
            if (!parse_num(eq + 1, &v)) rc = GE_LAYERS_ESYNTAX;
            else if (!strcmp(k, "factor")) l.factor_x = v;
            else if (!strcmp(k, "factor_y")) l.factor_y = v;
            else if (!strcmp(k, "drift")) l.auto_x = v;
            else if (!strcmp(k, "y")) l.y = (int)v;
            else if (!strcmp(k, "alpha") && v >= 1 && v <= 255) l.alpha = (uint8_t)v;
            else rc = GE_LAYERS_ESYNTAX;
        }
        if (rc != GE_LAYERS_OK) break;
        if (w->nlayers >= GE_MAX_LAYERS) { rc = GE_LAYERS_EFULL; break; }

        if (base_dir && base_dir[0] && !is_absolute(tok[1])) snprintf(path, sizeof path, "%s/%s", base_dir, tok[1]);
        else snprintf(path, sizeof path, "%s", tok[1]);
        if (ge_image_load_pam(&l.img, path) != 0) { rc = GE_LAYERS_EIMAGE; break; }
        l.opaque = image_is_opaque(&l.img);
        ge_world_add_layer(w, l);
    }

    if (rc != GE_LAYERS_OK) undo(w, first);
    if (err_line) *err_line = rc == GE_LAYERS_OK ? 0 : lineno;
    return rc;
}

int ge_world_load_layers(GeWorld *w, const char *path, int *err_line) {
    if (err_line) *err_line = 0;
    char *text = ge_read_file(path);
    if (!text) return GE_LAYERS_EOPEN;
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/'), *bslash = strrchr(dir, '\\');
    if (bslash > slash) slash = bslash;
    if (slash == dir) dir[1] = 0;          /* "/backdrop.layers" -> images from "/" */
    else if (slash) *slash = 0;
    else dir[0] = 0;
    int rc = ge_world_parse_layers(w, text, slash ? dir : NULL, err_line);
    free(text);
    return rc;
}
