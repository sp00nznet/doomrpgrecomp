/*
 * devkeypad.h -- on-screen numeric keypad for entering door/puzzle codes.
 *
 * The phone original had a 12-key numeric pad; a controller doesn't, so codes
 * like "1234" aren't enterable by binding alone. This overlay maps the d-pad +
 * A (or a mouse click) onto a 1-2-3 / 4-5-6 / 7-8-9 / *-0-# grid and feeds the
 * matching key code to the game. devinput.c drives it from the pad; devgui.cpp
 * draws it.
 */
#ifndef DOOMRPG_DEVKEYPAD_H
#define DOOMRPG_DEVKEYPAD_H

#ifdef __cplusplus
extern "C" {
#endif

int  devkeypad_is_open(void);
void devkeypad_toggle(void);
void devkeypad_close(void);
void devkeypad_open(void);
void devkeypad_auto(int game_state);   /* open at the code prompt, close on leave */
void devkeypad_move(int dx, int dy);   /* move the highlight (wraps) */
void devkeypad_press(void);            /* enter the highlighted key */
void devkeypad_press_index(int i);     /* enter key i directly (mouse click) */

int         devkeypad_cursor(void);    /* highlighted index 0..11 */
int         devkeypad_count(void);     /* 12 */
const char *devkeypad_label(int i);    /* "1".."9","*","0","#" */

#ifdef __cplusplus
}
#endif

#endif /* DOOMRPG_DEVKEYPAD_H */
