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
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <stdio.h>
#include <stdlib.h>

/* ---- the recompiled game's native state (C linkage; defined in generated/) ---
 * These are ordinary globals emitted by the translator, so we can read/write
 * them straight from here. jint is int32_t == int on MSVC/x64. */
extern "C" {
    extern int S_k__w__I;   /* the game's state-machine value (8=main menu, 7=briefing, ...) */
    extern int S_k__f__Z;   /* the game's own debug overlay (ms/li/sp/nd counters) */
}

/* cheat toggles (enforcement wired in a later pass) */
static bool g_godmode      = false;
static bool g_infinite_ammo = false;
static bool g_show_demo    = false;

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
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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
            ImGui::MenuItem("Godmode", "F2", &g_godmode);
            ImGui::MenuItem("Infinite ammo", nullptr, &g_infinite_ammo);
            ImGui::Separator();
            ImGui::TextDisabled("enforcement lands in the next pass");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("State")) {
            ImGui::Text("Current state: %d", S_k__w__I);
            ImGui::Separator();
            if (ImGui::MenuItem("State -")) S_k__w__I--;
            if (ImGui::MenuItem("State +")) S_k__w__I++;
            ImGui::TextDisabled("8=main menu  7=briefing");
            ImGui::TextDisabled("(raw poke; level/state jumps come next)");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            bool dbg = S_k__f__Z != 0;
            if (ImGui::MenuItem("Built-in debug counters", nullptr, &dbg)) S_k__f__Z = dbg ? 1 : 0;
            ImGui::MenuItem("ImGui demo window", nullptr, &g_show_demo);
            ImGui::EndMenu();
        }

        /* right-aligned status readout */
        char status[64];
        snprintf(status, sizeof status, "state %d   %.0f fps", S_k__w__I, ImGui::GetIO().Framerate);
        float tw = ImGui::CalcTextSize(status).x;
        ImGui::SameLine(ImGui::GetWindowWidth() - tw - 12.0f);
        ImGui::TextDisabled("%s", status);

        ImGui::EndMainMenuBar();
    }

    if (g_show_demo) ImGui::ShowDemoWindow(&g_show_demo);
}

void devgui_present(SDL_Texture *game_tex)
{
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
    const char *wn = getenv("DOOMRPG_WINDUMP_N");   /* present-frame index to grab (default 0) */
    static long pf = 0; long target = wn ? atol(wn) : 0;
    static bool dumped = false;
    if (wd && !dumped && pf++ >= target) {
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
