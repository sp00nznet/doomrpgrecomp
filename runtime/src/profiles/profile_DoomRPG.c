/*
 * profile_DoomRPG.c -- dev-menu binding for the original Doom RPG.
 *
 * Mappings recovered by cross-referencing the DoomRPG-RE Player struct (credit:
 * Erick194 / github.com/Erick194/DoomRPG-RE) against our obfuscated code; no code
 * was copied. health/armor live on the combat object t = S_j__a__Lt (a()/b() =
 * HP/maxHP, c()/d() = armor/maxArmor; a(I)/c(I) set). The rest are class-j
 * globals; state + current level are class-k.
 *
 * Compiled ONLY for the Doom RPG build (it names that game's generated symbols).
 */
#include "doomrpg.h"
#include "j2me/runtime.h"
#include "gameprofile.h"

/* Level strings must be stable literals (j_strlit interns by pointer). */
static const char *const drpg_levels[] = {
    "/intro.bsp", "/junction.bsp",
    "/level01.bsp", "/level02.bsp", "/level03.bsp", "/level04.bsp",
    "/level05.bsp", "/level06.bsp", "/level07.bsp",
    "/junction_destroyed.bsp", "/reactor.bsp", "/endgame.bsp",
};

const GameProfile g_profile = {
    .combat_obj = &S_j__a__Lt,
    .ammo_array = &S_j__b__aB,
    .state      = &S_k__w__I,
    .credits    = &S_j__d__I,
    .weapons    = &S_j__a__I,
    .keys       = &S_j__b__I,
    .level_str  = &S_k__b__Ljava_lang_String,
    .debug_flag = &S_k__f__Z,

    .m_hp_get = "a", .m_hp_set = "a", .m_hp_max = "b",
    .m_armor_get = "c", .m_armor_set = "c",

    .set_state = m_k__a__I__V,
    .warp      = m_k__a__Ljava_lang_String__V,

    .weapons_all = 0x1FF,   /* 9 standard weapons (skip 9..11 dog weapons) */
    .keys_all    = 0xFF,
    .ammo_max    = 99,

    .levels      = drpg_levels,
    .level_count = (int)(sizeof drpg_levels / sizeof drpg_levels[0]),
};
