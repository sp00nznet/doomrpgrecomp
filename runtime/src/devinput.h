/*
 * devinput.h -- rebindable keyboard + Xbox-controller input.
 *
 * Owns the action<->binding table and the SDL game controller. display.c feeds
 * every SDL event here; matched bindings dispatch the game's device key codes.
 * The Controls menu (devgui.cpp) reads/edits bindings through these accessors;
 * bindings persist to controls.cfg next to the exe.
 */
#ifndef DOOMRPG_DEVINPUT_H
#define DOOMRPG_DEVINPUT_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

void devinput_init(void);
void devinput_shutdown(void);
void devinput_rumble(int ms);   /* game vibrate() -> controller rumble */

/* Feed every SDL event. Returns 1 if the event was consumed as game input
 * (so the host should not treat it further). kbd_blocked = 1 when ImGui owns
 * the keyboard (e.g. typing), so keyboard bindings are ignored that frame. */
int  devinput_process_event(const SDL_Event *e, int kbd_blocked);

/* ---- accessors for the Controls menu ---- */
int         devinput_action_count(void);
const char *devinput_action_name(int action);
const char *devinput_key_label(int action);     /* e.g. "Up Arrow", "(unset)" */
const char *devinput_button_label(int action);  /* e.g. "A", "DpadUp", "(unset)" */
int         devinput_has_controller(void);
const char *devinput_controller_name(void);

/* Rebinding: arm capture of the next key / controller button for an action. */
void devinput_rebind_key(int action);
void devinput_rebind_button(int action);
int  devinput_rebinding_action(void);           /* action being rebound, or -1 */
int  devinput_rebinding_is_button(void);

void devinput_reset_defaults(void);
void devinput_save(void);
void devinput_load(void);

#ifdef __cplusplus
}
#endif

#endif /* DOOMRPG_DEVINPUT_H */
