/*
 * display.c -- SDL2 window, integer-scaled from the native 128x128, plus
 * keyboard input routed to the current Canvas's keyPressed/keyReleased.
 *
 * Keycodes follow the common MIDP convention (arrows are negative, digits are
 * their ASCII codes); getGameAction() in lcdui.c maps them to UP/DOWN/.../FIRE.
 */
#include "j2me/runtime.h"
#include <SDL.h>
#include <stdio.h>

/* MIDP-ish device key codes */
enum { KEY_UP = -1, KEY_DOWN = -2, KEY_LEFT = -3, KEY_RIGHT = -4, KEY_FIRE = -5,
       KEY_SOFT1 = -6, KEY_SOFT2 = -7 };

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture  *g_tex;
static int g_w, g_h, g_scale, g_quit;
static int g_keystate;
static int g_lastkey;
static jref g_canvas;

void display_set_canvas(jref c) { g_canvas = c; }

int display_init(int w, int h, int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    g_w = w; g_h = h; g_scale = scale;
    g_win = SDL_CreateWindow("Doom RPG (recomp)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        w * scale, h * scale, SDL_WINDOW_RESIZABLE);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(g_ren, w, h);   /* integer-ish scaling, keeps aspect */
    SDL_RenderSetIntegerScale(g_ren, SDL_TRUE);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    return 0;
}

void display_present(const uint32_t *argb) {
    SDL_UpdateTexture(g_tex, NULL, argb, g_w * (int)sizeof(uint32_t));
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

static int map_key(SDL_Keycode k, int *gameaction_bit) {
    *gameaction_bit = 0;
    switch (k) {
        case SDLK_UP:    *gameaction_bit = 1 << 1; return KEY_UP;
        case SDLK_DOWN:  *gameaction_bit = 1 << 6; return KEY_DOWN;
        case SDLK_LEFT:  *gameaction_bit = 1 << 2; return KEY_LEFT;
        case SDLK_RIGHT: *gameaction_bit = 1 << 5; return KEY_RIGHT;
        case SDLK_RETURN: case SDLK_SPACE: case SDLK_LCTRL:
                         *gameaction_bit = 1 << 8; return KEY_FIRE;
        case SDLK_q:     return KEY_SOFT1;
        case SDLK_w:     return KEY_SOFT2;
        default:
            if (k >= SDLK_0 && k <= SDLK_9) return '0' + (k - SDLK_0);
            return 0;
    }
}

static void dispatch_key(const char *method, int keycode) {
    if (!g_canvas || !keycode) return;
    typedef void (*kfn)(jref, jint);
    kfn fn = (kfn)j_vfind_opt(g_canvas->cls, method, "(I)V");
    if (fn) fn(g_canvas, keycode);
}

void display_pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_quit = 1;
        else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            int ga = 0, kc = map_key(e.key.keysym.sym, &ga);
            if (e.key.keysym.sym == SDLK_ESCAPE) { g_quit = 1; continue; }
            g_keystate |= ga; g_lastkey = kc;
            dispatch_key("keyPressed", kc);
        } else if (e.type == SDL_KEYUP) {
            int ga = 0, kc = map_key(e.key.keysym.sym, &ga);
            g_keystate &= ~ga;
            dispatch_key("keyReleased", kc);
        }
    }
}

int  display_should_quit(void) { return g_quit; }
int  display_key_state(void)   { return g_keystate; }
int  display_last_keycode(void){ return g_lastkey; }
void display_shutdown(void)    { if (g_win) SDL_DestroyWindow(g_win); SDL_Quit(); }
