/*
 * jio.c -- java.io streams. Doom RPG reads its assets through
 * DataInputStream(getResourceAsStream(...)) and serializes saves through
 * DataOutputStream(ByteArrayOutputStream). All concrete streams are backed by
 * a single StreamObj (a byte buffer + cursor); Data* streams wrap one and do
 * big-endian framing on top.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include <stdio.h>

/* ---- factory used by getResourceAsStream / createImage -------------------- */
jref j_input_stream_from_bytes(const uint8_t *data, int len) {
    StreamObj *s = (StreamObj *)j_new(&CLASS_java_io_ByteArrayInputStream);
    s->buf = (uint8_t *)data; s->len = len; s->pos = 0; s->owns_buf = 0;
    return (jref)s;
}

/* ---- ByteArrayInputStream / InputStream ----------------------------------- */
void m_java_io_ByteArrayInputStream___init___aB__V(jref this_, jref arr) {
    StreamObj *s = (StreamObj *)this_;
    ArrayObj *a = (ArrayObj *)arr;
    s->buf = (uint8_t *)J_ARRDATA(a); s->len = a ? a->length : 0; s->pos = 0; s->owns_buf = 0;
}

jint m_java_io_InputStream__read____I(jref this_) {
    StreamObj *s = (StreamObj *)this_;
    if (s->pos >= s->len) return -1;
    return s->buf[s->pos++];
}
jint m_java_io_InputStream__read__aBII__I(jref this_, jref arr, jint off, jint len) {
    StreamObj *s = (StreamObj *)this_;
    if (s->pos >= s->len) return -1;
    jint n = s->len - s->pos; if (n > len) n = len;
    memcpy((uint8_t *)J_ARRDATA(arr) + off, s->buf + s->pos, (size_t)n);
    s->pos += n;
    return n;
}
jint m_java_io_InputStream__read__aB__I(jref this_, jref arr) {
    ArrayObj *a = (ArrayObj *)arr;
    return m_java_io_InputStream__read__aBII__I(this_, arr, 0, a ? a->length : 0);
}
jlong m_java_io_InputStream__skip__J__J(jref this_, jlong n) {
    StreamObj *s = (StreamObj *)this_;
    jlong avail = s->len - s->pos; if (n > avail) n = avail;
    s->pos += (jint)n; return n;
}
void m_java_io_InputStream__close____V(jref this_) { (void)this_; }

/* ---- DataInputStream ------------------------------------------------------ */
void m_java_io_DataInputStream___init___Ljava_io_InputStream__V(jref this_, jref in) {
    StreamObj *d = (StreamObj *)this_;
    d->wrapped = in;        /* read straight from the underlying StreamObj buffer */
}
static StreamObj *dis_src(jref this_) { return (StreamObj *)((StreamObj *)this_)->wrapped; }
static int dis_u8(jref this_) {
    StreamObj *s = dis_src(this_);
    if (s->pos >= s->len) { j_throw_class(&CLASS_java_io_IOException, "EOF"); return 0; }
    return s->buf[s->pos++];
}
jint m_java_io_DataInputStream__read____I(jref this_) {
    StreamObj *s = dis_src(this_);
    if (s->pos >= s->len) return -1;
    return s->buf[s->pos++];
}
jint m_java_io_DataInputStream__readUnsignedByte____I(jref this_) { return dis_u8(this_) & 0xFF; }
jint m_java_io_DataInputStream__readChar____C(jref this_) {
    int hi = dis_u8(this_), lo = dis_u8(this_);
    return (jint)(jchar)((hi << 8) | lo);
}
void m_java_io_DataInputStream__close____V(jref this_) { (void)this_; }
void m_java_io_DataInputStream__readFully__aBII__V(jref this_, jref arr, jint off, jint len) {
    StreamObj *s = dis_src(this_);
    uint8_t *dst = (uint8_t *)J_ARRDATA(arr) + off;
    for (jint i = 0; i < len; i++) {
        if (s->pos >= s->len) { j_throw_class(&CLASS_java_io_IOException, "EOF"); return; }
        dst[i] = s->buf[s->pos++];
    }
}
jint m_java_io_DataInputStream__readByte____B(jref this_) { return (jint)(jbyte)dis_u8(this_); }
jint m_java_io_DataInputStream__readBoolean____Z(jref this_) { return dis_u8(this_) != 0; }
jint m_java_io_DataInputStream__readShort____S(jref this_) {
    int hi = dis_u8(this_), lo = dis_u8(this_);
    return (jint)(jshort)((hi << 8) | lo);
}
jint m_java_io_DataInputStream__readInt____I(jref this_) {
    int b0 = dis_u8(this_), b1 = dis_u8(this_), b2 = dis_u8(this_), b3 = dis_u8(this_);
    return (jint)((b0 << 24) | (b1 << 16) | (b2 << 8) | b3);
}
jlong m_java_io_DataInputStream__readLong____J(jref this_) {
    jlong v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | (jlong)dis_u8(this_);
    return v;
}
jref m_java_io_DataInputStream__readUTF____Ljava_lang_String(jref this_) {
    int hi = dis_u8(this_), lo = dis_u8(this_);
    int n = (hi << 8) | lo;
    char *tmp = (char *)malloc((size_t)n + 1);   /* scratch; arena is never freed */
    for (int i = 0; i < n; i++) tmp[i] = (char)dis_u8(this_);
    jref s = j_string_from_utf8(tmp, n);
    free(tmp);
    return s;
}

/* this interface-declared shim is unreferenced (calls go through j_vfind), but
 * define it so the table entry can point at a real symbol if ever needed. */
jint m_java_io_DataInput__readByte____B(jref this_) { return m_java_io_DataInputStream__readByte____B(this_); }

/* ---- ByteArrayOutputStream / DataOutputStream ----------------------------- */
void m_java_io_ByteArrayOutputStream___init_____V(jref this_) {
    StreamObj *s = (StreamObj *)this_;
    s->cap = 64; s->len = 0; s->pos = 0; s->owns_buf = 1;
    s->buf = (uint8_t *)j_alloc((size_t)s->cap);
}
static void baos_put(StreamObj *s, int b) {
    if (s->len >= s->cap) {
        s->cap *= 2;
        uint8_t *nb = (uint8_t *)j_alloc((size_t)s->cap);
        memcpy(nb, s->buf, (size_t)s->len);
        s->buf = nb;
    }
    s->buf[s->len++] = (uint8_t)b;
}
jref m_java_io_ByteArrayOutputStream__toByteArray____aB(jref this_) {
    StreamObj *s = (StreamObj *)this_;
    jref arr = j_newarray(J_AT_B, s->len);
    memcpy(J_ARRDATA(arr), s->buf, (size_t)s->len);
    return arr;
}
void m_java_io_ByteArrayOutputStream__write__I__V(jref this_, jint b) { baos_put((StreamObj *)this_, b & 0xFF); }
void m_java_io_ByteArrayOutputStream__write__aBII__V(jref this_, jref arr, jint off, jint len) {
    StreamObj *s = (StreamObj *)this_;
    const uint8_t *src = (const uint8_t *)J_ARRDATA(arr) + off;
    for (jint i = 0; i < len; i++) baos_put(s, src[i]);
}
void m_java_io_ByteArrayOutputStream__close____V(jref this_) { (void)this_; }

void m_java_io_DataOutputStream___init___Ljava_io_OutputStream__V(jref this_, jref out) {
    ((StreamObj *)this_)->wrapped = out;
}
static StreamObj *dos_dst(jref this_) { return (StreamObj *)((StreamObj *)this_)->wrapped; }
void m_java_io_DataOutputStream__writeByte__I__V(jref this_, jint v) { baos_put(dos_dst(this_), v & 0xFF); }
void m_java_io_DataOutputStream__writeBoolean__Z__V(jref this_, jint v) { baos_put(dos_dst(this_), v ? 1 : 0); }
void m_java_io_DataOutputStream__writeShort__I__V(jref this_, jint v) {
    StreamObj *d = dos_dst(this_); baos_put(d, (v >> 8) & 0xFF); baos_put(d, v & 0xFF);
}
void m_java_io_DataOutputStream__writeInt__I__V(jref this_, jint v) {
    StreamObj *d = dos_dst(this_);
    baos_put(d, (v >> 24) & 0xFF); baos_put(d, (v >> 16) & 0xFF);
    baos_put(d, (v >> 8) & 0xFF);  baos_put(d, v & 0xFF);
}
void m_java_io_DataOutputStream__writeLong__J__V(jref this_, jlong v) {
    StreamObj *d = dos_dst(this_);
    for (int i = 7; i >= 0; i--) baos_put(d, (int)((v >> (i * 8)) & 0xFF));
}
void m_java_io_DataOutputStream__writeChar__I__V(jref this_, jint v) {
    StreamObj *d = dos_dst(this_); baos_put(d, (v >> 8) & 0xFF); baos_put(d, v & 0xFF);
}
void m_java_io_DataOutputStream__write__I__V(jref this_, jint b) { baos_put(dos_dst(this_), b & 0xFF); }
void m_java_io_DataOutputStream__write__aBII__V(jref this_, jref arr, jint off, jint len) {
    StreamObj *d = dos_dst(this_);
    const uint8_t *src = (const uint8_t *)J_ARRDATA(arr) + off;
    for (jint i = 0; i < len; i++) baos_put(d, src[i]);
}
void m_java_io_DataOutputStream__close____V(jref this_) { (void)this_; }
void m_java_io_DataOutputStream__writeUTF__Ljava_lang_String__V(jref this_, jref str) {
    StreamObj *d = dos_dst(this_);
    char *u = j_string_to_cstr(str);
    int n = (int)strlen(u);
    baos_put(d, (n >> 8) & 0xFF); baos_put(d, n & 0xFF);
    for (int i = 0; i < n; i++) baos_put(d, (unsigned char)u[i]);
    free(u);
}

/* ---- PrintStream (System.out.println) ------------------------------------- */
void m_java_io_PrintStream__println__Ljava_lang_String__V(jref this_, jref s) {
    (void)this_;
    char *c = j_string_to_cstr(s);
    printf("%s\n", c);
    free(c);
}
void m_java_io_PrintStream__print__Ljava_lang_String__V(jref this_, jref s) {
    (void)this_;
    char *c = j_string_to_cstr(s);
    printf("%s", c);
    free(c);
}
