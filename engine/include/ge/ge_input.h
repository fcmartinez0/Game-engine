#ifndef GE_INPUT_H
#define GE_INPUT_H

#include <stdint.h>

/* Abstract buttons. Each platform backend maps its hardware onto these
 * (3DS: D-pad/circle pad, B=jump, Y=attack, A=dash, L/R=depth lane). */
enum {
    GE_BTN_LEFT = 1 << 0,
    GE_BTN_RIGHT = 1 << 1,
    GE_BTN_UP = 1 << 2,
    GE_BTN_DOWN = 1 << 3,
    GE_BTN_JUMP = 1 << 4,
    GE_BTN_ATTACK = 1 << 5,
    GE_BTN_DASH = 1 << 6,
    GE_BTN_LANE_BACK = 1 << 7,  /* step into the background lane */
    GE_BTN_LANE_FRONT = 1 << 8, /* step into the foreground lane */
    GE_BTN_START = 1 << 9
};

typedef struct {
    uint32_t down;     /* held this tick */
    uint32_t pressed;  /* went down this tick */
    uint32_t released;
} GeInput;

static inline void ge_input_update(GeInput *in, uint32_t now) {
    in->pressed = now & ~in->down;
    in->released = in->down & ~now;
    in->down = now;
}

#endif
