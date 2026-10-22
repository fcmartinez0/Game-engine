#ifndef GE_PLATFORM_H
#define GE_PLATFORM_H

#include "ge_gfx.h"
#include <stdint.h>

/* The only interface a backend has to implement. The engine core never talks
 * to SDL or libctru directly: platform/sdl2.c, platform/n3ds.c and
 * platform/null.c each provide these functions.
 *
 * Two screens: a game may also draw a 320x240 "bottom screen". On a 3DS that is
 * the real lower (touch) screen and is always there. A desktop window has no use for a second
 * screen most of the time, so it is hidden there until the game asks for it with
 * ge_plat_set_bottom: under the main view (the way an emulator shows it) while something needs
 * the room, or over the main view while a menu is open. The mouse stands in for the stylus on
 * whichever shows it. The game code is the same either way. */

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
void     ge_plat_present(const GeImage *fb);                /* show the frame (copy it out, request the flip) */
void     ge_plat_wait_frame(void);                         /* wait for the display to take it (vsync), if the backend does */
void     ge_plat_present_bottom(const GeImage *fb);        /* only called when the bottom screen changed */
int      ge_plat_touch(int *x, int *y);                    /* 1 while the bottom screen is touched (or clicked) */
typedef enum { GE_BOTTOM_SEPARATE, GE_BOTTOM_OVERLAY, GE_BOTTOM_HIDDEN } GeBottomMode;
/* Where the bottom screen is shown: its own screen (the window grows to hold it), over the main view, or not at
 * all. A 3DS ignores this (it is always its own screen), and so does the headless backend. Cheap when unchanged. */
void     ge_plat_set_bottom(GeBottomMode mode);
uint64_t ge_plat_time_us(void);                            /* monotonic */
void     ge_plat_sleep_us(uint32_t us);

#endif
