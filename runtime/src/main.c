/*
 * main.c -- host entry point. Wires up SDL + assets + audio, runs every class's
 * <clinit>, then drives the MIDlet lifecycle: new DoomRPG(); startApp().
 *
 * The game's worker Thread runs inline (see jlang.c), so startApp() typically
 * doesn't return until the game ends; a fallback pump loop keeps the window
 * alive if it ever does return early.
 */
#define SDL_MAIN_HANDLED          /* we provide our own main(); no SDL2main.
                                   * MUST precede any <SDL.h> (devgui.h pulls it in). */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include "devgui.h"
#include <SDL.h>
#include <stdio.h>

extern int g_quit_requested;             /* midlet.c */
void runtime_init_statics(void);          /* class_meta.c */

/* Thread.sleep() routes here so the inline game loop stays responsive. */
void runtime_idle(int ms) {
    display_pump();
    devgui_run_pending();     /* run queued menu actions here, between game frames */
    if (display_should_quit()) g_quit_requested = 1;
    if (ms > 0) SDL_Delay((Uint32)ms);
}

/* Locate the extracted-JAR assets. Explicit first arg wins; otherwise probe a
 * few sensible spots relative to the working dir and the exe (so double-clicking
 * the exe in build/ still finds ../game/extracted). Returns a malloc'd path or
 * NULL. */
static char *find_assets(const char *arg) {
    static char buf[1200];
    if (arg && *arg) { snprintf(buf, sizeof buf, "%s", arg); return assets_probe(buf) ? buf : NULL; }

    const char *cwd_rel[] = { "game/extracted", "extracted", "." };
    for (size_t i = 0; i < sizeof cwd_rel / sizeof cwd_rel[0]; i++)
        if (assets_probe(cwd_rel[i])) { snprintf(buf, sizeof buf, "%s", cwd_rel[i]); return buf; }

    char *base = SDL_GetBasePath();       /* directory containing the exe */
    if (base) {
        const char *rel[] = { "game/extracted", "../game/extracted",
                              "../../game/extracted", "extracted", "" };
        for (size_t i = 0; i < sizeof rel / sizeof rel[0]; i++) {
            snprintf(buf, sizeof buf, "%s%s", base, rel[i]);
            if (assets_probe(buf)) { SDL_free(base); return buf; }
        }
        SDL_free(base);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);     /* see progress even if we abort */
    int scale = (argc > 2) ? atoi(argv[2]) : 4;
    if (scale < 1) scale = 1;

    SDL_SetMainReady();
    const char *assets_dir = find_assets(argc > 1 ? argv[1] : NULL);
    if (!assets_dir) {
        fprintf(stderr,
            "Doom RPG (recomp): could not find the game assets.\n"
            "Extract your legally-obtained DoomRPG.jar into a folder, then either\n"
            "run from the project root (where game\\extracted lives) or pass the\n"
            "folder explicitly:\n"
            "    DoomRPG.exe <extracted-jar-folder> [scale]\n");
        return 1;
    }
    printf("doomrpgrecomp -- assets: %s, scale: %dx\n", assets_dir, scale);
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
