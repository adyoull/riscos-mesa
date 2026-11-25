/*
 * presenttest.c - EGL window surfaces shown by the GPU's present
 * (V3DRender 0.31, DRM_IOCTL_RISCOS_PRESENT) against the CPU plot.
 *
 * Opens a 640x400 window, draws into it through EGL (a background and a
 * moving box, with glClear and glScissor only) and:
 *   1. times frames for a few seconds (render, swap, frames a second);
 *   2. opens a menu over the window and keeps drawing, then reads screen
 *      pixels (OS_ReadPoint) inside the menu and inside the window, to
 *      check the menu stays on top (the Wimp's rectangles are honoured);
 *   3. closes the menu and checks the uncovered part is redrawn.
 * Results go to the file named by -o (default <PresentTest$Dir>.Out).
 * Run it twice, with *Set EGL$Present 0 and unset, to compare.
 *
 *   presenttest [-t seconds] [-m menu seconds] [-o file] [-i swap interval]
 *               [-16] [-sw] [-r width height] [-w width height] [-d]
 * -r: a render size (EGL_RENDER_WIDTH/HEIGHT_RISCOS), shown scaled to fill
 * the window; then the window's bottom left corner is checked too. -w: the
 * window's size (default 640x400). -d: a 24-bit depth buffer, cleared
 * each frame. -nf: no glFinish before the swap (the swap's time includes
 * the frame's drawing then, as in a game). -g N: N blended full-frame
 * quads a frame in the background's colour (load for the GPU; -N: green,
 * to see they're drawn). -p X Y: the window's bottom left (OS units;
 * default 400 300), off the screen's edge to try that.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_riscos.h>
#include <GL/gl.h>
#include "hrtime.h"

#define TASK 0x4B534154
static int WIN_W = 640, WIN_H = 400, depth, no_finish, gpu_load, pos_x = 400, pos_y = 300;

static int task;
static FILE *out;

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(out ? out : stdout, fmt, ap);
    va_end(ap);
    if (out) fflush(out);
}

/* the colour at a screen pixel (x, y from the top), as &BBGGRR; -1 if none */
static long long read_point(int x, int y)
{
    _kernel_swi_regs r;
    int xeig = 1, yeig = 1, h = 0;
    r.r[0] = -1; r.r[1] = 4; _kernel_swi(OS_ReadModeVariable, &r, &r); xeig = r.r[2];
    r.r[0] = -1; r.r[1] = 5; _kernel_swi(OS_ReadModeVariable, &r, &r); yeig = r.r[2];
    r.r[0] = -1; r.r[1] = 12; _kernel_swi(OS_ReadModeVariable, &r, &r); h = r.r[2] + 1;
    r.r[0] = x << xeig;
    r.r[1] = (h - 1 - y) << yeig;
    if (_kernel_swi(OS_ReadPoint, &r, &r) != NULL || r.r[4])
        return -1;
    return (long long) (unsigned) r.r[2] & 0xFFFFFF;    /* (without the alpha byte) */
}

/* whether a screen colour number is close to an &BBGGRR colour, in the
   current mode (true colour only: 16 or 32 bpp) */
static int colour_is(long long n, unsigned rgb)
{
    _kernel_swi_regs r;
    int l2;
    unsigned rr = rgb & 0xFF, gg = (rgb >> 8) & 0xFF, bb = (rgb >> 16) & 0xFF, R, G, B;
    r.r[0] = -1; r.r[1] = 9; _kernel_swi(OS_ReadModeVariable, &r, &r); l2 = r.r[2];
    if (n < 0) return 0;
    if (l2 == 5) {
        R = n & 0xFF; G = (n >> 8) & 0xFF; B = (n >> 16) & 0xFF;
        r.r[0] = -1; r.r[1] = 0; _kernel_swi(OS_ReadModeVariable, &r, &r);
        if (r.r[2] & 0x4000) { unsigned t = R; R = B; B = t; }      /* RGB order */
    } else if (l2 == 4) {
        r.r[0] = -1; r.r[1] = 0; _kernel_swi(OS_ReadModeVariable, &r, &r);
        if (r.r[2] & 0x80) {        /* 5:6:5 */
            R = (n & 31) << 3; G = ((n >> 5) & 63) << 2; B = ((n >> 11) & 31) << 3;
        } else {
            R = (n & 31) << 3; G = ((n >> 5) & 31) << 3; B = ((n >> 10) & 31) << 3;
        }
        if (r.r[2] & 0x4000) { unsigned t = R; R = B; B = t; }
    } else {
        return 0;
    }
    return abs((int) R - (int) rr) < 24 && abs((int) G - (int) gg) < 24 && abs((int) B - (int) bb) < 24;
}

static void scene(int w, int h, double t, unsigned bg)
{
    int bx = (int) ((w - 80) * (0.5 + 0.5 * __builtin_sin(t * 2.0)));
    glDisable(GL_SCISSOR_TEST);
    glClearColor((bg & 0xFF) / 255.0f, ((bg >> 8) & 0xFF) / 255.0f, ((bg >> 16) & 0xFF) / 255.0f, 1);
    glClear(GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0));
    if (gpu_load) {
        int q;
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (gpu_load < 0)
            glColor4f(0, 1, 0, 0.05f);  /* (-g -N: visible, to check it's drawn; colour checks fail) */
        else
            glColor4f((bg & 0xFF) / 255.0f, ((bg >> 8) & 0xFF) / 255.0f, ((bg >> 16) & 0xFF) / 255.0f, 0.5f);
        glBegin(GL_QUADS);
        for (q = 0; q < abs(gpu_load); q++) {
            glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
        }
        glEnd();
        glDisable(GL_BLEND);
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(bx, 20, 80, 80);            /* low down: clear of the pixels checked */
    glClearColor(1, 1, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

int main(int argc, char **argv)
{
    static const int messages[] = { 0 };
    static int menu[7 + 3 * 6];
    const char *outname = "<PresentTest$Dir>.Out";
    double secs = 5, menu_secs = 8, t0, t, swap_t = 0, render_t = 0, phase_t;
    int interval = -1, i, wb[23], block[64], handle, frames = 0, phase = 0, menu_x, menu_y;
    int checks = 0, fails = 0, wx0, wy0;
    _kernel_swi_regs r;
    EGLDisplay dpy;
    EGLConfig cfg;
    EGLContext ctx;
    EGLSurface ws;
    EGLint n, attrs[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                          EGL_BLUE_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE, 0, 0, 0, 0 };
    EGLint sw_attrs[] = { EGL_CONTEXT_SOFTWARE_RISCOS, EGL_TRUE, EGL_NONE };
    int software = 0, rw = 0, rh = 0;
    EGLint rs_attrs[] = { EGL_RENDER_WIDTH_RISCOS, 0, EGL_RENDER_HEIGHT_RISCOS, 0, EGL_NONE };

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t") && i + 1 < argc) secs = atof(argv[++i]);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) menu_secs = atof(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) outname = argv[++i];
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) interval = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-16")) {         /* a 16-bit (5:6:5) config */
            attrs[3] = 5; attrs[5] = 6; attrs[7] = 5;
            attrs[10] = EGL_MATCH_FORMAT_KHR; attrs[11] = EGL_FORMAT_RGB_565_KHR; attrs[12] = EGL_NONE;
        }
        else if (!strcmp(argv[i], "-sw")) software = 1;
        else if (!strcmp(argv[i], "-w") && i + 2 < argc) { WIN_W = atoi(argv[++i]); WIN_H = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-d")) depth = 1;
        else if (!strcmp(argv[i], "-nf")) no_finish = 1;
        else if (!strcmp(argv[i], "-p") && i + 2 < argc) { pos_x = atoi(argv[++i]); pos_y = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-g") && i + 1 < argc) gpu_load = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i + 2 < argc) { rw = atoi(argv[++i]); rh = atoi(argv[++i]); }
    }
    out = fopen(outname, "w");
    if (depth) {
        for (i = 0; attrs[i] != EGL_NONE; i += 2)
            ;
        attrs[i] = EGL_DEPTH_SIZE; attrs[i + 1] = 24; attrs[i + 2] = EGL_NONE;
    }

    r.r[0] = 380; r.r[1] = TASK; r.r[2] = (int) "PresentTest"; r.r[3] = (int) messages;
    if (_kernel_swi(Wimp_Initialise, &r, &r) != NULL) { say("no desktop\n"); return 1; }
    task = r.r[1];

    memset(wb, 0, sizeof wb);
    wx0 = pos_x; wy0 = pos_y;
    wb[0] = wx0; wb[1] = wy0; wb[2] = wx0 + WIN_W * 2; wb[3] = wy0 + WIN_H * 2;
    wb[6] = -1;
    wb[7] = (int) 0x87000042u;              /* moveable, may go off the screen, back, close, title */
    wb[8] = 7 | (2 << 8) | (7 << 16) | (1 << 24);
    wb[9] = 3 | (1 << 8) | (12 << 16);
    wb[10] = 0; wb[11] = -WIN_H * 2; wb[12] = WIN_W * 2; wb[13] = 0;
    wb[14] = 0x07000019;                    /* title: text, centred */
    memcpy(&wb[18], "PresentTest", 12);
    wb[21] = 0;
    r.r[1] = (int) wb;
    if (_kernel_swi(Wimp_CreateWindow, &r, &r) != NULL) { say("no window\n"); return 1; }
    handle = r.r[0];
    block[0] = handle;
    memcpy(&block[1], wb, 7 * sizeof(int));
    r.r[1] = (int) block;
    _kernel_swi(Wimp_OpenWindow, &r, &r);

    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(dpy, NULL, NULL);
    eglBindAPI(EGL_OPENGL_API);
    if (!eglChooseConfig(dpy, attrs, &cfg, 1, &n) || n < 1) { say("no config\n"); return 1; }
    ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, software ? sw_attrs : NULL);
    rs_attrs[1] = rw; rs_attrs[3] = rh;
    ws = eglCreateWindowSurface(dpy, cfg, handle, rw > 0 && rh > 0 ? rs_attrs : NULL);
    if (ctx == EGL_NO_CONTEXT || ws == EGL_NO_SURFACE || !eglMakeCurrent(dpy, ws, ws, ctx)) {
        say("EGL setup failed (0x%04x)\n", eglGetError());
        return 1;
    }
    if (interval >= 0)
        eglSwapInterval(dpy, interval);
    {
        EGLint bs = 0;
        eglGetConfigAttrib(dpy, cfg, EGL_BUFFER_SIZE, &bs);
        EGLint ds = 0;
        eglGetConfigAttrib(dpy, cfg, EGL_DEPTH_SIZE, &ds);
        say("config: %d bits, depth %d%s; window %dx%d\n", bs, ds, software ? ", software asked for" : "",
            WIN_W, WIN_H);
    }
    {
        GLint rb = 0, gb = 0, bb = 0;
        glGetIntegerv(GL_RED_BITS, &rb); glGetIntegerv(GL_GREEN_BITS, &gb); glGetIntegerv(GL_BLUE_BITS, &bb);
        say("GL colour bits %d/%d/%d, GL_VERSION %s\n", rb, gb, bb, (const char *) glGetString(GL_VERSION));
        if (rw > 0 && rh > 0)
            say("render size %dx%d, shown at %dx%d\n", rw, rh, WIN_W, WIN_H);
    }
    say("GL_RENDERER %s\nEGL$Present %s, swap interval %s\n", (const char *) glGetString(GL_RENDERER),
        getenv("EGL$Present") ? getenv("EGL$Present") : "(unset)", interval >= 0 ? "as -i" : "default");

    /* the menu: 3 items, opened over the middle of the window */
    memcpy(menu, "Present\0\0\0\0", 12);
    menu[3] = 0x00070207; menu[4] = 200; menu[5] = 44; menu[6] = 0;
    for (i = 0; i < 3; i++) {
        int *it = menu + 7 + 6 * i;       /* after the 28-byte header */
        it[0] = i == 2 ? 0x80 : 0;
        it[1] = -1;
        it[2] = 0x07000021;
        { const char *nm = i == 0 ? "First" : i == 1 ? "Second" : "Third"; memcpy(&it[3], nm, strlen(nm) + 1); }
    }
    menu_x = wx0 + WIN_W - 100;             /* OS units: the menu's top left */
    menu_y = wy0 + WIN_H + 100;

    t0 = phase_t = hr_seconds();
    while (phase < 3) {
        r.r[0] = 0;
        r.r[1] = (int) block;
        if (_kernel_swi(Wimp_Poll, &r, &r) != NULL) break;
        if (r.r[0] == 1) { eglRedrawWindowRISCOS(dpy, block); continue; }
        if (r.r[0] == 2) { r.r[1] = (int) block; _kernel_swi(Wimp_OpenWindow, &r, &r); continue; }
        if (r.r[0] == 3) break;
        if (r.r[0] != 0) continue;
        t = hr_seconds();
        scene(rw > 0 ? rw : WIN_W, rh > 0 ? rh : WIN_H, t - t0, phase == 1 ? 0xFF0000u : 0x400000u);  /* blue (&BBGGRR) */
        if (!no_finish)
            glFinish();
        if (frames == 0) {
            GLenum e = glGetError();
            if (e)
                say("GL error 0x%04x after the first frame\n", e);
        }
        render_t += hr_seconds() - t;
        t = hr_seconds();
        eglSwapBuffers(dpy, ws);
        swap_t += hr_seconds() - t;
        frames++;
        if (phase == 0 && t - phase_t >= secs) {
            say("phase 1: %d frames in %.2f s: %.1f fps; render+finish %.2f ms, swap %.2f ms a frame\n",
                frames, t - phase_t, frames / (t - phase_t), 1000 * render_t / frames, 1000 * swap_t / frames);
            r.r[1] = (int) menu; r.r[2] = menu_x; r.r[3] = menu_y;
            _kernel_swi(Wimp_CreateMenu, &r, &r);
            phase = 1; phase_t = t; frames = 0; render_t = swap_t = 0;
        } else if (phase == 1 && t - phase_t >= menu_secs) {
            /* pixels: inside the menu (its first item) and in the window
               away from it (the left edge, mid height) */
            int sh, l;
            r.r[0] = -1; r.r[1] = 12; _kernel_swi(OS_ReadModeVariable, &r, &r); sh = r.r[2] + 1;
            {
                int px = (menu_x + 40) / 2, py = sh - (menu_y - 30) / 2;
                long long in_menu = read_point(px, py);
                long long in_win = read_point((wx0 < 0 ? 0 : wx0 / 2) + 4, sh - (wy0 + WIN_H) / 2 - 20);
                checks += 2;
                l = colour_is(in_menu, 0xFF0000u);
                if (l) fails++;
                say("menu at pixel %d,%d: colour &%X: %s\n", px, py, (unsigned) in_menu,
                    l ? "FAIL: the window's blue drawn over the menu" : "ok, not the window's blue");
                l = colour_is(in_win, 0xFF0000u);
                if (!l) fails++;
                say("window at pixel %d,%d: colour &%X: %s\n", (wx0 < 0 ? 0 : wx0 / 2) + 4, sh - (wy0 + WIN_H) / 2 - 20,
                    (unsigned) in_win, l ? "ok, the window's blue" : "FAIL: not the window's blue");
            }
            {
                /* the window's bottom left corner: the frame fills the
                   window (scaled, with -r) */
                int cx = (wx0 < 0 ? 0 : wx0 / 2) + 3, cy = sh - wy0 / 2 - 4;
                long long c = read_point(cx, cy);
                checks++;
                l = colour_is(c, 0xFF0000u);
                if (!l) fails++;
                say("window corner at pixel %d,%d: colour &%X: %s\n", cx, cy, (unsigned) c,
                    l ? "ok, the window's blue" : "FAIL: not the window's blue");
            }
            say("phase 2 (menu open): %d frames, %.1f fps, swap %.2f ms\n", frames,
                frames / (t - phase_t), 1000 * swap_t / (frames ? frames : 1));
            r.r[1] = -1;
            _kernel_swi(Wimp_CreateMenu, &r, &r);  /* close it */
            phase = 2; phase_t = t; frames = 0;
        } else if (phase == 2 && t - phase_t >= 1.0) {
            int sh, px, py;
            long long c;
            r.r[0] = -1; r.r[1] = 12; _kernel_swi(OS_ReadModeVariable, &r, &r); sh = r.r[2] + 1;
            px = (menu_x + 40) / 2; py = sh - (menu_y - 30) / 2;
            c = read_point(px, py);
            checks++;
            if (!colour_is(c, 0x400000u)) fails++;
            say("menu closed: pixel %d,%d colour &%X: %s\n", px, py, (unsigned) c,
                colour_is(c, 0x400000u) ? "ok, the window drawn there again" : "FAIL: not the window");
            phase = 3;
        }
    }
    say("%d checks, %d failures: %s\n", checks, fails, fails ? "FAIL" : "PASS");
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(dpy, ws);
    eglDestroyContext(dpy, ctx);
    eglTerminate(dpy);
    r.r[0] = task; r.r[1] = TASK;
    _kernel_swi(Wimp_CloseDown, &r, &r);
    if (out) fclose(out);
    return fails != 0;
}
