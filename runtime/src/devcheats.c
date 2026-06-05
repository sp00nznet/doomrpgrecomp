/*
 * devcheats.c -- the actual game-state pokes behind the ImGui dev menu.
 *
 * Kept in C (not devgui.cpp) so it can use the recomp's native types and call
 * j_vfind directly. The C++ menu calls these. All game-specific knowledge
 * (which obfuscated global holds health, etc.) lives in the per-game profile
 * (gameprofile.h / profile_<game>.c); this file is game-agnostic and NULL-guards
 * every profile field, so it compiles and runs for every game -- titles without
 * a decoded profile simply get inert stat pokes (the built-in 3-6-6-6 debug
 * menu still gives God Mode etc.).
 */
#include "j2me/runtime.h"
#include "gameprofile.h"
#include "devcheats.h"
#include <string.h>
#include <stdlib.h>

extern void display_dispatch(int keycode, int gabit, int down);   /* display.c */

/* cheat toggles, owned here, read/written by the menu */
int g_cheat_godmode   = 0;
int g_cheat_inf_ammo  = 0;
int g_cheat_health_target = 100;   /* what godmode pins health to */

/* ---- built-in debug menu (3-6-6-6) ---------------------------------------- *
 * Every id/Fountainhead engine title opens its own debug menu (God Mode etc.)
 * when you slowly press 3,6,6,6 at the title/pause screen. We feed those keys
 * into the game's input one at a time (a few frames apart, so the engine's
 * debounce sees distinct presses) -- a cheat path that works on EVERY game with
 * no per-game reverse engineering. */
static char g_code[16];
static int  g_code_len = 0, g_code_pos = 0, g_code_wait = 0;
void cheats_send_code(const char *digits) {
    g_code_len = 0; g_code_pos = 0; g_code_wait = 0;
    for (const char *p = digits; *p && g_code_len < (int)sizeof g_code; p++)
        g_code[g_code_len++] = *p;
}
static void code_pump(void) {
    if (g_code_pos >= g_code_len) return;
    if (g_code_wait-- > 0) return;
    int key = g_code[g_code_pos++];
    display_dispatch(key, 0, 1);   /* keyPressed  */
    display_dispatch(key, 0, 0);   /* keyReleased */
    g_code_wait = 8;               /* ~8 frames between digits */
}

/* ---- stat array (HP) -- games that store stats in a short[] --------------- */
static jshort *stat_hp_ptr(void) {
    if (!g_profile.stat_array || g_profile.stat_hp < 0) return 0;
    jref a = *g_profile.stat_array;
    if (!a) return 0;
    ArrayObj *o = (ArrayObj *)a;
    if (g_profile.stat_hp >= o->length) return 0;
    return (jshort *)J_ARRDATA(a) + g_profile.stat_hp;
}
static int stat_hp_cap(void) { return g_profile.stat_hp_cap ? g_profile.stat_hp_cap : 100; }

/* ---- combat object (HP/armor) -------------------------------------------- */
static jref combat(void) {
    return g_profile.combat_obj ? *g_profile.combat_obj : (jref)0;
}
static int t_geti(const char *m) {
    jref t = combat();
    if (!m || !t || !t->cls) return -1;
    void *fn = j_vfind_opt(t->cls, m, "()I");
    return fn ? ((jint (*)(jref))fn)(t) : -1;
}
static void t_seti(const char *m, int v) {
    jref t = combat();
    if (!m || !t || !t->cls) return;
    void *fn = j_vfind_opt(t->cls, m, "(I)V");
    if (fn) ((void (*)(jref, jint))fn)(t, v);
}

/* ---- ammo byte[] --------------------------------------------------------- */
static jref ammo_arr(void) {
    return g_profile.ammo_array ? *g_profile.ammo_array : (jref)0;
}
static int  ammo_n(void)            { jref a = ammo_arr(); return a ? j_arraylength(a) : 0; }
static int  ammo_get(int i)         { jref a = ammo_arr(); return (a && i >= 0 && i < ammo_n()) ? *j_barr(a, i) : -1; }
static void ammo_set(int i, int v)  { jref a = ammo_arr(); if (a && i >= 0 && i < ammo_n()) *j_barr(a, i) = (jbyte)v; }
static int  ammo_cap(void)          { return g_profile.ammo_max ? g_profile.ammo_max : 99; }

/* ---- built-in debug-counters flag (per-game; via profile) ----------------- */
int  cheats_has_debug_flag(void) { return g_profile.debug_flag != 0; }
int  cheats_get_debug_flag(void) { return g_profile.debug_flag ? *g_profile.debug_flag : 0; }
void cheats_set_debug_flag(int v) { if (g_profile.debug_flag) *g_profile.debug_flag = v; }

/* ---- per-frame enforcement (called from the present hook) ----------------- */
void cheats_per_frame(void) {
    code_pump();                          /* drive any queued 3-6-6-6 sequence */
    if (g_cheat_godmode) {
        jshort *hp = stat_hp_ptr();
        if (hp) {
            if (*hp < stat_hp_cap()) *hp = (jshort)stat_hp_cap();   /* array-stat games */
        } else {
            int hpmax = t_geti(g_profile.m_hp_max);
            if (hpmax > 0) t_seti(g_profile.m_hp_set, hpmax);
        }
    }
    if (g_cheat_inf_ammo)
        for (int i = 0, n = ammo_n(); i < n; i++) ammo_set(i, ammo_cap());
}

/* ---- live readouts for the menu ------------------------------------------- */
int cheats_get_health(void)  { jshort *hp = stat_hp_ptr(); return hp ? *hp : t_geti(g_profile.m_hp_get); }
int cheats_get_armor(void)   { return t_geti(g_profile.m_armor_get); }
int cheats_get_state(void)   { return g_profile.state   ? *g_profile.state   : -1; }
int cheats_get_credits(void) { return g_profile.credits ? *g_profile.credits : -1; }
int cheats_ammo_count(void)  { return ammo_n(); }
int cheats_ammo_get(int i)   { return ammo_get(i); }

/* ---- give / set ----------------------------------------------------------- */
void cheats_set_health(int v)  { jshort *hp = stat_hp_ptr(); if (hp) *hp = (jshort)v; else t_seti(g_profile.m_hp_set, v); }
void cheats_set_armor(int v)   { t_seti(g_profile.m_armor_set, v); }
void cheats_set_credits(int v) { if (g_profile.credits) *g_profile.credits = v; }
void cheats_set_all_ammo(int v) { for (int i = 0, n = ammo_n(); i < n; i++) ammo_set(i, v); }
void cheats_give_all_weapons(void) { if (g_profile.weapons) *g_profile.weapons |= (g_profile.weapons_all ? g_profile.weapons_all : ~0); }
void cheats_give_all_keys(void)    { if (g_profile.keys)    *g_profile.keys    |= (g_profile.keys_all    ? g_profile.keys_all    : ~0); }

/* current level name into a caller buffer (best-effort) */
void cheats_current_level(char *out, int cap) {
    out[0] = 0;
    jref s = g_profile.level_str ? *g_profile.level_str : (jref)0;
    if (!s) return;
    char *c = j_string_to_cstr(s);
    if (c) { strncpy(out, c, cap - 1); out[cap - 1] = 0; free(c); }
}

/* ---- jumps ---------------------------------------------------------------- */
void cheats_set_state(int st) { if (g_profile.set_state) g_profile.set_state(st); }

int         cheats_level_count(void) { return g_profile.level_count; }
const char *cheats_level_name(int i) {
    return (g_profile.levels && i >= 0 && i < g_profile.level_count) ? g_profile.levels[i] : "";
}
void cheats_warp(int i) {
    if (!g_profile.warp || !g_profile.levels || i < 0 || i >= g_profile.level_count) return;
    g_profile.warp(j_strlit(g_profile.levels[i]));
}

/* Start a fresh run at the first playable level (index 1). */
void cheats_new_game(void) { cheats_warp(1); }
