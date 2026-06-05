/*
 * profile_DoomIIRPG.c -- dev-menu binding for Doom II RPG (352x416 + SE share
 * the engine, so this fits both).
 *
 * Doom II RPG keeps the player stats in a short[] array S_x__e (class x, field e)
 * rather than a combat object. Found empirically with the F8 static-discovery
 * dump (see docs/PROFILES.md): taking a hit moved exactly S_x__e[1] from 100->90,
 * so index 1 is current HP. State machine is class-k's int (8 = main menu, the
 * same convention as Doom RPG). Other stats (max-HP, armor, ammo, level warp)
 * aren't decoded yet -- use the engine's built-in 3-6-6-6 debug menu for those.
 *
 * Compiled ONLY for the Doom II RPG builds.
 */
#include "doomrpg.h"
#include "j2me/runtime.h"
#include "gameprofile.h"

const GameProfile g_profile = {
    .stat_array  = &S_x__e__aS,   /* short[] player stats */
    .stat_hp     = 1,             /* verified: 100 -> 90 on a hit */
    .stat_hp_cap = 100,           /* godmode pins HP here; give-HP target */

    .state       = &S_k__f__I,    /* state machine (8 = main menu) -- readout */

    /* Not yet decoded for this game; the built-in 3-6-6-6 menu covers them. */
    .ammo_max    = 99,
};
