/*
 * devgui.h -- C-callable interface to the Dear ImGui dev/cheat overlay.
 *
 * The host (display.c, plain C) owns the SDL window/renderer and the 128x150
 * game texture; devgui (C++) owns the ImGui context and the composite present:
 * a dev bar docked across the top with the game viewport rendered below it.
 *
 * Because doomrpgrecomp is a true static recompilation, every game variable is
 * a native C global and every method a callable C function -- the panels in
 * devgui.cpp poke those directly.
 */
#ifndef DOOMRPG_DEVGUI_H
#define DOOMRPG_DEVGUI_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set up ImGui on an existing window+renderer. game_w/game_h are the native
 * framebuffer size (128x150); scale is the integer zoom of the game viewport. */
void devgui_init(SDL_Window *win, SDL_Renderer *ren, int game_w, int game_h, int scale);

/* Feed every SDL event here (before the host decides routing). */
void devgui_process_event(const SDL_Event *e);

/* 1 when ImGui is using the keyboard/mouse (host should withhold it from the
 * game), e.g. while typing in a text field. */
int  devgui_capture_keyboard(void);
int  devgui_capture_mouse(void);

/* Height in pixels of the docked dev bar (top strip the game sits below). */
int  devgui_bar_height(void);

/* Build the dev UI, blit game_tex into the viewport below the bar, draw ImGui
 * over the top, and present the whole window. Replaces a raw RenderPresent.
 * Re-entrancy-guarded, so it's safe to call from both the game's flushGraphics
 * and the idle loop. */
void devgui_present(SDL_Texture *game_tex);

/* 1 if it's been long enough to re-render the overlay (keeps the menu smooth
 * even when the game itself flushes rarely). */
int  devgui_should_present(void);

/* Run any menu action queued during the last UI frame (warp, save/load, ...).
 * Must be called between game frames (from runtime_idle), NOT inside a flush. */
void devgui_run_pending(void);

void devgui_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* DOOMRPG_DEVGUI_H */
