#include "ge_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *ge_read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *text = NULL;
    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0) n = ftell(f);
    if (n >= 0 && fseek(f, 0, SEEK_SET) == 0) text = (char *)malloc((size_t)n + 1);
    if (text && fread(text, 1, (size_t)n, f) != (size_t)n) { free(text); text = NULL; }
    if (text) text[n] = 0;
    fclose(f);
    return text;
}

int ge_next_line(const char **p, char *line, size_t cap) {
    if (!**p) return 0;
    size_t n = 0;
    while (**p && **p != '\n') { if (n < cap - 1) line[n++] = **p; (*p)++; }
    if (**p == '\n') (*p)++;
    line[n] = 0;
    char *hash = strchr(line, '#');
    if (hash) *hash = 0;
    return 1;
}

int ge_tokenize(char *line, char **tok, int max) {
    int nt = 0;
    for (char *q = line; *q && nt < max;) {
        while (*q == ' ' || *q == '\t' || *q == '\r') *q++ = 0;
        if (!*q) break;
        tok[nt++] = q;
        while (*q && *q != ' ' && *q != '\t' && *q != '\r') q++;
    }
    return nt;
}
