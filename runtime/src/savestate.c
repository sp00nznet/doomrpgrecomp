/*
 * savestate.c -- read/write a full snapshot of game state (see savestate.h).
 *
 * File layout:
 *   u32 magic 'DRSS', u32 version
 *   u64 arena_base   (to detect cross-session pointer invalidation)
 *   u64 arena_used
 *   u64 statics_count
 *   bytes[arena_used]                 the live arena
 *   void* intern_head                 string intern list head
 *   per static: bytes[size]           in registry order
 */
#include "savestate.h"
#include "j2me/runtime.h"    /* jref / ArrayObj / jclass for the discovery dump */
#include <stdio.h>
#include <stdint.h>

#define SS_MAGIC   0x53535244u   /* 'DRSS' little-endian */
#define SS_VERSION 1u

/* Discovery aid (dev menu / F8): append every int static's current value to
 * statics_dump.txt (next to the exe) and stderr. Diff a "menu" snapshot against
 * an "in-game" one to find a game's state/stat globals. */
void dbg_dump_int_statics(const char *tag) {
    FILE *f = fopen("statics_dump.txt", "a");
    #define DEMIT(...) do { fprintf(stderr, __VA_ARGS__); if (f) fprintf(f, __VA_ARGS__); } while (0)
    DEMIT("=== int statics [%s] ===\n", tag ? tag : "");
    for (int i = 0; i < g_static_info_count; i++) {
        const StaticInfo *s = &g_static_info[i];
        if (s->is_int && s->size == 4)
            DEMIT("%s = %d\n", s->name, *(int *)s->addr);
    }
    /* Stats like health usually live in a heap object/array (not a scalar
     * static), so also walk every reference static: dump array elements and the
     * int-slots of plain objects. Diff two states to find e.g. HP 90 -> 80. */
    DEMIT("--- ref statics (arrays + object int-fields) [%s] ---\n", tag ? tag : "");
    for (int i = 0; i < g_static_info_count; i++) {
        const StaticInfo *s = &g_static_info[i];
        if (!s->is_ref) continue;
        jref o = *(jref *)s->addr;
        if (!o || !o->cls) continue;
        if (o->cls->prim) {                       /* primitive array */
            ArrayObj *a = (ArrayObj *)o;
            int n = a->length; if (n < 0) n = 0; if (n > 96) n = 96;
            const void *p = J_ARRDATA(a);
            for (int k = 0; k < n; k++) {
                int v;
                if (a->atype == J_AT_I)      v = ((const int *)p)[k];
                else if (a->atype == J_AT_S) v = ((const short *)p)[k];
                else if (a->atype == J_AT_B) v = ((const signed char *)p)[k];
                else break;                       /* non-numeric array: skip */
                DEMIT("%s[%d] = %d\n", s->name, k, v);
            }
        } else {                                  /* plain object: int-aligned slots */
            unsigned sz = o->cls->instance_size; if (sz > 4096) sz = 4096;
            for (unsigned off = (unsigned)sizeof(jobject); off + 4 <= sz; off += 4)
                DEMIT("%s@%u = %d\n", s->name, off, *(int *)((char *)o + off));
        }
    }
    #undef DEMIT
    if (f) fclose(f);
    fflush(stderr);
}

int savestate_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int savestate_save(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t magic = SS_MAGIC, ver = SS_VERSION;
    uint64_t base = (uint64_t)(uintptr_t)j_arena_base();
    uint64_t used = (uint64_t)j_arena_used();
    uint64_t nstat = (uint64_t)g_savestate_statics_count;
    void *intern = *j_intern_head_slot();

    int ok = 1;
    ok &= fwrite(&magic, 4, 1, f) == 1;
    ok &= fwrite(&ver,   4, 1, f) == 1;
    ok &= fwrite(&base,  8, 1, f) == 1;
    ok &= fwrite(&used,  8, 1, f) == 1;
    ok &= fwrite(&nstat, 8, 1, f) == 1;
    ok &= fwrite(j_arena_base(), 1, (size_t)used, f) == (size_t)used;
    ok &= fwrite(&intern, sizeof intern, 1, f) == 1;
    for (int i = 0; i < g_savestate_statics_count; i++)
        ok &= fwrite(g_savestate_statics[i].addr, 1, g_savestate_statics[i].size, f)
              == g_savestate_statics[i].size;
    fclose(f);
    return ok ? 0 : -1;
}

int savestate_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t magic = 0, ver = 0;
    uint64_t base = 0, used = 0, nstat = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != SS_MAGIC) { fclose(f); return -1; }
    if (fread(&ver, 4, 1, f) != 1 || ver != SS_VERSION)   { fclose(f); return -1; }
    if (fread(&base, 8, 1, f) != 1 || fread(&used, 8, 1, f) != 1 ||
        fread(&nstat, 8, 1, f) != 1)                      { fclose(f); return -1; }

    /* absolute pointers in the snapshot are only valid at the same arena base */
    if (base != (uint64_t)(uintptr_t)j_arena_base())      { fclose(f); return -2; }
    if (nstat != (uint64_t)g_savestate_statics_count)     { fclose(f); return -1; }

    int ok = 1;
    ok &= fread(j_arena_base(), 1, (size_t)used, f) == (size_t)used;
    j_arena_set_used((size_t)used);
    void *intern = 0;
    ok &= fread(&intern, sizeof intern, 1, f) == 1;
    *j_intern_head_slot() = intern;
    for (int i = 0; i < g_savestate_statics_count; i++)
        ok &= fread(g_savestate_statics[i].addr, 1, g_savestate_statics[i].size, f)
              == g_savestate_statics[i].size;
    fclose(f);
    return ok ? 0 : -1;
}
