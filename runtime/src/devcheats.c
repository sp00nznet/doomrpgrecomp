/*
 * devcheats.c -- the actual game-state pokes behind the ImGui dev menu.
 *
 * Kept in C (not devgui.cpp) so it can use the recomp's native types and call
 * generated game functions / j_vfind directly. The C++ menu calls these.
 *
 * Everything here reads/writes the recompiled game's own globals:
 *   S_j__j__I  player health (scalar; the HUD bars are filled from it)
 *   S_j__k__I  player armor
 *   S_j__a__Lt the current weapon/ammo object t  (a()=cur, b()=max, a(I)=set)
 *   S_k__w__I  game-state machine   S_k__b__Ljava_lang_String  current level
 */
#include "doomrpg.h"
#include "j2me/runtime.h"
#include "devcheats.h"
#include <string.h>
#include <stdlib.h>

/* cheat toggles, owned here, read/written by the menu */
int g_cheat_godmode   = 0;
int g_cheat_inf_ammo  = 0;
int g_cheat_health_target = 100;   /* what godmode pins health to */

/* ---- player object (t = S_j__a__Lt) stat accessors ------------------------
 * Verified against the HUD: a()/a(I)/b()=health, c()/c(I)/d()=armor.
 * e()/e(I) and f()/f(I) are two more capped-at-99 resources (ammo pools). */
static int  t_geti(const char *m) {
    jref t = S_j__a__Lt;
    if (!t || !t->cls) return -1;
    void *fn = j_vfind_opt(t->cls, m, "()I");
    return fn ? ((jint (*)(jref))fn)(t) : -1;
}
static void t_seti(const char *m, int v) {
    jref t = S_j__a__Lt;
    if (!t || !t->cls) return;
    void *fn = j_vfind_opt(t->cls, m, "(I)V");
    if (fn) ((void (*)(jref, jint))fn)(t, v);
}

/* ---- per-frame enforcement (called from the present hook) ----------------- */
void cheats_per_frame(void) {
    jref t = S_j__a__Lt;
    if (!t || !t->cls) return;            /* not in-game yet */
    if (g_cheat_godmode) {
        int hpmax = t_geti("b");
        if (hpmax > 0) t_seti("a", hpmax);
    }
    if (g_cheat_inf_ammo) {
        t_seti("e", 99);                  /* pin both ammo pools to their 99 cap */
        t_seti("f", 99);
    }
}

/* ---- live readouts for the menu ------------------------------------------- */
int cheats_get_health(void)  { return t_geti("a"); }
int cheats_get_armor(void)   { return t_geti("c"); }
int cheats_get_ammo(void)    { return t_geti("e"); }
int cheats_get_ammo_max(void){ return t_geti("f"); }   /* second pool (probe) */
int cheats_get_state(void)   { return S_k__w__I; }

/* ---- give / set ----------------------------------------------------------- */
void cheats_set_health(int v) { t_seti("a", v); }
void cheats_set_armor(int v)  { t_seti("c", v); }
void cheats_set_ammo(int v)   { t_seti("e", v); t_seti("f", v); }

/* current level name into a caller buffer (best-effort) */
void cheats_current_level(char *out, int cap) {
    out[0] = 0;
    jref s = S_k__b__Ljava_lang_String;
    if (!s) return;
    char *c = j_string_to_cstr(s);
    if (c) { strncpy(out, c, cap - 1); out[cap - 1] = 0; free(c); }
}

/* ---- jumps ---------------------------------------------------------------- */
void cheats_set_state(int st) { m_k__a__I__V(st); }

/* Warp to a level. The level strings must be stable literals (j_strlit interns
 * by pointer); the table below provides them. idx selects from cheats_levels(). */
static const char *const k_levels[] = {
    "/intro.bsp", "/junction.bsp",
    "/level01.bsp", "/level02.bsp", "/level03.bsp", "/level04.bsp",
    "/level05.bsp", "/level06.bsp", "/level07.bsp",
    "/junction_destroyed.bsp", "/reactor.bsp", "/endgame.bsp",
};
int         cheats_level_count(void)     { return (int)(sizeof k_levels / sizeof k_levels[0]); }
const char *cheats_level_name(int i)     { return (i >= 0 && i < cheats_level_count()) ? k_levels[i] : ""; }
void        cheats_warp(int i) {
    if (i < 0 || i >= cheats_level_count()) return;
    m_k__a__Ljava_lang_String__V(j_strlit(k_levels[i]));
}

/* Start a fresh run at the first playable level (index 1 = /junction.bsp). */
void cheats_new_game(void) { cheats_warp(1); }
