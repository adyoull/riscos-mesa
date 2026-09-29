/*
 * EGL host harness: window surfaces shown through a hardware overlay
 * (EGL_RISCOS_overlay, egl/parts/overlay.c), against the fake VideoOverlay
 * module in ../ovl/fake_ovl.c. Runs last: fake_ovl_init() hooks the fake.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_riscos.h>
#include <GL/gl.h>

#include "fake_riscos.h"

extern int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

void fake_ovl_init(void);
int fake_ovl_live(void);
int fake_ovl_errors(void);
int fake_ovl_info(int *w, int *h, int *banks, int *flags, int *shown, int *scale_w, int *scale_h);
unsigned int fake_ovl_pixel(int b, int x, int y);
extern int fake_ovl_creates, fake_ovl_redraws;

#define RGB(p) ((p) & 0x00FFFFFFu)
#define RED   0x0000FFu        /* TBGR */
#define BLUE  0xFF0000u
#define GREEN 0x00FF00u

static EGLDisplay dpy;

static EGLConfig choose(EGLint visual)
{
    EGLint attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE };
    EGLConfig cfgs[32];
    EGLint n = 0, i, v;
    eglChooseConfig(dpy, attrs, cfgs, 32, &n);
    for (i = 0; i < n; i++) {
        eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &v);
        if (v == visual) return cfgs[i];
    }
    return NULL;
}

static void clear(float r, float g, float b)
{
    glClearColor(r, g, b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

static void wipe(void)
{
    memset(fake_screen.mem, 0, (size_t) fake_screen.w * fake_screen.h * 4);
}

/* every screen pixel of the box equal to v */
static int box_is(int x0, int y0, int w, int h, unsigned int v)
{
    int x, y;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            if (RGB(fake_screen_pixel(x, y)) != v) return 0;
    return 1;
}

static int query(EGLSurface s)
{
    EGLint v = -99;
    eglQuerySurface(dpy, s, EGL_OVERLAY_RISCOS, &v);
    return v;
}

static int shown(void)
{
    int b = -3;
    if (!fake_ovl_info(NULL, NULL, NULL, NULL, &b, NULL, NULL)) return -3;
    return b;
}

void test_overlay(EGLDisplay d)
{
    EGLConfig cfg, trgb;
    EGLContext ctx;
    EGLSurface ws;
    int w, h, banks, flags, b, sw, sh, n, block[64];

    dpy = d;
    eglInitialize(dpy, NULL, NULL);    /* test_trgb_screen terminated it */
    fake_set_screen(640, 480, 0, 5);
    fake_ovl_init();
    cfg = choose(EGL_RISCOS_VISUAL_TBGR);
    trgb = choose(EGL_RISCOS_VISUAL_TRGB);
    CHECK(cfg && trgb, "configs");
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    CHECK(strstr(eglQueryString(dpy, EGL_EXTENSIONS), "EGL_RISCOS_overlay") != NULL, "extension listed");
    CHECK(eglGetProcAddress("eglCheckOverlaysRISCOS") == (void (*)(void)) eglCheckOverlaysRISCOS,
          "eglCheckOverlaysRISCOS from eglGetProcAddress");

    /* Window 0x5000: 100x80 pixels, top left pixel (100, 250) */
    fake_open_window(0x5000, 200, 300, 400, 460, 0, 0);
    fake_window_behind = -1;
    {
        EGLint on[] = { EGL_OVERLAY_RISCOS, EGL_TRUE, EGL_NONE };  /* opt-in */
        ws = eglCreateWindowSurface(dpy, cfg, 0x5000, on);
    }
    CHECK(ws != EGL_NO_SURFACE && eglMakeCurrent(dpy, ws, ws, ctx), "window surface");
    CHECK(query(ws) == 0, "no overlay before the first swap");

    /* VideoOverlay not loaded: plotted as always, tried again later */
    setenv("FAKE_OVL_MISSING", "1", 1);
    wipe();
    clear(0, 0, 1);
    eglSwapBuffers(dpy, ws);
    CHECK(box_is(100, 250, 100, 80, BLUE) && fake_ovl_live() == 0 && query(ws) == 0,
          "no VideoOverlay: plotted");
    unsetenv("FAKE_OVL_MISSING");

    /* Loaded: not before the program has swapped 3 times in a row */
    unsetenv("FAKE_OVL_MISSING");
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_live() == 0, "second swap: no overlay yet");

    /* the third makes an overlay and shows the frame through it */
    wipe();
    n = fake_update_calls;
    clear(0, 0, 1);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 100, 40);            /* bottom half red */
    clear(1, 0, 0);
    glDisable(GL_SCISSOR_TEST);
    b = fake_force_redraws;
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_live() == 1 && query(ws) == 1, "overlay made and shown (%d, %d)", fake_ovl_live(), query(ws));
    fake_ovl_info(&w, &h, &banks, &flags, NULL, &sw, &sh);
    CHECK(w == 100 && h == 80 && banks == 3 && flags == 0 && sw == 100 && sh == 80,
          "100x80, 3 buffers, TBGR, scaled 1:1 (%dx%d %d &%X %dx%d)", w, h, banks, flags, sw, sh);
    CHECK(shown() == 0, "buffer 0 shown");
    CHECK(RGB(fake_ovl_pixel(0, 5, 0)) == BLUE && RGB(fake_ovl_pixel(0, 99, 39)) == BLUE &&
          RGB(fake_ovl_pixel(0, 5, 79)) == RED && RGB(fake_ovl_pixel(0, 0, 40)) == RED,
          "buffer holds the frame, top row first (%06X %06X)",
          RGB(fake_ovl_pixel(0, 5, 0)), RGB(fake_ovl_pixel(0, 5, 79)));
    CHECK(RGB(fake_screen_pixel(150, 260)) == 0 && fake_update_calls == n, "nothing plotted");
    CHECK(fake_force_redraws == b + 1 && fake_force_rect[0] == 0x5000, "window area redrawn once when it appeared");

    /* the next frames go to buffers 1, 2, 0 */
    clear(0, 1, 0);
    eglSwapBuffers(dpy, ws);
    CHECK(shown() == 1 && RGB(fake_ovl_pixel(1, 50, 50)) == GREEN, "buffer 1");
    eglSwapBuffers(dpy, ws);
    CHECK(shown() == 2, "buffer 2");
    eglSwapBuffers(dpy, ws);
    CHECK(shown() == 0 && fake_force_redraws == b + 1 && fake_ovl_creates == 1, "buffer 0 again, no more redraws");

    /* vsync: wait only if none has passed since the last switch */
    n = fake_vsyncs;                        /* (resetting it would count as a vsync) */
    eglSwapBuffers(dpy, ws);
    eglSwapBuffers(dpy, ws);
    CHECK(fake_vsyncs == n + 2, "back-to-back swaps wait for a vsync each (%d)", fake_vsyncs - n);
    CHECK(eglSwapWouldWaitRISCOS(dpy, ws), "overlay, no vsync since the switch: a swap would wait");
    fake_vsyncs++;                          /* one passes by itself */
    CHECK(!eglSwapWouldWaitRISCOS(dpy, ws), "a vsync has passed: it wouldn't");
    eglSwapBuffers(dpy, ws);
    CHECK(fake_vsyncs == n + 3, "no wait when a vsync has passed (%d)", fake_vsyncs - n);
    eglSwapInterval(dpy, 0);
    eglSwapBuffers(dpy, ws);
    eglSwapBuffers(dpy, ws);
    CHECK(fake_vsyncs == n + 3, "swap interval 0: no waits (%d)", fake_vsyncs - n);
    eglSwapInterval(dpy, 1);

    /* a window over it: hidden, the sprite plotted; away: shown again */
    fake_open_window(0x5001, 350, 400, 600, 700, 0, 0);
    fake_window_behind = 0x5001;
    wipe();
    clear(1, 0, 0);
    eglSwapBuffers(dpy, ws);
    CHECK(shown() == -1 && query(ws) == 2 && box_is(100, 250, 100, 80, RED),
          "covered: overlay hidden, frame plotted (%d, %d)", shown(), query(ws));
    fake_open_window(0x5001, 800, 100, 1000, 200, 0, 0);
    b = fake_force_redraws;
    clear(0, 0, 1);
    eglSwapBuffers(dpy, ws);
    CHECK(shown() >= 0 && query(ws) == 1 && fake_force_redraws == b + 1,
          "a window in front that doesn't overlap: shown (%d)", shown());

    /* stopped swapping (a paused video): eglCheckOverlaysRISCOS */
    fake_open_window(0x5001, 350, 400, 600, 700, 0, 0);
    wipe();
    n = fake_update_calls;
    CHECK(eglCheckOverlaysRISCOS(dpy), "check overlays");
    CHECK(shown() == -1 && fake_update_calls == n + 1 && box_is(100, 250, 100, 80, BLUE),
          "check: covered, hidden and the last frame plotted");
    fake_open_window(0x5001, 800, 100, 1000, 200, 0, 0);
    eglCheckOverlaysRISCOS(dpy);
    CHECK(shown() >= 0 && query(ws) == 1, "check: uncovered, shown again");

    /* paused (no swap for 40 cs): the check goes back to plotting, keeps
       the overlay, and the next swap shows through it at once */
    fake_window_behind = -1;
    fake_open_window(0x5001, 800, 100, 1000, 200, 0, 0);
    n = fake_ovl_creates;
    b = fake_update_calls;
    wipe();
    fake_wimp_polls += 20;
    eglCheckOverlaysRISCOS(dpy);
    CHECK(shown() == -1 && query(ws) == 2 && fake_update_calls == b + 1 && box_is(100, 250, 100, 80, BLUE),
          "paused: overlay hidden, last frame plotted (%d, %d)", shown(), query(ws));
    eglCheckOverlaysRISCOS(dpy);
    CHECK(shown() == -1 && fake_update_calls == b + 1, "paused: stays plotted, no more work");
    eglSwapBuffers(dpy, ws);
    CHECK(shown() >= 0 && query(ws) == 1 && fake_ovl_creates == n, "resumed: shown at once, same overlay");
    fake_window_behind = 0x5001;
    fake_window_behind = -1;

    /* redraw requests: VideoOverlay_RedrawWindow instead of a plot */
    wipe();
    n = fake_ovl_redraws;
    block[0] = 0x5000;
    CHECK(eglRedrawWindowRISCOS(dpy, block), "redraw");
    CHECK(fake_ovl_redraws == n + 1 && RGB(fake_screen_pixel(150, 260)) == 0, "redraw went to the overlay");

    /* the program's "hardware acceleration" off and on */
    wipe();
    CHECK(eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, EGL_FALSE), "attrib off");
    CHECK(fake_ovl_live() == 0 && query(ws) == 0 && box_is(100, 250, 100, 80, BLUE), "off: gone, frame plotted");
    CHECK(!eglSwapWouldWaitRISCOS(dpy, ws), "plotted window: a swap never waits");
    wipe();
    clear(0, 1, 0);
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_live() == 0 && box_is(100, 250, 100, 80, GREEN), "off: swaps plot");
    CHECK(!eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, 7) && eglGetError() == EGL_BAD_PARAMETER, "bad value");
    CHECK(eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, EGL_TRUE), "attrib on");
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_live() == 1 && query(ws) == 1, "on again");

    /* the user's EGL$Overlay off */
    setenv("EGL$Overlay", "off", 1);
    wipe();
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_live() == 0 && box_is(100, 250, 100, 80, GREEN), "EGL$Overlay off: plotted");
    unsetenv("EGL$Overlay");
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_live() == 1, "EGL$Overlay unset: back");

    /* resize to 150x60: a new overlay of that size */
    fake_open_window(0x5000, 200, 300, 500, 420, 0, 0);
    eglSwapBuffers(dpy, ws);
    glViewport(0, 0, 150, 60);
    clear(0, 0, 1);
    eglSwapBuffers(dpy, ws);
    fake_ovl_info(&w, &h, &banks, NULL, NULL, &sw, &sh);
    CHECK(fake_ovl_live() == 1 && w == 150 && h == 60 && sw == 150 && sh == 60 && query(ws) == 1,
          "resized overlay %dx%d", w, h);
    CHECK(RGB(fake_ovl_pixel(shown(), 149, 59)) == BLUE, "resized frame in it");

    /* a mode change: made again */
    n = fake_ovl_creates;
    fake_set_screen(800, 600, 0, 5);
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_creates == n + 1 && fake_ovl_live() == 1 && query(ws) == 1, "new mode: new overlay");
    fake_set_screen(640, 480, 0, 5);
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_creates == n + 2 && fake_ovl_live() == 1, "and back");

    /* GPU short of memory: 2 buffers; none at all: plotted until it changes */
    eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, EGL_FALSE);
    setenv("FAKE_OVL_GPU_BYTES", "80000", 1);       /* a 150x60 buffer is 39840 */
    eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, EGL_TRUE);
    eglSwapBuffers(dpy, ws);
    fake_ovl_info(NULL, NULL, &banks, NULL, NULL, NULL, NULL);
    CHECK(fake_ovl_live() == 1 && banks == 2, "room for 2 buffers: 2 (%d)", banks);
    eglSwapBuffers(dpy, ws);
    eglSwapBuffers(dpy, ws);
    CHECK(shown() == 0, "2 buffers cycle");
    eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, EGL_FALSE);
    setenv("FAKE_OVL_GPU_BYTES", "30000", 1);
    eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, EGL_TRUE);
    n = fake_ovl_creates;
    wipe();
    clear(1, 0, 0);
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_live() == 0 && query(ws) == 0 && box_is(100, 270, 150, 60, RED), "no room: plotted");
    unsetenv("FAKE_OVL_GPU_BYTES");
    n = fake_ovl_creates;
    eglSwapBuffers(dpy, ws);
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_creates == n && fake_ovl_live() == 0, "failed: not retried every frame");
    eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, EGL_TRUE);
    eglSwapBuffers(dpy, ws);
    CHECK(fake_ovl_live() == 1, "retried when asked again");

    /* a work area surface in the same window: no overlay while it exists */
    {
        EGLint a[] = { EGL_WORK_AREA_X_RISCOS, 0, EGL_WORK_AREA_Y_RISCOS, 0,
                       EGL_WORK_AREA_WIDTH_RISCOS, 20, EGL_WORK_AREA_HEIGHT_RISCOS, 20, EGL_NONE };
        EGLSurface fx = eglCreateWindowSurface(dpy, cfg, 0x5000, a);
        CHECK(fx != EGL_NO_SURFACE, "work area surface");
        wipe();
        clear(0, 1, 0);
        eglSwapBuffers(dpy, ws);
        CHECK(fake_ovl_live() == 0 && box_is(120, 280, 100, 40, GREEN), "shared window: plotted");
        eglDestroySurface(dpy, fx);
        eglSwapBuffers(dpy, ws);
        CHECK(fake_ovl_live() == 1, "alone again: overlay");
    }

    /* EGL_OVERLAY_RISCOS at creation, and a TRGB surface */
    {
        EGLint off[] = { EGL_OVERLAY_RISCOS, EGL_FALSE, EGL_NONE };
        EGLint bad[] = { EGL_OVERLAY_RISCOS, 3, EGL_NONE };
        EGLContext c2 = eglCreateContext(dpy, trgb, EGL_NO_CONTEXT, NULL);
        EGLSurface s2;
        fake_open_window(0x5002, 700, 100, 900, 260, 0, 0);
        CHECK(eglCreateWindowSurface(dpy, cfg, 0x5002, bad) == EGL_NO_SURFACE &&
              eglGetError() == EGL_BAD_ATTRIBUTE, "bad creation value");
        s2 = eglCreateWindowSurface(dpy, cfg, 0x5002, off);
        eglMakeCurrent(dpy, s2, s2, ctx);
        eglSwapBuffers(dpy, s2);
        CHECK(fake_ovl_live() == 1 && query(s2) == 0, "created with EGL_FALSE: no overlay");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, s2);
        {
            EGLint on[] = { EGL_OVERLAY_RISCOS, EGL_TRUE, EGL_NONE };
            s2 = eglCreateWindowSurface(dpy, trgb, 0x5002, on);
        }
        eglMakeCurrent(dpy, s2, s2, c2);
        clear(1, 0, 0);
        eglSwapBuffers(dpy, s2);
        fake_wimp_polls += 20;              /* 40 cs later: not animating */
        eglSwapBuffers(dpy, s2);
        eglSwapBuffers(dpy, s2);
        CHECK(fake_ovl_live() == 1 && query(s2) == 0, "slow swaps: no overlay");
        eglSwapBuffers(dpy, s2);
        fake_ovl_info(NULL, NULL, NULL, &flags, NULL, NULL, NULL);
        CHECK(fake_ovl_live() == 2 && (flags & 0x4000) && RGB(fake_ovl_pixel(0, 3, 3)) == 0xFF0000u,
              "TRGB surface: TRGB overlay (&%X)", flags);
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, s2);
        eglDestroyContext(dpy, c2);
        CHECK(fake_ovl_live() == 1, "its overlay destroyed with it");
        eglMakeCurrent(dpy, ws, ws, ctx);
    }

    /* A Z-Order overlay isn't hidden when something is in front */
    setenv("FAKE_OVL_TYPE", "0", 1);
    eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, EGL_FALSE);
    eglSurfaceAttrib(dpy, ws, EGL_OVERLAY_RISCOS, EGL_TRUE);
    eglSwapBuffers(dpy, ws);                /* (3 in a row again: the last was long ago) */
    eglSwapBuffers(dpy, ws);
    eglSwapBuffers(dpy, ws);
    fake_open_window(0x5001, 350, 400, 600, 700, 0, 0);
    fake_window_behind = 0x5001;
    eglSwapBuffers(dpy, ws);
    CHECK(query(ws) == 1 && shown() >= 0, "Z-Order: stays shown when covered");
    fake_window_behind = -1;
    unsetenv("FAKE_OVL_TYPE");

    /* Opt-in: a surface that didn't ask gets none, unless EGL$Overlay on */
    {
        EGLint off[] = { EGL_OVERLAY_RISCOS, EGL_FALSE, EGL_NONE };
        EGLSurface s3;
        fake_open_window(0x5003, 700, 400, 900, 560, 0, 0);
        s3 = eglCreateWindowSurface(dpy, cfg, 0x5003, NULL);
        eglMakeCurrent(dpy, s3, s3, ctx);
        n = fake_ovl_live();
        eglSwapBuffers(dpy, s3); eglSwapBuffers(dpy, s3); eglSwapBuffers(dpy, s3); eglSwapBuffers(dpy, s3);
        CHECK(fake_ovl_live() == n && query(s3) == 0, "not asked for: no overlay");
        setenv("EGL$Overlay", "On", 1);
        eglSwapBuffers(dpy, s3);
        CHECK(fake_ovl_live() == n + 1 && query(s3) == 1, "EGL$Overlay On: overlay for it too");
        eglSurfaceAttrib(dpy, s3, EGL_OVERLAY_RISCOS, EGL_FALSE);
        eglSwapBuffers(dpy, s3);
        CHECK(fake_ovl_live() == n && query(s3) == 0, "EGL$Overlay on, program said EGL_FALSE: none");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, s3);
        s3 = eglCreateWindowSurface(dpy, cfg, 0x5003, off);
        eglMakeCurrent(dpy, s3, s3, ctx);
        eglSwapBuffers(dpy, s3); eglSwapBuffers(dpy, s3); eglSwapBuffers(dpy, s3);
        CHECK(fake_ovl_live() == n && query(s3) == 0, "EGL$Overlay on, created with EGL_FALSE: none");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(dpy, s3);
        unsetenv("EGL$Overlay");
        eglMakeCurrent(dpy, ws, ws, ctx);
    }

    /* destroying the surface destroys the overlay */
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, ws);
    eglDestroyContext(dpy, ctx);
    CHECK(fake_ovl_live() == 0, "no overlays left (%d)", fake_ovl_live());
    CHECK(fake_ovl_errors() == 0, "VideoOverlay used by the rules (%d problems)", fake_ovl_errors());
}
