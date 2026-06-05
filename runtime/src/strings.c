/*
 * strings.c -- java.lang.String + StringBuffer, plus the UTF-8 <-> jchar[]
 * bridges the rest of the runtime uses. Strings are immutable arrays of UTF-16
 * code units (jchar); StringBuffers are growable.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include <stdio.h>

/* ---- construction helpers ------------------------------------------------- */
jref j_string_from_chars(const jchar *chars, jint len) {
    StringObj *s = (StringObj *)j_new(&CLASS_java_lang_String);
    s->length = len;
    s->chars = (jchar *)j_alloc((size_t)len * sizeof(jchar));
    if (chars) memcpy(s->chars, chars, (size_t)len * sizeof(jchar));
    return (jref)s;
}

/* decode UTF-8 (with the modified-UTF-8 NUL form tolerated) into jchars */
jref j_string_from_utf8(const char *u, int len) {
    if (len < 0) len = (int)strlen(u);
    /* scratch buffer: malloc (freed below). Arena memory is never freed. */
    jchar *tmp = (jchar *)malloc((size_t)(len + 1) * sizeof(jchar));
    jint n = 0;
    const unsigned char *p = (const unsigned char *)u, *end = p + len;
    while (p < end) {
        unsigned c = *p++;
        if (c < 0x80) {
            tmp[n++] = (jchar)c;
        } else if ((c & 0xE0) == 0xC0 && p < end) {
            tmp[n++] = (jchar)(((c & 0x1F) << 6) | (*p++ & 0x3F));
        } else if ((c & 0xF0) == 0xE0 && p + 1 < end) {
            jchar v = (jchar)(((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F));
            p += 2; tmp[n++] = v;
        } else {
            tmp[n++] = (jchar)c;   /* lenient */
        }
    }
    jref s = j_string_from_chars(tmp, n);
    free(tmp);
    return s;
}

char *j_string_to_cstr(jref so) {
    StringObj *s = (StringObj *)so;
    if (!s) { char *e = (char *)malloc(1); e[0] = 0; return e; }
    /* worst case 3 bytes per code unit */
    char *out = (char *)malloc((size_t)s->length * 3 + 1);
    int o = 0;
    for (jint i = 0; i < s->length; i++) {
        unsigned c = s->chars[i];
        if (c < 0x80) out[o++] = (char)c;
        else if (c < 0x800) { out[o++] = (char)(0xC0 | (c >> 6)); out[o++] = (char)(0x80 | (c & 0x3F)); }
        else { out[o++] = (char)(0xE0 | (c >> 12)); out[o++] = (char)(0x80 | ((c >> 6) & 0x3F)); out[o++] = (char)(0x80 | (c & 0x3F)); }
    }
    out[o] = 0;
    return out;
}

jint j_string_length(jref s) { return s ? ((StringObj *)s)->length : 0; }

/* ---- interning of string literals ----------------------------------------- */
/* ldc "..." must return a stable reference per distinct literal. The literal's
 * C-string pointer is stable, so we cache keyed on that pointer. */
struct intern { const char *key; jref val; struct intern *next; };
static struct intern *g_intern = NULL;

/* The intern list nodes live in the arena, but this head pointer is a runtime
 * static -- the savestate layer must snapshot it too, or a restore would leave
 * it pointing past the rewound arena. Exposed as a void* slot. */
void **j_intern_head_slot(void) { return (void **)&g_intern; }

jref j_strlit(const char *utf8) {
    for (struct intern *it = g_intern; it; it = it->next)
        if (it->key == utf8) return it->val;
    jref s = j_string_from_utf8(utf8, -1);
    struct intern *it = (struct intern *)j_alloc(sizeof *it);
    it->key = utf8; it->val = s; it->next = g_intern; g_intern = it;
    return s;
}

/* ---- generic toString ----------------------------------------------------- */
jref j_to_string(jref obj) {
    if (!obj) return j_strlit("null");
    if (obj->cls == &CLASS_java_lang_String) return obj;
    typedef jref (*tsfn)(jref);
    tsfn fn = (tsfn)j_vfind_opt(obj->cls, "toString", "()Ljava/lang/String;");
    if (fn) return fn(obj);
    return j_string_from_utf8(obj->cls->name, -1);
}

/* ===========================================================================
 * java.lang.String
 * =========================================================================== */
void m_java_lang_String___init___aCII__V(jref this_, jref a0, jint off, jint len) {
    StringObj *s = (StringObj *)this_;
    s->length = len;
    s->chars = (jchar *)j_alloc((size_t)len * sizeof(jchar));
    jchar *src = (jchar *)J_ARRDATA(a0);
    memcpy(s->chars, src + off, (size_t)len * sizeof(jchar));
}

/* new String(byte[], off, len) -- decode the byte range as (modified) UTF-8,
 * which also covers the ASCII the games actually store. */
void m_java_lang_String___init___aBII__V(jref this_, jref a0, jint off, jint len) {
    StringObj *s = (StringObj *)this_;
    const char *src = (const char *)J_ARRDATA(a0) + off;
    jref tmp = j_string_from_utf8(src, len);
    StringObj *t = (StringObj *)tmp;
    s->length = t->length;
    s->chars = t->chars;     /* arena-allocated; safe to alias */
}
/* new String(byte[], off, len, charset) -- we always treat bytes as UTF-8. */
void m_java_lang_String___init___aBIILjava_lang_String__V(jref this_, jref a0, jint off, jint len, jref charset) {
    (void)charset;
    m_java_lang_String___init___aBII__V(this_, a0, off, len);
}
/* new String(char[]) */
void m_java_lang_String___init___aC__V(jref this_, jref a0) {
    StringObj *s = (StringObj *)this_;
    ArrayObj *a = (ArrayObj *)a0;
    jint n = a ? a->length : 0;
    s->length = n;
    s->chars = (jchar *)j_alloc((size_t)(n > 0 ? n : 1) * sizeof(jchar));
    if (n) memcpy(s->chars, J_ARRDATA(a0), (size_t)n * sizeof(jchar));
}
/* new String(StringBuffer) */
void m_java_lang_String___init___Ljava_lang_StringBuffer__V(jref this_, jref sb) {
    StringObj *s = (StringObj *)this_;
    StringBufferObj *b = (StringBufferObj *)sb;
    jint n = b ? b->length : 0;
    s->length = n;
    s->chars = (jchar *)j_alloc((size_t)(n > 0 ? n : 1) * sizeof(jchar));
    if (n) memcpy(s->chars, b->chars, (size_t)n * sizeof(jchar));
}
jref m_java_lang_String__valueOf__I__Ljava_lang_String(jint v) {
    char b[16]; snprintf(b, sizeof b, "%d", v); return j_string_from_utf8(b, -1);
}
jint m_java_lang_String__indexOf__I__I(jref this_, jint ch) {
    return m_java_lang_String__indexOf__II__I(this_, ch, 0);
}

jint m_java_lang_String__charAt__I__C(jref this_, jint i) {
    StringObj *s = (StringObj *)this_;
    if ((uint32_t)i >= (uint32_t)s->length) { J_AIOOBE(i); return 0; }
    return s->chars[i];
}

jint m_java_lang_String__length____I(jref this_) { return ((StringObj *)this_)->length; }

jint m_java_lang_String__equals__Ljava_lang_Object__Z(jref this_, jref a0) {
    if (this_ == a0) return 1;
    if (!a0 || a0->cls != &CLASS_java_lang_String) return 0;
    StringObj *x = (StringObj *)this_, *y = (StringObj *)a0;
    if (x->length != y->length) return 0;
    return memcmp(x->chars, y->chars, (size_t)x->length * sizeof(jchar)) == 0;
}

jint m_java_lang_String__compareTo__Ljava_lang_String__I(jref this_, jref a0) {
    StringObj *x = (StringObj *)this_, *y = (StringObj *)a0;
    jint n = x->length < y->length ? x->length : y->length;
    for (jint i = 0; i < n; i++)
        if (x->chars[i] != y->chars[i]) return (jint)x->chars[i] - (jint)y->chars[i];
    return x->length - y->length;
}

jint m_java_lang_String__indexOf__II__I(jref this_, jint ch, jint from) {
    StringObj *s = (StringObj *)this_;
    if (from < 0) from = 0;
    for (jint i = from; i < s->length; i++) if (s->chars[i] == (jchar)ch) return i;
    return -1;
}

jint m_java_lang_String__lastIndexOf__I__I(jref this_, jint ch) {
    StringObj *s = (StringObj *)this_;
    for (jint i = s->length - 1; i >= 0; i--) if (s->chars[i] == (jchar)ch) return i;
    return -1;
}

jint m_java_lang_String__startsWith__Ljava_lang_String__Z(jref this_, jref a0) {
    StringObj *s = (StringObj *)this_, *p = (StringObj *)a0;
    if (p->length > s->length) return 0;
    return memcmp(s->chars, p->chars, (size_t)p->length * sizeof(jchar)) == 0;
}

jref m_java_lang_String__substring__II__Ljava_lang_String(jref this_, jint b, jint e) {
    StringObj *s = (StringObj *)this_;
    if (b < 0 || e > s->length || b > e) { J_AIOOBE(b); return 0; }
    return j_string_from_chars(s->chars + b, e - b);
}

jref m_java_lang_String__substring__I__Ljava_lang_String(jref this_, jint b) {
    return m_java_lang_String__substring__II__Ljava_lang_String(
        this_, b, ((StringObj *)this_)->length);
}

jref m_java_lang_String__replace__CC__Ljava_lang_String(jref this_, jint oldc, jint newc) {
    StringObj *s = (StringObj *)this_;
    jref r = j_string_from_chars(s->chars, s->length);
    StringObj *o = (StringObj *)r;
    for (jint i = 0; i < o->length; i++) if (o->chars[i] == (jchar)oldc) o->chars[i] = (jchar)newc;
    return r;
}

jref m_java_lang_String__valueOf__C__Ljava_lang_String(jint c) {
    jchar ch = (jchar)c;
    return j_string_from_chars(&ch, 1);
}

jref m_java_lang_String__toLowerCase____Ljava_lang_String(jref this_) {
    StringObj *s = (StringObj *)this_;
    jref r = j_string_from_chars(s->chars, s->length);
    StringObj *o = (StringObj *)r;
    for (jint i = 0; i < o->length; i++) { jchar c = o->chars[i]; if (c >= 'A' && c <= 'Z') o->chars[i] = c + 32; }
    return r;
}
jref m_java_lang_String__toUpperCase____Ljava_lang_String(jref this_) {
    StringObj *s = (StringObj *)this_;
    jref r = j_string_from_chars(s->chars, s->length);
    StringObj *o = (StringObj *)r;
    for (jint i = 0; i < o->length; i++) { jchar c = o->chars[i]; if (c >= 'a' && c <= 'z') o->chars[i] = c - 32; }
    return r;
}
jref m_java_lang_String__trim____Ljava_lang_String(jref this_) {
    StringObj *s = (StringObj *)this_;
    jint b = 0, e = s->length;
    while (b < e && s->chars[b] <= ' ') b++;
    while (e > b && s->chars[e - 1] <= ' ') e--;
    return j_string_from_chars(s->chars + b, e - b);
}

/* ===========================================================================
 * java.lang.StringBuffer
 * =========================================================================== */
static void sb_ensure(StringBufferObj *b, jint need) {
    if (need <= b->cap) return;
    jint cap = b->cap ? b->cap : 16;
    while (cap < need) cap *= 2;
    jchar *nc = (jchar *)j_alloc((size_t)cap * sizeof(jchar));
    if (b->chars) memcpy(nc, b->chars, (size_t)b->length * sizeof(jchar));
    b->chars = nc; b->cap = cap;
}
static void sb_append_chars(StringBufferObj *b, const jchar *c, jint n) {
    sb_ensure(b, b->length + n);
    memcpy(b->chars + b->length, c, (size_t)n * sizeof(jchar));
    b->length += n;
}
static void sb_append_str(StringBufferObj *b, jref s) {
    StringObj *so = (StringObj *)s;
    if (!so) { static const jchar nul[4] = {'n','u','l','l'}; sb_append_chars(b, nul, 4); return; }
    sb_append_chars(b, so->chars, so->length);
}

void m_java_lang_StringBuffer___init_____V(jref this_) {
    StringBufferObj *b = (StringBufferObj *)this_; b->length = 0; b->cap = 0; b->chars = 0;
    sb_ensure(b, 16);
}
void m_java_lang_StringBuffer___init___I__V(jref this_, jint cap) {
    StringBufferObj *b = (StringBufferObj *)this_; b->length = 0; b->cap = 0; b->chars = 0;
    sb_ensure(b, cap > 0 ? cap : 16);
}
void m_java_lang_StringBuffer___init___Ljava_lang_String__V(jref this_, jref s) {
    m_java_lang_StringBuffer___init_____V(this_);
    sb_append_str((StringBufferObj *)this_, s);
}

jref m_java_lang_StringBuffer__append__Ljava_lang_String__Ljava_lang_StringBuffer(jref this_, jref s) {
    sb_append_str((StringBufferObj *)this_, s); return this_;
}
jref m_java_lang_StringBuffer__append__Ljava_lang_Object__Ljava_lang_StringBuffer(jref this_, jref o) {
    sb_append_str((StringBufferObj *)this_, j_to_string(o)); return this_;
}
jref m_java_lang_StringBuffer__append__C__Ljava_lang_StringBuffer(jref this_, jint c) {
    jchar ch = (jchar)c; sb_append_chars((StringBufferObj *)this_, &ch, 1); return this_;
}
jref m_java_lang_StringBuffer__append__I__Ljava_lang_StringBuffer(jref this_, jint v) {
    char buf[16]; snprintf(buf, sizeof buf, "%d", v);
    jref s = j_string_from_utf8(buf, -1); sb_append_str((StringBufferObj *)this_, s); return this_;
}
jref m_java_lang_StringBuffer__append__J__Ljava_lang_StringBuffer(jref this_, jlong v) {
    char buf[32]; snprintf(buf, sizeof buf, "%lld", (long long)v);
    jref s = j_string_from_utf8(buf, -1); sb_append_str((StringBufferObj *)this_, s); return this_;
}

jref m_java_lang_StringBuffer__append__aC__Ljava_lang_StringBuffer(jref this_, jref arr) {
    ArrayObj *a = (ArrayObj *)arr;
    if (a) sb_append_chars((StringBufferObj *)this_, (jchar *)J_ARRDATA(arr), a->length);
    return this_;
}
jref m_java_lang_StringBuffer__deleteCharAt__I__Ljava_lang_StringBuffer(jref this_, jint i) {
    StringBufferObj *b = (StringBufferObj *)this_;
    if (i >= 0 && i < b->length) {
        memmove(b->chars + i, b->chars + i + 1, (size_t)(b->length - i - 1) * sizeof(jchar));
        b->length--;
    }
    return this_;
}
jint m_java_lang_StringBuffer__charAt__I__C(jref this_, jint i) {
    StringBufferObj *b = (StringBufferObj *)this_;
    if ((uint32_t)i >= (uint32_t)b->length) { J_AIOOBE(i); return 0; }
    return b->chars[i];
}
jint m_java_lang_StringBuffer__length____I(jref this_) { return ((StringBufferObj *)this_)->length; }

void m_java_lang_StringBuffer__setCharAt__IC__V(jref this_, jint i, jint c) {
    StringBufferObj *b = (StringBufferObj *)this_;
    if ((uint32_t)i < (uint32_t)b->length) b->chars[i] = (jchar)c;
}
void m_java_lang_StringBuffer__setLength__I__V(jref this_, jint n) {
    StringBufferObj *b = (StringBufferObj *)this_;
    sb_ensure(b, n);
    if (n > b->length) memset(b->chars + b->length, 0, (size_t)(n - b->length) * sizeof(jchar));
    b->length = n;
}
jref m_java_lang_StringBuffer__toString____Ljava_lang_String(jref this_) {
    StringBufferObj *b = (StringBufferObj *)this_;
    return j_string_from_chars(b->chars, b->length);
}

static jref sb_insert_chars(jref this_, jint idx, const jchar *c, jint n) {
    StringBufferObj *b = (StringBufferObj *)this_;
    if (idx < 0) idx = 0; if (idx > b->length) idx = b->length;
    sb_ensure(b, b->length + n);
    memmove(b->chars + idx + n, b->chars + idx, (size_t)(b->length - idx) * sizeof(jchar));
    memcpy(b->chars + idx, c, (size_t)n * sizeof(jchar));
    b->length += n;
    return this_;
}
jref m_java_lang_StringBuffer__insert__IC__Ljava_lang_StringBuffer(jref this_, jint i, jint c) {
    jchar ch = (jchar)c; return sb_insert_chars(this_, i, &ch, 1);
}
jref m_java_lang_StringBuffer__insert__ILjava_lang_String__Ljava_lang_StringBuffer(jref this_, jint i, jref s) {
    StringObj *so = (StringObj *)j_to_string(s);
    return sb_insert_chars(this_, i, so->chars, so->length);
}
jref m_java_lang_StringBuffer__insert__ILjava_lang_Object__Ljava_lang_StringBuffer(jref this_, jint i, jref o) {
    return m_java_lang_StringBuffer__insert__ILjava_lang_String__Ljava_lang_StringBuffer(this_, i, j_to_string(o));
}
