/*
 * gameprofile.h -- per-game binding for the dev/cheat menu.
 *
 * The recomp turns every game global into a native C symbol, but the symbols are
 * obfuscated and differ per game (Doom RPG's player-stat object is S_j__a__Lt;
 * another title's is something else entirely). devcheats.c is shared across all
 * games, so it can't name those symbols directly. Instead each game ships a
 * profile_<game>.c that fills in this struct with that game's symbol addresses,
 * method names and constants; devcheats.c reads it (NULL-guarding every field).
 *
 * Games whose layout hasn't been decoded yet link profile_stub.c (an all-zero
 * profile): the generic dev menu (save-states, graphics/audio, and the engine's
 * own built-in 3-6-6-6 debug menu) still works; only the direct stat pokes are
 * inert until a profile is written.
 */
#ifndef GAMEPROFILE_H
#define GAMEPROFILE_H

#include "j2me/runtime.h"   /* jref, jint, jbyte */

typedef struct GameProfile {
    /* Addresses of live game globals. Any may be NULL if unknown for a game. */
    jref *combat_obj;     /* object carrying HP/armor (queried by method below) */
    jref *ammo_array;     /* byte[] of per-type ammo counts */
    jint *state;          /* state-machine global (menu/in-game/...) */
    jint *credits;        /* money/credits scalar */
    jint *weapons;        /* weapons-owned bitmask */
    jint *keys;           /* keys-owned bitmask */
    jref *level_str;      /* current level name (java String) */
    jint *debug_flag;     /* engine's built-in debug-counters overlay flag */

    /* Virtual-method names on combat_obj's class (resolved via j_vfind_opt).
     * HP get/set share a name but differ by descriptor ("()I" vs "(I)V"). */
    const char *m_hp_get, *m_hp_set, *m_hp_max, *m_armor_get, *m_armor_set;

    /* Alternative stat model: some games (e.g. Doom II RPG) keep stats in a
     * short[] array rather than a combat object. If stat_array is non-NULL it
     * takes precedence over combat_obj for HP. */
    jref *stat_array;     /* short[] player stats */
    int   stat_hp;        /* HP element index (-1 = none) */
    int   stat_hp_cap;    /* godmode pin / give-HP target value */

    /* State transitions (static generated functions; NULL if unknown). */
    void (*set_state)(jint);
    void (*warp)(jref);          /* warp(levelNameString) */

    /* Constants. 0 means "use a safe default" in devcheats.c. */
    int weapons_all, keys_all, ammo_max;

    /* Level table -- stable string literals for warp (j_strlit interns them). */
    const char *const *levels;
    int level_count;
} GameProfile;

/* Exactly one profile_*.c defines this (the matching game's, or the stub). */
extern const GameProfile g_profile;

#endif /* GAMEPROFILE_H */
