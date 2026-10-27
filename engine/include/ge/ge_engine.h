#ifndef GE_ENGINE_H
#define GE_ENGINE_H

#include "ge_input.h"
#include "ge_platform.h"

#define GE_SCREEN_W 400
#define GE_SCREEN_H 240
#define GE_TICK_HZ 60

typedef struct {
    void *user;
    int  (*init)(void *user);                            /* 0 on success */
    void (*update)(void *user, const GeInput *in);       /* fixed 60 Hz */
    void (*draw)(void *user, GeImage *fb);
    void (*shutdown)(void *user);
    /* optional: draw the bottom screen (GE_BOTTOM_W x GE_BOTTOM_H). Return true when it changed; it is only
     * sent to the screen then, so a mostly still panel costs nothing on frames it doesn't change. */
    bool (*draw_bottom)(void *user, GeImage *fb);
} GeGame;

/* Where the last frame's time went outside the game's own update and draw, for perf readouts: drawing and
 * sending the bottom screen, sending the top screen (on a 3DS: rotating the picture into the sideways
 * framebuffer), and waiting for the display's vsync (time to spare, if the frame made it). */
typedef struct { float bottom_ms, present_ms, wait_ms; } GeFrameTimes;
GeFrameTimes ge_frame_times(void);

/* Fixed-timestep loop: game logic always advances in 1/60 s steps (so physics
 * and frame data behave identically on a 60 fps desktop and the 3DS), and
 * rendering happens once per loop. Returns 0 on a clean exit. */
int ge_run(const GeGame *game, const GePlatformConfig *cfg);
/* Ask ge_run to finish after this frame (a game's own QUIT): shutdown runs as on any exit. */
void ge_request_quit(void);

#endif
