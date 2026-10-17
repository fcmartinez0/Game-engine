#ifndef GE_PLATFORM_NULL_H
#define GE_PLATFORM_NULL_H

#include <stdint.h>

/* Headless backend: no window, virtual clock, scripted input, optional PNG
 * dumps. Used by the test-suite and for generating screenshots in CI. */

typedef uint32_t (*GeNullScript)(void *user, int frame); /* returns GE_BTN_* mask */

void     ge_null_configure(int max_frames, int dump_every, const char *dump_dir, GeNullScript script, void *user);
uint32_t ge_null_last_checksum(void);
/* scripted stylus: return 1 and fill x, y to touch the bottom screen on that frame */
typedef int (*GeNullTouch)(void *user, int frame, int *x, int *y);
void     ge_null_set_touch(GeNullTouch fn);
int      ge_null_bottom_updates(void);   /* how many times the bottom screen was sent */
uint32_t ge_null_bottom_checksum(void);  /* the bottom screen as last sent (0 if never) */

#endif
