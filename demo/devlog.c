#include "devlog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef __3DS__
#include <sys/stat.h>
#endif

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
#ifdef __3DS__
    mkdir("sdmc:/3ds", 0777);              /* in case the game was copied somewhere else: make its folder */
    mkdir("sdmc:/3ds/gloamwood", 0777);
#endif
    /* Each session gets a fresh log.txt, so the file to send is always the newest one; the session before
     * is kept as log_previous.txt. (Appending everything to one file buried the newest session at the end.) */
    for (unsigned i = 0; i < sizeof PATHS / sizeof PATHS[0]; i++) {
        char prev[96];
        size_t n = strlen(PATHS[i]);
        if (n > 4 && n + 10 < sizeof prev) {
            memcpy(prev, PATHS[i], n - 4);
            strcpy(prev + n - 4, "_previous.txt");
            FILE *probe = fopen(PATHS[i], "r");
            if (probe) { fclose(probe); remove(prev); rename(PATHS[i], prev); }
        }
        g_file = fopen(PATHS[i], "w");
        if (g_file) { g_path = PATHS[i]; break; }
    }
    return g_file != NULL;
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

/* Close and reopen rather than fflush: on the 3DS's FAT card the file's length is only written when the file is
 * closed, so a session that ends without START+SELECT (HOME, power button, a crash) used to leave an empty or
 * missing log even though the text had been written. Reopening costs little; it happens a few times a minute. */
void devlog_flush(void) {
    if (!g_file) return;
    fclose(g_file);
    g_file = fopen(g_path, "a");
}

void devlog_close(void) {
    if (!g_file) return;
    fclose(g_file);
    g_file = NULL;
}
