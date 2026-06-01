/*
 * jlang.c -- java.lang.* : Object, Integer, Long, Math, System, Runtime,
 * Thread, Throwable, Class, and Random (java.util but kept here for company).
 *
 * Threading model: the game spins a worker Thread for its loop. We run
 * cooperatively -- Thread.start() invokes run() inline, and Thread.sleep()
 * pumps the SDL event queue so the inline loop stays responsive. See DESIGN.md.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include <stdio.h>
#include <time.h>

/* ===== Object ============================================================== */
void m_java_lang_Object___init_____V(jref this_) { (void)this_; }

jref m_java_lang_Object__getClass____Ljava_lang_Class(jref this_) {
    return j_make_class_object(this_ ? this_->cls : 0);
}
/* defaults so virtual dispatch to equals/toString never hits j_vfind's abort */
jint m_java_lang_Object__equals__Ljava_lang_Object__Z(jref this_, jref o) { return this_ == o; }
jref m_java_lang_Object__toString____Ljava_lang_String(jref this_) {
    return j_string_from_utf8(this_ && this_->cls ? this_->cls->name : "null", -1);
}

/* ===== Class =============================================================== */
jref j_make_class_object(const jclass *target) {
    ClassObj *c = (ClassObj *)j_new(&CLASS_java_lang_Class);
    c->target = target;
    return (jref)c;
}

jref m_java_lang_Class__forName__Ljava_lang_String__Ljava_lang_Class(jref name) {
    (void)name;             /* the game only uses this to anchor getResourceAsStream */
    return j_make_class_object(0);
}

jref m_java_lang_Class__getResourceAsStream__Ljava_lang_String__Ljava_io_InputStream(jref this_, jref name) {
    (void)this_;
    char *n = j_string_to_cstr(name);
    const char *p = n;
    if (*p == '/') p++;     /* resources are referenced as "/foo" */
    int len = 0;
    uint8_t *bytes = assets_get(p, &len);
    jref s = bytes ? j_input_stream_from_bytes(bytes, len) : 0;
    free(n);
    return s;
}

/* ===== Integer / Long ====================================================== */
void m_java_lang_Integer___init___I__V(jref this_, jint v) {
    ((IntegerObj *)this_)->value = v;
}
jint m_java_lang_Integer__parseInt__Ljava_lang_String__I(jref s) {
    char *c = j_string_to_cstr(s);
    long v = strtol(c, 0, 10);
    free(c);
    return (jint)v;
}
jref m_java_lang_Integer__toString__I__Ljava_lang_String(jint v) {
    char b[16]; snprintf(b, sizeof b, "%d", v); return j_string_from_utf8(b, -1);
}
jref m_java_lang_Long__toString__J__Ljava_lang_String(jlong v) {
    char b[32]; snprintf(b, sizeof b, "%lld", (long long)v); return j_string_from_utf8(b, -1);
}

/* ===== Math ================================================================ */
jint m_java_lang_Math__abs__I__I(jint a) { return a < 0 ? -a : a; }
jint m_java_lang_Math__max__II__I(jint a, jint b) { return a > b ? a : b; }

/* ===== System ============================================================== */
void m_java_lang_System__arraycopy__Ljava_lang_ObjectILjava_lang_ObjectII__V(
        jref src, jint sp, jref dst, jint dp, jint n) {
    if (!src || !dst) { J_NPE(); return; }
    ArrayObj *s = (ArrayObj *)src, *d = (ArrayObj *)dst;
    if (sp < 0 || dp < 0 || n < 0 || sp + n > s->length || dp + n > d->length) {
        J_AIOOBE(0); return;
    }
    memmove((char *)J_ARRDATA(d) + (size_t)dp * d->elem_size,
            (char *)J_ARRDATA(s) + (size_t)sp * s->elem_size,
            (size_t)n * s->elem_size);
}

jlong m_java_lang_System__currentTimeMillis____J(void) {
#if defined(_WIN32)
    return (jlong)clock() * (1000 / CLOCKS_PER_SEC);
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (jlong)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}
void m_java_lang_System__gc____V(void) { /* no GC yet */ }

jref m_java_lang_System__getProperty__Ljava_lang_String__Ljava_lang_String(jref key) {
    (void)key; return 0;
}

/* ===== Runtime ============================================================= */
static jobject g_runtime;   /* singleton */
jref m_java_lang_Runtime__getRuntime____Ljava_lang_Runtime(void) {
    if (!g_runtime.cls) g_runtime.cls = &CLASS_java_lang_Runtime;
    return &g_runtime;
}
jlong m_java_lang_Runtime__freeMemory____J(jref this_)  { (void)this_; return 2 * 1024 * 1024; }
jlong m_java_lang_Runtime__totalMemory____J(jref this_) { (void)this_; return 4 * 1024 * 1024; }

/* ===== Thread ============================================================== */
void m_java_lang_Thread___init___Ljava_lang_Runnable__V(jref this_, jref runnable) {
    ((ThreadObj *)this_)->runnable = runnable;
}
void m_java_lang_Thread__start____V(jref this_) {
    jref r = ((ThreadObj *)this_)->runnable;
    if (!r) return;
    typedef void (*runfn)(jref);
    runfn run = (runfn)j_vfind(r->cls, "run", "()V");
    run(r);   /* cooperative: run the game loop inline (see header note) */
}
void m_java_lang_Thread__sleep__J__V(jlong ms) {
    extern void runtime_idle(int ms);   /* in main.c: pump events + sleep */
    runtime_idle((int)ms);
}

/* ===== Throwable / Exception =============================================== */
void m_java_lang_Exception___init___Ljava_lang_String__V(jref this_, jref msg) {
    ((ThrowableObj *)this_)->message = msg;
}
jref m_java_lang_Throwable__toString____Ljava_lang_String(jref this_) {
    ThrowableObj *t = (ThrowableObj *)this_;
    if (t->message) return t->message;
    return j_string_from_utf8(this_->cls->name, -1);
}

/* ===== java.util.Random ==================================================== */
void m_java_util_Random___init_____V(jref this_) {
    ((RandomObj *)this_)->seed = (uint64_t)m_java_lang_System__currentTimeMillis____J() ^ 0x5DEECE66Dull;
}
jint m_java_util_Random__nextInt____I(jref this_) {
    RandomObj *r = (RandomObj *)this_;
    r->seed = (r->seed * 0x5DEECE66Dull + 0xBull) & ((1ull << 48) - 1);
    return (jint)(r->seed >> 16);
}
