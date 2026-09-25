/*
 * glestest - OpenGL ES on native RISC OS EGL: the same spinning cube as
 * dmxtest, but in a Wimp window (a normal multitasking desktop task), full
 * screen, or into a sprite, using the RISC OS native types directly.
 *
 * Usage: glestest [-2] [-f | -p] [-t secs] [-o file]
 *   default  ES 1.1 in a desktop window (resize it, cover it, close it)
 *   -2       ES 2.0 with GLSL ES shaders instead of ES 1.1
 *   -f       full screen (EGL_RISCOS_SCREEN_WINDOW) for 5 s or -t
 *   -p       render one frame into a 32bpp sprite, save Sprite file
 *            "glespixmap" (no desktop needed)
 *   -t secs  stop after this long; -o file: save the summary there
 * Build with es_cube.c. Link: -lEGL -lOSMesa -lstdc++ -lz -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <kernel.h>
#include <swis.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext_riscos.h>
#include <GLES/gl.h>
#include "hrtime.h"
#include "es_cube.h"

#define TASK 0x4B534154

static FILE *outf;
static char summary[1024];

static void say(const char *fmt, ...)
{
    size_t n = strlen(summary);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(summary + n, sizeof summary - n, fmt, ap);
    va_end(ap);
}

static int task;

static int wimp_start(void)
{
    static const int messages[] = { 0 };
    _kernel_swi_regs r;
    r.r[0] = 380; r.r[1] = TASK; r.r[2] = (int) "glestest"; r.r[3] = (int) messages;
    if (_kernel_swi(Wimp_Initialise, &r, &r) != NULL)
        return 0;
    task = r.r[1];
    return 1;
}

static void wimp_end(void)
{
    _kernel_swi_regs r;
    if (!task) return;
    r.r[0] = task; r.r[1] = TASK;
    _kernel_swi(Wimp_CloseDown, &r, &r);
    task = 0;
}

static EGLDisplay dpy;
static EGLConfig cfg;

static EGLContext es_context(int es2)
{
    static const EGLint attr[] = { EGL_DEPTH_SIZE, 16, EGL_SURFACE_TYPE,
                                   EGL_WINDOW_BIT | EGL_PIXMAP_BIT, EGL_NONE };
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, es2 ? 2 : 1, EGL_NONE };
    EGLint n = 0;
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, NULL, NULL) || !eglChooseConfig(dpy, attr, &cfg, 1, &n) || n < 1)
        return EGL_NO_CONTEXT;
    eglBindAPI(EGL_OPENGL_ES_API);          /* then contexts are OpenGL ES */
    return eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
}

/* A desktop window: the usual Wimp_Poll loop; EGL does the plotting. */
static int run_window(int es2, double limit)
{
    static char title[96] = "glestest";
    int wb[23], block[64], handle, quit = 0;
    _kernel_swi_regs r;
    EGLContext ctx;
    EGLSurface ws;
    EGLint w = 0, h = 0, lw = 0, lh = 0;
    double t0, tstart, last_title, render = 0;
    long frames = 0, title_frames = 0;
    float a = 0;

    if (!wimp_start()) { say("Needs the desktop\n"); return 1; }
    memset(wb, 0, sizeof wb);
    wb[0] = 200; wb[1] = 200; wb[2] = 200 + 1280; wb[3] = 200 + 960;
    wb[6] = -1;
    wb[7] = (int) 0xCF000002u;              /* moveable, back, close, title, toggle, adjust */
    wb[8] = 7 | (2 << 8) | (7 << 16) | (4 << 24);
    wb[9] = 3 | (1 << 8) | (12 << 16);
    wb[10] = 0; wb[11] = -2400; wb[12] = 3840; wb[13] = 0;
    wb[14] = 0x07000119;                    /* indirected text title */
    wb[16] = 1;
    wb[18] = (int) title; wb[19] = -1; wb[20] = sizeof title;
    r.r[1] = (int) wb;
    if (_kernel_swi(Wimp_CreateWindow, &r, &r) != NULL) { wimp_end(); return 1; }
    handle = r.r[0];
    block[0] = handle;
    memcpy(&block[1], wb, 7 * sizeof(int));
    r.r[1] = (int) block;
    _kernel_swi(Wimp_OpenWindow, &r, &r);

    /* The native window type: the Wimp window handle. */
    ctx = es_context(es2);
    ws = ctx != EGL_NO_CONTEXT ? eglCreateWindowSurface(dpy, cfg, handle, NULL) : EGL_NO_SURFACE;
    if (ws == EGL_NO_SURFACE || !eglMakeCurrent(dpy, ws, ws, ctx)) {
        say("EGL setup failed (0x%04x)\n", eglGetError());
        wimp_end();
        return 1;
    }
    eglQuerySurface(dpy, ws, EGL_WIDTH, &w);
    eglQuerySurface(dpy, ws, EGL_HEIGHT, &h);
    if (!es_cube_init(es2, w, h)) { say("shaders failed\n"); wimp_end(); return 1; }
    lw = w; lh = h;
    say("glestest window: %s\n", (const char *) glGetString(GL_VERSION));

    tstart = last_title = hr_seconds();
    while (!quit) {
        r.r[0] = 0;
        r.r[1] = (int) block;
        if (_kernel_swi(Wimp_Poll, &r, &r) != NULL) break;
        switch (r.r[0]) {
        case 0:                             /* null: a frame */
            eglQuerySurface(dpy, ws, EGL_WIDTH, &w);
            eglQuerySurface(dpy, ws, EGL_HEIGHT, &h);
            if (w != lw || h != lh) { es_cube_resize(w, h); lw = w; lh = h; }
            t0 = hr_seconds();
            es_cube_draw(a);
            glFinish();
            render += hr_seconds() - t0;
            eglSwapBuffers(dpy, ws);
            frames++; title_frames++;
            a += 2;
            if (hr_seconds() - last_title >= 1.0) {
                double now = hr_seconds();
                snprintf(title, sizeof title, "glestest %s %dx%d  %.1f fps",
                         es2 ? "ES 2.0" : "ES 1.1", w, h, title_frames / (now - last_title));
                r.r[0] = handle; r.r[1] = TASK; r.r[2] = 3;
                _kernel_swi(Wimp_ForceRedraw, &r, &r);
                title_frames = 0;
                last_title = now;
            }
            if (limit > 0 && hr_seconds() - tstart >= limit) quit = 1;
            break;
        case 1:                             /* redraw: EGL plots the surface */
            if (!eglRedrawWindowRISCOS(dpy, block)) {
                r.r[1] = (int) block;
                _kernel_swi(Wimp_RedrawWindow, &r, &r);
                while (r.r[0]) { r.r[1] = (int) block; _kernel_swi(Wimp_GetRectangle, &r, &r); }
            }
            break;
        case 2:
            r.r[1] = (int) block;
            _kernel_swi(Wimp_OpenWindow, &r, &r);
            break;
        case 3:
            quit = 1;
            break;
        case 17: case 18:
            if (block[4] == 0) quit = 1;
            break;
        }
    }
    {
        double t = hr_seconds() - tstart;
        say("window %dx%d: %ld frames in %.1f s = %.1f fps, render %.2f ms per frame\n",
            w, h, frames, t, frames / (t > 0 ? t : 1), frames ? 1000 * render / frames : 0);
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    wimp_end();
    return 0;
}

/* Full screen: the native window EGL_RISCOS_SCREEN_WINDOW. */
static int run_fullscreen(int es2, double limit)
{
    EGLContext ctx = es_context(es2);
    EGLSurface fs;
    EGLint w = 0, h = 0;
    double t0, tstart, render = 0;
    long frames = 0;
    float a = 0;
    int in_desktop = wimp_start();

    fs = ctx != EGL_NO_CONTEXT ? eglCreateWindowSurface(dpy, cfg, EGL_RISCOS_SCREEN_WINDOW, NULL)
                               : EGL_NO_SURFACE;
    if (fs == EGL_NO_SURFACE || !eglMakeCurrent(dpy, fs, fs, ctx)) {
        say("EGL setup failed (0x%04x)\n", eglGetError());
        wimp_end();
        return 1;
    }
    eglQuerySurface(dpy, fs, EGL_WIDTH, &w);
    eglQuerySurface(dpy, fs, EGL_HEIGHT, &h);
    if (!es_cube_init(es2, w, h)) { say("shaders failed\n"); wimp_end(); return 1; }
    say("glestest full screen: %s\n", (const char *) glGetString(GL_VERSION));
    tstart = hr_seconds();
    while (hr_seconds() - tstart < limit) {
        t0 = hr_seconds();
        es_cube_draw(a);
        glFinish();
        render += hr_seconds() - t0;
        eglSwapBuffers(dpy, fs);            /* waits for vsync */
        frames++;
        a += 2;
    }
    {
        double t = hr_seconds() - tstart;
        say("full screen %dx%d: %ld frames in %.1f s = %.1f fps, render %.2f ms per frame\n",
            w, h, frames, t, frames / (t > 0 ? t : 1), frames ? 1000 * render / frames : 0);
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    if (in_desktop) {
        _kernel_swi_regs r;
        r.r[0] = -1; r.r[1] = 0; r.r[2] = 0; r.r[3] = 0x7FFF; r.r[4] = 0x7FFF;
        _kernel_swi(Wimp_ForceRedraw, &r, &r);
        wimp_end();
    }
    return 0;
}

/* A sprite as the native pixmap: GL ES renders straight into it. */
static int run_pixmap(int es2)
{
    int size = 16 + 44 + 256 * 192 * 4 + 64, *area = malloc(size), *spr;
    _kernel_swi_regs r;
    EGLContext ctx;
    EGLSurface ps;

    if (!area) return 1;
    area[0] = size; area[1] = 0; area[2] = 16; area[3] = 16;
    r.r[0] = 256 + 15; r.r[1] = (int) area; r.r[2] = (int) "glespixmap"; r.r[3] = 0;
    r.r[4] = 256; r.r[5] = 192; r.r[6] = 1 | (90 << 1) | (90 << 14) | (6 << 27);
    if (_kernel_swi(OS_SpriteOp, &r, &r)) { say("no sprite\n"); return 1; }
    spr = (int *) ((char *) area + area[2]);
    ctx = es_context(es2);
    ps = ctx != EGL_NO_CONTEXT ? eglCreatePixmapSurface(dpy, cfg, spr, NULL) : EGL_NO_SURFACE;
    if (ps == EGL_NO_SURFACE || !eglMakeCurrent(dpy, ps, ps, ctx)) {
        say("EGL setup failed (0x%04x)\n", eglGetError());
        return 1;
    }
    if (!es_cube_init(es2, 256, 192)) { say("shaders failed\n"); return 1; }
    es_cube_draw(30);
    eglWaitClient();
    r.r[0] = 256 + 12; r.r[1] = (int) area; r.r[2] = (int) "glespixmap";
    say("glestest pixmap: %s, %s\n", (const char *) glGetString(GL_VERSION),
        _kernel_swi(OS_SpriteOp, &r, &r) == NULL ? "saved Sprite file glespixmap" : "save failed");
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    free(area);
    return 0;
}

int main(int argc, char **argv)
{
    int es2 = 0, mode = 'w', i, rc;
    double limit = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-2")) es2 = 1;
        else if (!strcmp(argv[i], "-f")) mode = 'f';
        else if (!strcmp(argv[i], "-p")) mode = 'p';
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) limit = atof(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) outf = fopen(argv[++i], "w");
        else { printf("usage: glestest [-2] [-f | -p] [-t secs] [-o file]\n"); return 1; }
    }
    if (mode == 'f') rc = run_fullscreen(es2, limit > 0 ? limit : 5);
    else if (mode == 'p') rc = run_pixmap(es2);
    else rc = run_window(es2, limit);
    if (outf) { fputs(summary, outf); fclose(outf); }
    else fputs(summary, stdout);                /* after Wimp_CloseDown */
    return rc;
}
