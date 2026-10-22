#include "devlog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef __3DS__
#include <sys/stat.h>
#endif

/* Lines collect in memory and go to the card in one open-write-close: on the 3DS each of those costs
 * tens of milliseconds, and writing every line straight away (eight of them at the end of a duel) froze
 * the game for a quarter of a second. The game calls devlog_flush at calm moments (a bench, the menu, a
 * fade) and devlog_flush_if_due as a backstop; the file is complete once it has been flushed. */
static bool g_open;
static char g_buf[16384];
static size_t g_len;
static const char *g_path = "";
static uint64_t g_start_us, g_now_us, g_flushed_us;

/* Try the game's own folder first, then the SD card root. */
#ifdef __3DS__
static const char *const PATHS[] = { "sdmc:/3ds/gloamwood/log.txt", "sdmc:/3ds/gloamwood_log.txt", "sdmc:/gloamwood_log.txt" };
#else
static const char *const PATHS[] = { "gloamwood_log.txt" };
#endif

bool devlog_open(void) {
    if (g_open) return true;
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
        FILE *f = fopen(PATHS[i], "w");
        if (f) { fclose(f); g_path = PATHS[i]; g_open = true; break; }
    }
    g_len = 0;
    g_flushed_us = g_now_us;
    return g_open;
}

bool devlog_is_open(void) { return g_open; }
const char *devlog_path(void) { return g_path; }

void devlog_set_clock(uint64_t start_us, uint64_t now_us) {
    if (g_flushed_us == 0) g_flushed_us = now_us;   /* opened before the clock was first set */
    g_start_us = start_us;
    g_now_us = now_us;
}

const char *devlog_hms(uint64_t s) {
    static char bufs[4][16];   /* a few in rotation, so one log line can format several */
    static int next;
    char *buf = bufs[next++ & 3];
    snprintf(buf, sizeof bufs[0], "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    return buf;
}

void devlog(const char *fmt, ...) {
    if (!g_open) return;
    char line[1024];
    uint64_t s = g_now_us > g_start_us ? (g_now_us - g_start_us) / 1000000u : 0;
    int n = snprintf(line, sizeof line, "[%s] ", devlog_hms(s));
    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
    va_end(ap);
    n += m < 0 ? 0 : m > (int)(sizeof line) - n - 2 ? (int)(sizeof line) - n - 2 : m;
    line[n++] = '\n';
    if (g_len + (size_t)n > sizeof g_buf) devlog_flush();
    memcpy(g_buf + g_len, line, (size_t)n);
    g_len += (size_t)n;
}

/* Append what has collected: one open, one write, one close (on the 3DS's FAT card the file's length is
 * only recorded when it is closed, so a session that ends without START+SELECT still leaves a log). */
void devlog_flush(void) {
    if (!g_open || g_len == 0) return;
    FILE *f = fopen(g_path, "a");
    if (f) {
        fwrite(g_buf, 1, g_len, f);
        fclose(f);
    }
    g_len = 0;
    g_flushed_us = g_now_us;
}

void devlog_flush_if_due(uint64_t max_age_us) {
    if (g_len > 0 && g_now_us - g_flushed_us >= max_age_us) devlog_flush();
}

void devlog_close(void) {
    devlog_flush();
    g_open = false;
}
