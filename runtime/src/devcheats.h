/*
 * devcheats.h -- C-callable cheat API (impl in devcheats.c) used by devgui.cpp.
 * Each call reads/writes the recompiled game's native globals directly.
 */
#ifndef DOOMRPG_DEVCHEATS_H
#define DOOMRPG_DEVCHEATS_H

#ifdef __cplusplus
extern "C" {
#endif

/* toggles (owned in devcheats.c) */
extern int g_cheat_godmode;
extern int g_cheat_inf_ammo;
extern int g_cheat_health_target;

void cheats_per_frame(void);          /* enforce pinned cheats; call once per frame */

int  cheats_get_health(void);
int  cheats_get_armor(void);
int  cheats_get_state(void);
int  cheats_get_credits(void);
int  cheats_ammo_count(void);     /* number of ammo types (6) */
int  cheats_ammo_get(int i);
void cheats_current_level(char *out, int cap);

void cheats_set_health(int v);
void cheats_set_armor(int v);
void cheats_set_credits(int v);
void cheats_set_all_ammo(int v);
void cheats_give_all_weapons(void);
void cheats_give_all_keys(void);
void cheats_set_state(int st);

int         cheats_level_count(void);
const char *cheats_level_name(int i);
void        cheats_warp(int i);
void        cheats_new_game(void);

#ifdef __cplusplus
}
#endif

#endif /* DOOMRPG_DEVCHEATS_H */
