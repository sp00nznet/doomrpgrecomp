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
#include "devaudio.h"
#include "devinput.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <stdio.h>
#include <stdlib.h>

/* ---- the recompiled game's native state (C linkage; defined in generated/) ---
 * Ordinary globals emitted by the translator; jint is int32_t == int here. */
extern "C" {
    extern int S_k__f__Z;   /* the game's own debug overlay (ms/li/sp/nd counters) */
}

static bool g_show_demo = false;

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

static void apply_scale(int s) {
    if (s < 1) s = 1; if (s > 8) s = 8;
    g_scale = s;
    int w = g_game_w * s; if (w < 480) w = 480;
    int h = g_bar_h + g_game_h * s;
    if (g_win) SDL_SetWindowSize(g_win, w, h);
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
            if (ImGui::MenuItem("New Game")) { cheats_new_game(); set_msg("new game"); }
            ImGui::Separator();
            ImGui::SetNextItemWidth(120);
            ImGui::SliderInt("Slot", &g_slot, 1, 9);
            char path[64]; slot_path(path, sizeof path, g_slot);
            bool has = savestate_exists(path) != 0;
            if (ImGui::MenuItem("Save State")) {
                int r = savestate_save(path);
                set_msg(r == 0 ? "state saved" : "save failed");
            }
            if (ImGui::MenuItem("Load State", nullptr, false, has)) {
                int r = savestate_load(path);
                set_msg(r == 0 ? "state loaded" : r == -2 ? "from another session" : "load failed");
            }
            ImGui::TextDisabled("%s", has ? "slot has data" : "slot empty");
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
                ImGui::Text("HP %d   Armor %d   Ammo pools %d / %d",
                            cheats_get_health(), cheats_get_armor(),
                            cheats_get_ammo(), cheats_get_ammo_max());
                static int give_hp = 100, give_ar = 100, give_am = 99;
                ImGui::SetNextItemWidth(80); ImGui::InputInt("##hp", &give_hp);
                ImGui::SameLine(); if (ImGui::Button("Give HP"))    cheats_set_health(give_hp);
                ImGui::SetNextItemWidth(80); ImGui::InputInt("##ar", &give_ar);
                ImGui::SameLine(); if (ImGui::Button("Give Armor")) cheats_set_armor(give_ar);
                ImGui::SetNextItemWidth(80); ImGui::InputInt("##am", &give_am);
                ImGui::SameLine(); if (ImGui::Button("Give Ammo"))  cheats_set_ammo(give_am);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Warp level")) {
                char cur[64]; cheats_current_level(cur, sizeof cur);
                ImGui::Text("Current: %s", cur[0] ? cur : "(none)");
                ImGui::Separator();
                for (int i = 0; i < cheats_level_count(); i++)
                    if (ImGui::MenuItem(cheats_level_name(i))) cheats_warp(i);
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
                    if (ImGui::MenuItem(q.name)) cheats_set_state(q.st);
                ImGui::Separator();
                if (ImGui::MenuItem("State -")) cheats_set_state(cheats_get_state() - 1);
                if (ImGui::MenuItem("State +")) cheats_set_state(cheats_get_state() + 1);
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
            if (ImGui::SliderInt("Scale", &sc, 1, 8)) apply_scale(sc);
            ImGui::Separator();
            ImGui::TextDisabled("Filter");
            if (ImGui::RadioButton("Nearest (crisp)", g_filter == 0)) g_filter = 0;
            if (ImGui::RadioButton("Linear (smooth)", g_filter == 1)) g_filter = 1;
            ImGui::Separator();
            ImGui::Checkbox("Scanlines", &g_scanlines);
            ImGui::EndMenu();
        }

        /* ---- Audio: volumes ---------------------------------------------- */
        if (ImGui::BeginMenu("Audio")) {
            bool mute = devaudio_get_mute() != 0;
            if (ImGui::Checkbox("Mute", &mute)) devaudio_set_mute(mute);
            int master = devaudio_get_master();
            ImGui::SetNextItemWidth(160);
            if (ImGui::SliderInt("Master", &master, 0, 100)) devaudio_set_master(master);
            int music = devaudio_get_music();
            ImGui::SetNextItemWidth(160);
            if (ImGui::SliderInt("Music", &music, 0, 100)) devaudio_set_music(music);
            ImGui::EndMenu();
        }

        /* ---- Controls: keyboard + Xbox controller, rebindable ------------ */
        if (ImGui::BeginMenu("Controls")) {
            ImGui::Text("Controller: %s", devinput_controller_name());
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
        char status[96];
        snprintf(status, sizeof status, "HP %d  AR %d   state %d   %.0f fps",
                 cheats_get_health(), cheats_get_armor(),
                 cheats_get_state(), ImGui::GetIO().Framerate);
        float tw = ImGui::CalcTextSize(status).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - tw - 12.0f);
        ImGui::TextDisabled("%s", status);

        ImGui::EndMainMenuBar();
    }

    if (g_show_demo) ImGui::ShowDemoWindow(&g_show_demo);
}

void devgui_present(SDL_Texture *game_tex)
{
    cheats_per_frame();                 /* enforce pinned cheats (godmode, etc.) */

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
    SDL_RenderPresent(g_ren);
}

void devgui_shutdown(void)
{
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}
