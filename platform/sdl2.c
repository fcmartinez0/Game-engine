/* Desktop backend (Windows / macOS / Linux). Keyboard + first game controller. */

#include "ge/ge_input.h"
#include "ge/ge_platform.h"

#include <SDL.h>

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex, *g_btex;
static int g_w, g_h, g_two;
static int g_mouse_down, g_mx, g_my;   /* logical coordinates */
static SDL_GameController *g_pad;
static int g_shot_requested;

int ge_plat_init(const GePlatformConfig *cfg) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return -1;
    /* with a bottom screen the window is laid out like a 3DS: main view on top, the 320-wide screen centred under it */
    g_w = cfg->width; g_two = cfg->two_screens;
    g_h = cfg->height + (g_two ? GE_BOTTOM_H : 0);
    int scale = cfg->scale > 0 ? cfg->scale : 1;
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0)
        while (scale > 1 && (g_h * scale > dm.h - 80 || g_w * scale > dm.w)) scale--;   /* fit the screen */
    g_win = SDL_CreateWindow(cfg->title ? cfg->title : "ge", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             g_w * scale, g_h * scale, SDL_WINDOW_RESIZABLE);
    if (!g_win) return -2;
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) return -3;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* crisp pixels */
    SDL_RenderSetLogicalSize(g_ren, g_w, g_h);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, cfg->width, cfg->height);
    if (!g_tex) return -4;
    if (g_two) {
        g_btex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, GE_BOTTOM_W, GE_BOTTOM_H);
        if (!g_btex) return -5;
    }
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { g_pad = SDL_GameControllerOpen(i); break; }
    return 0;
}

void ge_plat_shutdown(void) {
    if (g_pad) SDL_GameControllerClose(g_pad);
    if (g_tex) SDL_DestroyTexture(g_tex);
    if (g_btex) SDL_DestroyTexture(g_btex);
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
}

int ge_plat_poll(uint32_t *buttons) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) return 0;
        if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
            if (ev.key.keysym.sym == SDLK_ESCAPE) return 0;
            if (ev.key.keysym.sym == SDLK_F12) g_shot_requested = 1;
        }
        /* the mouse is the stylus (SDL already reports it in logical pixels) */
        if (ev.type == SDL_MOUSEBUTTONDOWN && ev.button.button == SDL_BUTTON_LEFT) { g_mouse_down = 1; g_mx = ev.button.x; g_my = ev.button.y; }
        if (ev.type == SDL_MOUSEBUTTONUP && ev.button.button == SDL_BUTTON_LEFT) g_mouse_down = 0;
        if (ev.type == SDL_MOUSEMOTION) { g_mx = ev.motion.x; g_my = ev.motion.y; }
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint32_t b = 0;
    /* arrows or WASD to move; face buttons on Z X C V (or K J L I), shoulders on Q E (or U O) */
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) b |= GE_BTN_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) b |= GE_BTN_RIGHT;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) b |= GE_BTN_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) b |= GE_BTN_DOWN;
    if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_K]) b |= GE_BTN_B;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_J]) b |= GE_BTN_Y;
    if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_L]) b |= GE_BTN_A;   /* heavy / silk spin */
    if (k[SDL_SCANCODE_V] || k[SDL_SCANCODE_I]) b |= GE_BTN_X;
    if (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_U]) b |= GE_BTN_L;
    if (k[SDL_SCANCODE_E] || k[SDL_SCANCODE_O]) b |= GE_BTN_R;
    if (k[SDL_SCANCODE_RETURN]) b |= GE_BTN_START;
    if (k[SDL_SCANCODE_BACKSPACE]) b |= GE_BTN_SELECT;
    if (g_pad) {
        int lx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (lx < -12000 || SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) b |= GE_BTN_LEFT;
        if (lx > 12000 || SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= GE_BTN_RIGHT;
        if (ly < -12000 || SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) b |= GE_BTN_UP;
        if (ly > 12000 || SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) b |= GE_BTN_DOWN;
        /* SDL names buttons Xbox-style (A = bottom); map by position */
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A)) b |= GE_BTN_B;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B)) b |= GE_BTN_A;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_X)) b |= GE_BTN_Y;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_Y)) b |= GE_BTN_X;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) b |= GE_BTN_L;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= GE_BTN_R;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_START)) b |= GE_BTN_START;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_BACK)) b |= GE_BTN_SELECT;
    }
    *buttons = b;
    return 1;
}

int ge_plat_touch(int *x, int *y) {
    if (!g_two || !g_mouse_down) return 0;
    int bx = g_mx - (g_w - GE_BOTTOM_W) / 2, by = g_my - (g_h - GE_BOTTOM_H);
    if (bx < 0 || by < 0 || bx >= GE_BOTTOM_W || by >= GE_BOTTOM_H) return 0;
    *x = bx; *y = by;
    return 1;
}

void ge_plat_present_bottom(const GeImage *fb) {
    if (g_btex) SDL_UpdateTexture(g_btex, NULL, fb->px, fb->w * 4);
}

void ge_plat_present(const GeImage *fb) {
    if (g_shot_requested) {
        g_shot_requested = 0;
        ge_image_save_png(fb, "screenshot.png");
    }
    SDL_UpdateTexture(g_tex, NULL, fb->px, fb->w * 4);
    SDL_SetRenderDrawColor(g_ren, 12, 10, 18, 255);
    SDL_RenderClear(g_ren);
    SDL_Rect top = { 0, 0, fb->w, fb->h };
    SDL_RenderCopy(g_ren, g_tex, NULL, &top);
    if (g_btex) {
        SDL_Rect bot = { (g_w - GE_BOTTOM_W) / 2, fb->h, GE_BOTTOM_W, GE_BOTTOM_H };
        SDL_RenderCopy(g_ren, g_btex, NULL, &bot);
    }
    SDL_RenderPresent(g_ren);
}

void ge_plat_wait_frame(void) {}   /* SDL_RenderPresent already waits for vsync when it is on */

uint64_t ge_plat_time_us(void) {
    return (uint64_t)((double)SDL_GetPerformanceCounter() * 1e6 / (double)SDL_GetPerformanceFrequency());
}
void ge_plat_sleep_us(uint32_t us) { SDL_Delay(us / 1000 ? us / 1000 : 1); }
