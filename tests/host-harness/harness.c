/* Host harness for SDL_riscosopengl.c: emulates the two SWIs it uses
   (OS_SpriteOp 15 create sprite, OS_SpriteOp 52 via the framebuffer
   hook) and the handful of SDL internals it calls, then drives it the
   way SDL_video.c does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"
#include "SDL_riscosvideo.h"
#include "SDL_riscoswindow.h"
#include "SDL_riscosopengl.h"
#include <kernel.h>
#include <GL/gl.h>

/* ---- SDL stubs ---- */
static char errbuf[256];
#include <sys/mman.h>
/* RISC OS code casts pointers to int; keep heap below 2GB on this 64-bit host */
void *SDL_malloc(size_t n) {
    static char *pool, *top;
    void *p;
    if (!pool) { pool = top = mmap(NULL, 64<<20, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT, -1, 0); }
    n = (n + 15) & ~(size_t)15; p = top; top += n; return p;
}
void SDL_free(void *p) { (void)p; }
size_t SDL_strlcpy(char *d, const char *s, size_t n) { snprintf(d, n, "%s", s); return strlen(s); }
int SDL_SetError(const char *fmt, ...) { va_list a; va_start(a, fmt); vsnprintf(errbuf, sizeof errbuf, fmt, a); va_end(a); return -1; }
int SDL_Error(SDL_errorcode c) { return SDL_SetError("error %d", (int)c); }
static SDL_GLContext current_ctx;
SDL_GLContext SDL_GL_GetCurrentContext(void) { return current_ctx; }

/* ---- SWI emulation ---- */
static int plots;
_kernel_oserror *_kernel_swi(int swi, _kernel_swi_regs *in, _kernel_swi_regs *out)
{
    static _kernel_oserror e = {0, "bad"};
    if (swi == 0x2E && (in->r[0] & 0xFF) == 15) {
        int *area = (int *)(uintptr_t)(unsigned)in->r[1];      /* 64-bit host: see note */
        int w = in->r[4], h = in->r[5];
        int bytes = 44 + w * 4 * h;
        int *spr;
        if (area[3] + bytes > area[0]) return &e;
        spr = (int *)((char *)area + area[3]);
        memset(spr, 0, 44);
        spr[0] = bytes; strncpy((char *)&spr[1], (char *)(uintptr_t)(unsigned)in->r[2], 12);
        spr[4] = w - 1; spr[5] = h - 1; spr[6] = 0; spr[7] = 31;
        spr[8] = 44; spr[9] = 44; spr[10] = in->r[6];
        area[1]++; area[3] += bytes;
        return NULL;
    }
    if (swi == 0x06) return NULL;
    return &e;
}

/* Stand-in for the driver's plot: check what it would put on screen. */
static unsigned last_top_left, last_bottom_left; static int last_w, last_h;
int RISCOS_UpdateWindowFramebuffer(_THIS, SDL_Window *window, const SDL_Rect *r, int n)
{
    SDL_WindowData *d = (SDL_WindowData *)window->driverdata;
    unsigned *px = (unsigned *)((char *)d->fb_sprite + d->fb_sprite->image_offset);
    last_w = d->fb_sprite->width + 1; last_h = d->fb_sprite->height + 1;
    last_top_left = px[0];
    last_bottom_left = px[(last_h - 1) * last_w];
    plots++;
    return 0;
}

int main(void)
{
    SDL_VideoDevice dev; SDL_VideoData vd; SDL_Window win; SDL_WindowData wd;
    SDL_GLContext ctx; int fails = 0;
    memset(&dev, 0, sizeof dev); memset(&vd, 0, sizeof vd);
    memset(&win, 0, sizeof win); memset(&wd, 0, sizeof wd);
    dev.driverdata = &vd; win.driverdata = &wd; wd.window = &win;
    win.w = 64; win.h = 48;
    dev.gl_config.depth_size = 24; dev.gl_config.major_version = 2; dev.gl_config.minor_version = 1;

    RISCOS_GL_LoadLibrary(&dev, NULL);
    ctx = RISCOS_GL_CreateContext(&dev, &win);
    if (!ctx) { printf("CreateContext failed: %s\n", errbuf); return 1; }
    current_ctx = ctx;
    printf("GL_VERSION via SDL path: %s\n", glGetString(GL_VERSION));
    printf("GetProcAddress(glClear) = %p\n", RISCOS_GL_GetProcAddress(&dev, "glClear"));

    /* frame 1: blue, red top half -> screen top-left must be red (0x..0000FF) */
    glClearColor(0,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1,0,0); glRectf(-1,0,1,1);
    RISCOS_GL_SwapWindow(&dev, &win);
    printf("frame1 %dx%d top-left=%08x bottom-left=%08x\n", last_w, last_h, last_top_left, last_bottom_left);
    if ((last_top_left & 0xFFFFFF) != 0x0000FF || (last_bottom_left & 0xFFFFFF) != 0xFF0000) fails++;
    if (wd.fb_area != NULL) { printf("fb pointers not restored\n"); fails++; }

    /* resize, as SDL_SetWindowSize would; next swap rebinds */
    win.w = 100; win.h = 80;
    RISCOS_GL_SwapWindow(&dev, &win);
    glViewport(0, 0, 100, 80);
    glClearColor(0,1,0,1); glClear(GL_COLOR_BUFFER_BIT);
    RISCOS_GL_SwapWindow(&dev, &win);
    printf("after resize %dx%d top-left=%08x\n", last_w, last_h, last_top_left);
    if (last_w != 100 || last_h != 80 || (last_top_left & 0xFFFFFF) != 0x00FF00) fails++;

    /* ES must be refused cleanly */
    dev.gl_config.profile_mask = SDL_GL_CONTEXT_PROFILE_ES;
    if (RISCOS_GL_CreateContext(&dev, &win)) fails++; else printf("ES refused: %s\n", errbuf);
    dev.gl_config.profile_mask = SDL_GL_CONTEXT_PROFILE_CORE; dev.gl_config.major_version = 3; dev.gl_config.minor_version = 2;
    if (RISCOS_GL_CreateContext(&dev, &win)) fails++; else printf("Core 3.2 refused: %s\n", errbuf);

    RISCOS_GL_DeleteContext(&dev, ctx);
    RISCOS_GL_DestroyWindowBuffer(&win);
    printf("%d plots, %s\n", plots, fails ? "FAIL" : "ALL CHECKS PASSED");
    return fails;
}
