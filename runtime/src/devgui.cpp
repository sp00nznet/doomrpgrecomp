/*
 * devgui.cpp -- Dear ImGui dev/cheat overlay for doomrpgrecomp.
 *
 * Layout: a dev bar docked across the top of the window, with the native
 * 128x150 game framebuffer rendered into a viewport directly below it. The
 * panels poke the recompiled game's native globals directly (see devgui.h).
 *
 * Built on the vendored SDL2 + SDLRenderer2 ImGui backends; the ImGui core is
 * linked from vcpkg (imgui.lib).
 */
#include "devgui.h"
#include "devcheats.h"
#include "savestate.h"
#include "devsaves.h"
#include "devaudio.h"
#include "devinput.h"
#include "devkeypad.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the recompiled game's native state (C linkage; defined in generated/) ---
 * Ordinary globals emitted by the translator; jint is int32_t == int here. */
extern "C" {
    extern int S_k__f__Z;   /* the game's own debug overlay (ms/li/sp/nd counters) */
}

static bool g_show_demo = false;

/* Controller menu navigation: when on, ImGui reads the pad for nav and the game
 * is frozen out (devinput swallows the pad). Toggled by Start (see devinput.c). */
static bool g_menu_nav = false;
extern "C" void devgui_toggle_menu_nav(void) { g_menu_nav = !g_menu_nav; }
extern "C" int  devgui_menu_nav(void)        { return g_menu_nav; }

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;
static int g_game_w, g_game_h, g_scale;
static int g_bar_h = 24;        /* thin top menu-bar height (refined per frame) */

/* graphics options */
static int  g_filter = 0;       /* 0 = nearest (crisp), 1 = linear (smooth) */
static bool g_scanlines = false;

/* file / savestate */
static int  g_slot = 1;
static char g_msg[96] = "";     /* transient status line for save/load etc. */

static void set_msg(const char *m) { snprintf(g_msg, sizeof g_msg, "%s", m); }
static void slot_path(char *out, int cap, int slot) { snprintf(out, cap, "savestate%d.bin", slot); }

/* Heavy menu actions (warp, state jump, save/load) must NOT run inside the
 * ImGui frame -- they call game code that itself calls flushGraphics, which
 * would re-enter our renderer. We queue them and run them between game frames
 * (from runtime_idle, via devgui_run_pending). */
enum PendAct { P_NONE, P_NEWGAME, P_WARP, P_STATE,
               P_SS_SAVE, P_SS_LOAD, P_RMS_BACKUP, P_RMS_RESTORE };
static PendAct g_pend = P_NONE;
static int     g_pend_arg = 0;
static void queue(PendAct a, int arg) { g_pend = a; g_pend_arg = arg; }

extern "C" void devgui_run_pending(void) {
    if (g_pend == P_NONE) return;
    PendAct a = g_pend; int arg = g_pend_arg; g_pend = P_NONE;
    char p[64];
    switch (a) {
        case P_NEWGAME: cheats_new_game(); set_msg("new game"); break;
        case P_WARP:    cheats_warp(arg);  break;
        case P_STATE:   cheats_set_state(arg); break;
        case P_SS_SAVE: slot_path(p, sizeof p, arg);
                        set_msg(savestate_save(p) == 0 ? "state saved" : "save failed"); break;
        case P_SS_LOAD: { slot_path(p, sizeof p, arg); int r = savestate_load(p);
                        set_msg(r == 0 ? "state loaded" : r == -2 ? "from another session" : "load failed"); } break;
        case P_RMS_BACKUP: { int r = devsaves_backup(arg);
                        snprintf(g_msg, sizeof g_msg, r > 0 ? "backed up %d files to slot %d"
                                 : "no game save to back up", r, arg); } break;
        case P_RMS_RESTORE: { int r = devsaves_restore(arg);
                        set_msg(r > 0 ? "restored; use Continue to load" : "slot empty"); } break;
        default: break;
    }
}

/* ImGui re-render pacing, decoupled from the game's flush rate */
static Uint32 g_last_present = 0;
static int    g_in_present = 0;
extern "C" int devgui_should_present(void) { return (int)(SDL_GetTicks() - g_last_present) >= 15; }

static void apply_scale(int s) {
    if (s < 1) s = 1; if (s > 8) s = 8;
    g_scale = s;
    int w = g_game_w * s; if (w < 480) w = 480;
    int h = g_bar_h + g_game_h * s;
    if (g_win) SDL_SetWindowSize(g_win, w, h);
}

/* persist graphics + audio choices to settings.cfg (controls.cfg holds bindings) */
static int    g_settings_dirty = 0;
static Uint32 g_settings_saved = 0;
static void   mark_settings_dirty(void) { g_settings_dirty = 1; }

static void settings_save(void) {
    FILE *f = fopen("settings.cfg", "w");
    if (!f) return;
    fprintf(f, "scale %d\nfilter %d\nscanlines %d\nmaster %d\nmusic %d\nmute %d\n",
            g_scale, g_filter, g_scanlines ? 1 : 0,
            devaudio_get_master(), devaudio_get_music(), devaudio_get_mute());
    fclose(f);
}
static void settings_load(void) {
    FILE *f = fopen("settings.cfg", "r");
    if (!f) return;
    char k[32]; int v;
    while (fscanf(f, "%31s %d", k, &v) == 2) {
        if      (!strcmp(k, "scale"))     apply_scale(v);
        else if (!strcmp(k, "filter"))    g_filter = v;
        else if (!strcmp(k, "scanlines")) g_scanlines = v != 0;
        else if (!strcmp(k, "master"))    devaudio_set_master(v);
        else if (!strcmp(k, "music"))     devaudio_set_music(v);
        else if (!strcmp(k, "mute"))      devaudio_set_mute(v);
    }
    fclose(f);
}

/* ---- hotkey actions (display.c maps F-keys to these) ---------------------- */
static bool g_shot_pending = false;
extern "C" void devgui_request_screenshot(void) { g_shot_pending = true; }
extern "C" void devgui_quicksave(void) { queue(P_SS_SAVE, 0); }
extern "C" void devgui_quickload(void) { queue(P_SS_LOAD, 0); }
extern "C" void devgui_toggle_fullscreen(void) {
    if (!g_win) return;
    Uint32 fl = SDL_GetWindowFlags(g_win);
    SDL_SetWindowFullscreen(g_win, (fl & SDL_WINDOW_FULLSCREEN_DESKTOP) ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

int devgui_bar_height(void) { return g_bar_h; }

void devgui_init(SDL_Window *win, SDL_Renderer *ren, int game_w, int game_h, int scale)
{
    g_win = win; g_ren = ren; g_game_w = game_w; g_game_h = game_h; g_scale = scale;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;                 /* don't litter an imgui.ini */
    /* No keyboard nav: the game owns the keyboard (arrows/fire); the mouse
     * drives the menu. Otherwise ImGui would eat fire/arrows for menu nav. */
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer2_Init(ren);
    devaudio_init();
    settings_load();              /* restore graphics/audio prefs from last run */
}

void devgui_process_event(const SDL_Event *e)
{
    ImGui_ImplSDL2_ProcessEvent(e);
}

int devgui_capture_keyboard(void) { return ImGui::GetIO().WantCaptureKeyboard ? 1 : 0; }
int devgui_capture_mouse(void)    { return ImGui::GetIO().WantCaptureMouse ? 1 : 0; }

/* ---- the dev menu bar (thin, Windows-style, dropdowns) -------------------- */
static void build_bar(void)
{
    if (ImGui::BeginMainMenuBar()) {
        g_bar_h = (int)ImGui::GetWindowSize().y;   /* exact bar height for layout */

        /* ---- File: new game + save states -------------------------------- */
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Game")) queue(P_NEWGAME, 0);
            ImGui::Separator();
            ImGui::SetNextItemWidth(120);
            ImGui::SliderInt("Slot", &g_slot, 1, 9);
            char path[64]; slot_path(path, sizeof path, g_slot);
            bool has = savestate_exists(path) != 0;
            if (ImGui::MenuItem("Save State")) queue(P_SS_SAVE, g_slot);
            if (ImGui::MenuItem("Load State", nullptr, false, has)) queue(P_SS_LOAD, g_slot);
            ImGui::TextDisabled("%s", has ? "slot has data" : "slot empty");

            ImGui::Separator();
            /* the game's own RecordStore saves, backed up to numbered slots */
            if (ImGui::BeginMenu("Game saves (.rms)")) {
                ImGui::TextDisabled("backs up Config/Player/World; load via the");
                ImGui::TextDisabled("game's own \"Continue\" after restoring.");
                ImGui::Separator();
                for (int s = 1; s <= 5; s++) {
                    ImGui::PushID(s);
                    bool shas = devsaves_slot_exists(s) != 0;
                    ImGui::Text("Slot %d %s", s, shas ? "(saved)" : "(empty)");
                    ImGui::SameLine(150);
                    if (ImGui::SmallButton("Backup"))  queue(P_RMS_BACKUP, s);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Restore")) queue(P_RMS_RESTORE, s);
                    ImGui::PopID();
                }
                ImGui::EndMenu();
            }
            if (g_msg[0]) { ImGui::Separator(); ImGui::TextDisabled("%s", g_msg); }
            ImGui::EndMenu();
        }

        /* ---- Debug: cheats / warp / state / view ------------------------- */
        if (ImGui::BeginMenu("Debug")) {
            if (ImGui::BeginMenu("Cheats")) {
                bool god = g_cheat_godmode != 0;
                if (ImGui::MenuItem("Godmode", nullptr, &god)) g_cheat_godmode = god;
                bool inf = g_cheat_inf_ammo != 0;
                if (ImGui::MenuItem("Infinite ammo", nullptr, &inf)) g_cheat_inf_ammo = inf;
                ImGui::SetNextItemWidth(90);
                ImGui::InputInt("Godmode HP target", &g_cheat_health_target);
                ImGui::Separator();

                ImGui::Text("HP %d   Armor %d   Credits %d",
                            cheats_get_health(), cheats_get_armor(), cheats_get_credits());
                char ammo[64] = "Ammo:"; int n = cheats_ammo_count();
                for (int i = 0; i < n; i++) {
                    char tmp[12]; snprintf(tmp, sizeof tmp, " %d", cheats_ammo_get(i));
                    strncat(ammo, tmp, sizeof ammo - strlen(ammo) - 1);
                }
                ImGui::TextDisabled("%s", n ? ammo : "Ammo: (not in game)");
                ImGui::Separator();

                static int give_hp = 100, give_ar = 100, give_cr = 1000;
                ImGui::SetNextItemWidth(80); ImGui::InputInt("##hp", &give_hp);
                ImGui::SameLine(); if (ImGui::Button("Give HP"))      cheats_set_health(give_hp);
                ImGui::SetNextItemWidth(80); ImGui::InputInt("##ar", &give_ar);
                ImGui::SameLine(); if (ImGui::Button("Give Armor"))   cheats_set_armor(give_ar);
                ImGui::SetNextItemWidth(80); ImGui::InputInt("##cr", &give_cr);
                ImGui::SameLine(); if (ImGui::Button("Give Credits")) cheats_set_credits(give_cr);
                ImGui::Separator();
                if (ImGui::Button("Give all weapons")) cheats_give_all_weapons();
                ImGui::SameLine();
                if (ImGui::Button("Give all keys"))    cheats_give_all_keys();
                if (ImGui::Button("Fill all ammo (99)")) cheats_set_all_ammo(99);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Warp level")) {
                char cur[64]; cheats_current_level(cur, sizeof cur);
                ImGui::Text("Current: %s", cur[0] ? cur : "(none)");
                ImGui::Separator();
                for (int i = 0; i < cheats_level_count(); i++)
                    if (ImGui::MenuItem(cheats_level_name(i))) queue(P_WARP, i);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Jump state")) {
                ImGui::Text("Current state: %d", cheats_get_state());
                ImGui::Separator();
                struct { const char *name; int st; } quick[] = {
                    {"Main menu (8)", 8}, {"Briefing (7)", 7}, {"In-game (1)", 1},
                    {"Automap (3)", 3}, {"Inventory (6)", 6},
                };
                for (auto &q : quick)
                    if (ImGui::MenuItem(q.name)) queue(P_STATE, q.st);
                ImGui::Separator();
                if (ImGui::MenuItem("State -")) queue(P_STATE, cheats_get_state() - 1);
                if (ImGui::MenuItem("State +")) queue(P_STATE, cheats_get_state() + 1);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            bool dbg = S_k__f__Z != 0;
            if (ImGui::MenuItem("Built-in debug counters", nullptr, &dbg)) S_k__f__Z = dbg ? 1 : 0;
            ImGui::MenuItem("ImGui demo window", nullptr, &g_show_demo);
            ImGui::EndMenu();
        }

        /* ---- Graphics: scale + filter ------------------------------------ */
        if (ImGui::BeginMenu("Graphics")) {
            int sc = g_scale;
            ImGui::SetNextItemWidth(140);
            if (ImGui::SliderInt("Scale", &sc, 1, 8)) { apply_scale(sc); mark_settings_dirty(); }
            ImGui::Separator();
            ImGui::TextDisabled("Filter");
            if (ImGui::RadioButton("Nearest (crisp)", g_filter == 0)) { g_filter = 0; mark_settings_dirty(); }
            if (ImGui::RadioButton("Linear (smooth)", g_filter == 1)) { g_filter = 1; mark_settings_dirty(); }
            ImGui::Separator();
            if (ImGui::Checkbox("Scanlines", &g_scanlines)) mark_settings_dirty();
            ImGui::EndMenu();
        }

        /* ---- Audio: volumes ---------------------------------------------- */
        if (ImGui::BeginMenu("Audio")) {
            bool mute = devaudio_get_mute() != 0;
            if (ImGui::Checkbox("Mute", &mute)) { devaudio_set_mute(mute); mark_settings_dirty(); }
            int master = devaudio_get_master();
            ImGui::SetNextItemWidth(160);
            if (ImGui::SliderInt("Master", &master, 0, 100)) { devaudio_set_master(master); mark_settings_dirty(); }
            int music = devaudio_get_music();
            ImGui::SetNextItemWidth(160);
            if (ImGui::SliderInt("Music", &music, 0, 100)) { devaudio_set_music(music); mark_settings_dirty(); }
            ImGui::EndMenu();
        }

        /* ---- Controls: keyboard + Xbox controller, rebindable ------------ */
        if (ImGui::BeginMenu("Controls")) {
            ImGui::Text("Controller: %s", devinput_controller_name());
            bool kp = devkeypad_is_open();
            if (ImGui::MenuItem("On-screen keypad", "Back btn", &kp)) devkeypad_toggle();
            ImGui::TextDisabled("for door/puzzle codes (any digit)");
            bool nav = devgui_menu_nav() != 0;
            if (ImGui::MenuItem("Controller menu nav", "Start btn", &nav)) devgui_toggle_menu_nav();
            ImGui::TextDisabled("drive this menu with the pad (game pauses)");
            ImGui::Separator();
            int rb = devinput_rebinding_action();
            if (rb >= 0)
                ImGui::TextColored(ImVec4(1, 0.85f, 0.2f, 1), "Press a %s for \"%s\" (Esc cancels)",
                    devinput_rebinding_is_button() ? "button" : "key", devinput_action_name(rb));
            ImGui::Separator();
            ImGui::TextDisabled("%-14s %-14s %s", "Action", "Keyboard", "Controller");
            for (int i = 0; i < devinput_action_count(); i++) {
                ImGui::PushID(i);
                ImGui::Text("%-14s", devinput_action_name(i));
                ImGui::SameLine(150);
                ImGui::PushID("k");
                if (ImGui::SmallButton(devinput_key_label(i))) devinput_rebind_key(i);
                ImGui::PopID();
                ImGui::SameLine(280);
                ImGui::PushID("b");
                if (ImGui::SmallButton(devinput_button_label(i))) devinput_rebind_button(i);
                ImGui::PopID();
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset to defaults")) devinput_reset_defaults();
            ImGui::EndMenu();
        }

        /* right-aligned status readout */
        char status[112];
        snprintf(status, sizeof status, "%sHP %d  AR %d   state %d   %.0f fps",
                 g_menu_nav ? "[PAD NAV] " : "",
                 cheats_get_health(), cheats_get_armor(),
                 cheats_get_state(), ImGui::GetIO().Framerate);
        float tw = ImGui::CalcTextSize(status).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - tw - 12.0f);
        ImGui::TextDisabled("%s", status);

        ImGui::EndMainMenuBar();
    }

    if (g_show_demo) ImGui::ShowDemoWindow(&g_show_demo);

    /* On-screen numeric keypad for door/puzzle codes (controller or mouse). */
    if (devkeypad_is_open()) {
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, g_bar_h + 8.0f),
                                ImGuiCond_Always, ImVec2(0.5f, 0.0f));
        ImGui::Begin("Keypad", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
        for (int i = 0; i < devkeypad_count(); i++) {
            if (i % 3) ImGui::SameLine();
            bool hl = devkeypad_cursor() == i;
            if (hl) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.1f, 1));
            ImGui::PushID(i);
            if (ImGui::Button(devkeypad_label(i), ImVec2(46, 46))) devkeypad_press_index(i);
            ImGui::PopID();
            if (hl) ImGui::PopStyleColor();
        }
        ImGui::TextDisabled("D-pad move / A enter / B or Back close");
        ImGui::End();
    }
}

void devgui_present(SDL_Texture *game_tex)
{
    if (g_in_present) return;           /* never nest ImGui frames (re-entrancy) */
    g_in_present = 1;
    g_last_present = SDL_GetTicks();
    cheats_per_frame();                 /* enforce pinned cheats (godmode, etc.) */
    devkeypad_auto(cheats_get_state()); /* pop the keypad at a door-code prompt */

    ImGuiIO &io = ImGui::GetIO();        /* toggle gamepad nav for this frame */
    if (g_menu_nav) io.ConfigFlags |=  ImGuiConfigFlags_NavEnableGamepad;
    else            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;

    if (g_settings_dirty && SDL_GetTicks() - g_settings_saved > 800) {  /* debounce slider drags */
        settings_save(); g_settings_dirty = 0; g_settings_saved = SDL_GetTicks();
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    build_bar();
    ImGui::Render();

    SDL_SetRenderDrawColor(g_ren, 18, 18, 22, 255);
    SDL_RenderClear(g_ren);

    /* game viewport, integer-scaled, docked just under the dev bar */
    SDL_SetTextureScaleMode(game_tex, g_filter ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    SDL_Rect dst = { 0, g_bar_h, g_game_w * g_scale, g_game_h * g_scale };
    SDL_RenderCopy(g_ren, game_tex, NULL, &dst);

    if (g_scanlines) {   /* dim every other output row over the game viewport */
        SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_ren, 0, 0, 0, 64);
        for (int y = dst.y; y < dst.y + dst.h; y += 2)
            SDL_RenderDrawLine(g_ren, dst.x, y, dst.x + dst.w - 1, y);
    }

    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), g_ren);

    /* Debug/screenshot: DOOMRPG_WINDUMP=path.ppm dumps the whole composited
     * window (dev bar + game) once, so the overlay can be verified headlessly. */
    const char *wd = getenv("DOOMRPG_WINDUMP");
    const char *wn = getenv("DOOMRPG_WINDUMP_N");   /* overwrite stride (default 60); 0 = once */
    static long pf = 0; long stride = wn ? atol(wn) : 60;
    static bool dumped = false;
    bool do_dump = wd && (stride == 0 ? !dumped : (pf++ % stride) == 0);
    if (do_dump) {
        int ow = 0, oh = 0; SDL_GetRendererOutputSize(g_ren, &ow, &oh);
        unsigned char *px = (unsigned char *)malloc((size_t)ow * oh * 4);
        if (px && SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, ow * 4) == 0) {
            FILE *f = fopen(wd, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", ow, oh);
                for (int i = 0; i < ow * oh; i++) {
                    unsigned int p = ((unsigned int *)px)[i];   /* ARGB */
                    fputc((p >> 16) & 0xFF, f); fputc((p >> 8) & 0xFF, f); fputc(p & 0xFF, f);
                }
                fclose(f);
            }
            dumped = true;
        }
        free(px);
    }

    if (g_shot_pending) {            /* F12 screenshot -> next free shot_NNN.bmp */
        int ow = 0, oh = 0; SDL_GetRendererOutputSize(g_ren, &ow, &oh);
        SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, ow, oh, 32, SDL_PIXELFORMAT_ARGB8888);
        if (s && SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
            char name[64];
            for (int i = 0; ; i++) {
                snprintf(name, sizeof name, "shot_%03d.bmp", i);
                FILE *t = fopen(name, "rb"); if (!t) break; fclose(t);
            }
            if (SDL_SaveBMP(s, name) == 0) snprintf(g_msg, sizeof g_msg, "saved %s", name);
        }
        if (s) SDL_FreeSurface(s);
        g_shot_pending = false;
    }

    SDL_RenderPresent(g_ren);
    g_in_present = 0;
}

void devgui_shutdown(void)
{
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}
