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

#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_riscos.h>
#include <GL/gl.h>

#include "fake_riscos.h"

int failures, checks;
void test_gles(EGLDisplay dpy);
void test_dispmanx(EGLDisplay dpy);

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
    CHECK(strcmp(eglQueryString(dpy, EGL_CLIENT_APIS), "OpenGL OpenGL_ES") == 0, "client APIs");
    ext = eglQueryString(dpy, EGL_EXTENSIONS);
    CHECK(ext && strstr(ext, "EGL_RISCOS_wimp_window"), "extension string");
    CHECK(eglGetConfigs(dpy, NULL, 0, &n) && n == 8, "8 configs (got %d)", n);

    /* The spec's default EGL_RENDERABLE_TYPE is ES: desktop GL configs don't match. */
    CHECK(eglChooseConfig(dpy, NULL, cfgs, 16, &n) && n == 8, "default choose (ES) = all (%d)", n);
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
    CHECK(!eglBindAPI(EGL_OPENVG_API) && eglGetError() == EGL_BAD_PARAMETER, "no OpenVG");
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
        /* ... without ever plotting the window surface over it (that
           flashes the wrong image while the screen is scanned out) */
        fake_watch[0] = 105; fake_watch[1] = 273; fake_watch[2] = 145; fake_watch[3] = 293;
        fake_watch_value = BLUE_TBGR;
        fake_watch_hits = 0;
        clear(0, 0, 1);
        eglSwapBuffers(dpy, ws);
        CHECK(fake_watch_hits == 0, "window surface never drawn under the work area surface (%d px)", fake_watch_hits);
        block[0] = 0x1000;
        eglRedrawWindowRISCOS(dpy, block);
        CHECK(fake_watch_hits == 0 && box_is(105, 273, 40, 20, 0xFFFFFF) &&
              RGB(fake_screen_pixel(146, 273)) == BLUE_TBGR && RGB(fake_screen_pixel(105, 294)) == BLUE_TBGR,
              "redraw: same, and the window surface still fills around it");
        fake_watch[2] = fake_watch[0];      /* watch off */
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

    /* Full screen with screen banks: 3 when screen memory can grow that far */
    {
        EGLint nb = -1, sb = 0;
        int bank;
        fake_screen.da_max = 3 * 640 * 480 * 4;
        memset(fake_screen.mem, 0, 3 * 640 * 480 * 4);
        EGLint b3[] = { EGL_SCREEN_BANKS_RISCOS, 3, EGL_NONE };
        EGLint b2[] = { EGL_SCREEN_BANKS_RISCOS, 2, EGL_NONE };
        fs = eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, NULL);
        eglQuerySurface(dpy, fs, EGL_SCREEN_BANKS_RISCOS, &nb);
        CHECK(nb == 0 && fake_screen.da_size == 640 * 480 * 4, "no banks unless asked for");
        eglDestroySurface(dpy, fs);
        fs = eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, b3);
        eglQuerySurface(dpy, fs, EGL_SCREEN_BANKS_RISCOS, &nb);
        eglQuerySurface(dpy, fs, EGL_SWAP_BEHAVIOR, &sb);
        CHECK(nb == 3 && fake_screen.da_size == 3 * 640 * 480 * 4, "3 screen banks (%d), memory grown", nb);
        CHECK(sb == EGL_BUFFER_DESTROYED, "bank surface: contents not preserved");
        eglMakeCurrent(dpy, fs, fs, ctx);
        eglSwapInterval(dpy, 1);
        fake_vsyncs = 0;
        fake_plots = 0;
        clear(1, 0, 0);
        glFinish();
        CHECK(fake_screen.display_bank == 1 && RGB(fake_bank_pixel(1, 5, 5)) != RED_TBGR &&
              RGB(fake_bank_pixel(2, 5, 5)) == RED_TBGR, "drawn into hidden bank 2, bank 1 still shown");
        eglSwapBuffers(dpy, fs);
        CHECK(fake_screen.display_bank == 2 && fake_vsyncs == 1 && fake_plots == 0,
              "swap: vsync then show bank 2, no plot");
        clear(0, 1, 0);
        glFinish();
        CHECK(RGB(fake_bank_pixel(3, 5, 5)) == GREEN && RGB(fake_bank_pixel(2, 5, 5)) == RED_TBGR,
              "next frame goes to bank 3; bank 2 untouched while shown");
        eglSwapBuffers(dpy, fs);
        bank = fake_screen.display_bank;
        clear(0, 0, 1);
        glFinish();
        CHECK(bank == 3 && RGB(fake_bank_pixel(1, 5, 5)) == BLUE_TBGR, "then bank 3 shown, bank 1 drawn");
        eglSwapBuffers(dpy, fs);
        CHECK(fake_screen.display_bank == 1 && fake_screen.vdu_bank == 1, "round to bank 1");
        clear(1, 1, 1);
        eglSwapBuffers(dpy, fs);
        CHECK(fake_screen.display_bank == 2, "bank 2 again");
        /* asking for preserved contents switches to the sprite method */
        CHECK(eglSurfaceAttrib(dpy, fs, EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED), "set preserved");
        eglQuerySurface(dpy, fs, EGL_SCREEN_BANKS_RISCOS, &nb);
        CHECK(nb == 0 && fake_screen.display_bank == 1, "preserved: no banks, display back on bank 1");
        memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
        clear(0, 1, 0);
        eglSwapBuffers(dpy, fs);
        CHECK(box_is(0, 0, 640, 480, GREEN) && fake_plots == 1, "then plotted as a sprite");
        eglMakeCurrent(dpy, ws, ws, ctx);
        eglDestroySurface(dpy, fs);

        /* 2 banks when that's all there is; destroying restores bank 1 */
        fake_screen.da_size = 640 * 480 * 4;
        fake_screen.da_max = 2 * 640 * 480 * 4;
        fs = eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, b3);
        eglQuerySurface(dpy, fs, EGL_SCREEN_BANKS_RISCOS, &nb);
        CHECK(nb == 2, "2 banks (%d)", nb);
        CHECK(eglCreateWindowSurface(dpy, cfg, 0x1000, b2) == EGL_NO_SURFACE &&
              eglGetError() == EGL_BAD_ATTRIBUTE, "banks only for the whole screen");
        eglMakeCurrent(dpy, fs, fs, ctx);
        clear(1, 0, 0);
        eglSwapBuffers(dpy, fs);
        clear(0, 1, 0);
        glFinish();
        CHECK(fake_screen.display_bank == 2 && RGB(fake_bank_pixel(1, 5, 5)) == GREEN, "2 banks alternate");
        eglMakeCurrent(dpy, ws, ws, ctx);
        eglSwapBuffers(dpy, fs);            /* not current: refused, no flip */
        eglGetError();
        eglDestroySurface(dpy, fs);
        CHECK(fake_screen.display_bank == 1 && fake_screen.vdu_bank == 1, "destroy: display back on bank 1");
        fake_screen.da_size = fake_screen.da_max = 640 * 480 * 4;
    }

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

/* ------------------------------------------------------------------ */
/* Extensions                                                          */

static int dbg_calls;
static EGLenum dbg_error;
static const char *dbg_command;
static EGLint dbg_type;
static EGLLabelKHR dbg_thread, dbg_object;

static void EGLAPIENTRY dbg_cb(EGLenum error, const char *command, EGLint messageType,
                               EGLLabelKHR threadLabel, EGLLabelKHR objectLabel,
                               const char *message)
{
    (void) message;
    dbg_calls++;
    dbg_error = error;
    dbg_command = command;
    dbg_type = messageType;
    dbg_thread = threadLabel;
    dbg_object = objectLabel;
}

static void wipe(void)
{
    memset(fake_screen.mem, 0, fake_screen.w * fake_screen.h * 4);
}

static void test_client_and_platform(void)
{
    const char *s;
    int handle = 0x1000;
    EGLConfig cfg = choose(EGL_RISCOS_VISUAL_TBGR, 0, EGL_WINDOW_BIT);
    EGLSurface ws, ps;
    EGLint w = 0;
    int *spr = make_sprite(8, 8, 0);

    s = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    CHECK(s && strstr(s, "EGL_EXT_platform_base") && strstr(s, "EGL_KHR_debug") &&
          strstr(s, "EGL_RISCOS_platform_wimp") && eglGetError() == EGL_SUCCESS, "client extensions");
    CHECK(eglQueryString(EGL_NO_DISPLAY, EGL_VENDOR) == NULL && eglGetError() == EGL_BAD_DISPLAY,
          "other strings need a display");
    s = eglQueryString(dpy, EGL_EXTENSIONS);
    CHECK(s && strstr(s, "EGL_KHR_fence_sync") && strstr(s, "EGL_KHR_surfaceless_context") &&
          !strstr(s, "EGL_EXT_platform_base"), "display extensions, without the client ones");

    CHECK(eglGetPlatformDisplayEXT(EGL_PLATFORM_RISCOS, NULL, NULL) == dpy, "platform display");
    CHECK(eglGetPlatformDisplayEXT(EGL_PLATFORM_X11_KHR, NULL, NULL) == EGL_NO_DISPLAY &&
          eglGetError() == EGL_BAD_PARAMETER, "other platform refused");
    CHECK(eglGetPlatformDisplayEXT(EGL_PLATFORM_RISCOS, &handle, NULL) == EGL_NO_DISPLAY &&
          eglGetError() == EGL_BAD_PARAMETER, "non-default native display refused");

    fake_open_window(0x1000, 200, 300, 400, 460, 0, 0);
    ws = eglCreatePlatformWindowSurfaceEXT(dpy, cfg, &handle, NULL);
    eglQuerySurface(dpy, ws, EGL_WIDTH, &w);
    CHECK(ws != EGL_NO_SURFACE && w == 100, "platform window surface (width %d)", w);
    CHECK(eglCreatePlatformWindowSurfaceEXT(dpy, cfg, NULL, NULL) == EGL_NO_SURFACE &&
          eglGetError() == EGL_BAD_NATIVE_WINDOW, "NULL native window refused");
    ps = eglCreatePlatformPixmapSurfaceEXT(dpy, cfg, spr, NULL);
    CHECK(ps != EGL_NO_SURFACE, "platform pixmap surface");
    eglDestroySurface(dpy, ws);
    eglDestroySurface(dpy, ps);
    free(spr - 4);

    CHECK(eglGetProcAddress("eglCreateSyncKHR") == (__eglMustCastToProperFunctionPointerType) eglCreateSyncKHR &&
          eglGetProcAddress("eglSwapBuffersWithDamageKHR") != NULL &&
          eglGetProcAddress("eglLockSurfaceKHR") != NULL &&
          eglGetProcAddress("eglDebugMessageControlKHR") != NULL &&
          eglGetProcAddress("eglGetPlatformDisplayEXT") != NULL, "extension functions by name");
}

static void test_surfaceless_and_sync(void)
{
    EGLConfig cfg = choose(EGL_RISCOS_VISUAL_TBGR, 24, EGL_PBUFFER_BIT);
    EGLint pa[] = { EGL_WIDTH, 64, EGL_HEIGHT, 32, EGL_NONE };
    EGLint none_attr[] = { EGL_CONTEXT_RELEASE_BEHAVIOR_KHR, EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR, EGL_NONE };
    EGLint bad_rel[] = { EGL_CONTEXT_RELEASE_BEHAVIOR_KHR, 7, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL), ctx2;
    EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pa);
    EGLSyncKHR f, r;
    EGLint v;
    GLint vp[4];

    /* surfaceless */
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx), "surfaceless make current");
    CHECK(eglGetCurrentContext() == ctx && eglGetCurrentSurface(EGL_DRAW) == EGL_NO_SURFACE,
          "context current, no surface");
    CHECK(glGetString(GL_VERSION) != NULL, "GL usable without a surface");
    CHECK(!eglMakeCurrent(dpy, pb, EGL_NO_SURFACE, ctx) && eglGetError() == EGL_BAD_MATCH,
          "only one surface: BAD_MATCH");
    CHECK(eglMakeCurrent(dpy, pb, pb, ctx), "then with a surface");
    glGetIntegerv(GL_VIEWPORT, vp);
    CHECK(vp[2] == 64 && vp[3] == 32, "viewport = surface after surfaceless start (%dx%d)", vp[2], vp[3]);

    /* flush control */
    ctx2 = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, none_attr);
    CHECK(ctx2 != EGL_NO_CONTEXT, "release behaviour NONE accepted");
    CHECK(eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, bad_rel) == EGL_NO_CONTEXT &&
          eglGetError() == EGL_BAD_ATTRIBUTE, "bad release behaviour refused");
    CHECK(eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx2) &&
          eglMakeCurrent(dpy, pb, pb, ctx), "switch between them");

    /* fence */
    f = eglCreateSyncKHR(dpy, EGL_SYNC_FENCE_KHR, NULL);
    CHECK(f != EGL_NO_SYNC_KHR, "fence");
    CHECK(eglGetSyncAttribKHR(dpy, f, EGL_SYNC_STATUS_KHR, &v) && v == EGL_SIGNALED_KHR, "fence signalled");
    CHECK(eglGetSyncAttribKHR(dpy, f, EGL_SYNC_TYPE_KHR, &v) && v == EGL_SYNC_FENCE_KHR, "fence type");
    CHECK(eglGetSyncAttribKHR(dpy, f, EGL_SYNC_CONDITION_KHR, &v) &&
          v == EGL_SYNC_PRIOR_COMMANDS_COMPLETE_KHR, "fence condition");
    CHECK(eglClientWaitSyncKHR(dpy, f, EGL_SYNC_FLUSH_COMMANDS_BIT_KHR, EGL_FOREVER_KHR) ==
          EGL_CONDITION_SATISFIED_KHR, "wait on fence");
    CHECK(!eglSignalSyncKHR(dpy, f, EGL_UNSIGNALED_KHR) && eglGetError() == EGL_BAD_MATCH,
          "can't signal a fence");
    CHECK(eglWaitSyncKHR(dpy, f, 0) == EGL_TRUE, "server wait");
    CHECK(eglWaitSyncKHR(dpy, f, 1) == EGL_FALSE && eglGetError() == EGL_BAD_PARAMETER, "server wait flags");
    CHECK(eglDestroySyncKHR(dpy, f), "destroy fence");
    CHECK(!eglGetSyncAttribKHR(dpy, f, EGL_SYNC_STATUS_KHR, &v) && eglGetError() == EGL_BAD_PARAMETER,
          "destroyed sync");
    {
        EGLint a[] = { EGL_SYNC_STATUS_KHR, EGL_SIGNALED_KHR, EGL_NONE };
        CHECK(eglCreateSyncKHR(dpy, EGL_SYNC_FENCE_KHR, a) == EGL_NO_SYNC_KHR &&
              eglGetError() == EGL_BAD_ATTRIBUTE, "fence attributes refused");
    }
    CHECK(eglCreateSyncKHR(dpy, 0x1234, NULL) == EGL_NO_SYNC_KHR && eglGetError() == EGL_BAD_ATTRIBUTE,
          "unknown sync type");

    /* reusable */
    r = eglCreateSyncKHR(dpy, EGL_SYNC_REUSABLE_KHR, NULL);
    CHECK(r != EGL_NO_SYNC_KHR && eglGetSyncAttribKHR(dpy, r, EGL_SYNC_STATUS_KHR, &v) &&
          v == EGL_UNSIGNALED_KHR, "reusable starts unsignalled");
    CHECK(eglClientWaitSyncKHR(dpy, r, 0, 1000000) == EGL_TIMEOUT_EXPIRED_KHR, "wait times out");
    CHECK(!eglGetSyncAttribKHR(dpy, r, EGL_SYNC_CONDITION_KHR, &v) && eglGetError() == EGL_BAD_ATTRIBUTE,
          "no condition on a reusable sync");
    CHECK(!eglSignalSyncKHR(dpy, r, 0x1234) && eglGetError() == EGL_BAD_PARAMETER, "bad signal mode");
    CHECK(eglSignalSyncKHR(dpy, r, EGL_SIGNALED_KHR) &&
          eglClientWaitSyncKHR(dpy, r, 0, 0) == EGL_CONDITION_SATISFIED_KHR, "signalled: satisfied");

    /* fences need a current context */
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    CHECK(eglCreateSyncKHR(dpy, EGL_SYNC_FENCE_KHR, NULL) == EGL_NO_SYNC_KHR &&
          eglGetError() == EGL_BAD_MATCH, "fence without context");
    CHECK(eglWaitSyncKHR(dpy, r, 0) == EGL_FALSE && eglGetError() == EGL_BAD_MATCH,
          "server wait without context");
    /* r left for eglTerminate to free */
    eglDestroySurface(dpy, pb);
    eglDestroyContext(dpy, ctx);
    eglDestroyContext(dpy, ctx2);
}

static void test_damage(void)
{
    EGLConfig cfg = choose(EGL_RISCOS_VISUAL_TBGR, 0, EGL_WINDOW_BIT);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    EGLSurface ws, other, fs;
    EGLint age = -1, calls;
    EGLint r1[] = { 10, 10, 20, 20 };
    EGLint r2[] = { 0, 0, 5, 5, 95, 75, 5, 5 };
    EGLint big[4 * 20];
    int i;

    fake_open_window(0x1000, 200, 300, 400, 460, 0, 0);    /* 100x80 at pixel (100, 250) */
    ws = eglCreateWindowSurface(dpy, cfg, 0x1000, NULL);
    other = eglCreateWindowSurface(dpy, cfg, 0x1000, NULL);
    eglMakeCurrent(dpy, ws, ws, ctx);
    CHECK(!eglQuerySurface(dpy, other, EGL_BUFFER_AGE_EXT, &age) && eglGetError() == EGL_BAD_SURFACE,
          "age only of the current surface");
    CHECK(!eglSetDamageRegionKHR(dpy, ws, r1, 1) && eglGetError() == EGL_BAD_ACCESS,
          "damage region needs the age asked for first");
    CHECK(eglQuerySurface(dpy, ws, EGL_BUFFER_AGE_EXT, &age) && age == 0, "new buffer: age 0 (%d)", age);
    clear(0, 0, 1);
    eglSwapBuffers(dpy, ws);
    CHECK(eglQuerySurface(dpy, ws, EGL_BUFFER_AGE_EXT, &age) && age == 1, "after a swap: age 1 (%d)", age);

    /* one damaged rectangle, 10,10 20x20 from the bottom left: screen
       pixels x 110..129, rows 250+80-30 = 300..319 */
    wipe();
    clear(1, 0, 0);
    calls = fake_update_calls;
    CHECK(eglSwapBuffersWithDamageKHR(dpy, ws, r1, 1), "swap with damage");
    CHECK(box_is(110, 300, 20, 20, RED_TBGR), "damaged part shown");
    CHECK(RGB(fake_screen_pixel(109, 300)) == 0 && RGB(fake_screen_pixel(110, 299)) == 0 &&
          RGB(fake_screen_pixel(130, 319)) == 0 && RGB(fake_screen_pixel(110, 320)) == 0 &&
          RGB(fake_screen_pixel(100, 250)) == 0, "nothing else");
    CHECK(fake_update_calls == calls + 1, "one UpdateWindow");

    /* two rectangles in opposite corners, one clipped by the surface edge */
    wipe();
    clear(0, 1, 0);
    calls = fake_update_calls;
    CHECK(eglSwapBuffersWithDamageEXT(dpy, ws, r2, 2), "EXT swap with damage");
    CHECK(box_is(100, 325, 5, 5, GREEN) && box_is(195, 250, 5, 5, GREEN) &&
          RGB(fake_screen_pixel(150, 290)) == 0 && fake_update_calls == calls + 2, "two corners, two updates");
    CHECK(!eglSwapBuffersWithDamageKHR(dpy, ws, r1, -1) && eglGetError() == EGL_BAD_PARAMETER,
          "negative count refused");

    /* more than 16: their bounding box */
    for (i = 0; i < 20; i++) {
        big[i * 4] = 10 + i; big[i * 4 + 1] = 10; big[i * 4 + 2] = 1; big[i * 4 + 3] = 1;
    }
    wipe();
    calls = fake_update_calls;
    eglSwapBuffersWithDamageKHR(dpy, ws, big, 20);
    CHECK(fake_update_calls == calls + 1 && box_is(110, 319, 20, 1, GREEN), "many rectangles: one box");

    /* n = 0: all of it */
    wipe();
    eglSwapBuffersWithDamageKHR(dpy, ws, NULL, 0);
    CHECK(box_is(100, 250, 100, 80, GREEN), "no rectangles = whole surface");

    /* partial update: the damage region set for the frame is what's shown */
    eglQuerySurface(dpy, ws, EGL_BUFFER_AGE_EXT, &age);
    CHECK(eglSetDamageRegionKHR(dpy, ws, r1, 1), "set damage region");
    CHECK(!eglSetDamageRegionKHR(dpy, ws, r1, 1) && eglGetError() == EGL_BAD_ACCESS, "only once a frame");
    CHECK(!eglSetDamageRegionKHR(dpy, other, r1, 1) && eglGetError() == EGL_BAD_MATCH,
          "only on the current surface");
    wipe();
    clear(1, 1, 1);
    eglSwapBuffers(dpy, ws);
    CHECK(box_is(110, 300, 20, 20, 0xFFFFFF) && RGB(fake_screen_pixel(100, 250)) == 0,
          "plain swap shows the damage region");
    CHECK(!eglSetDamageRegionKHR(dpy, ws, r1, 1) && eglGetError() == EGL_BAD_ACCESS,
          "age must be asked again each frame");
    wipe();
    eglSwapBuffers(dpy, ws);
    CHECK(box_is(100, 250, 100, 80, 0xFFFFFF), "next frame: whole surface again");

    /* resize: new buffer, age 0 */
    fake_open_window(0x1000, 200, 300, 500, 420, 0, 0);
    eglSwapBuffers(dpy, ws);
    CHECK(eglQuerySurface(dpy, ws, EGL_BUFFER_AGE_EXT, &age) && age == 0, "resized: age 0 (%d)", age);
    fake_open_window(0x1000, 200, 300, 400, 460, 0, 0);

    /* full screen sprite: the damaged part only, after the vsync wait */
    fs = eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, NULL);
    eglMakeCurrent(dpy, fs, fs, ctx);
    glViewport(0, 0, 640, 480);
    clear(0, 1, 0);
    eglSwapBuffers(dpy, fs);
    wipe();
    clear(1, 0, 0);
    fake_vsyncs = 0;
    {
        EGLint r[] = { 0, 0, 10, 10 };
        eglSwapBuffersWithDamageKHR(dpy, fs, r, 1);
    }
    CHECK(box_is(0, 470, 10, 10, RED_TBGR) && RGB(fake_screen_pixel(0, 469)) == 0 &&
          RGB(fake_screen_pixel(10, 479)) == 0 && fake_vsyncs == 1, "full screen: bottom left corner only");
    eglSwapBuffers(dpy, fs);
    CHECK(box_is(0, 0, 640, 480, RED_TBGR), "graphics window restored afterwards");

    /* screen banks: age = banks once each has been drawn */
    eglMakeCurrent(dpy, ws, ws, ctx);
    eglDestroySurface(dpy, fs);
    {
        EGLint b3[] = { EGL_SCREEN_BANKS_RISCOS, 3, EGL_NONE };
        EGLint ages[4];
        fake_screen.da_max = 3 * 640 * 480 * 4;
        fs = eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, b3);
        eglMakeCurrent(dpy, fs, fs, ctx);
        for (i = 0; i < 4; i++) {
            eglQuerySurface(dpy, fs, EGL_BUFFER_AGE_EXT, &ages[i]);
            eglSwapBuffers(dpy, fs);
        }
        CHECK(ages[0] == 0 && ages[1] == 0 && ages[2] == 0 && ages[3] == 3,
              "3 banks: ages 0 0 0 3 (%d %d %d %d)", ages[0], ages[1], ages[2], ages[3]);
        eglMakeCurrent(dpy, ws, ws, ctx);
        eglDestroySurface(dpy, fs);
        fake_screen.da_size = fake_screen.da_max = 640 * 480 * 4;
    }

    /* pbuffers have no history */
    {
        EGLint pa[] = { EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE };
        EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pa);
        eglMakeCurrent(dpy, pb, pb, ctx);
        eglSwapBuffers(dpy, pb);
        CHECK(eglQuerySurface(dpy, pb, EGL_BUFFER_AGE_EXT, &age) && age == 0, "pbuffer age 0");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, pb);
    }
    eglDestroySurface(dpy, ws);
    eglDestroySurface(dpy, other);
    eglDestroyContext(dpy, ctx);
}

static void test_lock(void)
{
    EGLConfig cfgs[16], cfg = choose(EGL_RISCOS_VISUAL_TBGR, 0, EGL_WINDOW_BIT);
    EGLConfig trgb = choose(EGL_RISCOS_VISUAL_TRGB, 0, EGL_WINDOW_BIT);
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    EGLSurface ws, ls;
    EGLint n, v, pitch = 0, ptr = 0;
    EGLAttribKHR p64 = 0;
    int x, y;

    eglGetConfigAttrib(dpy, cfg, EGL_SURFACE_TYPE, &v);
    CHECK(v & EGL_LOCK_SURFACE_BIT_KHR, "configs are lockable");
    eglGetConfigAttrib(dpy, cfg, EGL_MATCH_FORMAT_KHR, &v);
    CHECK(v == EGL_FORMAT_RGBA_8888_KHR, "TBGR format RGBA_8888");
    eglGetConfigAttrib(dpy, trgb, EGL_MATCH_FORMAT_KHR, &v);
    CHECK(v == EGL_FORMAT_RGBA_8888_EXACT_KHR, "TRGB format RGBA_8888_EXACT (B,G,R,A bytes)");
    {
        EGLint a[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_MATCH_FORMAT_KHR, EGL_FORMAT_RGBA_8888_EXACT_KHR, EGL_NONE };
        EGLint b[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_MATCH_FORMAT_KHR, EGL_FORMAT_RGBA_8888_KHR, EGL_NONE };
        EGLint c[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_MATCH_FORMAT_KHR, EGL_NONE, EGL_NONE };
        EGLint e = 0, f = 0, g = 0;
        eglChooseConfig(dpy, a, cfgs, 16, &e);
        eglChooseConfig(dpy, b, cfgs, 16, &f);
        eglChooseConfig(dpy, c, cfgs, 16, &g);
        CHECK(e == 4 && f == 8 && g == 0, "MATCH_FORMAT: exact 4, 8888 8, none 0 (%d %d %d)", e, f, g);
    }

    fake_open_window(0x1000, 200, 300, 400, 460, 0, 0);
    ws = eglCreateWindowSurface(dpy, cfg, 0x1000, NULL);
    eglMakeCurrent(dpy, ws, ws, ctx);
    CHECK(!eglLockSurfaceKHR(dpy, ws, NULL) && eglGetError() == EGL_BAD_ACCESS, "current surface can't be locked");

    ls = eglCreateWindowSurface(dpy, cfg, 0x1000, NULL);
    CHECK(!eglQuerySurface(dpy, ls, EGL_BITMAP_POINTER_KHR, &v) && eglGetError() == EGL_BAD_ACCESS,
          "bitmap only while locked");
    {
        EGLint bad[] = { EGL_WIDTH, 1, EGL_NONE };
        CHECK(!eglLockSurfaceKHR(dpy, ls, bad) && eglGetError() == EGL_BAD_ATTRIBUTE, "lock attribute checked");
    }
    {
        EGLint a[] = { EGL_MAP_PRESERVE_PIXELS_KHR, EGL_TRUE, EGL_LOCK_USAGE_HINT_KHR, EGL_WRITE_SURFACE_BIT_KHR, EGL_NONE };
        CHECK(eglLockSurfaceKHR(dpy, ls, a), "lock");
    }
    CHECK(!eglLockSurfaceKHR(dpy, ls, NULL) && eglGetError() == EGL_BAD_ACCESS, "already locked");
    eglQuerySurface(dpy, ls, EGL_BITMAP_POINTER_KHR, &ptr);
    eglQuerySurface(dpy, ls, EGL_BITMAP_PITCH_KHR, &pitch);
    eglQuerySurface64KHR(dpy, ls, EGL_BITMAP_POINTER_KHR, &p64);
    CHECK(ptr != 0 && (EGLAttribKHR) ptr == p64 && pitch == 400, "pointer and pitch (%d)", pitch);
    eglQuerySurface(dpy, ls, EGL_BITMAP_ORIGIN_KHR, &v);
    CHECK(v == EGL_UPPER_LEFT_KHR, "origin top left");
    eglQuerySurface(dpy, ls, EGL_BITMAP_PIXEL_RED_OFFSET_KHR, &v);
    CHECK(v == 0, "TBGR red at bit 0");
    eglQuerySurface(dpy, ls, EGL_BITMAP_PIXEL_BLUE_OFFSET_KHR, &v);
    CHECK(v == 16, "TBGR blue at bit 16");
    eglQuerySurface(dpy, ls, EGL_BITMAP_PIXEL_SIZE_KHR, &v);
    CHECK(v == 32, "32 bits a pixel");
    CHECK(!eglMakeCurrent(dpy, ls, ls, ctx) && eglGetError() == EGL_BAD_ACCESS, "locked can't be current");
    CHECK(!eglSwapBuffers(dpy, ls) && eglGetError() == EGL_BAD_ACCESS, "locked can't be swapped");
    for (y = 0; y < 80; y++)
        for (x = 0; x < 100; x++)
            ((unsigned int *) ((char *) (long) ptr + y * pitch))[x] = y < 40 ? RED_TBGR : GREEN;
    CHECK(eglUnlockSurfaceKHR(dpy, ls), "unlock");
    CHECK(!eglUnlockSurfaceKHR(dpy, ls) && eglGetError() == EGL_BAD_ACCESS, "not locked");
    wipe();
    CHECK(eglSwapBuffers(dpy, ls), "swap a written lockable surface without a context");
    CHECK(box_is(100, 250, 100, 40, RED_TBGR) && box_is(100, 290, 100, 40, GREEN), "what was written is shown");
    {
        EGLint l[] = { EGL_NONE };
        EGLSurface t2;
        eglLockSurfaceKHR(dpy, ls, l);
        eglUnlockSurfaceKHR(dpy, ls);
        t2 = eglCreateWindowSurface(dpy, cfg, 0x1000, NULL);
        CHECK(!eglSwapBuffers(dpy, t2) && eglGetError() == EGL_BAD_SURFACE,
              "never-locked, non-current surface still can't be swapped");
        eglDestroySurface(dpy, t2);
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, ws);
    eglDestroySurface(dpy, ls);
    eglDestroyContext(dpy, ctx);
}

static void test_debug(void)
{
    EGLConfig cfg = choose(EGL_RISCOS_VISUAL_TBGR, 0, EGL_PBUFFER_BIT);
    EGLint pa[] = { EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE };
    EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pa);
    EGLAttrib on[] = { EGL_DEBUG_MSG_WARN_KHR, EGL_TRUE, EGL_NONE };
    EGLAttrib off[] = { EGL_DEBUG_MSG_ERROR_KHR, EGL_FALSE, EGL_NONE };
    EGLAttrib bad[] = { EGL_WIDTH, EGL_TRUE, EGL_NONE };
    EGLAttrib a;
    EGLint v;
    static int thread_tag, dpy_tag, surf_tag;

    CHECK(eglQueryDebugKHR(EGL_DEBUG_MSG_ERROR_KHR, &a) && a == EGL_TRUE, "errors reported by default");
    CHECK(eglQueryDebugKHR(EGL_DEBUG_MSG_INFO_KHR, &a) && a == EGL_FALSE, "info not by default");
    CHECK(eglDebugMessageControlKHR(dbg_cb, on) == EGL_SUCCESS, "set callback");
    CHECK(eglQueryDebugKHR(EGL_DEBUG_MSG_WARN_KHR, &a) && a == EGL_TRUE &&
          eglQueryDebugKHR(EGL_DEBUG_MSG_ERROR_KHR, &a) && a == EGL_TRUE, "warn on, error unchanged");
    CHECK(eglQueryDebugKHR(EGL_DEBUG_CALLBACK_KHR, &a) && a == (EGLAttrib) dbg_cb, "callback queried");
    CHECK(eglDebugMessageControlKHR(dbg_cb, bad) == EGL_BAD_ATTRIBUTE, "bad message type");

    CHECK(eglLabelObjectKHR(NULL, EGL_OBJECT_THREAD_KHR, NULL, &thread_tag) == EGL_SUCCESS, "label thread");
    CHECK(eglLabelObjectKHR(dpy, EGL_OBJECT_DISPLAY_KHR, dpy, &dpy_tag) == EGL_SUCCESS, "label display");
    CHECK(eglLabelObjectKHR(dpy, EGL_OBJECT_SURFACE_KHR, pb, &surf_tag) == EGL_SUCCESS, "label surface");
    CHECK(eglLabelObjectKHR(dpy, EGL_OBJECT_SURFACE_KHR, (EGLObjectKHR) 0x1234, &surf_tag) == EGL_BAD_PARAMETER,
          "label a bad surface");
    CHECK(eglLabelObjectKHR(dpy, EGL_OBJECT_DISPLAY_KHR, (EGLObjectKHR) 0x1234, &dpy_tag) == EGL_BAD_PARAMETER,
          "display label object must be the display");

    dbg_calls = 0;
    eglQuerySurface(dpy, pb, 0x1234, &v);
    CHECK(dbg_calls == 1 && dbg_error == EGL_BAD_ATTRIBUTE && dbg_type == EGL_DEBUG_MSG_ERROR_KHR &&
          dbg_command && strcmp(dbg_command, "eglQuerySurface") == 0 &&
          dbg_thread == &thread_tag && dbg_object == &surf_tag, "error reported with command and labels (%s)",
          dbg_command ? dbg_command : "null");
    eglInitialize((EGLDisplay) 0x42, NULL, NULL);
    CHECK(dbg_calls == 2 && dbg_error == EGL_BAD_DISPLAY && dbg_object == NULL &&
          strcmp(dbg_command, "eglInitialize") == 0, "bad display reported");
    eglGetConfigs(dpy, NULL, 0, NULL);
    CHECK(dbg_calls == 3 && dbg_object == &dpy_tag, "display label");
    CHECK(eglDebugMessageControlKHR(dbg_cb, off) == EGL_SUCCESS, "errors off");
    eglQuerySurface(dpy, pb, 0x1234, &v);
    CHECK(dbg_calls == 3 && eglGetError() == EGL_BAD_ATTRIBUTE, "not reported, still an error");
    eglDebugMessageControlKHR(NULL, NULL);
    {
        EGLAttrib reset[] = { EGL_DEBUG_MSG_ERROR_KHR, EGL_TRUE, EGL_DEBUG_MSG_WARN_KHR, EGL_FALSE, EGL_NONE };
        eglDebugMessageControlKHR(NULL, reset);
    }
    eglLabelObjectKHR(NULL, EGL_OBJECT_THREAD_KHR, NULL, NULL);
    eglDestroySurface(dpy, pb);
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
    test_client_and_platform();
    test_surfaceless_and_sync();
    test_damage();
    test_lock();
    test_debug();
    test_gles(dpy);
    test_dispmanx(dpy);
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
