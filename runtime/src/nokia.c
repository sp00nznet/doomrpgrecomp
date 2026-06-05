/*
 * nokia.c -- com.nokia.mid.ui.DirectGraphics / DirectUtils.
 *
 * The Nokia-targeted ports (e.g. Doom RPG II 352x416) render their 3D view by
 * handing a packed 16-bit pixel buffer to DirectGraphics.drawPixels(short[]...)
 * rather than going through Image. We wrap the existing Graphics (and its
 * clip/translate-aware plot) and unpack the 16-bit formats to ARGB.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include <stdio.h>
#include <stdlib.h>

void j_gfx_plot(jref gr, jint x, jint y, uint32_t argb);   /* lcdui.c */

/* The game never does `new DirectGraphics` (it calls getDirectGraphics), so this
 * class isn't in the generated header; class_meta.c defines it. */
extern const jclass CLASS_com_nokia_mid_ui_DirectGraphics;

/* Nokia DirectGraphics pixel-format constants (subset the games use). */
#define NOKIA_TYPE_USHORT_4444_ARGB 4444
#define NOKIA_TYPE_USHORT_565_RGB    565
#define NOKIA_TYPE_USHORT_1555_ARGB 1555

jref m_com_nokia_mid_ui_DirectUtils__getDirectGraphics__Ljavax_microedition_lcdui_Graphics__Lcom_nokia_mid_ui_DirectGraphics(jref g) {
    DirectGraphicsObj *d = (DirectGraphicsObj *)j_new(&CLASS_com_nokia_mid_ui_DirectGraphics);
    d->graphics = (struct jobject *)g;
    return (jref)d;
}

/* drawPixels(short[] pix, boolean transparency, int offset, int scanlength,
 *            int x, int y, int width, int height, int manipulation, int format) */
void m_com_nokia_mid_ui_DirectGraphics__drawPixels__aSZIIIIIIII__V(
        jref this_, jref pix, jint transparency, jint off, jint scan,
        jint x, jint y, jint w, jint h, jint manipulation, jint format) {
    DirectGraphicsObj *d = (DirectGraphicsObj *)this_;
    if (!d->graphics || !pix) return;
    (void)transparency; (void)manipulation;   /* no flip/mirror support yet */
    const uint16_t *src = (const uint16_t *)J_ARRDATA(pix);
    jref g = (jref)d->graphics;
    for (jint j = 0; j < h; j++) for (jint i = 0; i < w; i++) {
        uint16_t v = src[off + j * scan + i];
        uint32_t argb;
        if (format == NOKIA_TYPE_USHORT_4444_ARGB) {
            uint32_t a = (v >> 12) & 0xF, r = (v >> 8) & 0xF, gg = (v >> 4) & 0xF, b = v & 0xF;
            argb = (a * 0x11u << 24) | (r * 0x11u << 16) | (gg * 0x11u << 8) | (b * 0x11u);
        } else if (format == NOKIA_TYPE_USHORT_1555_ARGB) {
            uint32_t a = (v >> 15) ? 0xFFu : 0u, r = (v >> 10) & 0x1F, gg = (v >> 5) & 0x1F, b = v & 0x1F;
            argb = (a << 24) | ((r * 255 / 31) << 16) | ((gg * 255 / 31) << 8) | (b * 255 / 31);
        } else {   /* default: 565 RGB, opaque */
            uint32_t r = (v >> 11) & 0x1F, gg = (v >> 5) & 0x3F, b = v & 0x1F;
            argb = 0xFF000000u | ((r * 255 / 31) << 16) | ((gg * 255 / 63) << 8) | (b * 255 / 31);
        }
        j_gfx_plot(g, x + i, y + j, argb);
    }
}
