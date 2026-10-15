/* Desktop backend (Windows / macOS / Linux). Keyboard + first game controller. */

#include "ge/ge_input.h"
#include "ge/ge_platform.h"

#include <SDL.h>

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static SDL_GameController *g_pad;
static int g_shot_requested;

int ge_plat_init(const GePlatformConfig *cfg) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) return -1;
    g_win = SDL_CreateWindow(cfg->title ? cfg->title : "ge", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             cfg->width * cfg->scale, cfg->height * cfg->scale, SDL_WINDOW_RESIZABLE);
    if (!g_win) return -2;
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_SOFTWARE);
    if (!g_ren) return -3;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* crisp pixels */
    SDL_RenderSetLogicalSize(g_ren, cfg->width, cfg->height);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, cfg->width, cfg->height);
    if (!g_tex) return -4;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { g_pad = SDL_GameControllerOpen(i); break; }
    return 0;
}

void ge_plat_shutdown(void) {
    if (g_pad) SDL_GameControllerClose(g_pad);
    if (g_tex) SDL_DestroyTexture(g_tex);
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
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint32_t b = 0;
    if (k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]) b |= GE_BTN_LEFT;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) b |= GE_BTN_RIGHT;
    if (k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]) b |= GE_BTN_UP;
    if (k[SDL_SCANCODE_DOWN] || k[SDL_SCANCODE_S]) b |= GE_BTN_DOWN;
    if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE]) b |= GE_BTN_JUMP;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_J]) b |= GE_BTN_ATTACK;
    if (k[SDL_SCANCODE_C] || k[SDL_SCANCODE_K]) b |= GE_BTN_DASH;
    if (k[SDL_SCANCODE_Q]) b |= GE_BTN_LANE_BACK;
    if (k[SDL_SCANCODE_E]) b |= GE_BTN_LANE_FRONT;
    if (k[SDL_SCANCODE_RETURN]) b |= GE_BTN_START;
    if (g_pad) {
        int lx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX);
        if (lx < -12000 || SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) b |= GE_BTN_LEFT;
        if (lx > 12000 || SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= GE_BTN_RIGHT;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) b |= GE_BTN_UP;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) b |= GE_BTN_DOWN;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A)) b |= GE_BTN_JUMP;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_X)) b |= GE_BTN_ATTACK;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B)) b |= GE_BTN_DASH;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) b |= GE_BTN_LANE_BACK;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= GE_BTN_LANE_FRONT;
        if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_START)) b |= GE_BTN_START;
    }
    *buttons = b;
    return 1;
}

void ge_plat_present(const GeImage *fb) {
    if (g_shot_requested) {
        g_shot_requested = 0;
        ge_image_save_png(fb, "screenshot.png");
    }
    SDL_UpdateTexture(g_tex, NULL, fb->px, fb->w * 4);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

uint64_t ge_plat_time_us(void) {
    return (uint64_t)((double)SDL_GetPerformanceCounter() * 1e6 / (double)SDL_GetPerformanceFrequency());
}
void ge_plat_sleep_us(uint32_t us) { SDL_Delay(us / 1000 ? us / 1000 : 1); }
