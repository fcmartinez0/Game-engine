#include "ge/ge_engine.h"

int ge_run(const GeGame *game, const GePlatformConfig *cfg) {
    GePlatformConfig c = *cfg;
    if (c.width <= 0) { c.width = GE_SCREEN_W; c.height = GE_SCREEN_H; }
    if (c.scale <= 0) c.scale = 3;
    if (ge_plat_init(&c) != 0) return 1;

    GeImage fb = ge_image_create(c.width, c.height);
    int rc = fb.px ? 0 : 1;
    if (rc == 0 && game->init && game->init(game->user) != 0) rc = 1;

    const uint64_t step = 1000000u / GE_TICK_HZ;
    uint64_t last = ge_plat_time_us(), acc = 0;
    GeInput in = { 0, 0, 0 };
    uint32_t buttons = 0;

    while (rc == 0 && ge_plat_poll(&buttons)) {
        uint64_t now = ge_plat_time_us();
        acc += now - last;
        last = now;
        if (acc > step * 5) acc = step * 5; /* don't spiral after a stall */
        if (acc < step) { ge_plat_sleep_us(1000); continue; }

        while (acc >= step) {
            ge_input_update(&in, buttons);
            game->update(game->user, &in);
            acc -= step;
        }
        game->draw(game->user, &fb);
        ge_plat_present(&fb);
    }

    if (game->shutdown) game->shutdown(game->user);
    ge_image_free(&fb);
    ge_plat_shutdown();
    return rc;
}
