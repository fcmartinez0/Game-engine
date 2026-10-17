#include "ge/ge_engine.h"

#include <string.h>

static GeFrameTimes g_times;
GeFrameTimes ge_frame_times(void) { return g_times; }

int ge_run(const GeGame *game, const GePlatformConfig *cfg) {
    GePlatformConfig c = *cfg;
    c.two_screens = game->draw_bottom != NULL;
    if (c.width <= 0) { c.width = GE_SCREEN_W; c.height = GE_SCREEN_H; }
    if (c.scale <= 0) c.scale = 3;
    if (ge_plat_init(&c) != 0) return 1;

    GeImage fb = ge_image_create(c.width, c.height);
    GeImage bottom = { 0, 0, NULL };
    if (c.two_screens) bottom = ge_image_create(GE_BOTTOM_W, GE_BOTTOM_H);
    int rc = fb.px && (!c.two_screens || bottom.px) ? 0 : 1;
    if (rc == 0 && game->init && game->init(game->user) != 0) rc = 1;

    const uint64_t step = 1000000u / GE_TICK_HZ;
    uint64_t last = ge_plat_time_us(), acc = 0;
    GeInput in;
    memset(&in, 0, sizeof in);
    uint32_t buttons = 0;

    while (rc == 0 && ge_plat_poll(&buttons)) {
        uint64_t now = ge_plat_time_us();
        acc += now - last;
        last = now;
        if (acc > step * 5) acc = step * 5; /* don't spiral after a stall */
        if (acc < step) { ge_plat_sleep_us(1000); continue; }

        while (acc >= step) {
            ge_input_update(&in, buttons);
            int tx = 0, ty = 0;
            bool t = ge_plat_touch(&tx, &ty) != 0;
            in.touch_pressed = t && !in.touch;
            in.touch = t;
            if (t) { in.touch_x = tx; in.touch_y = ty; }
            game->update(game->user, &in);
            acc -= step;
        }
        game->draw(game->user, &fb);
        uint64_t t0 = ge_plat_time_us();
        if (c.two_screens && game->draw_bottom(game->user, &bottom)) ge_plat_present_bottom(&bottom);
        uint64_t t1 = ge_plat_time_us();
        ge_plat_present(&fb);
        uint64_t t2 = ge_plat_time_us();
        ge_plat_wait_frame();
        uint64_t t3 = ge_plat_time_us();
        g_times.bottom_ms = (float)(t1 - t0) / 1000.0f;
        g_times.present_ms = (float)(t2 - t1) / 1000.0f;
        g_times.wait_ms = (float)(t3 - t2) / 1000.0f;
    }

    if (game->shutdown) game->shutdown(game->user);
    ge_image_free(&fb);
    if (bottom.px) ge_image_free(&bottom);
    ge_plat_shutdown();
    return rc;
}
