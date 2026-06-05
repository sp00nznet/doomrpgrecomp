/*
 * profile_stub.c -- empty dev-menu binding for games whose memory layout hasn't
 * been decoded yet. Every field is NULL/0, so devcheats.c's direct stat pokes
 * are inert; the generic dev menu (save-states, graphics/audio/controls) and the
 * engine's own built-in 3-6-6-6 debug menu still work. Replace with a real
 * profile_<game>.c once that title's globals are mapped.
 */
#include "gameprofile.h"

const GameProfile g_profile = { 0 };
