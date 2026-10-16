#ifndef GE_PLATFORM_H
#define GE_PLATFORM_H

#include "ge_gfx.h"
#include <stdint.h>

/* The only interface a backend has to implement. The engine core never talks
 * to SDL or libctru directly: platform/sdl2.c, platform/n3ds.c and
 * platform/null.c each provide these functions.
 *
 * Two screens: a game may also draw a 320x240 "bottom screen". On a 3DS that is
 * the real lower (touch) screen; on a desktop the window grows to show it under
 * the main view, the way an emulator does, and the mouse stands in for the
 * stylus. The game code is the same either way. */

#include <stdbool.h>

#define GE_BOTTOM_W 320
#define GE_BOTTOM_H 240

typedef struct {
    const char *title;
    int width, height; /* internal framebuffer size, 400x240 */
    int scale;         /* desktop window multiplier */
    bool two_screens;  /* also show a GE_BOTTOM_W x GE_BOTTOM_H bottom screen (set by ge_run when the game draws one) */
} GePlatformConfig;

int      ge_plat_init(const GePlatformConfig *cfg);        /* 0 on success */
void     ge_plat_shutdown(void);
int      ge_plat_poll(uint32_t *buttons);                  /* fills GE_BTN_* mask; 0 = quit requested */
void     ge_plat_present(const GeImage *fb);
void     ge_plat_present_bottom(const GeImage *fb);        /* only called when the bottom screen changed */
int      ge_plat_touch(int *x, int *y);                    /* 1 while the bottom screen is touched (or clicked) */
uint64_t ge_plat_time_us(void);                            /* monotonic */
void     ge_plat_sleep_us(uint32_t us);

#endif
