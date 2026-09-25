/*
 * EGL host harness: runs egl/egl_riscos.c against a host-built OSMesa and
 * the fake RISC OS in fake_riscos.c, and checks what reaches the "screen".
 * See README.md for the build line.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/mman.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/eglext_riscos.h>
#include <GL/gl.h>

#include "fake_riscos.h"

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define RGB(p) ((p) & 0x00FFFFFFu)
/* 0x00BBGGRR (TBGR) / 0x00RRGGBB (TRGB) values */
#define RED_TBGR   0x0000FFu
#define BLUE_TBGR  0xFF0000u
#define GREEN      0x00FF00u
#define RED_TRGB   0xFF0000u

static EGLDisplay dpy;

static EGLConfig choose(EGLint layout_visual, EGLint depth, EGLint surface_type)
{
    EGLint attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, surface_type,
                       EGL_DEPTH_SIZE, depth, EGL_NONE };
    EGLConfig cfgs[16];
    EGLint n = 0, i, v;
    eglChooseConfig(dpy, attrs, cfgs, 16, &n);
    for (i = 0; i < n; i++) {
        eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &v);
        if (v == layout_visual) return cfgs[i];
    }
    return NULL;
}

static void clear(float r, float g, float b)
{
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

/* Count screen pixels (x, y from top, pixels) in a box equal to v. */
static int box_is(int x0, int y0, int w, int h, unsigned int v)
{
    int x, y;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            if (RGB(fake_screen_pixel(x, y)) != v) return 0;
    return 1;
}

static void test_basics(void)
{
    EGLint major = 0, minor = 0, n = 0, v;
    EGLConfig cfgs[16];
    const char *ext;

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    CHECK(dpy != EGL_NO_DISPLAY, "eglGetDisplay");
    CHECK(eglGetDisplay((EGLNativeDisplayType) 5) == EGL_NO_DISPLAY, "only the default display");
    CHECK(!eglGetConfigs(dpy, cfgs, 16, &n) && eglGetError() == EGL_NOT_INITIALIZED, "not initialised");
    CHECK(eglInitialize(dpy, &major, &minor) && major == 1 && minor == 4, "initialise 1.4");
    CHECK(strcmp(eglQueryString(dpy, EGL_CLIENT_APIS), "OpenGL") == 0, "client APIs");
    ext = eglQueryString(dpy, EGL_EXTENSIONS);
    CHECK(ext && strstr(ext, "EGL_RISCOS_wimp_window"), "extension string");
    CHECK(eglGetConfigs(dpy, NULL, 0, &n) && n == 8, "8 configs (got %d)", n);

    /* The spec's default EGL_RENDERABLE_TYPE is ES: desktop GL configs don't match. */
    CHECK(eglChooseConfig(dpy, NULL, cfgs, 16, &n) && n == 0, "default choose = ES = none (%d)", n);
    {
        EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
        CHECK(eglChooseConfig(dpy, a, cfgs, 16, &n) && n == 8, "GL configs (%d)", n);
        eglGetConfigAttrib(dpy, cfgs[0], EGL_DEPTH_SIZE, &v);
        CHECK(v == 0, "sorted: smallest depth first (%d)", v);
        eglGetConfigAttrib(dpy, cfgs[0], EGL_NATIVE_VISUAL_ID, &v);
        CHECK(v == EGL_RISCOS_VISUAL_TBGR, "screen's colour order first");
    }
    {
        EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_DEPTH_SIZE, 17, EGL_STENCIL_SIZE, 1, EGL_NONE };
        CHECK(eglChooseConfig(dpy, a, cfgs, 16, &n) && n == 2, "depth>=17 stencil>=1 (%d)", n);
        eglGetConfigAttrib(dpy, cfgs[0], EGL_DEPTH_SIZE, &v);
        CHECK(v == 24, "depth 24");
    }
    {
        EGLint a[] = { EGL_CONFIG_ID, 3, EGL_RENDERABLE_TYPE, EGL_OPENVG_BIT, EGL_NONE };
        CHECK(eglChooseConfig(dpy, a, cfgs, 16, &n) && n == 1, "CONFIG_ID ignores the rest");
        eglGetConfigAttrib(dpy, cfgs[0], EGL_CONFIG_ID, &v);
        CHECK(v == 3, "config id 3");
    }
    {
        EGLint a[] = { EGL_FRONT_BUFFER_AUTO_REFRESH_ANDROID, 1, EGL_NONE };
        CHECK(!eglChooseConfig(dpy, a, cfgs, 16, &n) && eglGetError() == EGL_BAD_ATTRIBUTE, "bad attribute");
    }
    CHECK(!eglBindAPI(EGL_OPENGL_ES_API) && eglGetError() == EGL_BAD_PARAMETER, "no GLES yet");
    CHECK(eglBindAPI(EGL_OPENGL_API) && eglQueryAPI() == EGL_OPENGL_API, "bind GL");
    CHECK(eglGetProcAddress("glClear") != NULL, "GetProcAddress gl");
    CHECK(eglGetProcAddress("eglRedrawWindowRISCOS") ==
          (__eglMustCastToProperFunctionPointerType) eglRedrawWindowRISCOS, "GetProcAddress RISC OS ext");
    CHECK(eglGetProcAddress("eglNoSuchThing") == NULL, "unknown egl function");
}

static void test_pbuffer(void)
{
    EGLConfig cfg = choose(EGL_RISCOS_VISUAL_TBGR, 24, EGL_PBUFFER_BIT);
    EGLint pa[] = { EGL_WIDTH, 64, EGL_HEIGHT, 32, EGL_NONE };
    EGLint ca33[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
                      EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE };
    EGLSurface pb;
    EGLContext ctx;
    unsigned char px[4];
    EGLint v;
    const char *ver;

    pb = eglCreatePbufferSurface(dpy, cfg, pa);
    CHECK(pb != EGL_NO_SURFACE, "pbuffer");
    CHECK(eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ca33) == EGL_NO_CONTEXT &&
          eglGetError() == EGL_BAD_MATCH, "GL 3.3 core refused");
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    CHECK(ctx != EGL_NO_CONTEXT, "context");
    CHECK(!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx) && eglGetError() == EGL_BAD_MATCH,
          "surfaceless refused");
    CHECK(eglMakeCurrent(dpy, pb, pb, ctx), "make current pbuffer");
    ver = (const char *) glGetString(GL_VERSION);
    CHECK(ver && strncmp(ver, "2.1", 3) == 0, "GL 2.1 (%s)", ver ? ver : "null");
    eglQuerySurface(dpy, pb, EGL_WIDTH, &v);
    CHECK(v == 64, "pbuffer width");
    {
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        CHECK(vp[2] == 64 && vp[3] == 32, "initial viewport = surface");
    }
    clear(1, 0, 0);
    glReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0, "pbuffer red (%d %d %d)", px[0], px[1], px[2]);
    CHECK(eglSwapBuffers(dpy, pb), "swap pbuffer = no-op");

    /* destroy while current: deferred until released */
    CHECK(eglDestroySurface(dpy, pb), "destroy current pbuffer");
    CHECK(eglGetCurrentSurface(EGL_DRAW) == pb, "still current");
    CHECK(!eglQuerySurface(dpy, pb, EGL_WIDTH, &v) && eglGetError() == EGL_BAD_SURFACE, "handle dead to the app");
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "release");
    CHECK(eglGetCurrentContext() == EGL_NO_CONTEXT, "released");
    CHECK(eglDestroyContext(dpy, ctx), "destroy context");
}

static int *make_sprite(int w, int h, int trgb)
{
    /* A 32bpp sprite in its own area; header layout as OS_SpriteOp makes. */
    int *area = malloc(16 + 44 + w * h * 4);
    int *spr = area + 4;
    static int sel[] = { 1, 0, 0, 5, -1, 0, 0x4000, -1 };
    area[0] = 16 + 44 + w * h * 4; area[1] = 1; area[2] = 16; area[3] = area[0];
    memset(spr, 0, 44 + w * h * 4);
    spr[0] = 44 + w * h * 4;
    spr[4] = w - 1; spr[5] = h - 1; spr[6] = 0; spr[7] = 31; spr[8] = 44; spr[9] = 44;
    spr[10] = trgb ? (int) (long) sel : (1 | (90 << 1) | (90 << 14) | (6 << 27));
    return spr;
}

static void test_pixmap(void)
{
    int *spr = make_sprite(16, 8, 0), *spr2 = make_sprite(16, 8, 1), *spr8;
    EGLConfig tbgr = choose(EGL_RISCOS_VISUAL_TBGR, 0, EGL_PIXMAP_BIT);
    EGLConfig trgb = choose(EGL_RISCOS_VISUAL_TRGB, 0, EGL_PIXMAP_BIT);
    EGLContext ctx = eglCreateContext(dpy, tbgr, EGL_NO_CONTEXT, NULL);
    EGLContext ctx2 = eglCreateContext(dpy, trgb, EGL_NO_CONTEXT, NULL);
    EGLSurface ps, ps2;
    EGLConfig cfgs[16];
    EGLint n, v;
    unsigned int *pix = (unsigned int *) ((char *) spr + 44);
    unsigned int *pix2 = (unsigned int *) ((char *) spr2 + 44);

    {
        EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, EGL_PIXMAP_BIT,
                       EGL_MATCH_NATIVE_PIXMAP, (EGLint) (long) spr2, EGL_NONE };
        CHECK(eglChooseConfig(dpy, a, cfgs, 16, &n) && n == 4, "MATCH_NATIVE_PIXMAP (%d)", n);
        eglGetConfigAttrib(dpy, cfgs[0], EGL_NATIVE_VISUAL_ID, &v);
        CHECK(v == EGL_RISCOS_VISUAL_TRGB, "matched TRGB");
    }
    ps = eglCreatePixmapSurface(dpy, tbgr, spr, NULL);
    CHECK(ps != EGL_NO_SURFACE, "pixmap surface");
    CHECK(eglCreatePixmapSurface(dpy, tbgr, spr2, NULL) == EGL_NO_SURFACE &&
          eglGetError() == EGL_BAD_MATCH, "colour order mismatch refused");
    spr8 = make_sprite(16, 8, 0);
    spr8[10] = 28;                              /* an 8bpp numbered mode */
    CHECK(eglCreatePixmapSurface(dpy, tbgr, spr8, NULL) == EGL_NO_SURFACE &&
          eglGetError() == EGL_BAD_NATIVE_PIXMAP, "8bpp sprite refused");

    CHECK(eglMakeCurrent(dpy, ps, ps, ctx), "current pixmap");
    clear(0, 1, 0);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 7, 1, 1);                      /* GL row 7 = top row of the sprite */
    clear(1, 0, 0);
    glDisable(GL_SCISSOR_TEST);
    glFinish();
    CHECK(RGB(pix[0]) == RED_TBGR, "sprite top-left red (%08x)", pix[0]);
    CHECK(RGB(pix[16 * 7]) == GREEN, "sprite bottom-left green");
    eglQuerySurface(dpy, ps, EGL_RENDER_BUFFER, &v);
    CHECK(v == EGL_SINGLE_BUFFER, "pixmap single buffered");
    CHECK(!eglMakeCurrent(dpy, ps, ps, ctx2) && eglGetError() == EGL_BAD_MATCH, "ctx/surface order mismatch");

    /* eglCopyBuffers swaps R and B into a TRGB sprite */
    CHECK(eglCopyBuffers(dpy, ps, spr2), "copy buffers");
    CHECK(RGB(pix2[0]) == RED_TRGB, "copied top-left red in TRGB (%08x)", pix2[0]);

    ps2 = eglCreatePixmapSurface(dpy, trgb, spr2, NULL);
    CHECK(eglMakeCurrent(dpy, ps2, ps2, ctx2), "TRGB pixmap current");
    clear(0, 0, 1);
    glFinish();
    CHECK(RGB(pix2[5]) == 0x0000FF, "TRGB blue = 0x0000FF (%08x)", pix2[5]);

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, ps);
    eglDestroySurface(dpy, ps2);
    eglDestroyContext(dpy, ctx);
    eglDestroyContext(dpy, ctx2);
}

static void test_window(void)
{
    EGLConfig cfg = choose(EGL_RISCOS_VISUAL_TBGR, 16, EGL_WINDOW_BIT);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    EGLSurface ws, fs;
    EGLint v, w, h;
    int block[64];

    /* Window 0x1000: visible area x 200..400, y 300..460 OS units = 100x80
       pixels, top left pixel (100, 480-230=250) on a 640x480 eig 1 screen. */
    fake_open_window(0x1000, 200, 300, 400, 460, 0, 0);
    CHECK(eglCreateWindowSurface(dpy, cfg, 0x2222, NULL) == EGL_NO_SURFACE &&
          eglGetError() == EGL_BAD_NATIVE_WINDOW, "unknown window refused");
    ws = eglCreateWindowSurface(dpy, cfg, 0x1000, NULL);
    CHECK(ws != EGL_NO_SURFACE, "window surface");
    eglQuerySurface(dpy, ws, EGL_WIDTH, &w);
    eglQuerySurface(dpy, ws, EGL_HEIGHT, &h);
    CHECK(w == 100 && h == 80, "window size %dx%d", w, h);
    CHECK(eglMakeCurrent(dpy, ws, ws, ctx), "current window");
    clear(0, 0, 1);
    CHECK(!box_is(100, 250, 100, 80, BLUE_TBGR), "nothing on screen before swap");
    CHECK(eglSwapBuffers(dpy, ws), "swap");
    CHECK(box_is(100, 250, 100, 80, BLUE_TBGR), "blue in the visible area");
    CHECK(RGB(fake_screen_pixel(99, 250)) == 0 && RGB(fake_screen_pixel(200, 250)) == 0 &&
          RGB(fake_screen_pixel(100, 249)) == 0 && RGB(fake_screen_pixel(100, 330)) == 0,
          "nothing outside it");
    CHECK(fake_update_calls == 1 && fake_vsyncs == 0, "one UpdateWindow, no vsync wait in a window");

    /* Redraw: wipe the screen, then the Redraw_Window_Request path */
    memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
    block[0] = 0x1000;
    CHECK(eglRedrawWindowRISCOS(dpy, block), "redraw helper");
    CHECK(box_is(100, 250, 100, 80, BLUE_TBGR), "redraw restored the frame");
    block[0] = 0x3333;
    CHECK(!eglRedrawWindowRISCOS(dpy, block) && eglGetError() == EGL_BAD_NATIVE_WINDOW,
          "redraw of a window without surfaces declined");

    /* Scrolling doesn't move a default surface (pinned to the visible area) */
    fake_open_window(0x1000, 200, 300, 400, 460, 40, -20);
    memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
    clear(1, 0, 0);
    eglSwapBuffers(dpy, ws);
    CHECK(box_is(100, 250, 100, 80, RED_TBGR), "scrolled window: still at the visible area");

    /* Resize to 150x60: the finished frame shows, the next one is the new size */
    fake_open_window(0x1000, 200, 300, 500, 420, 0, 0);
    clear(0, 1, 0);
    eglSwapBuffers(dpy, ws);
    eglQuerySurface(dpy, ws, EGL_WIDTH, &w);
    eglQuerySurface(dpy, ws, EGL_HEIGHT, &h);
    CHECK(w == 150 && h == 60, "resized to %dx%d", w, h);
    glViewport(0, 0, w, h);
    memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
    clear(0, 0, 1);
    eglSwapBuffers(dpy, ws);
    /* top now at y 420 OS = row 480-210 = 270 */
    CHECK(box_is(100, 270, 150, 60, BLUE_TBGR), "new size on screen");

    /* Fixed work area surface: 40x20 pixels at work area (20, -10), window
       scrolled by (10, -4): screen OS x = 200-10+20 = 210 -> px 105;
       top OS y = 420-(-4)+(-10) = 414 -> row 480-207 = 273. */
    {
        EGLint a[] = { EGL_WORK_AREA_X_RISCOS, 20, EGL_WORK_AREA_Y_RISCOS, -10,
                       EGL_WORK_AREA_WIDTH_RISCOS, 40, EGL_WORK_AREA_HEIGHT_RISCOS, 20, EGL_NONE };
        EGLint bad[] = { EGL_WORK_AREA_WIDTH_RISCOS, 40, EGL_NONE };
        EGLSurface fx;
        CHECK(eglCreateWindowSurface(dpy, cfg, 0x1000, bad) == EGL_NO_SURFACE &&
              eglGetError() == EGL_BAD_ATTRIBUTE, "width without height refused");
        fake_open_window(0x1000, 200, 300, 500, 420, 10, -4);
        fx = eglCreateWindowSurface(dpy, cfg, 0x1000, a);
        CHECK(fx != EGL_NO_SURFACE, "work area surface");
        eglQuerySurface(dpy, fx, EGL_WIDTH, &w);
        CHECK(w == 40, "fixed width");
        CHECK(eglMakeCurrent(dpy, fx, fx, ctx), "current fixed");
        memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
        clear(1, 1, 1);
        eglSwapBuffers(dpy, fx);
        CHECK(box_is(105, 273, 40, 20, 0xFFFFFF), "fixed surface at its work area position");
        CHECK(RGB(fake_screen_pixel(104, 273)) == 0 && RGB(fake_screen_pixel(105, 272)) == 0,
              "and only there");
        /* redraw helper plots both surfaces of the window, fixed on top */
        memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
        block[0] = 0x1000;
        eglRedrawWindowRISCOS(dpy, block);
        CHECK(box_is(105, 273, 40, 20, 0xFFFFFF) && RGB(fake_screen_pixel(101, 271)) == BLUE_TBGR,
              "redraw: both surfaces");
        /* swapping the visible area surface replots the work area one on top */
        eglMakeCurrent(dpy, ws, ws, ctx);
        memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
        clear(0, 0, 1);
        eglSwapBuffers(dpy, ws);
        CHECK(box_is(105, 273, 40, 20, 0xFFFFFF) && RGB(fake_screen_pixel(101, 271)) == BLUE_TBGR,
              "swap of the window surface keeps the work area surface on top");
        eglDestroySurface(dpy, fx);
    }

    /* Swapping a surface that isn't current */
    fs = eglCreateWindowSurface(dpy, cfg, 0x1000, NULL);
    CHECK(!eglSwapBuffers(dpy, fs) && eglGetError() == EGL_BAD_SURFACE, "swap non-current refused");
    eglDestroySurface(dpy, fs);

    /* Full screen, double buffered: plotted top aligned, with vsync */
    fs = eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, NULL);
    CHECK(fs != EGL_NO_SURFACE, "screen surface");
    eglQuerySurface(dpy, fs, EGL_WIDTH, &w);
    eglQuerySurface(dpy, fs, EGL_HEIGHT, &h);
    CHECK(w == 640 && h == 480, "screen size %dx%d", w, h);
    eglMakeCurrent(dpy, fs, fs, ctx);
    eglSwapInterval(dpy, 2);
    fake_vsyncs = 0;
    clear(1, 0, 0);
    CHECK(RGB(fake_screen_pixel(0, 0)) != RED_TBGR, "not on screen before swap");
    eglSwapBuffers(dpy, fs);
    CHECK(box_is(0, 0, 640, 480, RED_TBGR) && fake_vsyncs == 2, "full screen red, 2 vsyncs (%d)", fake_vsyncs);
    eglQuerySurface(dpy, fs, EGL_RENDER_BUFFER, &v);
    CHECK(v == EGL_BACK_BUFFER, "double buffered");
    eglMakeCurrent(dpy, ws, ws, ctx);
    eglDestroySurface(dpy, fs);

    /* Full screen, single buffered: straight into screen memory */
    {
        EGLint a[] = { EGL_RENDER_BUFFER, EGL_SINGLE_BUFFER, EGL_NONE };
        fs = eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, a);
        eglMakeCurrent(dpy, fs, fs, ctx);
        eglQuerySurface(dpy, fs, EGL_RENDER_BUFFER, &v);
        CHECK(v == EGL_SINGLE_BUFFER, "direct rendering");
        eglSwapInterval(dpy, 0);
        fake_plots = 0;
        clear(0, 1, 0);
        glFinish();
        CHECK(box_is(0, 0, 640, 480, GREEN), "drawn straight to the screen");
        eglSwapBuffers(dpy, fs);
        CHECK(fake_plots == 0, "no plot on swap");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, fs);
    }
    eglDestroySurface(dpy, ws);
    eglDestroyContext(dpy, ctx);
}

static void test_trgb_screen(void)
{
    EGLConfig cfgs[16], cfg;
    EGLContext ctx;
    EGLSurface ws;
    EGLint n, v;
    EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };

    eglTerminate(dpy);
    fake_set_screen(640, 480, 1, 5);
    eglInitialize(dpy, NULL, NULL);
    eglChooseConfig(dpy, a, cfgs, 16, &n);
    eglGetConfigAttrib(dpy, cfgs[0], EGL_NATIVE_VISUAL_ID, &v);
    CHECK(v == EGL_RISCOS_VISUAL_TRGB, "TRGB screen: TRGB configs first");
    {
        EGLint d[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_DEPTH_SIZE, 16, EGL_NONE };
        EGLConfig c[16];
        EGLint d0, d1;
        eglChooseConfig(dpy, d, c, 16, &n);
        eglGetConfigAttrib(dpy, c[0], EGL_DEPTH_SIZE, &d0);
        eglGetConfigAttrib(dpy, c[1], EGL_DEPTH_SIZE, &d1);
        CHECK(n == 6 && d0 == 16 && d1 == 16, "sorted by depth before config id");
    }
    cfg = cfgs[0];
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    fake_open_window(0x1000, 200, 300, 400, 460, 0, 0);
    ws = eglCreateWindowSurface(dpy, cfg, 0x1000, NULL);
    eglMakeCurrent(dpy, ws, ws, ctx);
    clear(1, 0, 0);
    eglSwapBuffers(dpy, ws);
    CHECK(box_is(100, 250, 100, 80, RED_TRGB), "red on a TRGB screen");

    /* A TBGR config on the TRGB screen: type 6 sprite, converted on plot */
    {
        EGLConfig c2 = choose(EGL_RISCOS_VISUAL_TBGR, 0, EGL_WINDOW_BIT);
        EGLContext x2 = eglCreateContext(dpy, c2, EGL_NO_CONTEXT, NULL);
        EGLSurface w2 = eglCreateWindowSurface(dpy, c2, 0x1000, NULL);
        CHECK(eglMakeCurrent(dpy, w2, w2, x2), "TBGR on TRGB screen");
        clear(0, 0, 1);
        eglSwapBuffers(dpy, w2);
        CHECK(box_is(100, 250, 100, 80, 0x0000FF), "blue after conversion");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, w2);
        eglDestroyContext(dpy, x2);
    }
    /* Terminate with nothing current frees everything */
    CHECK(eglTerminate(dpy), "terminate");
    CHECK(!eglMakeCurrent(dpy, ws, ws, ctx) && eglGetError() == EGL_NOT_INITIALIZED, "gone after terminate");
}

static void *run(void *arg)
{
    (void) arg;
    fake_set_screen(640, 480, 0, 5);
    fake_reset_clip();
    test_basics();
    test_pbuffer();
    test_pixmap();
    test_window();
    test_trgb_screen();
    return NULL;
}

int main(void)
{
    /* Keep every allocation and the test stack below 2 GB (32-bit SWI registers). */
    pthread_attr_t attr;
    pthread_t t;
    size_t stack_size = 8 << 20;
    void *stack;

    mallopt(M_ARENA_MAX, 1);           /* threads use the brk heap too */
    mallopt(M_MMAP_MAX, 0);
    mallopt(M_TOP_PAD, 64 << 20);
    /* At 1.5 GB, well clear of the brk heap (which must be free to grow:
       when brk is blocked glibc falls back to mmap, far above 4 GB). */
    stack = mmap((void *) 0x60000000, stack_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (stack == MAP_FAILED) { perror("mmap"); return 2; }
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, stack_size);
    pthread_create(&t, &attr, run, NULL);
    pthread_join(t, NULL);

    printf("%d checks, %d failures: %s\n", checks, failures, failures ? "FAIL" : "ALL PASS");
    return failures != 0;
}
