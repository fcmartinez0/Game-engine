#ifndef GE_PLATFORM_H
#define GE_PLATFORM_H

#include "ge_gfx.h"
#include <stdint.h>

/* The only interface a backend has to implement. The engine core never talks
 * to SDL or libctru directly: platform/sdl2.c, platform/n3ds.c and
 * platform/null.c each provide these six functions. */

typedef struct {
    const char *title;
    int width, height; /* internal framebuffer size, 400x240 */
    int scale;         /* desktop window multiplier */
} GePlatformConfig;

int      ge_plat_init(const GePlatformConfig *cfg);        /* 0 on success */
void     ge_plat_shutdown(void);
int      ge_plat_poll(uint32_t *buttons);                  /* fills GE_BTN_* mask; 0 = quit requested */
void     ge_plat_present(const GeImage *fb);
uint64_t ge_plat_time_us(void);                            /* monotonic */
void     ge_plat_sleep_us(uint32_t us);

#endif
