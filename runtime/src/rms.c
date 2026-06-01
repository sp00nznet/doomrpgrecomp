/*
 * rms.c -- javax.microedition.rms RecordStore, persisted as "<name>.rms" files
 * next to the executable. Records are id-addressed (1-based, monotonic ids as
 * MIDP requires). The on-disk format is trivial: a count, then per live record
 * its id, length, and bytes.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include <stdio.h>

static void rms_path(const char *name, char *out, size_t n) {
    snprintf(out, n, "%s.rms", name);
}

static Rec *rms_find(RecordStoreObj *s, int id) {
    for (int i = 0; i < s->num; i++) if (s->recs[i].id == id) return &s->recs[i];
    return 0;
}

static void rms_load(RecordStoreObj *s) {
    char path[128]; rms_path(s->name, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return;
    int count = 0;
    if (fread(&count, sizeof(int), 1, f) == 1) {
        for (int i = 0; i < count; i++) {
            int id = 0, len = 0;
            if (fread(&id, sizeof(int), 1, f) != 1) break;
            if (fread(&len, sizeof(int), 1, f) != 1) break;
            uint8_t *d = (uint8_t *)j_alloc((size_t)(len > 0 ? len : 1));
            if (len > 0 && fread(d, 1, (size_t)len, f) != (size_t)len) break;
            if (s->num >= s->cap) { s->cap = s->cap ? s->cap * 2 : 8;
                Rec *nr = (Rec *)j_alloc((size_t)s->cap * sizeof(Rec));
                memcpy(nr, s->recs, (size_t)s->num * sizeof(Rec)); s->recs = nr; }
            s->recs[s->num].id = id; s->recs[s->num].len = len; s->recs[s->num].data = d;
            s->num++;
            if (id >= s->next_id) s->next_id = id + 1;
        }
    }
    fclose(f);
}

static void rms_save(RecordStoreObj *s) {
    char path[128]; rms_path(s->name, path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(&s->num, sizeof(int), 1, f);
    for (int i = 0; i < s->num; i++) {
        fwrite(&s->recs[i].id, sizeof(int), 1, f);
        fwrite(&s->recs[i].len, sizeof(int), 1, f);
        if (s->recs[i].len > 0) fwrite(s->recs[i].data, 1, (size_t)s->recs[i].len, f);
    }
    fclose(f);
}

jref m_javax_microedition_rms_RecordStore__openRecordStore__Ljava_lang_StringZ__Ljavax_microedition_rms_RecordStore(
        jref name, jint createIfNecessary) {
    (void)createIfNecessary;
    RecordStoreObj *s = (RecordStoreObj *)j_new(&CLASS_javax_microedition_rms_RecordStore);
    char *n = j_string_to_cstr(name);
    snprintf(s->name, sizeof s->name, "%s", n);
    free(n);
    s->next_id = 1;
    rms_load(s);
    return (jref)s;
}

void m_javax_microedition_rms_RecordStore__closeRecordStore____V(jref this_) {
    rms_save((RecordStoreObj *)this_);
}

jint m_javax_microedition_rms_RecordStore__addRecord__aBII__I(jref this_, jref data, jint off, jint len) {
    RecordStoreObj *s = (RecordStoreObj *)this_;
    if (s->num >= s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        Rec *nr = (Rec *)j_alloc((size_t)s->cap * sizeof(Rec));
        memcpy(nr, s->recs, (size_t)s->num * sizeof(Rec));
        s->recs = nr;
    }
    uint8_t *d = (uint8_t *)j_alloc((size_t)(len > 0 ? len : 1));
    if (data && len > 0) memcpy(d, (uint8_t *)J_ARRDATA(data) + off, (size_t)len);
    int id = s->next_id++;
    s->recs[s->num].id = id; s->recs[s->num].len = len; s->recs[s->num].data = d;
    s->num++;
    rms_save(s);
    return id;
}

jref m_javax_microedition_rms_RecordStore__getRecord__I__aB(jref this_, jint id) {
    Rec *r = rms_find((RecordStoreObj *)this_, id);
    if (!r) { j_throw_class(&CLASS_java_io_IOException, "InvalidRecordID"); return 0; }
    jref arr = j_newarray(J_AT_B, r->len);
    if (r->len > 0) memcpy(J_ARRDATA(arr), r->data, (size_t)r->len);
    return arr;
}

void m_javax_microedition_rms_RecordStore__setRecord__IaBII__V(jref this_, jint id, jref data, jint off, jint len) {
    Rec *r = rms_find((RecordStoreObj *)this_, id);
    if (!r) { j_throw_class(&CLASS_java_io_IOException, "InvalidRecordID"); return; }
    r->data = (uint8_t *)j_alloc((size_t)(len > 0 ? len : 1));
    r->len = len;
    if (data && len > 0) memcpy(r->data, (uint8_t *)J_ARRDATA(data) + off, (size_t)len);
    rms_save((RecordStoreObj *)this_);
}

jint m_javax_microedition_rms_RecordStore__getNumRecords____I(jref this_) {
    return ((RecordStoreObj *)this_)->num;
}

void m_javax_microedition_rms_RecordStore__deleteRecordStore__Ljava_lang_String__V(jref name) {
    char *n = j_string_to_cstr(name);
    char path[128]; rms_path(n, path, sizeof path);
    remove(path);
    free(n);
}

jref m_javax_microedition_rms_RecordStore__listRecordStores____aLjava_lang_String(void) {
    return j_anewarray(&CLASS_java_lang_String, 0);   /* enumeration of stores: TODO */
}

jref m_javax_microedition_rms_RecordStore__enumerateRecords__Ljavax_microedition_rms_RecordFilterLjavax_microedition_rms_RecordComparatorZ__Ljavax_microedition_rms_RecordEnumeration(
        jref this_, jref filter, jref comparator, jint keepUpdated) {
    (void)filter; (void)comparator; (void)keepUpdated;
    RecordStoreObj *s = (RecordStoreObj *)this_;
    EnumObj *e = (EnumObj *)j_new(&CLASS_javax_microedition_rms_RecordEnumeration);
    e->n = s->num; e->pos = 0;
    e->ids = (int *)j_alloc((size_t)(s->num > 0 ? s->num : 1) * sizeof(int));
    for (int i = 0; i < s->num; i++) e->ids[i] = s->recs[i].id;
    return (jref)e;
}

jint m_javax_microedition_rms_RecordEnumeration__nextRecordId____I(jref this_) {
    EnumObj *e = (EnumObj *)this_;
    if (e->pos >= e->n) { j_throw_class(&CLASS_java_io_IOException, "NoSuchElement"); return 0; }
    return e->ids[e->pos++];
}
