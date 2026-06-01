/*
 * runtime.h -- internal declarations shared across the hand-written J2ME
 * runtime .c files. Generated code never sees this; it only sees jvm.h plus the
 * generated doomrpg.h prototypes. Runtime objects (String, arrays, Vector, ...)
 * use real C structs here because only runtime code touches their fields.
 */
#ifndef DOOMRPG_RUNTIME_H
#define DOOMRPG_RUNTIME_H

#include "j2me/jvm.h"
#include <stdlib.h>
#include <string.h>

/* ---- runtime object layouts ----------------------------------------------- */
typedef struct ArrayObj {
    jobject  hdr;
    jint     length;
    uint8_t  atype;     /* J_AT_* */
    uint8_t  elem_size; /* bytes per element */
    /* element storage immediately follows (allocated inline) */
} ArrayObj;

#define J_ARRDATA(a) ((void *)((ArrayObj *)(a) + 1))

typedef struct StringObj {
    jobject hdr;
    jint    length;
    jchar  *chars;      /* not NUL-terminated */
} StringObj;

typedef struct StringBufferObj {
    jobject hdr;
    jint    length;
    jint    cap;
    jchar  *chars;
} StringBufferObj;

typedef struct VectorObj {
    jobject hdr;
    jint    size;
    jint    cap;
    jref   *elems;
} VectorObj;

typedef struct IntegerObj {
    jobject hdr;
    jint    value;
} IntegerObj;

typedef struct RandomObj {
    jobject hdr;
    uint64_t seed;
} RandomObj;

typedef struct ThrowableObj {
    jobject hdr;
    jref    message;    /* a String or null */
} ThrowableObj;

typedef struct ThreadObj {
    jobject hdr;
    jref    runnable;
} ThreadObj;

typedef struct ImageObj {
    jobject   hdr;
    jint      w, h;
    uint32_t *px;          /* ARGB8888 */
} ImageObj;

typedef struct GraphicsObj {
    jobject   hdr;
    uint32_t *fb;
    jint      w, h;
    uint32_t  color;
    jint      tx, ty;
    jint      cx, cy, cw, ch;
} GraphicsObj;

typedef struct PlayerObj {
    jobject hdr;
    void   *midi;
    jint    loop;
    jint    state;
} PlayerObj;

typedef struct Rec { int id, len; uint8_t *data; } Rec;
typedef struct RecordStoreObj {
    jobject hdr;
    char    name[64];
    Rec    *recs;
    int     num, cap;
    int     next_id;
} RecordStoreObj;
typedef struct EnumObj {
    jobject hdr;
    int    *ids;
    int     n, pos;
} EnumObj;

/* byte/data stream objects */
typedef struct StreamObj {
    jobject hdr;
    uint8_t *buf;       /* owned for output; borrowed for input */
    jint     len;
    jint     pos;
    jint     cap;
    jref     wrapped;   /* DataInput/OutputStream wrap an underlying stream */
    int      owns_buf;
} StreamObj;

void  *j_vfind_opt(const jclass *cls, const char *name, const char *desc);
jref   j_to_string(jref obj);             /* null-safe; calls toString() */

/* ---- string helpers -------------------------------------------------------- */
jref   j_string_from_chars(const jchar *chars, jint len);
jref   j_string_from_utf8(const char *utf8, int len /* -1 = strlen */);
char  *j_string_to_cstr(jref s);          /* malloc'd UTF-8, caller frees */
jint   j_string_length(jref s);

/* ---- exception throwing shortcuts ----------------------------------------- */
void   j_throw_class(const jclass *cls, const char *msg);
#define J_NPE()   j_throw_class(&CLASS_java_lang_RuntimeException, "NullPointerException")
#define J_AIOOBE(i) j_throw_class(&CLASS_java_lang_RuntimeException, "ArrayIndexOutOfBounds")

/* ---- runtime class metadata NOT declared by generated doomrpg.h ----------- */
extern const jclass CLASS_java_lang_Class;
extern const jclass CLASS_java_lang_Runtime;
extern const jclass CLASS_javax_microedition_lcdui_Display;
extern const jclass CLASS_java_io_PrintStream;
extern const jclass CLASS_javax_microedition_lcdui_Graphics;
extern const jclass CLASS_javax_microedition_lcdui_Image;
extern const jclass CLASS_javax_microedition_rms_RecordStore;
extern const jclass CLASS_javax_microedition_rms_RecordEnumeration;

/* ---- cross-module object factories ---------------------------------------- */
jref   j_input_stream_from_bytes(const uint8_t *data, int len);  /* ByteArrayInputStream */
jref   j_make_class_object(const jclass *target);                /* java.lang.Class */

/* a java.lang.Class instance wraps the jclass it represents */
typedef struct ClassObj { jobject hdr; const jclass *target; } ClassObj;

/* ---- assets (resources packed in the JAR) --------------------------------- */
int    assets_open(const char *jar_or_dir);
uint8_t *assets_get(const char *name, int *out_len);  /* borrowed bytes, or NULL */

/* ---- display / input (SDL2) ----------------------------------------------- */
int    display_init(int w, int h, int scale);
void   display_present(const uint32_t *argb /* w*h */);
void   display_pump(void);                 /* process events; updates key state */
int    display_should_quit(void);
int    display_key_state(void);            /* GameCanvas bitmask */
int    display_last_keycode(void);
void   display_shutdown(void);
void   display_set_canvas(jref canvas);    /* current Canvas; receives key events */

/* native game screen size (the phone's screen) */
#define SCREEN_W 128
#define SCREEN_H 128

/* ---- audio (winmm MIDI) ---------------------------------------------------- */
void   midi_init(void);
void  *midi_load(const uint8_t *smf, int len);   /* opaque player handle */
void   midi_play(void *handle, int loop);
void   midi_stop(void *handle);
void   midi_shutdown(void);

#endif /* DOOMRPG_RUNTIME_H */
