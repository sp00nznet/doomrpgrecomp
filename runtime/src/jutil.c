/*
 * jutil.c -- java.util.Vector (the only collection Doom RPG uses).
 */
#include "j2me/runtime.h"
#include "doomrpg.h"

void m_java_util_Vector___init_____V(jref this_) {
    VectorObj *v = (VectorObj *)this_;
    v->size = 0; v->cap = 8;
    v->elems = (jref *)j_alloc((size_t)v->cap * sizeof(jref));
}

void m_java_util_Vector__addElement__Ljava_lang_Object__V(jref this_, jref e) {
    VectorObj *v = (VectorObj *)this_;
    if (v->size >= v->cap) {
        jint nc = v->cap * 2;
        jref *ne = (jref *)j_alloc((size_t)nc * sizeof(jref));
        memcpy(ne, v->elems, (size_t)v->size * sizeof(jref));
        v->elems = ne; v->cap = nc;
    }
    v->elems[v->size++] = e;
}

jref m_java_util_Vector__elementAt__I__Ljava_lang_Object(jref this_, jint i) {
    VectorObj *v = (VectorObj *)this_;
    if ((uint32_t)i >= (uint32_t)v->size) { J_AIOOBE(i); return 0; }
    return v->elems[i];
}

jint m_java_util_Vector__size____I(jref this_) { return ((VectorObj *)this_)->size; }
