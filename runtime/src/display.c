/*
 * display.c -- SDL2 window, integer-scaled from the native 128x128, plus
 * keyboard input routed to the current Canvas's keyPressed/keyReleased.
 *
 * Keycodes follow the common MIDP convention (arrows are negative, digits are
 * their ASCII codes); getGameAction() in lcdui.c maps them to UP/DOWN/.../FIRE.
 */
#include "j2me/runtime.h"
#include "devgui.h"
#include "devinput.h"
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
    /* Window holds the thin dev menu bar on top + the scaled game viewport below. */
    int win_w = w * scale;
    if (win_w < 480) win_w = 480;            /* room for the menu-bar titles */
    int win_h = devgui_bar_height() + h * scale;
    g_win = SDL_CreateWindow("Doom RPG (recomp)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        win_w, win_h, SDL_WINDOW_RESIZABLE);
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING, w, h);
    devgui_init(g_win, g_ren, w, h, scale);
    devinput_init();
    return 0;
}

void display_present(const uint32_t *argb) {
    SDL_UpdateTexture(g_tex, NULL, argb, g_w * (int)sizeof(uint32_t));
    devgui_present(g_tex);                    /* dev bar + game viewport + present */
}

static void dispatch_key(const char *method, int keycode) {
    if (!g_canvas || !keycode) return;
    typedef void (*kfn)(jref, jint);
    kfn fn = (kfn)j_vfind_opt(g_canvas->cls, method, "(I)V");
    if (fn) fn(g_canvas, keycode);
}

/* Deliver a resolved game key (device code + getKeyStates bit). devinput.c
 * calls this once a real keyboard/controller event matches a binding. */
void display_dispatch(int keycode, int gabit, int down) {
    if (down) { g_keystate |= gabit; g_lastkey = keycode; dispatch_key("keyPressed", keycode); }
    else      { g_keystate &= ~gabit; dispatch_key("keyReleased", keycode); }
}

/* Scripted harness synthesizes real SDL key events so they flow through the
 * same devinput path (and honour rebindings) as a player's keyboard. */
static void push_key(SDL_Keycode sym, int down) {
    SDL_Event e; SDL_zero(e);
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.keysym.sym = sym;
    e.key.repeat = 0;
    SDL_PushEvent(&e);
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
            push_key(g_script[i].sym, 1); g_script[i].down_done = 1;
        }
        if (g_script[i].down_done && !g_script[i].up_done && now >= g_script[i].t + 120) {
            push_key(g_script[i].sym, 0); g_script[i].up_done = 1;
        }
    }
}

void display_pump(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        devgui_process_event(&e);             /* ImGui sees every event first */
        if (e.type == SDL_QUIT) { g_quit = 1; continue; }
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) { g_quit = 1; continue; }
        /* real keyboard + controller input -> rebindable bindings (devinput) */
        devinput_process_event(&e, devgui_capture_keyboard());
    }
    pump_script();
}

int  display_should_quit(void) { return g_quit; }
int  display_key_state(void)   { return g_keystate; }
int  display_last_keycode(void){ return g_lastkey; }
void display_shutdown(void)    { devinput_shutdown(); devgui_shutdown(); if (g_win) SDL_DestroyWindow(g_win); SDL_Quit(); }
