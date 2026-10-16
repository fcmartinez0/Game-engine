#include "ge/ge_platform.h"
#include "ge/ge_platform_null.h"

#include <stdio.h>
#include <string.h>

static int g_max_frames = 600, g_dump_every = 0, g_frame = 0;
static const char *g_dump_dir = ".";
static GeNullScript g_script;
static void *g_script_user;
static uint64_t g_clock_us;
static uint32_t g_checksum;
static GeNullTouch g_touch;
static GeImage g_bottom;
static int g_bottom_updates;

void ge_null_configure(int max_frames, int dump_every, const char *dump_dir, GeNullScript script, void *user) {
    g_max_frames = max_frames; g_dump_every = dump_every; g_script = script; g_script_user = user;
    if (dump_dir) g_dump_dir = dump_dir;
    g_frame = 0; g_clock_us = 0;
}

uint32_t ge_null_last_checksum(void) { return g_checksum; }
void ge_null_set_touch(GeNullTouch fn) { g_touch = fn; }
int ge_null_bottom_updates(void) { return g_bottom_updates; }

int ge_plat_touch(int *x, int *y) { return g_touch ? g_touch(g_script_user, g_frame, x, y) : 0; }

void ge_plat_present_bottom(const GeImage *fb) {
    if (!g_bottom.px) g_bottom = ge_image_create(fb->w, fb->h);
    if (g_bottom.px) memcpy(g_bottom.px, fb->px, sizeof(uint32_t) * (size_t)(fb->w * fb->h));
    g_bottom_updates++;
}

int ge_plat_init(const GePlatformConfig *cfg) { (void)cfg; return 0; }
void ge_plat_shutdown(void) { if (g_bottom.px) ge_image_free(&g_bottom); g_bottom.px = NULL; }

int ge_plat_poll(uint32_t *buttons) {
    if (g_frame >= g_max_frames) return 0;
    *buttons = g_script ? g_script(g_script_user, g_frame) : 0;
    return 1;
}

void ge_plat_present(const GeImage *fb) {
    if (g_frame == g_max_frames - 1) g_checksum = ge_image_checksum(fb); /* only the last one is reported */
    if (g_dump_every > 0 && g_frame % g_dump_every == 0) {
        char path[512];
        snprintf(path, sizeof path, "%s/frame_%05d.png", g_dump_dir, g_frame);
        ge_image_save_png(fb, path);
        if (g_bottom.px) {   /* the bottom screen as it currently stands */
            snprintf(path, sizeof path, "%s/frame_%05d_bottom.png", g_dump_dir, g_frame);
            ge_image_save_png(&g_bottom, path);
        }
    }
    g_frame++;
}

uint64_t ge_plat_time_us(void) { return g_clock_us; }
void ge_plat_sleep_us(uint32_t us) { g_clock_us += us; }
