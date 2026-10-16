#ifndef GE_INPUT_H
#define GE_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/* Physical buttons, named by *position* in the Nintendo layout (A right,
 * B bottom, X top, Y left). Each game mode decides what they mean -- the
 * overworld and the fighting game use different layouts. Backends map their
 * hardware here: an Xbox pad's bottom button is GE_BTN_B, its right one GE_BTN_A. */
enum {
    GE_BTN_LEFT = 1 << 0,
    GE_BTN_RIGHT = 1 << 1,
    GE_BTN_UP = 1 << 2,
    GE_BTN_DOWN = 1 << 3,
    GE_BTN_A = 1 << 4,
    GE_BTN_B = 1 << 5,
    GE_BTN_X = 1 << 6,
    GE_BTN_Y = 1 << 7,
    GE_BTN_L = 1 << 8,
    GE_BTN_R = 1 << 9,
    GE_BTN_START = 1 << 10,
    GE_BTN_SELECT = 1 << 11
};

typedef struct {
    uint32_t down;     /* held this tick */
    uint32_t pressed;  /* went down this tick */
    uint32_t released;
    bool touch, touch_pressed;   /* stylus (or mouse) on the bottom screen: held / went down this tick */
    int touch_x, touch_y;        /* bottom-screen pixels, 0..319 x 0..239 */
} GeInput;

static inline void ge_input_update(GeInput *in, uint32_t now) {
    in->pressed = now & ~in->down;
    in->released = in->down & ~now;
    in->down = now;
}

#endif
