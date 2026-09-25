/*
 * egltest - checks riscos-mesa's EGL (egl/egl_riscos.c) on RISC OS.
 *
 * Usage:
 *   egltest [-o file]            checks without the desktop: strings, configs,
 *                                pbuffer and pixmap (sprite) rendering, error
 *                                cases. Saves the pixmap as Sprite file
 *                                "eglpixmap". Prints PASS/FAIL.
 *   egltest -w [-r] [-t secs] [-o file]
 *                                desktop window with a spinning cube, fps in
 *                                the title. Resize, scroll and cover it.
 *                                -r adds a second, fixed-size EGL surface at a
 *                                work area position (EGL_RISCOS_wimp_window).
 *   egltest -f [-d] [-b n] [-v n] [-s fv] [-p] [-t secs] [-o file]
 *                                full screen (native window -1) for 5 s or -t;
 *                                -d renders straight into screen memory
 *                                (EGL_SINGLE_BUFFER), -v swap interval (1),
 *                                -b 2|3 screen banks (experimental), -s fv
 *                                switches bank before the vsync wait,
 *                                -p a sweeping bar instead of the cube
 *                                (makes tearing easy to see).
 *   egltest -w -R                 as -r, but the second surface has its own
 *                                GL context.
 * The desktop modes print a summary when they finish (printing while a Wimp
 * task pops up a command window); -o also writes it to a file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>
#include <swis.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/eglext_riscos.h>
#include <GL/gl.h>

#include "hrtime.h"

#define TASK 0x4B534154

static FILE *outf;
static char summary[4096];
static size_t summary_len;
static int failures, checks;

/* In the desktop modes, collect output and print it at the end. */
static int collect;

static void say(const char *fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (collect) {
        size_t n = strlen(line);
        if (summary_len + n < sizeof summary) {
            memcpy(summary + summary_len, line, n + 1);
            summary_len += n;
        }
    } else {
        fputs(line, stdout);
    }
    if (outf) { fputs(line, outf); fflush(outf); }
}

#define CHECK(cond, what) do { checks++; if (cond) say("  ok   %s\n", what); \
    else { failures++; say("  FAIL %s (EGL error 0x%04x)\n", what, eglGetError()); } } while (0)

/* ------------------------------------------------------------------ */

static void cube(void)
{
    static const float n[6][3] = {{0,0,1},{0,0,-1},{0,1,0},{0,-1,0},{1,0,0},{-1,0,0}};
    static const float c[6][3] = {{1,.3f,.3f},{.3f,1,.3f},{.3f,.3f,1},{1,1,.3f},{1,.3f,1},{.3f,1,1}};
    static const int f[6][4][3] = {
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
        {{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1},{ 1,-1,-1}},
        {{-1, 1,-1},{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1}},
        {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},
        {{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1},{ 1,-1, 1}},
        {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}}};
    int i, j;
    glBegin(GL_QUADS);
    for (i = 0; i < 6; i++) {
        glColor3fv(c[i]);
        glNormal3fv(n[i]);
        for (j = 0; j < 4; j++)
            glVertex3f((float)f[i][j][0], (float)f[i][j][1], (float)f[i][j][2]);
    }
    glEnd();
}

/* Draw the cube at angle a into a w x h viewport. */
static float zoom = 6;             /* camera distance; the scroll wheel changes it */

static void scene(int w, int h, float a, float r, float g, float b)
{
    static const float lpos[4] = {2, 3, 4, 0};
    glViewport(0, 0, w, h);
    glClearColor(r, g, b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glLightfv(GL_LIGHT0, GL_POSITION, lpos);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-(float) w / h, (float) w / h, -1, 1, 2, 20);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -zoom);
    glRotatef(a, 1, 0.7f, 0.3f);
    cube();
}

static EGLDisplay dpy;

static EGLConfig pick_config(EGLint surface_type, EGLint depth)
{
    EGLint attrs[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_SURFACE_TYPE, surface_type,
                       EGL_DEPTH_SIZE, depth, EGL_NONE };
    EGLConfig cfg = NULL;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n) || n < 1)
        return NULL;
    return cfg;
}

static int egl_start(void)
{
    EGLint major = 0, minor = 0;
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
        say("EGL initialise failed (0x%04x)\n", eglGetError());
        return 0;
    }
    eglBindAPI(EGL_OPENGL_API);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Checks mode                                                         */

static int *make_sprite_area(int w, int h, int **sprite)
{
    int size = 16 + 44 + w * h * 4 + 64;
    int *area = malloc(size);
    _kernel_swi_regs r;
    if (!area) return NULL;
    area[0] = size; area[1] = 0; area[2] = 16; area[3] = 16;
    r.r[0] = 256 + 15;
    r.r[1] = (int) area;
    r.r[2] = (int) "eglpixmap";
    r.r[3] = 0;
    r.r[4] = w;
    r.r[5] = h;
    r.r[6] = 1 | (90 << 1) | (90 << 14) | (6 << 27);
    if (_kernel_swi(OS_SpriteOp, &r, &r)) { free(area); return NULL; }
    r.r[0] = 256 + 24;                  /* select sprite: R2 = its address */
    r.r[1] = (int) area;
    r.r[2] = (int) "eglpixmap";
    if (_kernel_swi(OS_SpriteOp, &r, &r)) { free(area); return NULL; }
    *sprite = (int *) r.r[2];
    return area;
}

static int run_checks(void)
{
    EGLConfig cfgs[16], cfg;
    EGLint n = 0, i, v[6];
    EGLSurface pb, ps;
    EGLContext ctx;
    unsigned char px[4];
    int *area, *spr;
    const char *s;

    say("egltest checks\n");
    if (!egl_start()) return 1;
    say("EGL_VENDOR     %s\n", eglQueryString(dpy, EGL_VENDOR));
    say("EGL_VERSION    %s\n", eglQueryString(dpy, EGL_VERSION));
    say("EGL_CLIENT_APIS %s\n", eglQueryString(dpy, EGL_CLIENT_APIS));
    say("EGL_EXTENSIONS %s\n", eglQueryString(dpy, EGL_EXTENSIONS));
    eglGetConfigs(dpy, cfgs, 16, &n);
    say("%d configs (id depth stencil visual):\n", n);
    for (i = 0; i < n; i++) {
        eglGetConfigAttrib(dpy, cfgs[i], EGL_CONFIG_ID, &v[0]);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_DEPTH_SIZE, &v[1]);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_STENCIL_SIZE, &v[2]);
        eglGetConfigAttrib(dpy, cfgs[i], EGL_NATIVE_VISUAL_ID, &v[3]);
        say("   %d  %2d %d  %s\n", v[0], v[1], v[2], v[3] ? "TRGB 0x00RRGGBB" : "TBGR 0x00BBGGRR");
    }

    cfg = pick_config(EGL_PBUFFER_BIT | EGL_PIXMAP_BIT, 16);
    CHECK(cfg != NULL, "config with depth for pbuffer + pixmap");
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
    CHECK(ctx != EGL_NO_CONTEXT, "create GL context");
    {
        EGLint pa[] = { EGL_WIDTH, 128, EGL_HEIGHT, 64, EGL_NONE };
        pb = eglCreatePbufferSurface(dpy, cfg, pa);
    }
    CHECK(pb != EGL_NO_SURFACE, "pbuffer 128x64");
    CHECK(eglMakeCurrent(dpy, pb, pb, ctx), "make pbuffer current");
    s = (const char *) glGetString(GL_VERSION);
    say("GL_VERSION  %s\nGL_RENDERER %s\n", s ? s : "(null)", (const char *) glGetString(GL_RENDERER));
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0, "pbuffer cleared red, read back");

    area = make_sprite_area(96, 96, &spr);
    CHECK(area != NULL, "32bpp sprite for the pixmap");
    if (area) {
        unsigned int *pix = (unsigned int *) ((char *) spr + spr[8]);
        ps = eglCreatePixmapSurface(dpy, cfg, spr, NULL);
        CHECK(ps != EGL_NO_SURFACE, "pixmap surface on the sprite");
        CHECK(eglMakeCurrent(dpy, ps, ps, ctx), "make pixmap current");
        scene(96, 96, 30, 0.3f, 0, 0);
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 95, 4, 1);                 /* top row: 4 green pixels */
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        eglWaitClient();
        say("sprite pixel (0,0) = %08x, (50,50) = %08x\n", pix[0], pix[50 * 96 + 50]);
        CHECK((pix[0] & 0xFFFFFF) == 0x00FF00, "GL rendered into the sprite (top row green)");
        CHECK((pix[95 * 96] & 0xFF) > 0x40 && (pix[95 * 96] & 0xFFFF00) == 0,
              "background dark red, in 0x00BBGGRR order");
        {
            _kernel_swi_regs r;
            r.r[0] = 256 + 12;                  /* save sprite file */
            r.r[1] = (int) area;
            r.r[2] = (int) "eglpixmap";
            CHECK(_kernel_swi(OS_SpriteOp, &r, &r) == NULL, "saved Sprite file eglpixmap (open in Paint)");
        }
        eglMakeCurrent(dpy, pb, pb, ctx);
        CHECK(eglDestroySurface(dpy, ps), "destroy pixmap surface");
    }

    /* error cases */
    CHECK(!eglBindAPI(EGL_OPENGL_ES_API) && eglGetError() == EGL_BAD_PARAMETER, "GLES refused (not yet)");
    {
        EGLint a[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 3,
                       EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE };
        CHECK(eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, a) == EGL_NO_CONTEXT &&
              eglGetError() == EGL_BAD_MATCH, "GL 3.3 core refused");
    }
    CHECK(eglCreateWindowSurface(dpy, cfg, 0x12345678, NULL) == EGL_NO_SURFACE &&
          eglGetError() == EGL_BAD_NATIVE_WINDOW, "bogus window handle refused");
    CHECK(eglGetProcAddress("glGenBuffers") != NULL, "eglGetProcAddress(glGenBuffers)");
    CHECK(eglGetProcAddress("eglRedrawWindowRISCOS") != NULL, "eglGetProcAddress(eglRedrawWindowRISCOS)");

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, pb);
    eglDestroyContext(dpy, ctx);
    CHECK(eglTerminate(dpy), "terminate");
    say("%d checks, %d failed: %s\n", checks, failures, failures ? "FAIL" : "PASS");
    free(area);
    return failures != 0;
}

/* ------------------------------------------------------------------ */
/* Desktop window mode                                                 */

static int task_handle;

static int wimp_start(const char *name)
{
    static const int messages[] = { 0 };
    _kernel_swi_regs r;
    r.r[0] = 380;
    r.r[1] = TASK;
    r.r[2] = (int) name;
    r.r[3] = (int) messages;
    if (_kernel_swi(Wimp_Initialise, &r, &r) != NULL)
        return 0;
    task_handle = r.r[1];
    return 1;
}

static void wimp_end(void)
{
    _kernel_swi_regs r;
    if (!task_handle) return;
    r.r[0] = task_handle;
    r.r[1] = TASK;
    _kernel_swi(Wimp_CloseDown, &r, &r);
    task_handle = 0;
}

static int fx_errors;
static EGLint fx_last_error;
static EGLContext fx_ctx;

/* Diagnostic for the work area surface: what GL thinks it drew, and what
   is actually in the surface's memory (copied out with eglCopyBuffers into
   a sprite, also saved as Sprite file "eglfx"). */
static int *make_sprite_area(int w, int h, int **sprite);
static void fx_check(EGLSurface fx)
{
    unsigned char px[4] = { 0, 0, 0, 0 };
    GLint vp[4] = { 0, 0, 0, 0 };
    int *area, *spr;
    glFinish();
    glReadPixels(80, 60, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glGetIntegerv(GL_VIEWPORT, vp);
    say("second surface check: GL error 0x%x, viewport %d,%d %dx%d, GL reads R%02x G%02x B%02x",
        glGetError(), vp[0], vp[1], vp[2], vp[3], px[0], px[1], px[2]);
    area = make_sprite_area(160, 120, &spr);
    if (area) {
        _kernel_swi_regs r;
        unsigned int *pix = (unsigned int *) ((char *) spr + spr[8]);
        if (eglCopyBuffers(dpy, fx, spr))
            say(", surface memory %08x (0x00BBGGRR)\n", pix[59 * 160 + 80]);
        else
            say(", eglCopyBuffers failed 0x%04x\n", eglGetError());
        r.r[0] = 256 + 12;
        r.r[1] = (int) area;
        r.r[2] = (int) "eglfx";
        _kernel_swi(OS_SpriteOp, &r, &r);
        free(area);
    } else {
        say("\n");
    }
}

/* Scroll wheel zoom (as in sdlgltest): OS_Pointer 2 gives the wheel's
   accumulated position; only used while the pointer is over our window,
   and not with -r, where the wheel scrolls the window. */
static void wheel_zoom(int handle, int second)
{
    static int last_y, valid;
    int ptr[5];
    _kernel_swi_regs r;
    int dy;

    r.r[0] = 2;
    if (_kernel_swi(OS_Pointer, &r, &r) != NULL)
        return;
    dy = r.r[1] - last_y;
    last_y = r.r[1];
    if (!valid) { valid = 1; return; }
    if (dy == 0 || dy > 64 || dy < -64 || second)
        return;
    r.r[1] = (int) ptr;
    if (_kernel_swi(Wimp_GetPointerInfo, &r, &r) != NULL || ptr[3] != handle)
        return;
    zoom -= 0.5f * dy;
    if (zoom < 3.5f) zoom = 3.5f;
    if (zoom > 20) zoom = 20;
}

static int run_window(int second, double limit)
{
    static char title[96] = "egltest";
    int wb[23], block[64], handle;
    _kernel_swi_regs r;
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface ws, fx = EGL_NO_SURFACE;
    EGLint w = 0, h = 0;
    double t0, t1, t2, tstart, last_title, render = 0, present = 0;
    long frames = 0, title_frames = 0;
    float a = 0;
    int quit = 0;

    collect = 1;
    if (!wimp_start("egltest")) { collect = 0; say("Needs the desktop (Wimp_Initialise failed)\n"); return 1; }
    if (!egl_start()) { wimp_end(); collect = 0; fputs(summary, stdout); return 1; }

    memset(wb, 0, sizeof wb);
    wb[0] = 200; wb[1] = 200; wb[2] = 200 + 1280; wb[3] = 200 + 960;   /* 640x480 at eig 1 */
    wb[4] = 0; wb[5] = 0; wb[6] = -1;
    /* Moveable, back, close, title, toggle size, adjust size. Scroll bars
       only with -r: the main surface is pinned to the visible area, so
       without a work area surface there's nothing to scroll, and the wheel
       zooms instead. */
    wb[7] = (int) (second ? 0xFF000002u : 0xCF000002u);
    wb[8] = 7 | (2 << 8) | (7 << 16) | (4 << 24);   /* title fg/bg, work fg/bg (grey) */
    wb[9] = 3 | (1 << 8) | (12 << 16);
    wb[10] = 0; wb[11] = -2400; wb[12] = 3840; wb[13] = 0;
    wb[14] = 0x07000119;                    /* title: text, centred, indirected */
    wb[15] = 0;
    wb[16] = 1;
    wb[17] = 0;
    wb[18] = (int) title; wb[19] = -1; wb[20] = sizeof title;
    wb[21] = 0;
    r.r[1] = (int) wb;
    if (_kernel_swi(Wimp_CreateWindow, &r, &r) != NULL) { wimp_end(); return 1; }
    handle = r.r[0];
    block[0] = handle;
    memcpy(&block[1], wb, 7 * sizeof(int));
    r.r[1] = (int) block;
    _kernel_swi(Wimp_OpenWindow, &r, &r);

    cfg = pick_config(EGL_WINDOW_BIT, 16);
    ctx = cfg ? eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL) : EGL_NO_CONTEXT;
    ws = cfg ? eglCreateWindowSurface(dpy, cfg, handle, NULL) : EGL_NO_SURFACE;
    fx_ctx = ctx;
    if (second == 2 && ctx != EGL_NO_CONTEXT)
        fx_ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);   /* -R: its own context */
    if (second && cfg) {
        EGLint fa[] = { EGL_WORK_AREA_X_RISCOS, 32, EGL_WORK_AREA_Y_RISCOS, -32,
                        EGL_WORK_AREA_WIDTH_RISCOS, 160, EGL_WORK_AREA_HEIGHT_RISCOS, 120, EGL_NONE };
        fx = eglCreateWindowSurface(dpy, cfg, handle, fa);
        if (fx == EGL_NO_SURFACE) say("work area surface failed (0x%04x)\n", eglGetError());
    }
    if (ctx == EGL_NO_CONTEXT || ws == EGL_NO_SURFACE || !eglMakeCurrent(dpy, ws, ws, ctx)) {
        say("EGL window setup failed (0x%04x)\n", eglGetError());
        wimp_end();
        collect = 0;
        fputs(summary, stdout);
        return 1;
    }
    say("GL_RENDERER %s, GL_VERSION %s\n", (const char *) glGetString(GL_RENDERER),
        (const char *) glGetString(GL_VERSION));

    tstart = last_title = hr_seconds();
    while (!quit) {
        r.r[0] = 0;                         /* null events on: animate */
        r.r[1] = (int) block;
        if (_kernel_swi(Wimp_Poll, &r, &r) != NULL) break;
        switch (r.r[0]) {
        case 0:                             /* null: draw a frame */
            eglQuerySurface(dpy, ws, EGL_WIDTH, &w);
            eglQuerySurface(dpy, ws, EGL_HEIGHT, &h);
            t0 = hr_seconds();
            scene(w, h, a, 0.1f, 0.1f, 0.25f);
            glFinish();
            t1 = hr_seconds();
            eglSwapBuffers(dpy, ws);
            if (fx != EGL_NO_SURFACE) {
                float z = zoom;
                zoom = 6;
                if (!eglMakeCurrent(dpy, fx, fx, fx_ctx)) {
                    fx_errors++;
                    fx_last_error = eglGetError();
                } else {
                    scene(160, 120, -2 * a, 0.3f, 0.1f, 0.1f);
                    if (frames == 5)
                        fx_check(fx);
                    if (!eglSwapBuffers(dpy, fx)) {
                        fx_errors++;
                        fx_last_error = eglGetError();
                    }
                }
                zoom = z;
                eglMakeCurrent(dpy, ws, ws, ctx);
            }
            wheel_zoom(handle, second);
            t2 = hr_seconds();
            render += t1 - t0;
            present += t2 - t1;
            frames++;
            title_frames++;
            a += 2;
            if (t2 - last_title >= 1.0) {
                snprintf(title, sizeof title, "egltest %dx%d  %.1f fps  render %.1f ms  present %.1f ms",
                         w, h, title_frames / (t2 - last_title),
                         1000 * render / frames, 1000 * present / frames);
                r.r[0] = handle; r.r[1] = TASK; r.r[2] = 3;    /* redraw title bar */
                _kernel_swi(Wimp_ForceRedraw, &r, &r);
                title_frames = 0;
                last_title = t2;
            }
            if (limit > 0 && t2 - tstart >= limit) quit = 1;
            break;
        case 1:                             /* redraw request */
            if (!eglRedrawWindowRISCOS(dpy, block)) {
                r.r[1] = (int) block;       /* not ours: empty redraw loop */
                _kernel_swi(Wimp_RedrawWindow, &r, &r);
                while (r.r[0]) { r.r[1] = (int) block; _kernel_swi(Wimp_GetRectangle, &r, &r); }
            }
            break;
        case 2:                             /* open request */
            r.r[1] = (int) block;
            _kernel_swi(Wimp_OpenWindow, &r, &r);
            break;
        case 3:                             /* close */
            quit = 1;
            break;
        case 17: case 18:
            if (block[4] == 0) quit = 1;    /* Message_Quit */
            break;
        }
    }
    {
        double t = hr_seconds() - tstart;
        say("window %dx%d: %ld frames in %.1f s = %.1f fps; render %.2f ms, present %.2f ms per frame%s\n",
            w, h, frames, t, frames / (t > 0 ? t : 1), frames ? 1000 * render / frames : 0,
            frames ? 1000 * present / frames : 0, fx != EGL_NO_SURFACE ? " (incl. the second surface)" : "");
        if (fx != EGL_NO_SURFACE)
            say("second surface: %d errors%s (last EGL error 0x%04x)\n", fx_errors,
                fx_errors ? " - it wasn't drawn" : "", fx_last_error);
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    wimp_end();
    collect = 0;
    fputs(summary, stdout);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Full screen mode                                                    */

static int flip_first, want_banks, pattern;

static int run_fullscreen(int direct, int interval, double limit)
{
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface fs;
    EGLint w, h, rb, banks = 0;
    char how[64];
    EGLint attrs[] = { EGL_RENDER_BUFFER, direct ? EGL_SINGLE_BUFFER : EGL_BACK_BUFFER,
                       EGL_FLIP_FIRST_RISCOS, flip_first,
                       EGL_SCREEN_BANKS_RISCOS, want_banks, EGL_NONE };
    int bar = 0;
    double t0, t1, t2, tstart, render = 0, present = 0;
    long frames = 0;
    float a = 0;
    int in_desktop;

    collect = 1;
    in_desktop = wimp_start("egltest full screen");
    if (!egl_start()) { collect = 0; fputs(summary, stdout); return 1; }
    cfg = pick_config(EGL_WINDOW_BIT, 16);
    ctx = cfg ? eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL) : EGL_NO_CONTEXT;
    fs = cfg ? eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, attrs) : EGL_NO_SURFACE;
    if (ctx == EGL_NO_CONTEXT || fs == EGL_NO_SURFACE || !eglMakeCurrent(dpy, fs, fs, ctx)) {
        say("EGL full screen setup failed (0x%04x)\n", eglGetError());
        wimp_end();
        collect = 0;
        fputs(summary, stdout);
        return 1;
    }
    eglSwapInterval(dpy, interval);
    eglQuerySurface(dpy, fs, EGL_WIDTH, &w);
    eglQuerySurface(dpy, fs, EGL_HEIGHT, &h);
    eglQuerySurface(dpy, fs, EGL_RENDER_BUFFER, &rb);
    eglQuerySurface(dpy, fs, EGL_SCREEN_BANKS_RISCOS, &banks);

    tstart = hr_seconds();
    do {
        t0 = hr_seconds();
        if (pattern) {
            /* -p: a white bar sweeping across black. A tear shows as the
               bar broken sideways at the tear line. */
            glViewport(0, 0, w, h);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_SCISSOR_TEST);
            glScissor(bar, 0, 48, h);
            glClearColor(1, 1, 1, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
            bar = (bar + 24) % (w > 48 ? w - 48 : 1);
        } else {
            scene(w, h, a, 0.1f, 0.25f, 0.1f);
        }
        glFinish();
        t1 = hr_seconds();
        eglSwapBuffers(dpy, fs);
        t2 = hr_seconds();
        render += t1 - t0;
        present += t2 - t1;
        frames++;
        a += 2;
    } while (t2 - tstart < limit);

    if (rb == EGL_SINGLE_BUFFER)
        snprintf(how, sizeof how, "direct to screen memory");
    else if (banks > 0)
        snprintf(how, sizeof how, "%d screen banks, %s", banks,
                 flip_first ? "switch then vsync" : "vsync then switch");
    else
        snprintf(how, sizeof how, "double buffered (sprite plot)");
    say("full screen %dx%d, %s, swap interval %d: %ld frames in %.1f s = %.1f fps; "
        "render %.2f ms, present %.2f ms per frame\n",
        w, h, how,
        interval, frames, t2 - tstart, frames / (t2 - tstart), 1000 * render / frames,
        1000 * present / frames);
    if (want_banks && banks == 0)
        say("  (no screen banks: not enough screen memory, or not a 32bpp mode in this colour order)\n");
    if (direct && rb != EGL_SINGLE_BUFFER)
        say("  (-d asked for direct rendering but the screen mode isn't 32bpp in this colour order)\n");
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    if (in_desktop) {
        _kernel_swi_regs r;
        r.r[0] = -1; r.r[1] = 0; r.r[2] = 0; r.r[3] = 0x7FFF; r.r[4] = 0x7FFF;
        _kernel_swi(Wimp_ForceRedraw, &r, &r);  /* repaint the desktop */
        wimp_end();
    }
    collect = 0;
    fputs(summary, stdout);
    return 0;
}

int main(int argc, char **argv)
{
    int mode = 'c', second = 0, direct = 0, interval = 1, i, rc;
    double limit = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w")) mode = 'w';
        else if (!strcmp(argv[i], "-f")) mode = 'f';
        else if (!strcmp(argv[i], "-r")) second = 1;
        else if (!strcmp(argv[i], "-R")) second = 2;
        else if (!strcmp(argv[i], "-d")) direct = 1;
        else if (!strcmp(argv[i], "-p")) pattern = 1;
        else if (!strcmp(argv[i], "-b") && i + 1 < argc) want_banks = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) flip_first = !strcmp(argv[++i], "fv");
        else if (!strcmp(argv[i], "-v") && i + 1 < argc) interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) limit = atof(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            outf = fopen(argv[++i], "w");
            if (!outf) printf("can't write %s\n", argv[i]);
        } else {
            printf("usage: egltest [-o file] | -w [-r] [-t secs] [-o file] | "
                   "-f [-d] [-b n] [-v n] [-s fv] [-p] [-t secs] [-o file]\n");
            return 1;
        }
    }
    if (mode == 'w') rc = run_window(second, limit);
    else if (mode == 'f') rc = run_fullscreen(direct, interval, limit > 0 ? limit : 5);
    else rc = run_checks();
    if (outf) fclose(outf);
    return rc;
}
