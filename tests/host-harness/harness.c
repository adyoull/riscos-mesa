/* Host harness for SDL_riscosopengl.c (Wimp-aware driver version).
   Emulates OS_SpriteOp 15 and the few SDL internals the file uses, and
   stands in for the driver's plot, then drives it the way SDL does:
   window, full screen in an XRGB mode, back to a window, resize. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/mman.h>
#include "../SDL_sysvideo.h"
#include "SDL_riscosvideo.h"
#include "SDL_riscoswindow.h"
#include "SDL_riscosopengl.h"
#include <kernel.h>
#include <GL/gl.h>

/* ---- SDL stubs ---- */
static char errbuf[256];
void *SDL_malloc(size_t n) {          /* RISC OS code casts pointers to int: stay below 2GB */
    static char *pool, *top; void *p;
    if (!pool) pool = top = mmap(NULL, 64<<20, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT, -1, 0);
    n = (n + 15) & ~(size_t)15; p = top; top += n; return p;
}
void *SDL_calloc(size_t a, size_t b) { void *p = SDL_malloc(a*b); memset(p, 0, a*b); return p; }
void SDL_free(void *p) { (void)p; }
size_t SDL_strlcpy(char *d, const char *s, size_t n) { snprintf(d, n, "%s", s); return strlen(s); }
int SDL_SetError(const char *fmt, ...) { va_list a; va_start(a, fmt); vsnprintf(errbuf, sizeof errbuf, fmt, a); va_end(a); return -1; }
int SDL_Error(SDL_errorcode c) { return SDL_SetError("error %d", (int)c); }
static SDL_GLContext current_ctx;
SDL_GLContext SDL_GL_GetCurrentContext(void) { return current_ctx; }
int SDL_GetWindowDisplayIndex(SDL_Window *w) { (void)w; return 0; }
static Uint32 screen_format = SDL_PIXELFORMAT_XBGR8888;
static int fake_selector[8];
int SDL_GetCurrentDisplayMode(int i, SDL_DisplayMode *m) {
    (void)i; memset(m, 0, sizeof *m); m->format = screen_format; m->driverdata = fake_selector; return 0;
}
static int osbyte19;
int _kernel_osbyte(int a, int x, int y) { (void)x; (void)y; if (a == 19) osbyte19++; return 0; }
static Uint32 fake_ticks = 1000; static int delays; static Uint32 delay_ms_total;
Uint32 SDL_GetTicks(void) { return fake_ticks; }
void SDL_Delay(Uint32 ms) { fake_ticks += ms; }
SDL_bool RISCOS_WimpDelay(_THIS, Uint32 ms) { (void)_this; delays++; delay_ms_total += ms; fake_ticks += ms; return SDL_TRUE; }

/* ---- OS_SpriteOp 15 ---- */
static int last_mode_arg;
_kernel_oserror *_kernel_swi(int swi, _kernel_swi_regs *in, _kernel_swi_regs *out) {
    static _kernel_oserror e = {0, "bad"};
    (void)out;
    if (swi == 0x2E && (in->r[0] & 0xFF) == 15) {
        int *area = (int *)(uintptr_t)(unsigned)in->r[1];
        int w = in->r[4], h = in->r[5], bytes = 44 + w*4*h; int *spr;
        if (area[3] + bytes > area[0]) return &e;
        spr = (int *)((char *)area + area[3]); memset(spr, 0, 44);
        spr[0] = bytes; strncpy((char *)&spr[1], (char *)(uintptr_t)(unsigned)in->r[2], 12);
        spr[4] = w-1; spr[5] = h-1; spr[7] = 31; spr[8] = 44; spr[9] = 44; spr[10] = in->r[6];
        last_mode_arg = in->r[6];
        area[1]++; area[3] += bytes; return NULL;
    }
    return &e;
}

/* ---- the driver's plot, as seen by the screen ---- */
static int plots; static unsigned tl, bl; static int pw, ph;
int RISCOS_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *r, int n) {
    SDL_WindowData *d = (SDL_WindowData *)window->driverdata;
    unsigned *px = (unsigned *)((char *)d->fb_sprite + d->fb_sprite->image_offset);
    (void)_this; (void)r; (void)n;
    pw = d->fb_sprite->width + 1; ph = d->fb_sprite->height + 1;
    tl = px[0]; bl = px[(ph-1)*pw]; plots++; return 0;
}

static SDL_Window *cur_win;
static void frame(float r, float g, float b) {   /* top half colour, bottom half blue */
    glViewport(0, 0, cur_win->w, cur_win->h);
    glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(r,g,b); glRectf(-1,0,1,1);
}
#define CHECK(c, msg) do { if (c) printf("  ok   %s\n", msg); else { printf("  FAIL %s\n", msg); fails++; } } while (0)

int main(void) {
    SDL_VideoDevice dev; SDL_VideoData vd; SDL_Window win; SDL_WindowData *wd;
    SDL_GLContext ctx, ctx2; int fails = 0, p;
    memset(&dev,0,sizeof dev); memset(&vd,0,sizeof vd); memset(&win,0,sizeof win);
    wd = SDL_calloc(1, sizeof *wd); wd->window = &win;
    dev.driverdata = &vd; win.driverdata = wd; win.w = 64; win.h = 48;
    dev.gl_config.depth_size = 24; dev.gl_config.major_version = 2; dev.gl_config.minor_version = 1;

    cur_win = &win;
    RISCOS_GL_LoadLibrary(&dev, NULL);
    ctx = RISCOS_GL_CreateContext(&dev, &win); current_ctx = ctx;
    CHECK(ctx != NULL, "context created in an XBGR desktop mode");
    printf("  GL_VERSION %s\n", glGetString(GL_VERSION));
    CHECK(wd->gl_active && wd->fb_sprite, "GL sprite is the window framebuffer sprite");
    CHECK(last_mode_arg == (int)(intptr_t)fake_selector, "sprite made in the screen's own mode");

    frame(1,0,0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(plots == 1 && (tl & 0xFFFFFF) == 0x0000FF && (bl & 0xFFFFFF) == 0xFF0000,
          "window frame: red top (0x0000FF), blue bottom, rows top down");

    /* A Wimp redraw after being covered plots fb_sprite again: content kept */
    p = plots; RISCOS_UpdateWindowFramebuffer(&dev, &win, NULL, 0);
    CHECK(plots == p + 1 && (tl & 0xFFFFFF) == 0x0000FF, "redraw request shows the last GL frame");


    /* Full screen at 640x480 in a mode with the other RGB order: the swap
       notices, rebinds, skips that one empty frame; the next frame shows. */
    win.w = 640; win.h = 480; screen_format = SDL_PIXELFORMAT_XRGB8888;
    p = plots; RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(plots == p && wd->gl_w == 640, "mode/size change: rebind, no stale plot");
    frame(1,0,0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(pw == 640 && ph == 480 && last_mode_arg == (1 | (90 << 1) | (90 << 14) | (6 << 27)), "RGBA context in XRGB mode falls back to a type 6 sprite");
    CHECK((tl & 0xFFFFFF) == 0x0000FF, "  ...and red is still red");

    /* A context made while in the XRGB mode renders BGRA into the screen-mode sprite */
    ctx2 = RISCOS_GL_CreateContext(&dev, &win); current_ctx = ctx2;
    CHECK(ctx2 != NULL, "second context in XRGB mode");
    frame(1,0,0); RISCOS_GL_SwapWindow(&dev, &win);
    /* first swap may rebind (sprite mode changes to the screen's); draw again */
    frame(1,0,0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(last_mode_arg == (int)(intptr_t)fake_selector && (tl & 0xFFFFFF) == 0xFF0000,
          "XRGB mode: sprite in screen mode, red = 0x00FF0000");
    RISCOS_GL_DeleteContext(&dev, ctx2); current_ctx = ctx;

    /* Back to the desktop window */
    win.w = 64; win.h = 48; screen_format = SDL_PIXELFORMAT_XBGR8888;
    RISCOS_GL_MakeCurrent(&dev, &win, ctx);
    frame(0,1,0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(pw == 64 && (tl & 0xFFFFFF) == 0x00FF00, "back to a window: green top shown at 64x48");

    dev.gl_config.profile_mask = SDL_GL_CONTEXT_PROFILE_ES;
    CHECK(RISCOS_GL_CreateContext(&dev, &win) == NULL, "GLES refused");
    dev.gl_config.profile_mask = SDL_GL_CONTEXT_PROFILE_CORE; dev.gl_config.major_version = 3; dev.gl_config.minor_version = 2;
    CHECK(RISCOS_GL_CreateContext(&dev, &win) == NULL, "core 3.2 refused");
    printf("  (%s)\n", errbuf);

    /* ---- cooperative pacing (swap interval 1) ---- */
    dev.gl_config.profile_mask = 0; dev.gl_config.major_version = 2; dev.gl_config.minor_version = 1;
    RISCOS_GL_MakeCurrent(&dev, &win, ctx);
    RISCOS_GL_SetSwapInterval(&dev, 1);
    vd.wimp_window = 7; vd.wimp_sdl_window = &win;       /* a desktop window */
    { int k; osbyte19 = 0; delays = 0; delay_ms_total = 0;
      for (k = 0; k < 10; k++) { frame(0,0,1); fake_ticks += 2; RISCOS_GL_SwapWindow(&dev, &win); }
      printf("  window, vsync: 10 frames of 2 ms work -> %d cooperative waits, %u ms waited, OS_Byte 19 x%d\n",
             delays, (unsigned)delay_ms_total, osbyte19);
      CHECK(osbyte19 == 0, "window: never blocks the desktop with OS_Byte 19");
      CHECK(delays >= 8 && delay_ms_total >= 8 * 12, "window: waits ~1 frame period each frame, via the Wimp");
      /* a slow app (30 ms frames) must not be delayed further */
      delays = 0; for (k = 0; k < 5; k++) { fake_ticks += 30; RISCOS_GL_SwapWindow(&dev, &win); }
      CHECK(delays == 0, "window: no extra wait when frames are already slower than the display");
      vd.wimp_window = 0; vd.wimp_sdl_window = NULL;   /* full screen */
      osbyte19 = 0; delays = 0; RISCOS_GL_SwapWindow(&dev, &win);
      CHECK(osbyte19 == 1 && delays == 0, "full screen: real vsync with OS_Byte 19");
      RISCOS_GL_SetSwapInterval(&dev, 0); osbyte19 = 0; RISCOS_GL_SwapWindow(&dev, &win);
      CHECK(osbyte19 == 0, "vsync off: no waiting at all"); }

    RISCOS_GL_DeleteContext(&dev, ctx);
    RISCOS_GL_DestroyWindowBuffer(&win);
    CHECK(!wd->gl_active && !wd->fb_area, "window destroy frees the GL sprite");
    printf("%d plots, %s\n", plots, fails ? "FAILURES" : "ALL CHECKS PASSED");
    return fails;
}
