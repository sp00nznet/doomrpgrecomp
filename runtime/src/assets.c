/*
 * assets.c -- resource loader. The game asks for "/a.png", "/intro.bsp", "/0.mid"
 * etc.; these are the entries of DoomRPG.jar. To avoid bundling a zip+inflate
 * decoder, the build (or run script) extracts the JAR into a directory and we
 * read files from there. assets_open() takes that directory; assets_get()
 * memory-maps-ish loads a named file and caches the bytes for the process life.
 */
#include "j2me/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_root[1024] = ".";

struct cache { char *name; uint8_t *data; int len; struct cache *next; };
static struct cache *g_cache = NULL;

int assets_open(const char *dir) {
    if (dir && *dir) snprintf(g_root, sizeof g_root, "%s", dir);
    return 0;
}

uint8_t *assets_get(const char *name, int *out_len) {
    for (struct cache *c = g_cache; c; c = c->next)
        if (strcmp(c->name, name) == 0) { if (out_len) *out_len = c->len; return c->data; }

    char path[1100];
    snprintf(path, sizeof path, "%s/%s", g_root, name);
    FILE *f = fopen(path, "rb");
    if (!f) { if (out_len) *out_len = 0; return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)(n > 0 ? n : 1));
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);

    struct cache *c = (struct cache *)malloc(sizeof *c);
    c->name = _strdup(name);
    c->data = buf; c->len = (int)got; c->next = g_cache;
    g_cache = c;
    if (out_len) *out_len = c->len;
    return buf;
}
