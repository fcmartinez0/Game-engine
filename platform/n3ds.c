/* Nintendo 3DS backend (devkitPro + libctru). Draws the 400x240 software
 * framebuffer straight into the top screen. NOTE: written against libctru's
 * documented API but not yet run on hardware -- see README. */

#include "ge/ge_input.h"
#include "ge/ge_platform.h"

#include <3ds.h>
#include <stddef.h>

int ge_plat_init(const GePlatformConfig *cfg) {
    (void)cfg;
    gfxInitDefault();
    gfxSet3D(false);
    osSetSpeedupEnable(true); /* New 3DS: 804 MHz; ignored on the original */
    return 0;
}

void ge_plat_shutdown(void) { gfxExit(); }

int ge_plat_poll(uint32_t *buttons) {
    if (!aptMainLoop()) return 0;
    hidScanInput();
    u32 k = hidKeysHeld();
    if (k & KEY_SELECT) return 0;
    circlePosition cp;
    hidCircleRead(&cp);
    uint32_t b = 0;
    if ((k & KEY_DLEFT) || cp.dx < -40) b |= GE_BTN_LEFT;
    if ((k & KEY_DRIGHT) || cp.dx > 40) b |= GE_BTN_RIGHT;
    if ((k & KEY_DUP) || cp.dy > 40) b |= GE_BTN_UP;
    if ((k & KEY_DDOWN) || cp.dy < -40) b |= GE_BTN_DOWN;
    if (k & KEY_B) b |= GE_BTN_JUMP;
    if (k & KEY_Y) b |= GE_BTN_ATTACK;
    if (k & KEY_A) b |= GE_BTN_DASH;
    if (k & KEY_L) b |= GE_BTN_LANE_BACK;
    if (k & KEY_R) b |= GE_BTN_LANE_FRONT;
    if (k & KEY_START) b |= GE_BTN_START;
    *buttons = b;
    return 1;
}

void ge_plat_present(const GeImage *fb) {
    /* The top screen framebuffer is 240 columns x 400 rows of BGR, stored
     * column-major with y flipped: pixel (x, y) lives at (x*240 + (239-y))*3. */
    u16 fw, fh;
    u8 *dst = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fw, &fh);
    for (int x = 0; x < fb->w; x++) {
        u8 *col = dst + (size_t)x * fb->h * 3;
        for (int y = 0; y < fb->h; y++) {
            u32 p = fb->px[(size_t)y * fb->w + x];
            u8 *o = col + (size_t)(fb->h - 1 - y) * 3;
            o[0] = (u8)p; o[1] = (u8)(p >> 8); o[2] = (u8)(p >> 16);
        }
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

uint64_t ge_plat_time_us(void) { return (uint64_t)osGetTime() * 1000ull; }
void ge_plat_sleep_us(uint32_t us) { svcSleepThread((s64)us * 1000); }
