/*
 * devsaves.c -- copy the game's RecordStore .rms files to/from numbered slots.
 * See devsaves.h. Slot files are flat: gameslot<N>_<store>.rms next to the exe.
 */
#include "devsaves.h"
#include <stdio.h>
#include <string.h>

/* the stores the game writes (rms.c persists each as "<name>.rms") */
static const char *const k_stores[] = { "Config", "Player", "Player2", "World" };
#define NSTORE ((int)(sizeof k_stores / sizeof k_stores[0]))

static void live_path(char *out, int cap, const char *store) {
    snprintf(out, cap, "%s.rms", store);
}
static void slot_path(char *out, int cap, int slot, const char *store) {
    snprintf(out, cap, "gameslot%d_%s.rms", slot, store);
}

/* binary copy src->dst; 1 ok, 0 if src missing, -1 on write error */
static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return 0;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[8192]; size_t n; int ok = 1;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        if (fwrite(buf, 1, n, out) != n) { ok = 0; break; }
    fclose(in); fclose(out);
    return ok ? 1 : -1;
}

static int file_exists(const char *p) {
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fclose(f); return 1;
}

int devsaves_live_exists(void) {
    char p[64]; live_path(p, sizeof p, "World");   /* World implies an actual game */
    return file_exists(p);
}
int devsaves_slot_exists(int slot) {
    char p[64]; slot_path(p, sizeof p, slot, "World");
    return file_exists(p);
}

int devsaves_backup(int slot) {
    int copied = 0;
    for (int i = 0; i < NSTORE; i++) {
        char src[64], dst[64];
        live_path(src, sizeof src, k_stores[i]);
        slot_path(dst, sizeof dst, slot, k_stores[i]);
        int r = copy_file(src, dst);
        if (r < 0) return -1;
        copied += r;
    }
    return copied;
}

int devsaves_restore(int slot) {
    if (!devsaves_slot_exists(slot)) return -1;
    int copied = 0;
    for (int i = 0; i < NSTORE; i++) {
        char src[64], dst[64];
        slot_path(src, sizeof src, slot, k_stores[i]);
        live_path(dst, sizeof dst, k_stores[i]);
        int r = copy_file(src, dst);
        if (r < 0) return -1;
        copied += r;
    }
    return copied;
}
