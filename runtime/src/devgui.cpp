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

static SDL_Renderer *g_ren;
static int g_game_w, g_game_h, g_scale;
static int g_bar_h = 24;        /* thin top menu-bar height (refined per frame) */

int devgui_bar_height(void) { return g_bar_h; }

void devgui_init(SDL_Window *win, SDL_Renderer *ren, int game_w, int game_h, int scale)
{
    g_ren = ren; g_game_w = game_w; g_game_h = game_h; g_scale = scale;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr;                 /* don't litter an imgui.ini */
    /* No keyboard nav: the game owns the keyboard (arrows/fire); the mouse
     * drives the menu. Otherwise ImGui would eat fire/arrows for menu nav. */
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer2_Init(ren);
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
        if (ImGui::BeginMenu("Warp")) {
            char cur[64]; cheats_current_level(cur, sizeof cur);
            ImGui::Text("Current: %s", cur[0] ? cur : "(none)");
            ImGui::Separator();
            for (int i = 0; i < cheats_level_count(); i++)
                if (ImGui::MenuItem(cheats_level_name(i))) cheats_warp(i);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("State")) {
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
        if (ImGui::BeginMenu("View")) {
            bool dbg = S_k__f__Z != 0;
            if (ImGui::MenuItem("Built-in debug counters", nullptr, &dbg)) S_k__f__Z = dbg ? 1 : 0;
            ImGui::MenuItem("ImGui demo window", nullptr, &g_show_demo);
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
    SDL_Rect dst = { 0, g_bar_h, g_game_w * g_scale, g_game_h * g_scale };
    SDL_RenderCopy(g_ren, game_tex, NULL, &dst);

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
