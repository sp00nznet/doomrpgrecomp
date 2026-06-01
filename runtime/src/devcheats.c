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

/* ---- player stats -----------------------------------------------------------
 * Mappings recovered by cross-referencing the DoomRPG-RE Player struct (credit:
 * Erick194 / github.com/Erick194/DoomRPG-RE) against our obfuscated code; no
 * code was copied. health/armor live on the combat object t = S_j__a__Lt
 * (a()/b()=HP/maxHP, c()/d()=armor/maxArmor). The rest are class-j globals:
 *   S_j__a__I  weapons bitmask (bits 0..11; 9..11 are the dog familiar)
 *   S_j__b__I  keys bitmask        S_j__b__aB  ammo[6] (byte, cap 99)
 *   S_j__d__I  credits             S_j__f__I   current weapon index           */
#define WEAPONS_ALL 0x1FF   /* the 9 standard weapons (skip 9..11 dog weapons) */
#define KEYS_ALL    0xFF
#define AMMO_MAX    99

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
static int ammo_n(void)            { return S_j__b__aB ? j_arraylength(S_j__b__aB) : 0; }
static int ammo_get(int i)         { return (i >= 0 && i < ammo_n()) ? *j_barr(S_j__b__aB, i) : -1; }
static void ammo_set(int i, int v) { if (i >= 0 && i < ammo_n()) *j_barr(S_j__b__aB, i) = (jbyte)v; }

/* ---- per-frame enforcement (called from the present hook) ----------------- */
void cheats_per_frame(void) {
    jref t = S_j__a__Lt;
    if (!t || !t->cls) return;            /* not in-game yet */
    if (g_cheat_godmode) {
        int hpmax = t_geti("b");
        if (hpmax > 0) t_seti("a", hpmax);
    }
    if (g_cheat_inf_ammo)
        for (int i = 0, n = ammo_n(); i < n; i++) ammo_set(i, AMMO_MAX);
}

/* ---- live readouts for the menu ------------------------------------------- */
int cheats_get_health(void)  { return t_geti("a"); }
int cheats_get_armor(void)   { return t_geti("c"); }
int cheats_get_state(void)   { return S_k__w__I; }
int cheats_get_credits(void) { return S_j__d__I; }
int cheats_ammo_count(void)  { return ammo_n(); }
int cheats_ammo_get(int i)   { return ammo_get(i); }

/* ---- give / set ----------------------------------------------------------- */
void cheats_set_health(int v)  { t_seti("a", v); }
void cheats_set_armor(int v)   { t_seti("c", v); }
void cheats_set_credits(int v) { S_j__d__I = v; }
void cheats_set_all_ammo(int v) { for (int i = 0, n = ammo_n(); i < n; i++) ammo_set(i, v); }
void cheats_give_all_weapons(void) { S_j__a__I |= WEAPONS_ALL; }
void cheats_give_all_keys(void)    { S_j__b__I |= KEYS_ALL; }

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
