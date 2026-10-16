#include "devlog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static FILE *g_file;
static const char *g_path = "";
static uint64_t g_start_us, g_now_us;

/* Try the game's own folder first, then the SD card root. */
#ifdef __3DS__
static const char *const PATHS[] = { "sdmc:/3ds/gloamwood/log.txt", "sdmc:/3ds/gloamwood_log.txt", "sdmc:/gloamwood_log.txt" };
#else
static const char *const PATHS[] = { "gloamwood_log.txt" };
#endif

bool devlog_open(void) {
    if (g_file) return true;
    for (unsigned i = 0; i < sizeof PATHS / sizeof PATHS[0]; i++) {
        g_file = fopen(PATHS[i], "a");
        if (g_file) { g_path = PATHS[i]; break; }
    }
    if (!g_file) return false;
    fputs("\n==================================================================\n", g_file);
    return true;
}

bool devlog_is_open(void) { return g_file != NULL; }
const char *devlog_path(void) { return g_path; }

void devlog_set_clock(uint64_t start_us, uint64_t now_us) { g_start_us = start_us; g_now_us = now_us; }

const char *devlog_hms(uint64_t s) {
    static char bufs[4][16];   /* a few in rotation, so one log line can format several */
    static int next;
    char *buf = bufs[next++ & 3];
    snprintf(buf, sizeof bufs[0], "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    return buf;
}

void devlog(const char *fmt, ...) {
    if (!g_file) return;
    uint64_t s = g_now_us > g_start_us ? (g_now_us - g_start_us) / 1000000u : 0;
    fprintf(g_file, "[%s] ", devlog_hms(s));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_file, fmt, ap);
    va_end(ap);
    fputc('\n', g_file);
}

void devlog_flush(void) { if (g_file) fflush(g_file); }

void devlog_close(void) {
    if (!g_file) return;
    fclose(g_file);
    g_file = NULL;
}
