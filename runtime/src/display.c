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

/* Shared press/release logic so both real SDL events and the scripted-input
 * harness (DOOMRPG_KEYS) go through the exact same code path. */
static void key_down(SDL_Keycode sym) {
    int ga = 0, kc = map_key(sym, &ga);
    g_keystate |= ga; g_lastkey = kc;
    dispatch_key("keyPressed", kc);
}
static void key_up(SDL_Keycode sym) {
    int ga = 0, kc = map_key(sym, &ga);
    g_keystate &= ~ga;
    dispatch_key("keyReleased", kc);
}

/* ---- scripted input: DOOMRPG_KEYS="fire@6000,down@7000,fire@8000,..." -------
 * Each token is <name>@<ms> where ms is wall time from startup. The key is
 * pressed at that time and released ~120ms later. Useful for driving the menus
 * without a focused window (CI / headless verification). */
#define MAX_SCRIPT 64
static struct { SDL_Keycode sym; Uint32 t; int down_done, up_done; } g_script[MAX_SCRIPT];
static int  g_script_n = -1;     /* -1 = not yet parsed */

static SDL_Keycode name_to_sym(const char *s, int len) {
    if (!strncmp(s, "up", len) && len == 2)    return SDLK_UP;
    if (!strncmp(s, "down", len) && len == 4)  return SDLK_DOWN;
    if (!strncmp(s, "left", len) && len == 4)  return SDLK_LEFT;
    if (!strncmp(s, "right", len) && len == 5) return SDLK_RIGHT;
    if (!strncmp(s, "fire", len) && len == 4)  return SDLK_RETURN;
    if (!strncmp(s, "soft1", len) && len == 5) return SDLK_q;
    if (!strncmp(s, "soft2", len) && len == 5) return SDLK_w;
    if (len == 1 && s[0] >= '0' && s[0] <= '9') return SDLK_0 + (s[0] - '0');
    return 0;
}
static void parse_script(void) {
    g_script_n = 0;
    const char *env = getenv("DOOMRPG_KEYS");
    if (!env) return;
    const char *p = env;
    while (*p && g_script_n < MAX_SCRIPT) {
        const char *at = p;
        while (*at && *at != '@' && *at != ',') at++;
        if (*at != '@') break;
        SDL_Keycode sym = name_to_sym(p, (int)(at - p));
        Uint32 t = (Uint32)atoi(at + 1);
        if (sym) { g_script[g_script_n].sym = sym; g_script[g_script_n].t = t;
                   g_script[g_script_n].down_done = g_script[g_script_n].up_done = 0;
                   g_script_n++; }
        const char *comma = at;
        while (*comma && *comma != ',') comma++;
        if (*comma == ',') p = comma + 1; else break;
    }
}
static void pump_script(void) {
    if (g_script_n < 0) parse_script();
    if (g_script_n == 0) return;
    Uint32 now = SDL_GetTicks();
    for (int i = 0; i < g_script_n; i++) {
        if (!g_script[i].down_done && now >= g_script[i].t) {
            key_down(g_script[i].sym); g_script[i].down_done = 1;
        }
        if (g_script[i].down_done && !g_script[i].up_done && now >= g_script[i].t + 120) {
            key_up(g_script[i].sym); g_script[i].up_done = 1;
        }
    }
}

void display_pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_quit = 1;
        else if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            if (e.key.keysym.sym == SDLK_ESCAPE) { g_quit = 1; continue; }
            key_down(e.key.keysym.sym);
        } else if (e.type == SDL_KEYUP) {
            key_up(e.key.keysym.sym);
        }
    }
    pump_script();
}

int  display_should_quit(void) { return g_quit; }
int  display_key_state(void)   { return g_keystate; }
int  display_last_keycode(void){ return g_lastkey; }
void display_shutdown(void)    { if (g_win) SDL_DestroyWindow(g_win); SDL_Quit(); }
