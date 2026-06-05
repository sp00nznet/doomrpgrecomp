/*
 * savestate.h -- emulator-style save states for the recompiled game.
 *
 * A savestate is a snapshot of all mutable game state: the bump arena (every
 * Java object), the arena's used-offset, every generated static global
 * (S_*, table generated into savestate_registry.c by tools/gen_savestate.py),
 * and the string intern-list head. Restoring is a memcpy back; pointers stay
 * valid because the arena lives at a fixed base (see jvm_core.c arena_init).
 */
#ifndef DOOMRPG_SAVESTATE_H
#define DOOMRPG_SAVESTATE_H

#include <stddef.h>

/* one snapshot-able global: its address and byte size */
typedef struct { void *addr; unsigned size; } SaveSlot;

#ifdef __cplusplus
extern "C" {
#endif

/* generated table of every S_* static (savestate_registry.c) */
extern const SaveSlot g_savestate_statics[];
extern const int      g_savestate_statics_count;

/* named registry for the dev-menu static-discovery dump (savestate_registry.c) */
typedef struct { const char *name; void *addr; unsigned size; int is_int; int is_ref; } StaticInfo;
extern const StaticInfo g_static_info[];
extern const int        g_static_info_count;
/* dump current values of int statics to stderr; tag labels the snapshot. Used to
 * reverse-engineer per-game state/stat globals (diff two snapshots). */
void dbg_dump_int_statics(const char *tag);

/* arena hooks (jvm_core.c) */
unsigned char *j_arena_base(void);
size_t         j_arena_used(void);
void           j_arena_set_used(size_t used);
/* intern-list head slot (strings.c) */
void         **j_intern_head_slot(void);

/* save/load a slot to/from a file. 0 = ok, negative = error.
 * savestate_load returns -2 if the file is from a session whose arena base
 * differs from this one (its pointers would be invalid). */
int savestate_save(const char *path);
int savestate_load(const char *path);
int savestate_exists(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* DOOMRPG_SAVESTATE_H */
