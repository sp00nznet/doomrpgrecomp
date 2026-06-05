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

/* clipRect intersects the new rect with the current clip (MIDP semantics). */
void m_javax_microedition_lcdui_Graphics__clipRect__IIII__V(jref this_, jint x, jint y, jint w, jint h) {
    GraphicsObj *g = (GraphicsObj *)this_;
    jint x0 = g->cx > x ? g->cx : x, y0 = g->cy > y ? g->cy : y;
    jint x1 = (g->cx + g->cw) < (x + w) ? (g->cx + g->cw) : (x + w);
    jint y1 = (g->cy + g->ch) < (y + h) ? (g->cy + g->ch) : (y + h);
    g->cx = x0; g->cy = y0; g->cw = x1 > x0 ? x1 - x0 : 0; g->ch = y1 > y0 ? y1 - y0 : 0;
}
jint m_javax_microedition_lcdui_Graphics__getColor____I(jref this_) {
    return (jint)(((GraphicsObj *)this_)->color & 0xFFFFFF);
}
void m_javax_microedition_lcdui_Graphics__setColor__III__V(jref this_, jint r, jint g, jint b) {
    ((GraphicsObj *)this_)->color = 0xFF000000u | (((uint32_t)r & 0xFF) << 16) |
                                    (((uint32_t)g & 0xFF) << 8) | ((uint32_t)b & 0xFF);
}
/* fillArc: approximate as a filled ellipse over the bounding box (the arc
 * sweep is ignored -- good enough for the round HUD elements that use it). */
void m_javax_microedition_lcdui_Graphics__fillArc__IIIIII__V(jref this_, jint x, jint y, jint w, jint h, jint sa, jint aa) {
    GraphicsObj *g = (GraphicsObj *)this_; (void)sa; (void)aa;
    jint rx = w / 2, ry = h / 2; if (rx <= 0 || ry <= 0) return;
    jint cx = x + rx, cy = y + ry;
    for (jint j = -ry; j <= ry; j++) for (jint i = -rx; i <= rx; i++)
        if ((long)i * i * ry * ry + (long)j * j * rx * rx <= (long)rx * rx * ry * ry)
            plot(g, cx + i, cy + j, g->color);
}

/* Non-static blit helper so other runtime units (nokia.c) can draw into a
 * Graphics honoring its translate + clip. */
void j_gfx_plot(jref gr, jint x, jint y, uint32_t argb) { plot((GraphicsObj *)gr, x, y, argb); }
uint32_t j_gfx_color(jref gr) { return ((GraphicsObj *)gr)->color; }

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
/* createImage(w,h): a blank MUTABLE image (MIDP starts it filled white). */
jref m_javax_microedition_lcdui_Image__createImage__II__Ljavax_microedition_lcdui_Image(jint w, jint h) {
    jref img = make_image(w, h);
    ImageObj *im = (ImageObj *)img;
    for (jint i = 0; i < w * h; i++) im->px[i] = 0xFFFFFFFFu;
    return img;
}
/* createImage(byte[],off,len): decode a PNG held in a byte array. */
jref m_javax_microedition_lcdui_Image__createImage__aBII__Ljavax_microedition_lcdui_Image(jref bytes, jint off, jint len) {
    if (!bytes) return make_image(1, 1);
    uint8_t *data = (uint8_t *)J_ARRDATA(bytes) + off;
    int w = 0, h = 0;
    uint32_t *px = png_decode(data, len, &w, &h);
    if (!px) return make_image(1, 1);
    jref img = make_image(w, h);
    memcpy(((ImageObj *)img)->px, px, (size_t)w * h * sizeof(uint32_t));
    free(px);
    return img;
}
jref m_javax_microedition_lcdui_Image__getGraphics____Ljavax_microedition_lcdui_Graphics(jref this_) {
    ImageObj *im = (ImageObj *)this_;
    return make_graphics(im->px, im->w, im->h);   /* draw straight into the image */
}
jint m_javax_microedition_lcdui_Image__getWidth____I(jref this_)  { return ((ImageObj *)this_)->w; }
jint m_javax_microedition_lcdui_Image__getHeight____I(jref this_) { return ((ImageObj *)this_)->h; }
void m_javax_microedition_lcdui_Image__getRGB__aIIIIIII__V(
        jref this_, jref rgb, jint off, jint scan, jint x, jint y, jint w, jint h) {
    ImageObj *im = (ImageObj *)this_;
    jint *dst = (jint *)J_ARRDATA(rgb);
    for (jint j = 0; j < h; j++) for (jint i = 0; i < w; i++) {
        jint sxx = x + i, syy = y + j;
        uint32_t p = (sxx >= 0 && syy >= 0 && sxx < im->w && syy < im->h) ? im->px[syy * im->w + sxx] : 0;
        dst[off + j * scan + i] = (jint)p;
    }
}

/* ===== Canvas ============================================================== */
/* Plain Canvas (not GameCanvas): allocate a paint() framebuffer + Graphics. */
void m_javax_microedition_lcdui_Canvas___init_____V(jref this_) {
    CanvasObj *c = (CanvasObj *)this_;
    c->base.width = g_screen_w; c->base.height = g_screen_h;
    c->offscreen = j_alloc((size_t)g_screen_w * g_screen_h * sizeof(uint32_t));
    c->graphics = (struct jobject *)make_graphics((uint32_t *)c->offscreen, g_screen_w, g_screen_h);
}
/* repaint/serviceRepaints: synchronously call the subclass paint(g), then push
 * the framebuffer to SDL (our model is single-threaded + cooperative). */
static void canvas_paint_present(jref this_) {
    CanvasObj *c = (CanvasObj *)this_;
    if (!c->offscreen) return;
    typedef void (*pfn)(jref, jref);
    pfn paint = (pfn)j_vfind_opt(this_->cls, "paint", "(Ljavax/microedition/lcdui/Graphics;)V");
    if (paint && c->graphics) paint(this_, (jref)c->graphics);
    display_present((const uint32_t *)c->offscreen);
    display_pump();
    /* Optional headless capture: DOOMRPG_DUMPDIR -> every Nth frame as PPM (the
     * GameCanvas path has the same hook; Canvas-based games use this one). */
    const char *dumpdir = getenv("DOOMRPG_DUMPDIR");
    if (dumpdir) {
        static int sf = 0, sw = 0;
        const char *sn = getenv("DOOMRPG_DUMPN"); int stride = sn ? atoi(sn) : 8;
        if (stride < 1) stride = 1;
        if ((sf++ % stride) == 0 && sw < 400) {
            char path[512]; snprintf(path, sizeof path, "%s/frame%05d.ppm", dumpdir, sw++);
            FILE *f = fopen(path, "wb");
            if (f) {
                const uint32_t *fb = (const uint32_t *)c->offscreen;
                fprintf(f, "P6\n%d %d\n255\n", g_screen_w, g_screen_h);
                for (int i = 0; i < g_screen_w * g_screen_h; i++)
                    { uint32_t p = fb[i]; fputc((p>>16)&0xFF,f); fputc((p>>8)&0xFF,f); fputc(p&0xFF,f); }
                fclose(f);
            }
        }
    }
}
void m_javax_microedition_lcdui_Canvas__repaint____V(jref this_)        { canvas_paint_present(this_); }
void m_javax_microedition_lcdui_Canvas__serviceRepaints____V(jref this_){ canvas_paint_present(this_); }
jint m_javax_microedition_lcdui_Canvas__getWidth____I(jref this_)  { return ((CanvasObj *)this_)->base.width; }
jint m_javax_microedition_lcdui_Canvas__getHeight____I(jref this_) { return ((CanvasObj *)this_)->base.height; }
jint m_javax_microedition_lcdui_Displayable__isShown____Z(jref this_) { (void)this_; return 1; }
void m_javax_microedition_lcdui_Canvas__setFullScreenMode__Z__V(jref this_, jint full) {
    (void)this_; (void)full;
}
/* Map the device key codes our display layer emits (see display.c) to the
 * standard MIDP Canvas game-action constants. The game's getGameAction()
 * consumers (e.g. k.a(I)I) compare against UP=1/DOWN=6/LEFT=2/RIGHT=5/FIRE=8,
 * so returning the raw key here would make every menu input a no-op. */
jint m_javax_microedition_lcdui_Canvas__getGameAction__I__I(jref this_, jint key) {
    (void)this_;
    switch (key) {
        case -1: return 1;   /* KEY_UP    -> Canvas.UP    */
        case -2: return 6;   /* KEY_DOWN  -> Canvas.DOWN  */
        case -3: return 2;   /* KEY_LEFT  -> Canvas.LEFT  */
        case -4: return 5;   /* KEY_RIGHT -> Canvas.RIGHT */
        case -5: return 8;   /* KEY_FIRE  -> Canvas.FIRE  */
        default: return 0;   /* digits / soft keys: no game action */
    }
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
/* GameCanvas-named key helpers (Orcs & Elves II super-calls these). */
jint m_javax_microedition_lcdui_game_GameCanvas__getGameAction__I__I(jref this_, jint key) {
    return m_javax_microedition_lcdui_Canvas__getGameAction__I__I(this_, key);
}
jint m_javax_microedition_lcdui_game_GameCanvas__getKeyCode__I__I(jref this_, jint action) {
    (void)this_;
    switch (action) { case 1: return -1; case 6: return -2; case 2: return -3;
                      case 5: return -4; case 8: return -5; default: return 0; }
}
jref m_javax_microedition_lcdui_game_GameCanvas__getKeyName__I__Ljava_lang_String(jref this_, jint key) {
    (void)this_; (void)key; return j_strlit("KEY");
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
jint m_javax_microedition_lcdui_Display__vibrate__I__Z(jref this_, jint ms) {
    (void)this_;
    extern void devinput_rumble(int ms);   /* devinput.c: pad rumble */
    devinput_rumble(ms);
    return 1;
}

/* ===== Alert / AlertType =================================================== */
void m_javax_microedition_lcdui_Alert___init___Ljava_lang_String__V(jref this_, jref s) { (void)this_; (void)s; }
void m_javax_microedition_lcdui_Alert__setString__Ljava_lang_String__V(jref this_, jref s) { (void)this_; (void)s; }
void m_javax_microedition_lcdui_Alert__setTimeout__I__V(jref this_, jint t) { (void)this_; (void)t; }
void m_javax_microedition_lcdui_Alert__setType__Ljavax_microedition_lcdui_AlertType__V(jref this_, jref t) { (void)this_; (void)t; }
