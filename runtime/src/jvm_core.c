/*
 * jvm_core.c -- the beating heart of the recompiled VM: allocation, method
 * dispatch, type checks, arrays, exceptions, and the integer-arithmetic helpers
 * with JVM-defined semantics.
 *
 * GC: none yet. We allocate and never free (a J2ME heap is tiny and the game
 * runs for minutes, not days). A real collector is a later concern; correctness
 * first, exactly as the design doc promises.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* ---- allocation -----------------------------------------------------------
 * A single contiguous bump arena (no GC, J2ME heaps are tiny). Keeping every
 * object in one block at a fixed base is what makes emulator-style savestates
 * possible: snapshot/restore is just memcpy of [0, used) -- pointers stay valid
 * because the base never moves. See savestate.c. */
#define J_ARENA_CAP (96u * 1024u * 1024u)
static unsigned char *g_arena;
static size_t g_arena_used;

/* Reserve at a fixed base so absolute pointers in a savestate stay valid even
 * across process restarts (ASLR would otherwise move a malloc'd block). Falls
 * back to malloc if the address is unavailable (savestates then same-session). */
static void arena_init(void) {
#ifdef _WIN32
    g_arena = (unsigned char *)VirtualAlloc((void *)0x0000040000000000ull,
        J_ARENA_CAP, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!g_arena)
        g_arena = (unsigned char *)VirtualAlloc(NULL, J_ARENA_CAP,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#endif
    if (!g_arena) g_arena = (unsigned char *)malloc(J_ARENA_CAP);
    if (!g_arena) { fprintf(stderr, "j_alloc: arena reserve failed\n"); abort(); }
}

void *j_alloc(size_t n) {
    if (!g_arena) arena_init();
    if (n == 0) n = 1;
    n = (n + 7u) & ~(size_t)7u;                 /* 8-byte align */
    if (g_arena_used + n > J_ARENA_CAP) {
        fprintf(stderr, "j_alloc: out of memory (%zu bytes)\n", n);
        abort();
    }
    void *p = g_arena + g_arena_used;
    g_arena_used += n;
    memset(p, 0, n);                            /* callers rely on zero-init */
    return p;
}

/* arena accessors for the savestate layer */
unsigned char *j_arena_base(void) { return g_arena; }
size_t j_arena_used(void)         { return g_arena_used; }
void   j_arena_set_used(size_t u) { g_arena_used = u; }

jref j_new(const jclass *cls) {
    jref o = (jref)j_alloc(cls->instance_size);
    o->cls = cls;
    return o;
}

/* ---- dispatch ------------------------------------------------------------- */
/* Walk the super chain, linear-searching each class's declared method table.
 * Simple and correct; the design doc flags this as the place to add vtables if
 * profiling ever shows it matters. */
void *j_vfind_opt(const jclass *cls, const char *name, const char *desc) {
    for (const jclass *c = cls; c; c = c->super) {
        for (uint32_t i = 0; i < c->num_methods; i++) {
            const jmethod *m = &c->methods[i];
            if (m->name && strcmp(m->name, name) == 0 && strcmp(m->desc, desc) == 0)
                return m->fn;
        }
    }
    return NULL;
}

void *j_vfind(const jclass *cls, const char *name, const char *desc) {
    void *fn = j_vfind_opt(cls, name, desc);
    if (!fn) {
        fprintf(stderr, "j_vfind: %s.%s%s not found (abstract or runtime-only?)\n",
                cls ? cls->name : "(null)", name, desc);
        abort();
    }
    return fn;
}

/* ---- type checks ---------------------------------------------------------- */
static int is_subclass(const jclass *c, const jclass *type) {
    /* Array assignability is approximated: any array is-a any array type. The
     * game only ever casts to [I and [[C, so this is safe for now (TODO: track
     * element types precisely). */
    if (type->name && type->name[0] == '[')
        return c && c->name && c->name[0] == '[';
    for (; c; c = c->super) {
        if (c == type) return 1;
        for (uint32_t i = 0; i < c->num_interfaces; i++)
            if (c->interfaces[i] == type ||
                is_subclass(c->interfaces[i], type)) return 1;
    }
    return 0;
}

jint j_instanceof(jref obj, const jclass *type) {
    if (!obj) return 0;
    return is_subclass(obj->cls, type) ? 1 : 0;
}

jref j_checkcast(jref obj, const jclass *type) {
    if (obj && !is_subclass(obj->cls, type))
        j_throw_class(&CLASS_java_lang_RuntimeException, "ClassCastException");
    return obj;
}

/* ---- arrays --------------------------------------------------------------- */
static uint8_t atype_elem_size(uint8_t atype) {
    switch (atype) {
        case J_AT_Z: case J_AT_B: return 1;
        case J_AT_C: case J_AT_S: return 2;
        case J_AT_I: case J_AT_F: return 4;
        case J_AT_J: case J_AT_D: return 8;
        default:                  return (uint8_t)sizeof(jref);
    }
}

/* primitive array class singletons (see class_meta.c) */
extern const jclass CLASS_array_boolean, CLASS_array_byte, CLASS_array_char,
    CLASS_array_short, CLASS_array_int, CLASS_array_long, CLASS_array_float,
    CLASS_array_double, CLASS_array_ref;

static const jclass *array_class_for(uint8_t atype) {
    switch (atype) {
        case J_AT_Z: return &CLASS_array_boolean;
        case J_AT_B: return &CLASS_array_byte;
        case J_AT_C: return &CLASS_array_char;
        case J_AT_S: return &CLASS_array_short;
        case J_AT_I: return &CLASS_array_int;
        case J_AT_J: return &CLASS_array_long;
        case J_AT_F: return &CLASS_array_float;
        case J_AT_D: return &CLASS_array_double;
        default:     return &CLASS_array_ref;
    }
}

jref j_newarray(uint8_t atype, jint len) {
    if (len < 0) { j_throw_class(&CLASS_java_lang_RuntimeException, "NegativeArraySize"); return 0; }
    uint8_t es = atype_elem_size(atype);
    ArrayObj *a = (ArrayObj *)j_alloc(sizeof(ArrayObj) + (size_t)len * es);
    a->hdr.cls = array_class_for(atype);
    a->length = len;
    a->atype = atype;
    a->elem_size = es;
    return (jref)a;
}

jref j_anewarray(const jclass *elem, jint len) {
    (void)elem;
    return j_newarray(J_AT_REF, len);   /* element class tracked loosely for now */
}

jref j_multianewarray(const jclass *cls, jint dims, const jint *counts) {
    (void)cls;
    if (dims == 1) return j_newarray(J_AT_REF, counts[0]);
    jref outer = j_newarray(J_AT_REF, counts[0]);
    jref *slots = (jref *)J_ARRDATA(outer);
    for (jint i = 0; i < counts[0]; i++)
        slots[i] = j_multianewarray(cls, dims - 1, counts + 1);
    return outer;
}

jint j_arraylength(jref a) {
    if (!a) { J_NPE(); return 0; }
    return ((ArrayObj *)a)->length;
}

static void *elem_ptr(jref a, jint i, uint8_t want_size) {
    if (!a) { J_NPE(); return 0; }
    ArrayObj *arr = (ArrayObj *)a;
    if ((uint32_t)i >= (uint32_t)arr->length) { J_AIOOBE(i); return 0; }
    (void)want_size;
    return (char *)J_ARRDATA(arr) + (size_t)i * arr->elem_size;
}

jref   *j_aref(jref a, jint i)  { return (jref   *)elem_ptr(a, i, sizeof(jref)); }
jint   *j_iarr(jref a, jint i)  { return (jint   *)elem_ptr(a, i, 4); }
jlong  *j_jarr(jref a, jint i)  { return (jlong  *)elem_ptr(a, i, 8); }
jbyte  *j_barr(jref a, jint i)  { return (jbyte  *)elem_ptr(a, i, 1); }
jchar  *j_carr(jref a, jint i)  { return (jchar  *)elem_ptr(a, i, 2); }
jshort *j_sarr(jref a, jint i)  { return (jshort *)elem_ptr(a, i, 2); }
jfloat *j_farr(jref a, jint i)  { return (jfloat *)elem_ptr(a, i, 4); }
jdouble*j_darr(jref a, jint i)  { return (jdouble*)elem_ptr(a, i, 8); }

/* ---- exceptions ----------------------------------------------------------- */
static j_eh *g_eh_top = NULL;

/* Bookkeeping half of the j_try macro (jvm.h). setjmp itself runs in the
 * generated method's frame (the macro), so longjmp lands in a live frame. */
void j_push_eh(j_eh *frame) {
    frame->prev = g_eh_top;
    frame->ex = NULL;
    g_eh_top = frame;
}

void j_pop_eh(void) {
    if (g_eh_top) g_eh_top = g_eh_top->prev;
}

void j_throw(jref ex) {
    j_eh *f = g_eh_top;
    if (!f) {
        const char *cn = (ex && ex->cls) ? ex->cls->name : "(unknown)";
        fprintf(stderr, "Uncaught exception: %s\n", cn);
        abort();
    }
    f->ex = ex;
    g_eh_top = f;            /* leave frame installed; handler will pop via rethrow/normal exit */
    longjmp(f->env, 1);
}

void j_rethrow(jref ex) {
    /* current frame failed to handle; pop and propagate to the next one */
    j_pop_eh();
    j_throw(ex);
}

void j_throw_class(const jclass *cls, const char *msg) {
    j_throw(j_new_exception(cls, msg));
}

jref j_new_exception(const jclass *cls, const char *msg) {
    ThrowableObj *t = (ThrowableObj *)j_new(cls);
    t->message = msg ? j_string_from_utf8(msg, -1) : NULL;
    return (jref)t;
}

/* ---- arithmetic with JVM semantics ---------------------------------------- */
jint j_idiv(jint a, jint b) {
    if (b == 0) { j_throw_class(&CLASS_java_lang_RuntimeException, "/ by zero"); return 0; }
    if (b == -1) return (jint)(0u - (uint32_t)a);   /* avoid INT_MIN/-1 UB */
    return a / b;
}
jint j_irem(jint a, jint b) {
    if (b == 0) { j_throw_class(&CLASS_java_lang_RuntimeException, "/ by zero"); return 0; }
    if (b == -1) return 0;
    return a % b;
}
jlong j_ldiv(jlong a, jlong b) {
    if (b == 0) { j_throw_class(&CLASS_java_lang_RuntimeException, "/ by zero"); return 0; }
    if (b == -1) return (jlong)(0ull - (uint64_t)a);
    return a / b;
}
jlong j_lrem(jlong a, jlong b) {
    if (b == 0) { j_throw_class(&CLASS_java_lang_RuntimeException, "/ by zero"); return 0; }
    if (b == -1) return 0;
    return a % b;
}
jint j_lcmp(jlong a, jlong b)   { return a < b ? -1 : (a > b ? 1 : 0); }
jint j_fcmpl(jfloat a, jfloat b){ if (a < b) return -1; if (a > b) return 1; if (a == b) return 0; return -1; }
jint j_fcmpg(jfloat a, jfloat b){ if (a < b) return -1; if (a > b) return 1; if (a == b) return 0; return 1; }
jint j_dcmpl(jdouble a, jdouble b){ if (a < b) return -1; if (a > b) return 1; if (a == b) return 0; return -1; }
jint j_dcmpg(jdouble a, jdouble b){ if (a < b) return -1; if (a > b) return 1; if (a == b) return 0; return 1; }
