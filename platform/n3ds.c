/* Nintendo 3DS backend (devkitPro + libctru). Draws the 400x240 software
 * framebuffer straight into the top screen, and the optional 320x240 one into
 * the bottom (touch) screen. NOTE: written against libctru's
 * documented API but not yet run on hardware -- see README. */

#include "ge/ge_input.h"
#include "ge/ge_platform.h"
#include "n3ds_blit.h"

#include <3ds.h>
#include <stddef.h>

int ge_plat_init(const GePlatformConfig *cfg) {
    (void)cfg;
    gfxInitDefault();
    gfxSet3D(false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);   /* the bottom screen is only redrawn when it changes */
    osSetSpeedupEnable(true); /* New 3DS: 804 MHz; ignored on the original */
    return 0;
}

void ge_plat_shutdown(void) { gfxExit(); }

int ge_plat_poll(uint32_t *buttons) {
    if (!aptMainLoop()) return 0;
    hidScanInput();
    u32 k = hidKeysHeld();
    if ((k & KEY_START) && (k & KEY_SELECT)) return 0; /* Start+Select quits */
    circlePosition cp;
    hidCircleRead(&cp);
    uint32_t b = 0;
    if ((k & KEY_DLEFT) || cp.dx < -40) b |= GE_BTN_LEFT;
    if ((k & KEY_DRIGHT) || cp.dx > 40) b |= GE_BTN_RIGHT;
    if ((k & KEY_DUP) || cp.dy > 40) b |= GE_BTN_UP;
    if ((k & KEY_DDOWN) || cp.dy < -40) b |= GE_BTN_DOWN;
    if (k & KEY_A) b |= GE_BTN_A;
    if (k & KEY_B) b |= GE_BTN_B;
    if (k & KEY_X) b |= GE_BTN_X;
    if (k & KEY_Y) b |= GE_BTN_Y;
    if (k & KEY_L) b |= GE_BTN_L;
    if (k & KEY_R) b |= GE_BTN_R;
    if (k & KEY_START) b |= GE_BTN_START;
    if (k & KEY_SELECT) b |= GE_BTN_SELECT;
    *buttons = b;
    return 1;
}

int ge_plat_touch(int *x, int *y) {
    if (!(hidKeysHeld() & KEY_TOUCH)) return 0;
    touchPosition t;
    hidTouchRead(&t);
    *x = t.px; *y = t.py;
    return 1;
}

void ge_plat_present_bottom(const GeImage *fb) {
    u16 fw, fh;   /* sideways too: 240 wide, 320 tall */
    u8 *dst = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fw, &fh);
    if (dst && fw == fb->h && fh == fb->w) ge_3ds_rotate_bgr8(fb->px, fb->w, fb->h, dst);
}

void ge_plat_present(const GeImage *fb) {
    u16 fw, fh;   /* libctru reports the top screen as 240 wide, 400 tall: it's mounted sideways */
    u8 *dst = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fw, &fh);
    if (dst && fw == fb->h && fh == fb->w) ge_3ds_rotate_bgr8(fb->px, fb->w, fb->h, dst);
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

/* The system tick runs at SYSCLOCK_ARM11 (~268 MHz) on every model: fine-grained
 * enough to time a frame, unlike osGetTime's milliseconds. */
uint64_t ge_plat_time_us(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000000ull); }
void ge_plat_sleep_us(uint32_t us) { svcSleepThread((s64)us * 1000); }
