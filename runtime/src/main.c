/*
 * main.c -- host entry point. Wires up SDL + assets + audio, runs every class's
 * <clinit>, then drives the MIDlet lifecycle: new DoomRPG(); startApp().
 *
 * The game's worker Thread runs inline (see jlang.c), so startApp() typically
 * doesn't return until the game ends; a fallback pump loop keeps the window
 * alive if it ever does return early.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#define SDL_MAIN_HANDLED          /* we provide our own main(); no SDL2main */
#include <SDL.h>
#include <stdio.h>

extern int g_quit_requested;             /* midlet.c */
void runtime_init_statics(void);          /* class_meta.c */

/* Thread.sleep() routes here so the inline game loop stays responsive. */
void runtime_idle(int ms) {
    display_pump();
    if (display_should_quit()) g_quit_requested = 1;
    if (ms > 0) SDL_Delay((Uint32)ms);
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);     /* see progress even if we abort */
    const char *assets_dir = (argc > 1) ? argv[1] : "game/extracted";
    int scale = (argc > 2) ? atoi(argv[2]) : 4;
    if (scale < 1) scale = 1;

    printf("doomrpgrecomp -- assets: %s, scale: %dx\n", assets_dir, scale);
    SDL_SetMainReady();
    assets_open(assets_dir);
    midi_init();

    if (display_init(SCREEN_W, SCREEN_H, scale) != 0)
        return 1;

    runtime_init_statics();
    j_init_all();                         /* run all <clinit> */

    /* MIDlet lifecycle */
    jref app = j_new(&CLASS_DoomRPG);
    m_DoomRPG___init_____V(app);
    m_DoomRPG__startApp____V(app);

    /* If startApp returned without an inline loop, keep the window responsive. */
    while (!g_quit_requested && !display_should_quit()) {
        display_pump();
        SDL_Delay(16);
    }

    midi_shutdown();
    display_shutdown();
    return 0;
}
