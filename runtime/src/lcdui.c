/*
 * lcdui.c -- javax.microedition.lcdui: Display, Canvas, GameCanvas, Graphics,
 * Image, Alert. Everything renders into a 128x128 ARGB framebuffer; the
 * GameCanvas owns the on-screen one, and flushGraphics() pushes it to SDL.
 *
 * Coordinate model matches MIDP: Graphics carries a translate (tx,ty) and a
 * clip rectangle expressed in translated coordinates.
 *
 * Image decoding (PNG) is not done yet -- createImage() returns a correctly
 * sized placeholder so drawing code runs; real decode is the next pass.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t rgb_to_argb(jint rgb) { return 0xFF000000u | ((uint32_t)rgb & 0xFFFFFF); }

static jref make_graphics(uint32_t *fb, jint w, jint h) {
    GraphicsObj *g = (GraphicsObj *)j_new(&CLASS_javax_microedition_lcdui_Graphics);
    g->fb = fb; g->w = w; g->h = h; g->color = 0xFF000000u;
    g->tx = g->ty = 0; g->cx = g->cy = 0; g->cw = w; g->ch = h;
    return (jref)g;
}

static inline void plot(GraphicsObj *g, jint ux, jint uy, uint32_t argb) {
    if (ux < g->cx || uy < g->cy || ux >= g->cx + g->cw || uy >= g->cy + g->ch) return;
    jint dx = ux + g->tx, dy = uy + g->ty;
    if (dx < 0 || dy < 0 || dx >= g->w || dy >= g->h) return;
    g->fb[dy * g->w + dx] = argb;
}

/* ===== Graphics ============================================================ */
void m_javax_microedition_lcdui_Graphics__setColor__I__V(jref this_, jint rgb) {
    ((GraphicsObj *)this_)->color = rgb_to_argb(rgb);
}
void m_javax_microedition_lcdui_Graphics__fillRect__IIII__V(jref this_, jint x, jint y, jint w, jint h) {
    GraphicsObj *g = (GraphicsObj *)this_;
    for (jint j = 0; j < h; j++) for (jint i = 0; i < w; i++) plot(g, x + i, y + j, g->color);
}
void m_javax_microedition_lcdui_Graphics__drawRect__IIII__V(jref this_, jint x, jint y, jint w, jint h) {
    GraphicsObj *g = (GraphicsObj *)this_;
    for (jint i = 0; i <= w; i++) { plot(g, x + i, y, g->color); plot(g, x + i, y + h, g->color); }
    for (jint j = 0; j <= h; j++) { plot(g, x, y + j, g->color); plot(g, x + w, y + j, g->color); }
}
void m_javax_microedition_lcdui_Graphics__drawLine__IIII__V(jref this_, jint x0, jint y0, jint x1, jint y1) {
    GraphicsObj *g = (GraphicsObj *)this_;
    jint dx = x1 - x0, dy = y1 - y0;
    jint adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    jint steps = adx > ady ? adx : ady;
    if (steps == 0) { plot(g, x0, y0, g->color); return; }
    for (jint i = 0; i <= steps; i++)
        plot(g, x0 + dx * i / steps, y0 + dy * i / steps, g->color);
}
void m_javax_microedition_lcdui_Graphics__translate__II__V(jref this_, jint x, jint y) {
    GraphicsObj *g = (GraphicsObj *)this_; g->tx += x; g->ty += y;
}
jint m_javax_microedition_lcdui_Graphics__getTranslateX____I(jref this_) { return ((GraphicsObj *)this_)->tx; }
jint m_javax_microedition_lcdui_Graphics__getTranslateY____I(jref this_) { return ((GraphicsObj *)this_)->ty; }
void m_javax_microedition_lcdui_Graphics__setClip__IIII__V(jref this_, jint x, jint y, jint w, jint h) {
    GraphicsObj *g = (GraphicsObj *)this_; g->cx = x; g->cy = y; g->cw = w; g->ch = h;
}
jint m_javax_microedition_lcdui_Graphics__getClipX____I(jref this_) { return ((GraphicsObj *)this_)->cx; }
jint m_javax_microedition_lcdui_Graphics__getClipY____I(jref this_) { return ((GraphicsObj *)this_)->cy; }
jint m_javax_microedition_lcdui_Graphics__getClipWidth____I(jref this_) { return ((GraphicsObj *)this_)->cw; }
jint m_javax_microedition_lcdui_Graphics__getClipHeight____I(jref this_) { return ((GraphicsObj *)this_)->ch; }

/* MIDP anchor bits (Graphics) and Sprite transform codes (Image.drawRegion). */
enum { A_HCENTER = 1, A_VCENTER = 2, A_LEFT = 4, A_RIGHT = 8,
       A_TOP = 16, A_BOTTOM = 32, A_BASELINE = 64 };

/* MIDP places the image so its anchor point coincides with (dx,dy); convert
 * that to a top-left origin given the (possibly transform-swapped) out dims. */
static void anchor_origin(jint anchor, jint dx, jint dy, jint ow, jint oh,
                          jint *tx, jint *ty) {
    if (anchor & A_HCENTER)       *tx = dx - ow / 2;
    else if (anchor & A_RIGHT)    *tx = dx - ow;
    else                          *tx = dx;                 /* LEFT / default */
    if (anchor & A_VCENTER)       *ty = dy - oh / 2;
    else if (anchor & (A_BOTTOM | A_BASELINE)) *ty = dy - oh;
    else                          *ty = dy;                 /* TOP / default */
}

/* Blit an sw x sh source region with origin at top-left (tx,ty), applying one
 * of the 8 MIDP Sprite transforms (0=none,1=vmirror,2=hmirror,3=rot180,
 * 4=transpose,5=rot90,6=rot270,7=anti-transpose). */
static void blit_xform(GraphicsObj *g, ImageObj *img, jint sx, jint sy, jint sw, jint sh,
                       jint transform, jint tx, jint ty) {
    if (!img || !img->px) return;
    for (jint j = 0; j < sh; j++) for (jint i = 0; i < sw; i++) {
        jint ix = sx + i, iy = sy + j;
        if (ix < 0 || iy < 0 || ix >= img->w || iy >= img->h) continue;
        uint32_t p = img->px[iy * img->w + ix];
        if (!(p >> 24)) continue;                  /* skip transparent */
        jint ox, oy;
        switch (transform) {
            default:
            case 0: ox = i;          oy = j;          break;
            case 1: ox = i;          oy = sh - 1 - j; break;
            case 2: ox = sw - 1 - i; oy = j;          break;
            case 3: ox = sw - 1 - i; oy = sh - 1 - j; break;
            case 4: ox = j;          oy = i;          break;
            case 5: ox = sh - 1 - j; oy = i;          break;
            case 6: ox = j;          oy = sw - 1 - i; break;
            case 7: ox = sh - 1 - j; oy = sw - 1 - i; break;
        }
        plot(g, tx + ox, ty + oy, p);
    }
}
void m_javax_microedition_lcdui_Graphics__drawImage__Ljavax_microedition_lcdui_ImageIII__V(
        jref this_, jref img, jint x, jint y, jint anchor) {
    ImageObj *im = (ImageObj *)img;
    if (!im) return;
    jint tx, ty;
    anchor_origin(anchor, x, y, im->w, im->h, &tx, &ty);
    blit_xform((GraphicsObj *)this_, im, 0, 0, im->w, im->h, 0, tx, ty);
}
void m_javax_microedition_lcdui_Graphics__drawRegion__Ljavax_microedition_lcdui_ImageIIIIIIII__V(
        jref this_, jref img, jint sx, jint sy, jint sw, jint sh, jint transform,
        jint dx, jint dy, jint anchor) {
    ImageObj *im = (ImageObj *)img;
    if (!im) return;
    /* transforms 4..7 rotate by 90/270, swapping the on-screen dimensions */
    jint ow = (transform >= 4) ? sh : sw;
    jint oh = (transform >= 4) ? sw : sh;
    jint tx, ty;
    anchor_origin(anchor, dx, dy, ow, oh, &tx, &ty);
    blit_xform((GraphicsObj *)this_, im, sx, sy, sw, sh, transform, tx, ty);
}
void m_javax_microedition_lcdui_Graphics__drawRGB__aIIIIIIIZ__V(
        jref this_, jref rgb, jint off, jint scan, jint x, jint y, jint w, jint h, jint alpha) {
    GraphicsObj *g = (GraphicsObj *)this_;
    jint *src = (jint *)J_ARRDATA(rgb);
    for (jint j = 0; j < h; j++) for (jint i = 0; i < w; i++) {
        uint32_t p = (uint32_t)src[off + j * scan + i];
        if (!alpha) p |= 0xFF000000u;
        if (p >> 24) plot(g, x + i, y + j, p);
    }
}

/* ===== Image =============================================================== */
static jref make_image(jint w, jint h) {
    ImageObj *im = (ImageObj *)j_new(&CLASS_javax_microedition_lcdui_Image);
    im->w = w; im->h = h;
    im->px = (uint32_t *)j_alloc((size_t)w * h * sizeof(uint32_t));
    return (jref)im;
}
jref m_javax_microedition_lcdui_Image__createImage__Ljava_lang_String__Ljavax_microedition_lcdui_Image(jref name) {
    char *n = j_string_to_cstr(name);
    const char *p = (*n == '/') ? n + 1 : n;
    int len = 0;
    uint8_t *bytes = assets_get(p, &len);
    free(n);
    int w = 0, h = 0;
    uint32_t *px = bytes ? png_decode(bytes, len, &w, &h) : NULL;
    if (!px) return make_image(16, 16);   /* decode failed: empty placeholder */
    jref img = make_image(w, h);
    memcpy(((ImageObj *)img)->px, px, (size_t)w * h * sizeof(uint32_t));
    free(px);
    return img;
}

/* ===== Canvas ============================================================== */
void m_javax_microedition_lcdui_Canvas__setFullScreenMode__Z__V(jref this_, jint full) {
    (void)this_; (void)full;
}
/* map device key codes to MIDP game actions (UP/DOWN/LEFT/RIGHT/FIRE = 1..5,
 * matching Canvas constants used by the game) */
jint m_javax_microedition_lcdui_Canvas__getGameAction__I__I(jref this_, jint key) {
    (void)this_; return key;   /* our display layer already reports game actions */
}

/* ===== GameCanvas ========================================================== */
void m_javax_microedition_lcdui_game_GameCanvas___init___Z__V(jref this_, jint suppressKeys) {
    (void)suppressKeys;
    GameCanvasObj *gc = (GameCanvasObj *)this_;
    gc->base.base.width = SCREEN_W; gc->base.base.height = SCREEN_H;
    gc->offscreen = j_alloc((size_t)SCREEN_W * SCREEN_H * sizeof(uint32_t));
    gc->graphics = (struct jobject *)make_graphics((uint32_t *)gc->offscreen, SCREEN_W, SCREEN_H);
}
jref m_javax_microedition_lcdui_game_GameCanvas__getGraphics____Ljavax_microedition_lcdui_Graphics(jref this_) {
    return (jref)((GameCanvasObj *)this_)->graphics;
}
void m_javax_microedition_lcdui_game_GameCanvas__flushGraphics____V(jref this_) {
    GameCanvasObj *gc = (GameCanvasObj *)this_;
    const uint32_t *fb = (const uint32_t *)gc->offscreen;
    display_present(fb);
    display_pump();
    /* Debug: if DOOMRPG_DUMP is set, write the latest frame as a PPM there.
     * If DOOMRPG_DUMPDIR is set, write every Nth frame as a numbered PPM into
     * that directory (frame00000.ppm, ...) so a whole boot sequence is captured.
     * DOOMRPG_DUMPN controls the sampling stride (default 8); DOOMRPG_DUMPMAX
     * caps the number of files written (default 400). */
    static int s_frame = 0, s_written = 0;
    const char *dump = getenv("DOOMRPG_DUMP");
    if (dump) {
        FILE *f = fopen(dump, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
            for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
                uint32_t p = fb[i];
                fputc((p >> 16) & 0xFF, f); fputc((p >> 8) & 0xFF, f); fputc(p & 0xFF, f);
            }
            fclose(f);
        }
    }
    const char *dumpdir = getenv("DOOMRPG_DUMPDIR");
    if (dumpdir) {
        const char *sn = getenv("DOOMRPG_DUMPN");
        const char *sm = getenv("DOOMRPG_DUMPMAX");
        int stride = sn ? atoi(sn) : 8;
        int cap = sm ? atoi(sm) : 400;
        if (stride < 1) stride = 1;
        if ((s_frame++ % stride) == 0 && s_written < cap) {
            char path[512];
            snprintf(path, sizeof path, "%s/frame%05d.ppm", dumpdir, s_written++);
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
                for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
                    uint32_t p = fb[i];
                    fputc((p >> 16) & 0xFF, f); fputc((p >> 8) & 0xFF, f); fputc(p & 0xFF, f);
                }
                fclose(f);
            }
        }
    }
}

/* ===== Display ============================================================= */
static jobject g_display;
jref m_javax_microedition_lcdui_Display__getDisplay__Ljavax_microedition_midlet_MIDlet__Ljavax_microedition_lcdui_Display(jref midlet) {
    (void)midlet;
    if (!g_display.cls) g_display.cls = &CLASS_javax_microedition_lcdui_Display;
    return &g_display;
}
void m_javax_microedition_lcdui_Display__setCurrent__Ljavax_microedition_lcdui_Displayable__V(jref this_, jref d) {
    (void)this_;
    display_set_canvas(d);   /* route key events to this Canvas's keyPressed/Released */
}
jint m_javax_microedition_lcdui_Display__vibrate__I__Z(jref this_, jint ms) { (void)this_; (void)ms; return 1; }

/* ===== Alert / AlertType =================================================== */
void m_javax_microedition_lcdui_Alert___init___Ljava_lang_String__V(jref this_, jref s) { (void)this_; (void)s; }
void m_javax_microedition_lcdui_Alert__setString__Ljava_lang_String__V(jref this_, jref s) { (void)this_; (void)s; }
void m_javax_microedition_lcdui_Alert__setTimeout__I__V(jref this_, jint t) { (void)this_; (void)t; }
void m_javax_microedition_lcdui_Alert__setType__Ljavax_microedition_lcdui_AlertType__V(jref this_, jref t) { (void)this_; (void)t; }
