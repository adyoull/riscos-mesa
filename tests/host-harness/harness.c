/* Host harness for SDL_riscosopengl.c: SDL's RISC OS GL windows, which are
   riscos-mesa EGL window surfaces. Links the driver file with the real EGL
   (egl/egl_riscos.c), a host OSMesa, the EGL harness's fake RISC OS
   (egl/fake_riscos.c: screen, Wimp windows, sprites) and the fake
   VideoOverlay (ovl/fake_ovl.c), and drives it the way SDL does: a desktop
   window, a render size (SDL_RISCOS_GL_RENDER_SIZE), redraws, full screen
   and back, the other colour order, an overlay (SDL_RISCOS_GL_OVERLAY) with
   frames held for a vsync, OpenGL ES, and the pacing of swap interval 1.
   See README.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <malloc.h>
#include <pthread.h>
#include <sys/mman.h>
#include "../SDL_sysvideo.h"
#include "SDL_riscosvideo.h"
#include "SDL_riscoswindow.h"
#include "SDL_riscosopengl.h"
#include <GL/gl.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext_riscos.h>
#include "fake_riscos.h"

void fake_ovl_init(void);
int fake_ovl_live(void);
int fake_ovl_errors(void);
int fake_ovl_info(int *w, int *h, int *banks, int *flags, int *shown, int *scale_w, int *scale_h);
extern int fake_ovl_displays;

/* ---- SDL stubs ---- */
static char errbuf[256];
void *SDL_malloc(size_t n) { return malloc(n); }
void *SDL_calloc(size_t a, size_t b) { return calloc(a, b); }
void SDL_free(void *p) { free(p); }
size_t SDL_strlcpy(char *d, const char *s, size_t n) { snprintf(d, n, "%s", s); return strlen(s); }
long SDL_strtol(const char *s, char **e, int b) { return strtol(s, e, b); }
int SDL_SetError(const char *fmt, ...) { va_list a; va_start(a, fmt); vsnprintf(errbuf, sizeof errbuf, fmt, a); va_end(a); return -1; }
int SDL_Error(SDL_errorcode c) { return SDL_SetError("error %d", (int) c); }
static SDL_GLContext current_ctx;
SDL_GLContext SDL_GL_GetCurrentContext(void) { return current_ctx; }
int SDL_GetWindowDisplayIndex(SDL_Window *w) { (void) w; return 0; }
static Uint32 screen_format = SDL_PIXELFORMAT_XBGR8888;
int SDL_GetCurrentDisplayMode(int i, SDL_DisplayMode *m) {
    (void) i; memset(m, 0, sizeof *m); m->format = screen_format; m->refresh_rate = 60;
    m->driverdata = (void *) (intptr_t) (1 | (90 << 1) | (90 << 14) | (6 << 27));  /* the screen's sprite mode */
    return 0;
}
static const char *hint_size, *hint_ovl, *hint_egl;
const char *SDL_GetHint(const char *name) {
    if (!strcmp(name, SDL_HINT_RISCOS_GL_RENDER_SIZE)) return hint_size;
    if (!strcmp(name, SDL_HINT_RISCOS_GL_OVERLAY)) return hint_ovl;
    if (!strcmp(name, SDL_HINT_RISCOS_GL_EGL)) return hint_egl;
    return NULL;
}
SDL_bool SDL_GetStringBoolean(const char *v, SDL_bool d) {
    if (!v || !*v) return d;
    return (*v == '0' || !strcmp(v, "false")) ? SDL_FALSE : SDL_TRUE;
}
static Uint32 fake_ticks = 1000; static int delays, vsync_in_delay; static Uint32 delay_ms_total;
Uint32 SDL_GetTicks(void) { return fake_ticks; }
static SDL_threadID fake_thread = 1;            /* the calling thread, as SDL sees it */
SDL_threadID SDL_ThreadID(void) { return fake_thread; }
void SDL_Delay(Uint32 ms) { fake_ticks += ms; }
SDL_bool RISCOS_WimpDelay(_THIS, Uint32 ms) {
    (void) _this; delays++; delay_ms_total += ms; fake_ticks += ms;
    if (vsync_in_delay) fake_vsyncs++;          /* a vsync goes by while we wait */
    return SDL_TRUE;
}

/* The sprite path shows its frames through the driver's framebuffer plot:
   record what would reach the screen. */
static int fb_plots; static unsigned fb_tl, fb_bl; static int fb_w, fb_h;
int RISCOS_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *r, int n) {
    SDL_WindowData *d = (SDL_WindowData *) window->driverdata;
    unsigned *p = (unsigned *) ((char *) d->fb_sprite + d->fb_sprite->image_offset);
    (void) _this; (void) r; (void) n;
    fb_w = d->fb_sprite->width + 1; fb_h = d->fb_sprite->height + 1;
    fb_tl = p[0]; fb_bl = p[(fb_h - 1) * fb_w]; fb_plots++; return 0;
}

#define CHECK(c, ...) do { if (c) { printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } \
    else { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
#define RGB(p) ((p) & 0xFFFFFFu)
static int fails;

static SDL_Window *cur_win;
static void frame(float r, float g, float b) {   /* top half colour, bottom half blue */
    glViewport(0, 0, cur_win->w, cur_win->h);
    glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(r, g, b); glRectf(-1, 0, 1, 1);
}
/* the screen pixel at x, y (from the top) */
static unsigned px(int x, int y) { return RGB(fake_screen_pixel(x, y)); }
static void wipe(void) { memset(fake_screen.mem, 0, (size_t) fake_screen.w * fake_screen.h * 4); }

/* window 0x6000: visible area OS 200..600 x 200..520 = 200x160 pixels,
   top left pixel (100, 220) on the 640x480 eig 1 screen */
#define WX 100
#define WY 220

static void *run(void *arg)
{
    SDL_VideoDevice dev; SDL_VideoData vd; SDL_Window win; SDL_WindowData *wd;
    SDL_GLContext ctx, ctx2;
    int sp, n, shown, sw, sh, w, h;
    int block[64];
    (void) arg;

    fake_set_screen(640, 480, 0, 5);
    fake_reset_clip();
    fake_open_window(0x6000, 200, 200, 600, 520, 0, 0);
    memset(&dev, 0, sizeof dev); memset(&vd, 0, sizeof vd); memset(&win, 0, sizeof win);
    wd = SDL_calloc(1, sizeof *wd); wd->window = &win;
    dev.driverdata = &vd; win.driverdata = wd; win.w = 200; win.h = 160;
    win.flags = SDL_WINDOW_OPENGL;
    vd.wimp_window = 0x6000; vd.wimp_sdl_window = &win;
    vd.main_thread = 1;
    dev.gl_config.depth_size = 16; dev.gl_config.major_version = 2; dev.gl_config.minor_version = 1;
    cur_win = &win;

    /* ---- no hints: the sprite path, as before ---- */
    RISCOS_GL_LoadLibrary(&dev, NULL);
    RISCOS_GL_ApplyRenderSize(&win);
    CHECK(win.w == 200 && wd->render_w == 0, "no render size hint: the window keeps its size");
    ctx = RISCOS_GL_CreateContext(&dev, &win); current_ctx = ctx;
    CHECK(ctx && wd->gl_active && wd->fb_sprite && !wd->egl_surface && !wd->gl_egl,
          "no hints: GL renders into the window's sprite (no EGL)");
    frame(1, 0, 0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(fb_plots == 1 && fb_w == 200 && RGB(fb_tl) == 0x0000FF && RGB(fb_bl) == 0xFF0000,
          "sprite path: frame plotted by the driver (red top, blue bottom)");
    RISCOS_GL_SetSwapInterval(&dev, 1);
    sp = fake_vsyncs; delays = 0;
    frame(1, 0, 0); fake_ticks += 2; RISCOS_GL_SwapWindow(&dev, &win);
    frame(1, 0, 0); fake_ticks += 2; RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(fake_vsyncs == sp && delays >= 1, "sprite path, vsync in a window: cooperative wait, no OS_Byte 19");
    RISCOS_GL_SetSwapInterval(&dev, 0);
    block[0] = 0x6000;
    CHECK(!RISCOS_GL_Redraw(&dev, &win, block), "sprite path: redraws stay the driver's");
    RISCOS_GL_DeleteContext(&dev, ctx);
    {
        SDL_GLContext es;
        const char *v;
        dev.gl_config.profile_mask = SDL_GL_CONTEXT_PROFILE_ES;
        dev.gl_config.major_version = 2; dev.gl_config.minor_version = 0;
        es = RISCOS_GL_CreateContext(&dev, &win);
        v = es ? (const char *) glGetString(GL_VERSION) : NULL;
        CHECK(es && v && strncmp(v, "OpenGL ES 2.0", 13) == 0 && !wd->egl_surface,
              "sprite path: GLES 2.0 context (%s)", v ? v : errbuf);
        if (es) RISCOS_GL_DeleteContext(&dev, es);
        dev.gl_config.profile_mask = 0; dev.gl_config.major_version = 2; dev.gl_config.minor_version = 1;
    }
    RISCOS_GL_DestroyWindowBuffer(&win);
    CHECK(!wd->gl_active && !wd->fb_area, "sprite freed");

    /* ---- SDL_RISCOS_GL_EGL=1: the EGL path, no render size ---- */
    hint_egl = "1";
    ctx = RISCOS_GL_CreateContext(&dev, &win); current_ctx = ctx;
    CHECK(ctx != NULL, "context (%s)", ctx ? "made" : errbuf);
    printf("  GL_VERSION %s\n", glGetString(GL_VERSION));
    CHECK(wd->gl_active && wd->egl_surface && wd->egl_handle == 0x6000, "an EGL surface on the desktop window");
    wipe(); sp = fake_scaled_plots;
    frame(1, 0, 0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(px(WX + 5, WY + 5) == 0x0000FF && px(WX + 195, WY + 155) == 0xFF0000 &&
          px(WX - 1, WY + 5) == 0 && px(WX + 200, WY + 5) == 0,
          "frame in the window: red top, blue bottom, nothing outside (%06X %06X)",
          px(WX + 5, WY + 5), px(WX + 195, WY + 155));
    CHECK(fake_scaled_plots == sp, "1:1: a plain plot, not a scaled one");
    wipe();
    block[0] = 0x6000;
    CHECK(RISCOS_GL_Redraw(&dev, &win, block) && px(WX + 5, WY + 5) == 0x0000FF,
          "Redraw_Window_Request: EGL redraws the last frame");

    /* ---- a render size: 100x80 stretched over the 200x160 window ---- */
    RISCOS_GL_DeleteContext(&dev, ctx); current_ctx = NULL;
    RISCOS_GL_DestroyWindowBuffer(&win);
    hint_egl = NULL;                                /* the render size selects EGL by itself */
    hint_size = "100x80";
    RISCOS_GL_ApplyRenderSize(&win);
    CHECK(win.w == 100 && win.h == 80 && wd->disp_w == 200 && wd->disp_h == 160 &&
          RISCOS_ShownW(&win) == 200 && RISCOS_ShownH(&win) == 160,
          "render size: the program sees 100x80, the desktop window stays 200x160");
    ctx = RISCOS_GL_CreateContext(&dev, &win); current_ctx = ctx;
    {
        EGLint ew = 0, eh = 0;
        eglQuerySurface(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW), EGL_WIDTH, &ew);
        eglQuerySurface(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW), EGL_HEIGHT, &eh);
        CHECK(ew == 100 && eh == 80, "EGL renders at 100x80 (%dx%d)", ew, eh);
    }
    wipe(); sp = fake_scaled_plots;
    frame(0, 1, 0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(fake_scaled_plots > sp && px(WX + 5, WY + 5) == 0x00FF00 && px(WX + 195, WY + 75) == 0x00FF00 &&
          px(WX + 5, WY + 85) == 0xFF0000 && px(WX + 195, WY + 155) == 0xFF0000,
          "stretched 2x over the whole window (%06X %06X)", px(WX + 195, WY + 75), px(WX + 195, WY + 155));
    wipe();
    RISCOS_GL_Redraw(&dev, &win, block);
    CHECK(px(WX + 150, WY + 40) == 0x00FF00 && px(WX + 150, WY + 120) == 0xFF0000, "redraw: stretched too");

    /* ---- full screen: the render size stretched to the screen ---- */
    vd.wimp_window = 0; vd.wimp_sdl_window = NULL;
    RISCOS_GL_SwapWindow(&dev, &win);               /* moves the surface: no frame */
    CHECK(wd->egl_handle == -1, "full screen: the surface moves to the screen");
    wipe(); sp = fake_scaled_plots;
    frame(1, 0, 0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(fake_scaled_plots > sp && px(5, 5) == 0x0000FF && px(634, 230) == 0x0000FF &&
          px(5, 250) == 0xFF0000 && px(634, 474) == 0xFF0000,
          "full screen: 100x80 stretched to 640x480 (%06X %06X)", px(634, 230), px(634, 474));

    /* ---- back to the window ---- */
    vd.wimp_window = 0x6000; vd.wimp_sdl_window = &win;
    RISCOS_GL_SwapWindow(&dev, &win);
    wipe();
    frame(0, 1, 0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(wd->egl_handle == 0x6000 && px(WX + 150, WY + 40) == 0x00FF00, "back in the window");

    /* ---- a new render size (SDL window resized by the driver) ---- */
    win.w = 50; win.h = 40;
    RISCOS_GL_SwapWindow(&dev, &win);               /* EGL takes the size at this swap */
    wipe();
    frame(1, 0, 0); RISCOS_GL_SwapWindow(&dev, &win);
    {
        EGLint ew = 0;
        eglQuerySurface(eglGetCurrentDisplay(), eglGetCurrentSurface(EGL_DRAW), EGL_WIDTH, &ew);
        CHECK(ew == 50 && px(WX + 195, WY + 5) == 0x0000FF && px(WX + 195, WY + 155) == 0xFF0000,
              "render size 50x40, still filling the window (%d)", ew);
    }
    win.w = 100; win.h = 80;
    RISCOS_GL_SwapWindow(&dev, &win);

    /* ---- the other colour order (XRGB screen): red stays red ---- */
    fake_set_screen(640, 480, 1, 5);
    screen_format = SDL_PIXELFORMAT_XRGB8888;
    ctx2 = RISCOS_GL_CreateContext(&dev, &win); current_ctx = ctx2;
    CHECK(ctx2 != NULL, "context in an XRGB mode");
    RISCOS_GL_SwapWindow(&dev, &win);               /* new config: new surface */
    wipe();
    frame(1, 0, 0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(px(WX + 5, WY + 5) == 0xFF0000 && px(WX + 5, WY + 155) == 0x0000FF,
          "XRGB screen: red is 0xFF0000, blue 0x0000FF (%06X)", px(WX + 5, WY + 5));
    RISCOS_GL_DeleteContext(&dev, ctx2);
    fake_set_screen(640, 480, 0, 5);
    screen_format = SDL_PIXELFORMAT_XBGR8888;
    current_ctx = ctx;
    RISCOS_GL_MakeCurrent(&dev, &win, ctx);

    /* ---- an overlay (SDL_RISCOS_GL_OVERLAY=1): made at the render size,
       scaled to the window; frames held for a vsync, never blocking ---- */
    fake_ovl_init();
    hint_ovl = "1";
    RISCOS_GL_DestroyWindowBuffer(&win);
    RISCOS_GL_MakeCurrent(&dev, &win, ctx);
    RISCOS_GL_SetSwapInterval(&dev, 0);
    for (n = 0; n < 3; n++) { frame(0, 1, 0); fake_vsyncs++; RISCOS_GL_SwapWindow(&dev, &win); }
    fake_ovl_info(&w, &h, NULL, NULL, &shown, &sw, &sh);
    CHECK(fake_ovl_live() == 1 && w == 100 && h == 80 && sw == 200 && sh == 160 && shown >= 0,
          "overlay: 100x80, scaled to 200x160 (%dx%d -> %dx%d)", w, h, sw, sh);
    n = fake_ovl_displays; delays = 0;
    frame(1, 0, 0); RISCOS_GL_SwapWindow(&dev, &win);       /* no vsync since the last switch */
    CHECK(wd->gl_pending == 1 && fake_ovl_displays == n && delays == 0,
          "vsync off, none since the last switch: frame held, no wait");
    CHECK(RISCOS_GL_Idle(&dev, SDL_FALSE) == 1, "the event loop is asked to come back soon");
    RISCOS_GL_Idle(&dev, SDL_TRUE);
    CHECK(wd->gl_pending == 1 && fake_ovl_displays == n, "still no vsync: still held");
    fake_vsyncs++;
    RISCOS_GL_Idle(&dev, SDL_TRUE);
    CHECK(wd->gl_pending == 0 && fake_ovl_displays == n + 1, "a vsync later the event loop shows it");
    /* A swap from another thread isn't held (only the event loop's thread
       shows held frames): EGL waits for the vsync instead (code review
       2026-10-02) */
    fake_thread = 2; n = fake_ovl_displays;
    frame(1, 0, 0); RISCOS_GL_SwapWindow(&dev, &win);     /* no vsync since the last switch */
    CHECK(wd->gl_pending == 0 && fake_ovl_displays == n + 1,
          "another thread's swap: not held, shown (%d)", fake_ovl_displays - n);
    fake_thread = 1;
    /* vsync on: wait for it cooperatively (the Wimp runs meanwhile) */
    RISCOS_GL_SetSwapInterval(&dev, 1);
    vsync_in_delay = 1; delays = 0; n = fake_ovl_displays; sp = fake_vsyncs;
    fake_ticks += 100;                              /* no pacing wait */
    frame(0, 1, 0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(wd->gl_pending == 0 && fake_ovl_displays == n + 1 && delays == 1 && fake_vsyncs == sp + 1,
          "vsync on: one cooperative wait, then shown (%d waits)", delays);
    vsync_in_delay = 0;
    /* covered by another window while idle: RISCOS_GL_Idle hides it */
    fake_open_window(0x6001, 300, 300, 700, 700, 0, 0);
    fake_window_behind = 0x6001;
    RISCOS_GL_Idle(&dev, SDL_TRUE);
    fake_ovl_info(NULL, NULL, NULL, NULL, &shown, NULL, NULL);
    CHECK(shown == -1, "covered while idle: the event loop hides the overlay");
    fake_window_behind = -1;
    RISCOS_GL_Idle(&dev, SDL_TRUE);
    fake_ovl_info(NULL, NULL, NULL, NULL, &shown, NULL, NULL);
    CHECK(shown >= 0, "uncovered: shown again");
    CHECK(RISCOS_GL_Idle(&dev, SDL_FALSE) == 10, "while an overlay is in use the event loop wakes 10 times a second");
    /* full screen: the overlay goes (the driver destroys the surface with
       the desktop window) */
    RISCOS_GL_DestroyWindowBuffer(&win);
    CHECK(fake_ovl_live() == 0, "surface destroyed: overlay gone");
    vd.wimp_window = 0; vd.wimp_sdl_window = NULL;
    RISCOS_GL_SwapWindow(&dev, &win);
    frame(0, 1, 0); RISCOS_GL_SwapWindow(&dev, &win);
    CHECK(fake_ovl_live() == 0 && wd->egl_handle == -1, "full screen: no overlay");
    vd.wimp_window = 0x6000; vd.wimp_sdl_window = &win;
    RISCOS_GL_DestroyWindowBuffer(&win);
    hint_ovl = NULL;
    RISCOS_GL_MakeCurrent(&dev, &win, ctx);

    /* ---- OpenGL ES and refused versions ---- */
    dev.gl_config.profile_mask = SDL_GL_CONTEXT_PROFILE_ES;
    dev.gl_config.major_version = 3; dev.gl_config.minor_version = 0;
    CHECK(RISCOS_GL_CreateContext(&dev, &win) == NULL, "GLES 3.0 refused");
    {
        SDL_GLContext es;
        const char *v;
        dev.gl_config.major_version = 2;
        es = RISCOS_GL_CreateContext(&dev, &win);
        v = es ? (const char *) glGetString(GL_VERSION) : NULL;
        CHECK(es && v && strncmp(v, "OpenGL ES 2.0", 13) == 0, "GLES 2.0 context (%s)", v ? v : errbuf);
        if (es) RISCOS_GL_DeleteContext(&dev, es);
        dev.gl_config.major_version = 1; dev.gl_config.minor_version = 1;
        es = RISCOS_GL_CreateContext(&dev, &win);
        v = es ? (const char *) glGetString(GL_VERSION) : NULL;
        CHECK(es && v && strncmp(v, "OpenGL ES-CM 1.1", 16) == 0, "GLES 1.1 context (%s)", v ? v : errbuf);
        if (es) RISCOS_GL_DeleteContext(&dev, es);
        current_ctx = ctx;
        RISCOS_GL_MakeCurrent(&dev, &win, ctx);
    }
    dev.gl_config.profile_mask = SDL_GL_CONTEXT_PROFILE_CORE; dev.gl_config.major_version = 3; dev.gl_config.minor_version = 2;
    CHECK(RISCOS_GL_CreateContext(&dev, &win) == NULL, "core 3.2 refused (%s)", errbuf);
    dev.gl_config.profile_mask = 0; dev.gl_config.major_version = 2; dev.gl_config.minor_version = 1;

    /* ---- cooperative pacing (swap interval 1) ---- */
    RISCOS_GL_SetSwapInterval(&dev, 1);
    {
        int k;
        sp = fake_vsyncs; delays = 0; delay_ms_total = 0;
        for (k = 0; k < 10; k++) { frame(0, 0, 1); fake_ticks += 2; RISCOS_GL_SwapWindow(&dev, &win); }
        printf("  window, vsync: 10 frames of 2 ms work -> %d cooperative waits, %u ms waited, OS_Byte 19 x%d\n",
               delays, (unsigned) delay_ms_total, fake_vsyncs - sp);
        CHECK(fake_vsyncs == sp, "window: never blocks the desktop with OS_Byte 19");
        CHECK(delays >= 8 && delay_ms_total >= 8 * 12, "window: waits ~1 frame period each frame, via the Wimp");
        delays = 0;
        for (k = 0; k < 5; k++) { fake_ticks += 30; RISCOS_GL_SwapWindow(&dev, &win); }
        CHECK(delays == 0, "window: no extra wait when frames are already slower than the display");
        vd.wimp_window = 0; vd.wimp_sdl_window = NULL;              /* full screen */
        RISCOS_GL_SwapWindow(&dev, &win);
        sp = fake_vsyncs; delays = 0;
        RISCOS_GL_SwapWindow(&dev, &win);
        CHECK(fake_vsyncs == sp + 1 && delays == 0, "full screen: one real vsync (OS_Byte 19), EGL doesn't add another");
        RISCOS_GL_SetSwapInterval(&dev, 0);
        sp = fake_vsyncs;
        RISCOS_GL_SwapWindow(&dev, &win);
        CHECK(fake_vsyncs == sp, "vsync off: no waiting at all");
    }

    RISCOS_GL_DeleteContext(&dev, ctx);
    RISCOS_GL_DestroyWindowBuffer(&win);
    CHECK(!wd->egl_surface && fake_ovl_live() == 0 && fake_ovl_errors() == 0,
          "window destroyed: surface gone, VideoOverlay used by the rules");
    printf("%s\n", fails ? "FAILURES" : "ALL CHECKS PASSED");
    return NULL;
}

int main(void)
{
    /* Keep every allocation and the stack below 2 GB (32-bit SWI registers). */
    pthread_attr_t attr;
    pthread_t t;
    size_t stack_size = 8 << 20;
    void *stack;
    mallopt(M_ARENA_MAX, 1);
    mallopt(M_MMAP_MAX, 0);
    mallopt(M_TOP_PAD, 64 << 20);
    stack = mmap((void *) 0x60000000, stack_size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (stack == MAP_FAILED) { perror("mmap"); return 2; }
    pthread_attr_init(&attr);
    pthread_attr_setstack(&attr, stack, stack_size);
    pthread_create(&t, &attr, run, NULL);
    pthread_join(t, NULL);
    return fails != 0;
}
