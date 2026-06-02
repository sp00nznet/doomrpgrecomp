/*
 * devinput.c -- rebindable keyboard + Xbox-controller input (see devinput.h).
 */
#include "devinput.h"
#include "devkeypad.h"
#include <stdio.h>
#include <string.h>

/* dispatch a game device key code (display.c owns the Canvas + key-state mask) */
extern void display_dispatch(int keycode, int gabit, int down);
/* controller menu-navigation toggle (devgui.cpp) */
extern int  devgui_menu_nav(void);
extern void devgui_toggle_menu_nav(void);

/* MIDP-ish device key codes the game expects (mirrors display.c) */
enum { KEY_UP = -1, KEY_DOWN = -2, KEY_LEFT = -3, KEY_RIGHT = -4,
       KEY_FIRE = -5, KEY_SOFT1 = -6, KEY_SOFT2 = -7 };

typedef struct {
    const char  *name;
    int          keycode;   /* game device code dispatched for this action */
    int          gabit;     /* getKeyStates() bit (0 if none) */
    SDL_Keycode  key;       /* bound keyboard key, or SDLK_UNKNOWN */
    int          button;    /* bound SDL_GameControllerButton, or -1 */
} Binding;

/* indices 0..3 are the d-pad directions (used by the analog-stick mapping) */
static Binding g_b[] = {
    { "Up",          KEY_UP,    1 << 1, SDLK_UP,     SDL_CONTROLLER_BUTTON_DPAD_UP },
    { "Down",        KEY_DOWN,  1 << 6, SDLK_DOWN,   SDL_CONTROLLER_BUTTON_DPAD_DOWN },
    { "Left",        KEY_LEFT,  1 << 2, SDLK_LEFT,   SDL_CONTROLLER_BUTTON_DPAD_LEFT },
    { "Right",       KEY_RIGHT, 1 << 5, SDLK_RIGHT,  SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
    { "Fire",        KEY_FIRE,  1 << 8, SDLK_RETURN, SDL_CONTROLLER_BUTTON_A },
    { "Soft1 (Menu)",KEY_SOFT1, 0,      SDLK_q,      SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
    { "Soft2 (Map)", KEY_SOFT2, 0,      SDLK_w,      SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
    { "Key 1",       '1', 0, SDLK_1, SDL_CONTROLLER_BUTTON_X },
    { "Key 2",       '2', 0, SDLK_2, SDL_CONTROLLER_BUTTON_Y },
    { "Key 3",       '3', 0, SDLK_3, SDL_CONTROLLER_BUTTON_B },
    { "Key 4",       '4', 0, SDLK_4, -1 },
    { "Key 5",       '5', 0, SDLK_5, -1 },
    { "Key 6",       '6', 0, SDLK_6, -1 },
    { "Key 7",       '7', 0, SDLK_7, -1 },
    { "Key 8",       '8', 0, SDLK_8, -1 },
    { "Key 9",       '9', 0, SDLK_9, -1 },
    { "Key 0",       '0', 0, SDLK_0, -1 },
};
#define NB ((int)(sizeof g_b / sizeof g_b[0]))

static Binding g_defaults[NB];         /* snapshot of the initial table */
static SDL_GameController *g_pad;
static int g_rebind = -1, g_rebind_btn = 0;
static int g_stick[4];                  /* analog-stick direction latch (0..3) */

static const char *cfg_path(void) { return "controls.cfg"; }

/* ---- lifecycle ------------------------------------------------------------ */
void devinput_init(void) {
    memcpy(g_defaults, g_b, sizeof g_b);           /* remember factory bindings */
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { g_pad = SDL_GameControllerOpen(i); if (g_pad) break; }
    devinput_load();
}
void devinput_shutdown(void) {
    if (g_pad) { SDL_GameControllerClose(g_pad); g_pad = 0; }
}

/* the game's Display.vibrate(ms) routes here -> controller rumble */
void devinput_rumble(int ms) {
    if (!g_pad) return;
    if (ms <= 0) ms = 200;
    if (ms > 1000) ms = 1000;
    SDL_GameControllerRumble(g_pad, 0xA000, 0xA000, (Uint32)ms);
}

/* ---- analog stick -> d-pad ------------------------------------------------ */
static void stick_set(int dir, int active) {
    if (active == g_stick[dir]) return;
    g_stick[dir] = active;
    display_dispatch(g_b[dir].keycode, g_b[dir].gabit, active);
}

/* ---- event handling ------------------------------------------------------- */
int devinput_process_event(const SDL_Event *e, int kbd_blocked) {
    /* Start toggles controller menu-navigation (ImGui drives the menu, game off). */
    if (e->type == SDL_CONTROLLERBUTTONDOWN &&
        e->cbutton.button == SDL_CONTROLLER_BUTTON_START) { devgui_toggle_menu_nav(); return 1; }

    /* On-screen keypad: Back/Select toggles it; while open it owns the pad so
     * the player can dial in door codes the controller otherwise can't type. */
    if (e->type == SDL_CONTROLLERBUTTONDOWN &&
        e->cbutton.button == SDL_CONTROLLER_BUTTON_BACK) { devkeypad_toggle(); return 1; }
    if (devkeypad_is_open()) {
        if (e->type == SDL_CONTROLLERBUTTONDOWN) {
            switch (e->cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP:    devkeypad_move(0, -1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  devkeypad_move(0,  1); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  devkeypad_move(-1, 0); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: devkeypad_move( 1, 0); break;
                case SDL_CONTROLLER_BUTTON_A:          devkeypad_press();     break;
                case SDL_CONTROLLER_BUTTON_B:          devkeypad_close();     break;
                default: break;
            }
            return 1;                       /* swallow the pad while the keypad is up */
        }
        if (e->type == SDL_CONTROLLERBUTTONUP || e->type == SDL_CONTROLLERAXISMOTION) return 1;
    }

    /* While menu-nav is active, ImGui reads the pad for navigation; keep it away
     * from the game (device add/remove still pass through below). */
    if (devgui_menu_nav() && (e->type == SDL_CONTROLLERBUTTONDOWN ||
        e->type == SDL_CONTROLLERBUTTONUP || e->type == SDL_CONTROLLERAXISMOTION))
        return 1;

    switch (e->type) {
    case SDL_CONTROLLERDEVICEADDED:
        if (!g_pad) g_pad = SDL_GameControllerOpen(e->cdevice.which);
        return 0;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (g_pad) { SDL_GameControllerClose(g_pad); g_pad = 0; }
        return 0;

    case SDL_KEYDOWN:
        if (e->key.repeat) return 0;
        if (g_rebind >= 0 && !g_rebind_btn) {            /* capture for rebind */
            if (e->key.keysym.sym != SDLK_ESCAPE) g_b[g_rebind].key = e->key.keysym.sym;
            g_rebind = -1; devinput_save(); return 1;
        }
        if (kbd_blocked) return 0;
        for (int i = 0; i < NB; i++)
            if (g_b[i].key && e->key.keysym.sym == g_b[i].key) {
                display_dispatch(g_b[i].keycode, g_b[i].gabit, 1); return 1;
            }
        return 0;
    case SDL_KEYUP:
        if (kbd_blocked) return 0;
        for (int i = 0; i < NB; i++)
            if (g_b[i].key && e->key.keysym.sym == g_b[i].key) {
                display_dispatch(g_b[i].keycode, g_b[i].gabit, 0); return 1;
            }
        return 0;

    case SDL_CONTROLLERBUTTONDOWN:
        if (g_rebind >= 0 && g_rebind_btn) {
            g_b[g_rebind].button = e->cbutton.button;
            g_rebind = -1; devinput_save(); return 1;
        }
        for (int i = 0; i < NB; i++)
            if (g_b[i].button >= 0 && e->cbutton.button == g_b[i].button) {
                display_dispatch(g_b[i].keycode, g_b[i].gabit, 1); return 1;
            }
        return 0;
    case SDL_CONTROLLERBUTTONUP:
        for (int i = 0; i < NB; i++)
            if (g_b[i].button >= 0 && e->cbutton.button == g_b[i].button) {
                display_dispatch(g_b[i].keycode, g_b[i].gabit, 0); return 1;
            }
        return 0;

    case SDL_CONTROLLERAXISMOTION: {
        const int DZ = 16000;
        if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
            stick_set(0, e->caxis.value < -DZ);          /* up */
            stick_set(1, e->caxis.value >  DZ);          /* down */
        } else if (e->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
            stick_set(2, e->caxis.value < -DZ);          /* left */
            stick_set(3, e->caxis.value >  DZ);          /* right */
        }
        return 0;
    }
    }
    return 0;
}

/* ---- accessors ------------------------------------------------------------ */
int         devinput_action_count(void)      { return NB; }
const char *devinput_action_name(int a)      { return (a >= 0 && a < NB) ? g_b[a].name : ""; }
int         devinput_has_controller(void)    { return g_pad != 0; }
const char *devinput_controller_name(void)   { return g_pad ? SDL_GameControllerName(g_pad) : "(none)"; }
int         devinput_rebinding_action(void)  { return g_rebind; }
int         devinput_rebinding_is_button(void){ return g_rebind_btn; }
void        devinput_rebind_key(int a)       { g_rebind = a; g_rebind_btn = 0; }
void        devinput_rebind_button(int a)    { g_rebind = a; g_rebind_btn = 1; }

const char *devinput_key_label(int a) {
    if (a < 0 || a >= NB || !g_b[a].key) return "(unset)";
    const char *n = SDL_GetKeyName(g_b[a].key);
    return (n && n[0]) ? n : "(unknown)";
}
const char *devinput_button_label(int a) {
    if (a < 0 || a >= NB || g_b[a].button < 0) return "(unset)";
    const char *n = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)g_b[a].button);
    return (n && n[0]) ? n : "(unknown)";
}

void devinput_reset_defaults(void) {
    memcpy(g_b, g_defaults, sizeof g_b);
    devinput_save();
}

/* ---- persistence: one "action key button" line per binding ---------------- */
void devinput_save(void) {
    FILE *f = fopen(cfg_path(), "w");
    if (!f) return;
    fprintf(f, "# doomrpgrecomp controls: <action> <sdl_keycode> <controller_button>\n");
    for (int i = 0; i < NB; i++)
        fprintf(f, "%d %d %d\n", i, (int)g_b[i].key, g_b[i].button);
    fclose(f);
}
void devinput_load(void) {
    FILE *f = fopen(cfg_path(), "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        int idx, key, btn;
        if (sscanf(line, "%d %d %d", &idx, &key, &btn) == 3 && idx >= 0 && idx < NB) {
            g_b[idx].key = (SDL_Keycode)key;
            g_b[idx].button = btn;
        }
    }
    fclose(f);
}
