/*
 * j2me_objects.h -- runtime base-class object layouts.
 *
 * Game classes extend exactly two runtime classes (besides Object): the MIDlet
 * `DoomRPG` and the GameCanvas `k`. Their generated instance fields begin right
 * after the corresponding base object struct, so the recompiler needs each
 * base's size as a compile-time constant -- provided here as J2ME_BASE_* macros
 * keyed by the mangled class name.
 *
 * The runtime owns everything inside these structs; generated code never reads
 * or writes base-class fields directly (it only calls base methods), so the
 * exact field set here can evolve without touching generated code.
 */
#ifndef DOOMRPG_J2ME_OBJECTS_H
#define DOOMRPG_J2ME_OBJECTS_H

#include <stdint.h>

/* forward: jobject is defined in jvm.h, which includes us last */
struct jobject;

/* javax.microedition.midlet.MIDlet */
typedef struct MIDletObj {
    struct jobject hdr;
    int            requested_destroy;
} MIDletObj;

/* javax.microedition.lcdui.Displayable -> Canvas -> game.GameCanvas */
typedef struct DisplayableObj {
    struct jobject hdr;
    int            width, height;
} DisplayableObj;

typedef struct CanvasObj {
    DisplayableObj base;
    int            full_screen;
    /* Plain-Canvas paint() path (Doom RPG II et al. -- not GameCanvas): the
     * runtime owns a framebuffer + a Graphics bound to it; repaint() calls the
     * subclass's paint(g) then presents. GameCanvas keeps its own pair below. */
    void          *offscreen;
    struct jobject *graphics;
} CanvasObj;

typedef struct GameCanvasObj {
    CanvasObj      base;
    void          *offscreen;    /* backing framebuffer (runtime-owned) */
    struct jobject *graphics;    /* cached Graphics object bound to offscreen */
    int            key_state;    /* GameCanvas.getKeyStates() bitmask */
} GameCanvasObj;

/* javax.microedition.lcdui.Font -- the runtime renders all system text with one
 * built-in 8x8 bitmap font, so a Font just remembers its requested attributes. */
typedef struct FontObj {
    struct jobject hdr;
    int            face, style, size;
} FontObj;

/* com.nokia.mid.ui.DirectGraphics -- a thin wrapper over a Graphics (used by
 * the Nokia-targeted ports, e.g. Doom RPG II, for 16-bit drawPixels blits). */
typedef struct DirectGraphicsObj {
    struct jobject hdr;
    struct jobject *graphics;    /* the wrapped Graphics object */
} DirectGraphicsObj;

#define J2ME_BASE_javax_microedition_midlet_MIDlet \
    ((uint32_t)sizeof(MIDletObj))
#define J2ME_BASE_javax_microedition_lcdui_Displayable \
    ((uint32_t)sizeof(DisplayableObj))
#define J2ME_BASE_javax_microedition_lcdui_Canvas \
    ((uint32_t)sizeof(CanvasObj))
#define J2ME_BASE_javax_microedition_lcdui_game_GameCanvas \
    ((uint32_t)sizeof(GameCanvasObj))

#endif /* DOOMRPG_J2ME_OBJECTS_H */
