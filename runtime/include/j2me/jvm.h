/*
 * jvm.h -- core object model and primitives for recompiled Doom RPG.
 *
 * The recompiler emits C against the contract in this header. Design choices:
 *
 *  - Every Java object is a heap block beginning with a `jobject` header (a
 *    pointer to its `jclass` metadata). Instance fields live at byte offsets
 *    past the header; the recompiler emits OFF_ macros for them. Field access
 *    is offset-based (the J_x accessor macros) rather than struct-member based, so the
 *    runtime and generated code never need to share a struct layout.
 *
 *  - Game classes only ever extend a *runtime* base (Object / MIDlet /
 *    GameCanvas), never each other, so a subclass's fields simply start after
 *    its base's J2ME_BASE_* size (a compile-time sizeof()).
 *
 *  - Virtual / interface dispatch is a runtime method search up the super chain
 *    (j_vfind). Slow but trivially correct; vtables can come later if needed.
 *
 *  - Exceptions use a per-thread setjmp frame stack (see j_try / j_throw).
 *
 * CLDC-1.0 means no floating point is expected from game code, but float/double
 * primitives are defined for completeness.
 */
#ifndef DOOMRPG_JVM_H
#define DOOMRPG_JVM_H

#include <stdint.h>
#include <stddef.h>
#include <setjmp.h>

/* ---- primitive types (JVM computational types) ---------------------------- */
typedef int32_t  jint;
typedef int64_t  jlong;
typedef float    jfloat;
typedef double   jdouble;
typedef int8_t   jbyte;
typedef uint16_t jchar;
typedef int16_t  jshort;
typedef int32_t  jbool;     /* booleans are ints on the operand stack */

typedef struct jobject jobject;
typedef jobject *jref;      /* a reference is a pointer to an object header */

/* ---- class metadata -------------------------------------------------------- */
typedef struct jmethod {
    const char *name;
    const char *desc;
    void       *fn;         /* cast to the concrete signature at the call site */
} jmethod;

typedef struct jclass {
    const char            *name;        /* internal name, e.g. "k" or "java/lang/Object" */
    const struct jclass   *super;       /* NULL for java/lang/Object */
    uint32_t               instance_size;
    const jmethod         *methods;     /* declared instance methods, for dispatch */
    uint32_t               num_methods;
    const struct jclass *const *interfaces;
    uint32_t               num_interfaces;
    const struct jclass   *element;     /* component class if this is an array type */
    uint8_t                prim;        /* nonzero => primitive-array tag (see J_AT_*) */
} jclass;

struct jobject {
    const jclass *cls;
};

/* primitive array element tags (match JVMS newarray atype where convenient) */
enum { J_AT_REF = 0, J_AT_Z = 4, J_AT_C = 5, J_AT_F = 6, J_AT_D = 7,
       J_AT_B = 8, J_AT_S = 9, J_AT_I = 10, J_AT_J = 11 };

/* ---- base-class sizes (so generated field offsets are compile-time consts) -- */
/* java/lang/Object: just the header. MIDlet / GameCanvas are defined in the
 * runtime; their J2ME_BASE_* macros resolve to sizeof(their object struct) and
 * are provided by j2me_objects.h, included below. */
#define J2ME_BASE_java_lang_Object  ((uint32_t)sizeof(jobject))

/* ---- allocation ------------------------------------------------------------ */
jref  j_new(const jclass *cls);                 /* zeroed instance */
void *j_alloc(size_t n);                         /* zeroed raw bytes (GC-managed later) */

/* ---- field access (offset-based) ------------------------------------------- */
#define J_REF(o, off)  (*(jref    *)((char *)(o) + (off)))
#define J_I(o, off)    (*(jint    *)((char *)(o) + (off)))
#define J_J(o, off)    (*(jlong   *)((char *)(o) + (off)))
#define J_F(o, off)    (*(jfloat  *)((char *)(o) + (off)))
#define J_D(o, off)    (*(jdouble *)((char *)(o) + (off)))
#define J_B(o, off)    (*(jbyte   *)((char *)(o) + (off)))
#define J_C(o, off)    (*(jchar   *)((char *)(o) + (off)))
#define J_S(o, off)    (*(jshort  *)((char *)(o) + (off)))
#define J_Z(o, off)    (*(jbyte   *)((char *)(o) + (off)))

/* ---- dispatch / type checks ------------------------------------------------ */
void *j_vfind(const jclass *cls, const char *name, const char *desc);
jint  j_instanceof(jref obj, const jclass *type);   /* 0/1, null-safe */
jref  j_checkcast(jref obj, const jclass *type);     /* returns obj or throws */

/* ---- arrays ---------------------------------------------------------------- */
jref  j_newarray(uint8_t atype, jint len);           /* primitive array */
jref  j_anewarray(const jclass *elem, jint len);     /* reference array */
jref  j_multianewarray(const jclass *cls, jint dims, const jint *counts);
jint  j_arraylength(jref a);
/* element accessors (bounds-checked); 'a' is a jref to an array object */
jref *j_aref(jref a, jint i);
jint  *j_iarr(jref a, jint i);
jlong *j_jarr(jref a, jint i);
jbyte *j_barr(jref a, jint i);   /* also boolean[] */
jchar *j_carr(jref a, jint i);
jshort*j_sarr(jref a, jint i);
jfloat*j_farr(jref a, jint i);
jdouble*j_darr(jref a, jint i);

/* ---- strings --------------------------------------------------------------- */
jref  j_strlit(const char *utf8);   /* interned by literal pointer identity */

/* ---- exceptions (per-thread setjmp frame stack) ---------------------------- */
typedef struct j_eh {
    jmp_buf       env;
    struct j_eh  *prev;
    jref          ex;
} j_eh;

int   j_try(j_eh *frame);            /* push frame; returns setjmp() value */
void  j_pop_eh(void);                /* pop the top frame (normal exit) */
void  j_throw(jref ex);              /* set current exception, longjmp to top frame */
void  j_rethrow(jref ex);            /* pop current frame then propagate */
jref  j_new_exception(const jclass *cls, const char *msg);

/* ---- arithmetic helpers that must trap or have defined semantics ----------- */
jint  j_idiv(jint a, jint b);
jint  j_irem(jint a, jint b);
jlong j_ldiv(jlong a, jlong b);
jlong j_lrem(jlong a, jlong b);
jint  j_lcmp(jlong a, jlong b);
jint  j_fcmpl(jfloat a, jfloat b);
jint  j_fcmpg(jfloat a, jfloat b);
jint  j_dcmpl(jdouble a, jdouble b);
jint  j_dcmpg(jdouble a, jdouble b);

/* ---- one-time static initialization --------------------------------------- */
void  j_init_all(void);              /* generated: runs every class's <clinit> */

#include "j2me/j2me_objects.h"       /* runtime base-class object structs + sizes */

#endif /* DOOMRPG_JVM_H */
