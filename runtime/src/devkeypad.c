/*
 * devkeypad.c -- see devkeypad.h. Sends the chosen key to the game's Canvas via
 * display_dispatch (a keyPressed/keyReleased pair, like a real number key).
 */
#include "devkeypad.h"

extern void display_dispatch(int keycode, int gabit, int down);   /* display.c */

/* phone layout: 1 2 3 / 4 5 6 / 7 8 9 / * 0 #  (3 columns x 4 rows) */
static const char *const k_labels[12] =
    { "1","2","3", "4","5","6", "7","8","9", "*","0","#" };
static const int k_codes[12] =
    { '1','2','3', '4','5','6', '7','8','9', 42,'0',35 };   /* '*'=42, '#'=35 */

static int g_open = 0;
static int g_cur  = 0;

int  devkeypad_is_open(void) { return g_open; }
void devkeypad_toggle(void)  { g_open = !g_open; }
void devkeypad_close(void)   { g_open = 0; }

void devkeypad_move(int dx, int dy) {
    int c = g_cur % 3 + dx, r = g_cur / 3 + dy;
    if (c < 0) c = 2; else if (c > 2) c = 0;
    if (r < 0) r = 3; else if (r > 3) r = 0;
    g_cur = r * 3 + c;
}

static void send(int i) {
    display_dispatch(k_codes[i], 0, 1);   /* keyPressed  */
    display_dispatch(k_codes[i], 0, 0);   /* keyReleased */
}
void devkeypad_press(void)            { send(g_cur); }
void devkeypad_press_index(int i)     { if (i >= 0 && i < 12) { g_cur = i; send(i); } }

int         devkeypad_cursor(void)    { return g_cur; }
int         devkeypad_count(void)     { return 12; }
const char *devkeypad_label(int i)    { return (i >= 0 && i < 12) ? k_labels[i] : ""; }
