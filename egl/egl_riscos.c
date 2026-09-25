/*
 * egl_riscos.c - EGL 1.4 for RISC OS on top of Mesa's OSMesa (software GL).
 * Part of riscos-mesa. MIT licence (see LICENSE).
 *
 * One display (the screen). Configs: RGBA8888 with depth 0/16/24 and
 * stencil 0/8, in both RISC OS 32bpp colour orders (the one matching the
 * screen first). Client API: desktop OpenGL (2.1 compatibility, from Mesa
 * 20.3 classic OSMesa). Surfaces:
 *   window  - Wimp window handle, or -1 for the whole screen. Rendered into
 *             a sprite that eglSwapBuffers plots (Wimp_UpdateWindow loop in a
 *             window, a plain plot full screen), or straight into screen
 *             memory full screen with EGL_RENDER_BUFFER = EGL_SINGLE_BUFFER.
 *   pbuffer - plain memory.
 *   pixmap  - a 32bpp sprite; GL renders into its image directly.
 * See include/EGL/eglext_riscos.h and README.md for the RISC OS details.
 *
 * Not thread safe: all EGL and GL calls must come from one thread (the
 * normal case on RISC OS). OSMesa has no "release current", so after
 * eglMakeCurrent(dpy, NO_SURFACE, NO_SURFACE, NO_CONTEXT) GL calls still
 * reach the last context; don't make them.
 */
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/eglext_riscos.h>
#include <GL/osmesa.h>

#include <kernel.h>
#include <swis.h>

#define EGL_RISCOS_VENDOR  "riscos-mesa"
#define EGL_RISCOS_VERSION "1.4 riscos-mesa (OSMesa)"
#define EGL_RISCOS_EXTENSIONS \
    "EGL_KHR_create_context EGL_KHR_get_all_proc_addresses EGL_RISCOS_wimp_window"

#define MAGIC_DISPLAY 0x444C4745   /* "EGLD" */
#define MAGIC_SURFACE 0x534C4745   /* "EGLS" */
#define MAGIC_CONTEXT 0x434C4745   /* "EGLC" */

#define LAYOUT_TBGR 0              /* 0x00BBGGRR: R,G,B,X in memory = OSMESA_RGBA */
#define LAYOUT_TRGB 1              /* 0x00RRGGBB: B,G,R,X in memory = OSMESA_BGRA */
#define MODEFLAG_TRGB 0x4000

#define MAX_PBUFFER 4096
#define MAX_SWAP_INTERVAL 4
#define MAX_BANKS 3
/* Window surface sprites smaller than this get extra (unused, clipped off)
   rows. On the Pi 4 (RISC OS 5) a small sprite kept showing the image it
   had when first plotted - black, or a frozen frame - while 400x300 and
   bigger ones updated: something caches small sprites between plots.
   Cleaning the CPU cache or using SpriteOp 52 didn't help; padding did. */
#define MIN_SPRITE_BYTES (1024 * 1024)

/* 32bpp, 90x90 dpi, sprite type 6: 0x00BBGGRR */
#define SPRITE_MODE_TYPE6 (1 | (90 << 1) | (90 << 14) | (6 << 27))

#ifndef Wimp_GetWindowState
#define Wimp_GetWindowState 0x400CB
#endif
#ifndef Wimp_RedrawWindow
#define Wimp_RedrawWindow   0x400C8
#endif
#ifndef Wimp_UpdateWindow
#define Wimp_UpdateWindow   0x400C9
#endif
#ifndef Wimp_GetRectangle
#define Wimp_GetRectangle   0x400CA
#endif
#ifndef OS_ReadDynamicArea
#define OS_ReadDynamicArea  0x5C
#endif
#ifndef OS_ChangeDynamicArea
#define OS_ChangeDynamicArea 0x2A
#endif
#ifndef OS_ScreenMode
#define OS_ScreenMode       0x65
#endif

/* ------------------------------------------------------------------ */
/* Objects                                                             */

typedef struct egl_config {
    EGLint id;
    EGLint depth, stencil;
    int layout;
} egl_config;

enum { SURF_WINDOW, SURF_PBUFFER, SURF_PIXMAP };

typedef struct egl_surface {
    EGLint magic;
    int kind;
    const egl_config *cfg;
    int w, h;                   /* pixels */
    int stride;                 /* pixels per row */
    void *pixels;               /* what OSMesa renders into */
    EGLint render_buffer;       /* what the app asked for */
    EGLint swap_behavior;
    int swap_interval;
    /* window */
    int handle;                 /* Wimp window handle, -1 = screen */
    int fixed;                  /* work area rectangle given */
    int wa_x, wa_y;             /* its top left, OS units */
    int direct;                 /* rendering into screen memory */
    int banks;                  /* > 0: flipping between this many screen banks */
    int draw_bank;              /* bank being drawn (1..banks) */
    void *bank_addr[MAX_BANKS + 1];
    int no_banks;               /* don't try screen banks (failed, or preserved contents wanted) */
    int want_banks;             /* banks asked for at creation: 0 (sprite plot), 2 or 3 */
    int flip_first;             /* experiment: switch bank before the vsync wait */
    int *area;                  /* malloc'd sprite area */
    int *sprite;                /* sprite in it */
    int sprite_mode;            /* mode word / selector used to make it */
    int sprite_h;               /* rows in the sprite: > h when padded (see MIN_SPRITE_BYTES) */
    /* pbuffer */
    void *mem;
    /* bookkeeping */
    int current;
    int destroy_pending;
    struct egl_surface *next;
} egl_surface;

typedef struct egl_context {
    EGLint magic;
    const egl_config *cfg;
    OSMesaContext om;
    int current;
    int destroy_pending;
    struct egl_context *next;
} egl_context;

typedef struct egl_display {
    EGLint magic;
    int initialised;
    egl_config configs[8];
    int nconfigs;
    egl_surface *surfaces;
    egl_context *contexts;
} egl_display;

static egl_display display = { MAGIC_DISPLAY, 0, {{0, 0, 0, 0}}, 0, NULL, NULL };
static EGLint last_error = EGL_SUCCESS;
static EGLenum bound_api = EGL_OPENGL_API;
static egl_context *cur_ctx;
static egl_surface *cur_surf;

static EGLBoolean fail(EGLint error)
{
    last_error = error;
    return EGL_FALSE;
}

static EGLBoolean ok(void)
{
    last_error = EGL_SUCCESS;
    return EGL_TRUE;
}

/* ------------------------------------------------------------------ */
/* Screen and Wimp                                                     */

typedef struct screen_info {
    int flags, xeig, yeig, log2bpp;
    int width, height;          /* pixels */
    void *start;
    int line_length;            /* bytes */
} screen_info;

static void read_screen(screen_info *s)
{
    /* VDU variables: 0 ModeFlags, 4/5 X/YEigFactor, 9 Log2BPP,
       11/12 X/YWindLimit, 148 ScreenStart, 6 LineLength. */
    static const int vars[] = { 0, 4, 5, 9, 11, 12, 148, 6, -1 };
    int vals[8];
    _kernel_swi_regs r;

    memset(vals, 0, sizeof vals);
    vals[1] = vals[2] = 1;
    r.r[0] = (int) vars;
    r.r[1] = (int) vals;
    _kernel_swi(OS_ReadVduVariables, &r, &r);
    s->flags = vals[0];
    s->xeig = vals[1];
    s->yeig = vals[2];
    s->log2bpp = vals[3];
    s->width = vals[4] + 1;
    s->height = vals[5] + 1;
    s->start = (void *) vals[6];
    s->line_length = vals[7];
}

/* Colour order of the screen, or -1 if it isn't 32bpp. */
static int screen_layout(const screen_info *s)
{
    if (s->log2bpp != 5)
        return -1;
    return (s->flags & MODEFLAG_TRGB) ? LAYOUT_TRGB : LAYOUT_TBGR;
}

static int read_mode_variable(int mode, int var, int *value)
{
    _kernel_swi_regs r;
    r.r[0] = mode;
    r.r[1] = var;
    if (_kernel_swi(OS_ReadModeVariable, &r, &r) != NULL)
        return 0;
    /* C flag set = invalid; _kernel_swi can't report it, so sanity check */
    *value = r.r[2];
    return 1;
}

/* A sprite mode for a 32bpp sprite in the given colour order. TBGR is sprite
   type 6. TRGB uses the screen's own mode when that is 32bpp TRGB (what the
   SDL driver does), else a mode selector asking for TRGB. */
static int sprite_mode_for(int layout, const screen_info *s)
{
    static int trgb_selector[] = {
        1, 640, 480, 5, -1,     /* flags, x, y, log2bpp, frame rate */
        0, MODEFLAG_TRGB,       /* ModeFlags */
        3, -1,                  /* NColour: 16M */
        4, 1, 5, 1,             /* X/YEigFactor */
        -1
    };
    _kernel_swi_regs r;

    if (layout == LAYOUT_TBGR)
        return SPRITE_MODE_TYPE6;
    if (screen_layout(s) == LAYOUT_TRGB) {
        r.r[0] = 1;             /* return current mode specifier */
        if (_kernel_swi(OS_ScreenMode, &r, &r) == NULL)
            return r.r[1];
    }
    trgb_selector[10] = s->xeig;
    trgb_selector[12] = s->yeig;
    return (int) trgb_selector;
}

typedef struct window_state {
    int handle;
    int x0, y0, x1, y1;         /* visible area, screen OS units */
    int scroll_x, scroll_y;
    int behind, flags;
} window_state;

static int get_window_state(int handle, window_state *ws)
{
    _kernel_swi_regs r;
    ws->handle = handle;
    r.r[1] = (int) ws;
    return _kernel_swi(Wimp_GetWindowState, &r, &r) == NULL;
}

/* The size a window surface should have now. */
static int wanted_size(const egl_surface *surf, const screen_info *s, int *w, int *h)
{
    window_state ws;

    if (surf->handle == -1) {
        *w = s->width;
        *h = s->height;
    } else if (surf->fixed) {
        *w = surf->w;
        *h = surf->h;
    } else {
        if (!get_window_state(surf->handle, &ws))
            return 0;
        *w = (ws.x1 - ws.x0) >> s->xeig;
        *h = (ws.y1 - ws.y0) >> s->yeig;
    }
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
    return 1;
}

typedef struct { int x0, y0, x1, y1; } os_rect;    /* screen OS units, x1/y1 exclusive */

#define MAX_PIECES 16

/* Set the graphics window (VDU 24 takes inclusive coordinates). */
static void set_graphics_window(const os_rect *c)
{
    int v[4], i;
    v[0] = c->x0; v[1] = c->y0; v[2] = c->x1 - 1; v[3] = c->y1 - 1;
    _kernel_oswrch(24);
    for (i = 0; i < 4; i++) {
        _kernel_oswrch(v[i] & 0xFF);
        _kernel_oswrch((v[i] >> 8) & 0xFF);
    }
}

/* Plot a window surface's sprite for one rectangle of a Wimp redraw or
   update loop (block = the Wimp_RedrawWindow/UpdateWindow block). */
static void plot_rectangle(const egl_surface *surf, const int *block, const screen_info *s,
                           const os_rect *clip)
{
    _kernel_swi_regs r;
    int x, top;
    os_rect vis;

    if (!surf->sprite)
        return;
    if (surf->handle == -1) {
        x = 0;
        top = s->height << s->yeig;                 /* top of the screen */
    } else if (surf->fixed) {
        x = block[1] - block[5] + surf->wa_x;       /* work area origin + offset */
        top = block[4] - block[6] + surf->wa_y;
    } else {
        x = block[1];                               /* visible area top left */
        top = block[4];
    }
    if (surf->sprite_h > surf->h) {
        /* padded sprite: show only the surface's own rows */
        vis.x0 = x > clip->x0 ? x : clip->x0;
        vis.x1 = x + (surf->w << s->xeig) < clip->x1 ? x + (surf->w << s->xeig) : clip->x1;
        vis.y1 = top < clip->y1 ? top : clip->y1;
        vis.y0 = top - (surf->h << s->yeig) > clip->y0 ? top - (surf->h << s->yeig) : clip->y0;
        if (vis.x0 >= vis.x1 || vis.y0 >= vis.y1)
            return;
        set_graphics_window(&vis);
    }
    r.r[0] = 512 + 34;          /* PutSpriteUserCoords, pixel for pixel */
    r.r[1] = (int) surf->area;
    r.r[2] = (int) surf->sprite;
    r.r[3] = x;
    r.r[4] = top - (surf->sprite_h << s->yeig);
    r.r[5] = 0;
    _kernel_swi(OS_SpriteOp, &r, &r);
    if (surf->sprite_h > surf->h)
        set_graphics_window(clip);
}

/* Where a work area surface sits on screen during a redraw/update loop. */
static os_rect fixed_rect(const egl_surface *surf, const int *block, const screen_info *s)
{
    os_rect r;
    r.x0 = block[1] - block[5] + surf->wa_x;
    r.y1 = block[4] - block[6] + surf->wa_y;
    r.x1 = r.x0 + (surf->w << s->xeig);
    r.y0 = r.y1 - (surf->h << s->yeig);
    return r;
}

/* Remove hole from the list of rectangles (each splits into up to 4). */
static int subtract_rect(os_rect *pieces, int n, const os_rect *hole)
{
    os_rect out[MAX_PIECES];
    int i, m = 0;
    for (i = 0; i < n; i++) {
        os_rect a = pieces[i];
        if (hole->x0 >= a.x1 || hole->x1 <= a.x0 || hole->y0 >= a.y1 || hole->y1 <= a.y0) {
            if (m < MAX_PIECES) out[m++] = a;
            continue;
        }
        if (hole->y1 < a.y1 && m < MAX_PIECES) {            /* band above */
            out[m].x0 = a.x0; out[m].x1 = a.x1; out[m].y0 = hole->y1; out[m].y1 = a.y1; m++;
        }
        if (hole->y0 > a.y0 && m < MAX_PIECES) {            /* band below */
            out[m].x0 = a.x0; out[m].x1 = a.x1; out[m].y0 = a.y0; out[m].y1 = hole->y0; m++;
        }
        {
            int y0 = hole->y0 > a.y0 ? hole->y0 : a.y0;
            int y1 = hole->y1 < a.y1 ? hole->y1 : a.y1;
            if (hole->x0 > a.x0 && m < MAX_PIECES) {        /* left of it */
                out[m].x0 = a.x0; out[m].x1 = hole->x0; out[m].y0 = y0; out[m].y1 = y1; m++;
            }
            if (hole->x1 < a.x1 && m < MAX_PIECES) {        /* right of it */
                out[m].x0 = hole->x1; out[m].x1 = a.x1; out[m].y0 = y0; out[m].y1 = y1; m++;
            }
        }
    }
    memcpy(pieces, out, m * sizeof out[0]);
    return m;
}

static void plot_loop(egl_display *d, int handle, egl_surface *only, int *block, int more)
{
    screen_info s;
    _kernel_swi_regs r;
    egl_surface *surf, *f;
    os_rect clip, pieces[MAX_PIECES], hole;
    int n, i;

    /* Visible area surfaces first, but never over the work area surfaces
       (plotting them twice would flash the wrong image there while the
       screen is being scanned out); then the work area surfaces. Showing a
       visible area surface (only != NULL) replots the work area ones too. */
    read_screen(&s);
    while (more) {
        clip.x0 = block[7]; clip.y0 = block[8]; clip.x1 = block[9]; clip.y1 = block[10];
        for (surf = d->surfaces; surf; surf = surf->next) {
            if (surf->kind != SURF_WINDOW || surf->handle != handle ||
                surf->destroy_pending || surf->fixed || (only && only != surf))
                continue;
            pieces[0] = clip;
            n = 1;
            for (f = d->surfaces; f; f = f->next) {
                if (f->kind == SURF_WINDOW && f->handle == handle && f->fixed &&
                    !f->destroy_pending && f->sprite) {
                    hole = fixed_rect(f, block, &s);
                    n = subtract_rect(pieces, n, &hole);
                }
            }
            if (n == 1 && pieces[0].x0 == clip.x0 && pieces[0].x1 == clip.x1 &&
                pieces[0].y0 == clip.y0 && pieces[0].y1 == clip.y1) {
                plot_rectangle(surf, block, &s, &clip);
            } else {
                for (i = 0; i < n; i++) {
                    set_graphics_window(&pieces[i]);
                    plot_rectangle(surf, block, &s, &pieces[i]);
                }
                set_graphics_window(&clip);
            }
        }
        for (surf = d->surfaces; surf; surf = surf->next) {
            if (surf->kind != SURF_WINDOW || surf->handle != handle ||
                surf->destroy_pending || !surf->fixed)
                continue;
            if (only == NULL || only == surf || !only->fixed)
                plot_rectangle(surf, block, &s, &clip);
        }
        r.r[1] = (int) block;
        if (_kernel_swi(Wimp_GetRectangle, &r, &r) != NULL)
            break;
        more = r.r[0];
    }
}

/* Show a finished frame of a window surface. */
static void present(egl_display *d, egl_surface *surf, const screen_info *s)
{
    _kernel_swi_regs r;
    int block[11];
    int i;

    if (surf->handle == -1) {
        if (surf->banks && surf->flip_first)
            _kernel_osbyte(113, surf->draw_bank, 0);
        for (i = 0; i < surf->swap_interval; i++)
            _kernel_osbyte(19, 0, 0);
        if (surf->banks) {
            /* Show the finished bank, then draw into the oldest one. With
               three banks that one isn't on screen even if the display
               only switches at the next vsync. */
            if (!surf->flip_first)
                _kernel_osbyte(113, surf->draw_bank, 0);
            surf->draw_bank = surf->draw_bank % surf->banks + 1;
            surf->pixels = surf->bank_addr[surf->draw_bank];
            return;
        }
        if (surf->direct || !surf->sprite)
            return;
        {
            os_rect all;
            all.x0 = 0; all.y0 = 0;
            all.x1 = s->width << s->xeig; all.y1 = s->height << s->yeig;
            plot_rectangle(surf, NULL, s, &all);
        }
        return;
    }

    /* Update the part of the work area the surface covers. */
    block[0] = surf->handle;
    if (surf->fixed) {
        block[1] = surf->wa_x;
        block[2] = surf->wa_y - (surf->h << s->yeig);
        block[3] = surf->wa_x + (surf->w << s->xeig);
        block[4] = surf->wa_y;
    } else {
        window_state ws;
        if (!get_window_state(surf->handle, &ws))
            return;
        block[1] = ws.scroll_x;
        block[2] = ws.scroll_y - (surf->h << s->yeig);
        block[3] = ws.scroll_x + (surf->w << s->xeig);
        block[4] = ws.scroll_y;
    }
    r.r[1] = (int) block;
    if (_kernel_swi(Wimp_UpdateWindow, &r, &r) != NULL)
        return;
    plot_loop(d, surf->handle, surf, block, r.r[0]);
}

/* ------------------------------------------------------------------ */
/* Buffers                                                             */

static int banks_in_use;

static void restore_banks(void)
{
    _kernel_osbyte(113, 1, 0);          /* display and draw bank 1 again */
    _kernel_osbyte(112, 1, 0);
}

static void restore_banks_atexit(void)
{
    if (banks_in_use)
        restore_banks();
}

static void free_buffers(egl_surface *surf)
{
    if (surf->banks) {
        restore_banks();
        surf->banks = 0;
        banks_in_use--;
    }
    free(surf->area);
    surf->area = NULL;
    surf->sprite = NULL;
    free(surf->mem);
    surf->mem = NULL;
    surf->pixels = NULL;
    surf->direct = 0;
}

static void *vdu_bank_start(int bank)
{
    static const int vars[] = { 148, -1 };
    int val = 0;
    _kernel_swi_regs r;
    _kernel_osbyte(112, bank, 0);
    r.r[0] = (int) vars;
    r.r[1] = (int) &val;
    _kernel_swi(OS_ReadVduVariables, &r, &r);
    return (void *) val;
}

/* Hardware double (or triple) buffering: render into a screen bank that
   isn't being shown and switch the display to it on swap (OS_Byte 113),
   so a frame is never seen half drawn and nothing is copied. Grows screen
   memory if it can. Returns the number of banks (2 or 3), or 0. */
static int setup_banks(egl_surface *surf, const screen_info *s)
{
    static const int vars[] = { 7, -1 };    /* ScreenSize */
    static int atexit_done;
    _kernel_swi_regs r;
    int screen_size = 0, have, n, i;

    r.r[0] = (int) vars;
    r.r[1] = (int) &screen_size;
    if (_kernel_swi(OS_ReadVduVariables, &r, &r) != NULL || screen_size <= 0)
        return 0;
    r.r[0] = 2;                             /* screen memory dynamic area */
    if (_kernel_swi(OS_ReadDynamicArea, &r, &r) != NULL)
        return 0;
    have = r.r[1];
    for (n = surf->want_banks; n >= 2; n--) {
        if (have < n * screen_size) {
            r.r[0] = 2;
            r.r[1] = n * screen_size - have;
            _kernel_swi(OS_ChangeDynamicArea, &r, &r);
            r.r[0] = 2;
            if (_kernel_swi(OS_ReadDynamicArea, &r, &r) != NULL)
                return 0;
            have = r.r[1];
        }
        if (have >= n * screen_size)
            break;
    }
    if (n < 2)
        return 0;
    for (i = 1; i <= n; i++)
        surf->bank_addr[i] = vdu_bank_start(i);
    _kernel_osbyte(112, 1, 0);
    _kernel_osbyte(113, 1, 0);
    if (surf->bank_addr[1] != s->start || surf->bank_addr[2] == surf->bank_addr[1])
        return 0;
    if (!atexit_done) {
        atexit(restore_banks_atexit);   /* never leave the desktop on bank 2 */
        atexit_done = 1;
    }
    return n;
}

/* Make a window surface's buffer fit the window and the screen mode.
   Returns 1 if it changed, 0 if not, -1 on failure (EGL error set). */
static int update_window_buffer(egl_surface *surf, const screen_info *s)
{
    _kernel_swi_regs r;
    int w, h, size, mode, sprite_h;

    if (!wanted_size(surf, s, &w, &h))
        return fail(EGL_BAD_NATIVE_WINDOW), -1;

    if (surf->handle == -1 && surf->render_buffer == EGL_BACK_BUFFER && !surf->no_banks &&
        surf->want_banks >= 2 &&
        screen_layout(s) == surf->cfg->layout && s->start != NULL &&
        (s->line_length & 3) == 0) {
        if (surf->banks && surf->bank_addr[1] == s->start && surf->w == w &&
            surf->h == h && surf->stride == s->line_length / 4)
            return 0;
        free_buffers(surf);
        surf->banks = setup_banks(surf, s);
        if (surf->banks) {
            banks_in_use++;
            surf->draw_bank = 2;
            surf->pixels = surf->bank_addr[2];
            surf->w = w;
            surf->h = h;
            surf->stride = s->line_length / 4;
            surf->swap_behavior = EGL_BUFFER_DESTROYED;
            return 1;
        }
        surf->no_banks = 1;             /* not enough screen memory: plot a sprite */
    }

    if (surf->handle == -1 && surf->render_buffer == EGL_SINGLE_BUFFER &&
        screen_layout(s) == surf->cfg->layout && s->start != NULL &&
        (s->line_length & 3) == 0) {
        if (surf->direct && surf->pixels == s->start && surf->w == w &&
            surf->h == h && surf->stride == s->line_length / 4)
            return 0;
        free_buffers(surf);
        surf->direct = 1;
        surf->pixels = s->start;
        surf->w = w;
        surf->h = h;
        surf->stride = s->line_length / 4;
        return 1;
    }

    mode = sprite_mode_for(surf->cfg->layout, s);
    if (surf->sprite && surf->w == w && surf->h == h && surf->sprite_mode == mode)
        return 0;

    free_buffers(surf);
    sprite_h = h;
    if ((long) w * 4 * h < MIN_SPRITE_BYTES)
        sprite_h = (MIN_SPRITE_BYTES + w * 4 - 1) / (w * 4);
    size = 16 + 44 + w * 4 * sprite_h;
    surf->area = (int *) malloc(size);
    if (!surf->area)
        return fail(EGL_BAD_ALLOC), -1;
    surf->area[0] = size;
    surf->area[1] = 0;
    surf->area[2] = 16;
    surf->area[3] = 16;
    r.r[0] = 256 + 15;          /* create sprite */
    r.r[1] = (int) surf->area;
    r.r[2] = (int) "egl";
    r.r[3] = 0;                 /* no palette */
    r.r[4] = w;
    r.r[5] = sprite_h;
    r.r[6] = mode;
    if (_kernel_swi(OS_SpriteOp, &r, &r) != NULL) {
        free(surf->area);
        surf->area = NULL;
        return fail(EGL_BAD_ALLOC), -1;
    }
    surf->sprite = (int *) ((char *) surf->area + surf->area[2]);
    surf->pixels = (char *) surf->sprite + surf->sprite[8];
    surf->w = w;
    surf->h = h;
    surf->sprite_h = sprite_h;
    surf->stride = w;
    surf->sprite_mode = mode;
    return 1;
}

/* Check a native pixmap (a sprite header) and fill in its format. */
static int pixmap_info(void *pixmap, int *w, int *h, int *layout, void **pixels)
{
    const int *spr = (const int *) pixmap;
    int log2bpp, flags;

    if (!spr)
        return 0;
    if (!read_mode_variable(spr[10], 9, &log2bpp) || log2bpp != 5)
        return 0;
    if (!read_mode_variable(spr[10], 0, &flags))
        flags = 0;
    if (spr[6] != 0 || spr[7] != 31 || spr[4] < 0 || spr[5] < 0)
        return 0;
    *w = spr[4] + 1;
    *h = spr[5] + 1;
    *layout = (flags & MODEFLAG_TRGB) ? LAYOUT_TRGB : LAYOUT_TBGR;
    *pixels = (char *) pixmap + spr[8];
    return 1;
}

static int bind(egl_context *ctx, egl_surface *surf)
{
    if (!OSMesaMakeCurrent(ctx->om, surf->pixels, GL_UNSIGNED_BYTE, surf->w, surf->h))
        return 0;
    OSMesaPixelStore(OSMESA_Y_UP, 0);           /* rows run top down */
    OSMesaPixelStore(OSMESA_ROW_LENGTH, surf->stride);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Validation                                                          */

static egl_display *get_display(EGLDisplay dpy, int need_init)
{
    egl_display *d = (egl_display *) dpy;
    if (d != &display) {
        fail(EGL_BAD_DISPLAY);
        return NULL;
    }
    if (need_init && !d->initialised) {
        fail(EGL_NOT_INITIALIZED);
        return NULL;
    }
    return d;
}

static const egl_config *get_config(egl_display *d, EGLConfig config)
{
    const egl_config *c = (const egl_config *) config;
    if (c < d->configs || c >= d->configs + d->nconfigs) {
        fail(EGL_BAD_CONFIG);
        return NULL;
    }
    return c;
}

static egl_surface *get_surface(egl_display *d, EGLSurface surface)
{
    egl_surface *s;
    for (s = d->surfaces; s; s = s->next)
        if (s == (egl_surface *) surface && !s->destroy_pending)
            return s;
    fail(EGL_BAD_SURFACE);
    return NULL;
}

static egl_context *get_context(egl_display *d, EGLContext context)
{
    egl_context *c;
    for (c = d->contexts; c; c = c->next)
        if (c == (egl_context *) context && !c->destroy_pending)
            return c;
    fail(EGL_BAD_CONTEXT);
    return NULL;
}

static void unlink_surface(egl_display *d, egl_surface *surf)
{
    egl_surface **p;
    for (p = &d->surfaces; *p; p = &(*p)->next)
        if (*p == surf) {
            *p = surf->next;
            break;
        }
    free_buffers(surf);
    surf->magic = 0;
    free(surf);
}

static void unlink_context(egl_display *d, egl_context *ctx)
{
    egl_context **p;
    for (p = &d->contexts; *p; p = &(*p)->next)
        if (*p == ctx) {
            *p = ctx->next;
            break;
        }
    OSMesaDestroyContext(ctx->om);
    ctx->magic = 0;
    free(ctx);
}

/* ------------------------------------------------------------------ */
/* Configs                                                             */

static int config_attrib(const egl_config *c, EGLint attrib, EGLint *value)
{
    EGLint v;
    switch (attrib) {
    case EGL_BUFFER_SIZE:              v = 32; break;
    case EGL_RED_SIZE:
    case EGL_GREEN_SIZE:
    case EGL_BLUE_SIZE:
    case EGL_ALPHA_SIZE:               v = 8; break;
    case EGL_LUMINANCE_SIZE:           v = 0; break;
    case EGL_ALPHA_MASK_SIZE:          v = 0; break;
    case EGL_BIND_TO_TEXTURE_RGB:
    case EGL_BIND_TO_TEXTURE_RGBA:     v = EGL_FALSE; break;
    case EGL_COLOR_BUFFER_TYPE:        v = EGL_RGB_BUFFER; break;
    case EGL_CONFIG_CAVEAT:            v = EGL_NONE; break;
    case EGL_CONFIG_ID:                v = c->id; break;
    case EGL_CONFORMANT:               v = EGL_OPENGL_BIT; break;
    case EGL_DEPTH_SIZE:               v = c->depth; break;
    case EGL_LEVEL:                    v = 0; break;
    case EGL_MAX_PBUFFER_WIDTH:
    case EGL_MAX_PBUFFER_HEIGHT:       v = MAX_PBUFFER; break;
    case EGL_MAX_PBUFFER_PIXELS:       v = MAX_PBUFFER * MAX_PBUFFER; break;
    case EGL_MAX_SWAP_INTERVAL:        v = MAX_SWAP_INTERVAL; break;
    case EGL_MIN_SWAP_INTERVAL:        v = 0; break;
    case EGL_NATIVE_RENDERABLE:        v = EGL_TRUE; break;
    case EGL_NATIVE_VISUAL_ID:
        v = (c->layout == LAYOUT_TRGB) ? EGL_RISCOS_VISUAL_TRGB : EGL_RISCOS_VISUAL_TBGR;
        break;
    case EGL_NATIVE_VISUAL_TYPE:       v = EGL_NONE; break;
    case EGL_RENDERABLE_TYPE:          v = EGL_OPENGL_BIT; break;
    case EGL_SAMPLE_BUFFERS:
    case EGL_SAMPLES:                  v = 0; break;
    case EGL_STENCIL_SIZE:             v = c->stencil; break;
    case EGL_SURFACE_TYPE:
        v = EGL_WINDOW_BIT | EGL_PBUFFER_BIT | EGL_PIXMAP_BIT |
            EGL_SWAP_BEHAVIOR_PRESERVED_BIT;
        break;
    case EGL_TRANSPARENT_TYPE:         v = EGL_NONE; break;
    case EGL_TRANSPARENT_RED_VALUE:
    case EGL_TRANSPARENT_GREEN_VALUE:
    case EGL_TRANSPARENT_BLUE_VALUE:   v = 0; break;
    default:
        return 0;
    }
    *value = v;
    return 1;
}

enum { M_ATLEAST, M_EXACT, M_MASK, M_IGNORE };

static const struct {
    EGLint attrib, def;
    int match;
} criteria[] = {
    { EGL_BUFFER_SIZE,            0,              M_ATLEAST },
    { EGL_RED_SIZE,               0,              M_ATLEAST },
    { EGL_GREEN_SIZE,             0,              M_ATLEAST },
    { EGL_BLUE_SIZE,              0,              M_ATLEAST },
    { EGL_LUMINANCE_SIZE,         0,              M_ATLEAST },
    { EGL_ALPHA_SIZE,             0,              M_ATLEAST },
    { EGL_ALPHA_MASK_SIZE,        0,              M_ATLEAST },
    { EGL_BIND_TO_TEXTURE_RGB,    EGL_DONT_CARE,  M_EXACT },
    { EGL_BIND_TO_TEXTURE_RGBA,   EGL_DONT_CARE,  M_EXACT },
    { EGL_COLOR_BUFFER_TYPE,      EGL_RGB_BUFFER, M_EXACT },
    { EGL_CONFIG_CAVEAT,          EGL_DONT_CARE,  M_EXACT },
    { EGL_CONFIG_ID,              EGL_DONT_CARE,  M_EXACT },
    { EGL_CONFORMANT,             0,              M_MASK },
    { EGL_DEPTH_SIZE,             0,              M_ATLEAST },
    { EGL_LEVEL,                  0,              M_EXACT },
    { EGL_MAX_PBUFFER_WIDTH,      0,              M_IGNORE },
    { EGL_MAX_PBUFFER_HEIGHT,     0,              M_IGNORE },
    { EGL_MAX_PBUFFER_PIXELS,     0,              M_IGNORE },
    { EGL_MAX_SWAP_INTERVAL,      EGL_DONT_CARE,  M_EXACT },
    { EGL_MIN_SWAP_INTERVAL,      EGL_DONT_CARE,  M_EXACT },
    { EGL_NATIVE_RENDERABLE,      EGL_DONT_CARE,  M_EXACT },
    { EGL_NATIVE_VISUAL_ID,       0,              M_IGNORE },
    { EGL_NATIVE_VISUAL_TYPE,     EGL_DONT_CARE,  M_EXACT },
    { EGL_RENDERABLE_TYPE,        EGL_OPENGL_ES_BIT, M_MASK },
    { EGL_SAMPLE_BUFFERS,         0,              M_ATLEAST },
    { EGL_SAMPLES,                0,              M_ATLEAST },
    { EGL_STENCIL_SIZE,           0,              M_ATLEAST },
    { EGL_SURFACE_TYPE,           EGL_WINDOW_BIT, M_MASK },
    { EGL_TRANSPARENT_TYPE,       EGL_NONE,       M_EXACT },
    { EGL_TRANSPARENT_RED_VALUE,  EGL_DONT_CARE,  M_EXACT },
    { EGL_TRANSPARENT_GREEN_VALUE, EGL_DONT_CARE, M_EXACT },
    { EGL_TRANSPARENT_BLUE_VALUE, EGL_DONT_CARE,  M_EXACT },
};
#define NCRITERIA ((int) (sizeof criteria / sizeof criteria[0]))

static int config_compare(const void *a, const void *b)
{
    /* EGL 1.4 section 3.4.1.2 sort order; everything before depth is
       identical for our configs. */
    const egl_config *x = *(const egl_config * const *) a;
    const egl_config *y = *(const egl_config * const *) b;
    if (x->depth != y->depth)
        return x->depth - y->depth;
    if (x->stencil != y->stencil)
        return x->stencil - y->stencil;
    return x->id - y->id;
}

static void make_configs(egl_display *d)
{
    static const EGLint ds[4][2] = { { 0, 0 }, { 16, 0 }, { 24, 0 }, { 24, 8 } };
    screen_info s;
    int first, i, l, n = 0;

    read_screen(&s);
    first = (screen_layout(&s) == LAYOUT_TRGB) ? LAYOUT_TRGB : LAYOUT_TBGR;
    for (l = 0; l < 2; l++) {
        for (i = 0; i < 4; i++) {
            d->configs[n].id = n + 1;
            d->configs[n].depth = ds[i][0];
            d->configs[n].stencil = ds[i][1];
            d->configs[n].layout = l == 0 ? first : 1 - first;
            n++;
        }
    }
    d->nconfigs = n;
}

/* ------------------------------------------------------------------ */
/* EGL 1.4 API                                                         */

EGLAPI EGLint EGLAPIENTRY eglGetError(void)
{
    EGLint e = last_error;
    last_error = EGL_SUCCESS;
    return e;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType display_id)
{
    if (display_id != EGL_DEFAULT_DISPLAY)
        return EGL_NO_DISPLAY;
    return (EGLDisplay) &display;
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    egl_display *d = get_display(dpy, 0);
    if (!d)
        return EGL_FALSE;
    if (!d->initialised) {
        make_configs(d);
        d->initialised = 1;
    }
    if (major) *major = 1;
    if (minor) *minor = 4;
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy)
{
    egl_display *d = get_display(dpy, 0);
    egl_surface *s, *sn;
    egl_context *c, *cn;

    if (!d)
        return EGL_FALSE;
    for (s = d->surfaces; s; s = sn) {
        sn = s->next;
        if (s->current) s->destroy_pending = 1;
        else unlink_surface(d, s);
    }
    for (c = d->contexts; c; c = cn) {
        cn = c->next;
        if (c->current) c->destroy_pending = 1;
        else unlink_context(d, c);
    }
    d->initialised = 0;
    return ok();
}

EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name)
{
    if (!get_display(dpy, 1))
        return NULL;
    last_error = EGL_SUCCESS;
    switch (name) {
    case EGL_VENDOR:      return EGL_RISCOS_VENDOR;
    case EGL_VERSION:     return EGL_RISCOS_VERSION;
    case EGL_EXTENSIONS:  return EGL_RISCOS_EXTENSIONS;
    case EGL_CLIENT_APIS: return "OpenGL";
    }
    fail(EGL_BAD_PARAMETER);
    return NULL;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                                            EGLint config_size, EGLint *num_config)
{
    egl_display *d = get_display(dpy, 1);
    int i, n;

    if (!d)
        return EGL_FALSE;
    if (!num_config)
        return fail(EGL_BAD_PARAMETER);
    n = d->nconfigs;
    if (configs) {
        if (n > config_size) n = config_size;
        if (n < 0) n = 0;
        for (i = 0; i < n; i++)
            configs[i] = (EGLConfig) &d->configs[i];
    }
    *num_config = n;
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                                              EGLConfig *configs, EGLint config_size,
                                              EGLint *num_config)
{
    egl_display *d = get_display(dpy, 1);
    EGLint want[NCRITERIA];
    const egl_config *match[8];
    int i, j, n = 0, by_id = 0;
    EGLint want_id = 0;
    int pixmap_layout = -1;

    if (!d)
        return EGL_FALSE;
    if (!num_config)
        return fail(EGL_BAD_PARAMETER);

    for (j = 0; j < NCRITERIA; j++)
        want[j] = criteria[j].def;
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLint a = attrib_list[i], v = attrib_list[i + 1];
        if (a == EGL_MATCH_NATIVE_PIXMAP) {
            int w, h;
            void *px;
            if (v == EGL_NONE || !pixmap_info((void *) v, &w, &h, &pixmap_layout, &px))
                return fail(EGL_BAD_NATIVE_PIXMAP);
            continue;
        }
        for (j = 0; j < NCRITERIA; j++)
            if (criteria[j].attrib == a)
                break;
        if (j == NCRITERIA)
            return fail(EGL_BAD_ATTRIBUTE);
        want[j] = v;
        if (a == EGL_CONFIG_ID && v != EGL_DONT_CARE) {
            by_id = 1;
            want_id = v;
        }
    }

    for (i = 0; i < d->nconfigs; i++) {
        const egl_config *c = &d->configs[i];
        int good = 1;
        if (by_id) {
            good = (c->id == want_id);          /* all else ignored */
        } else {
            for (j = 0; j < NCRITERIA && good; j++) {
                EGLint have;
                if (criteria[j].match == M_IGNORE || want[j] == EGL_DONT_CARE)
                    continue;
                config_attrib(c, criteria[j].attrib, &have);
                switch (criteria[j].match) {
                case M_ATLEAST: good = have >= want[j]; break;
                case M_EXACT:   good = have == want[j]; break;
                case M_MASK:    good = (have & want[j]) == want[j]; break;
                }
            }
            if (pixmap_layout >= 0 && c->layout != pixmap_layout)
                good = 0;
        }
        if (good)
            match[n++] = c;
    }
    qsort(match, n, sizeof match[0], config_compare);
    if (configs) {
        if (n > config_size) n = config_size;
        if (n < 0) n = 0;
        for (i = 0; i < n; i++)
            configs[i] = (EGLConfig) match[i];
    }
    *num_config = n;
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                                 EGLint attribute, EGLint *value)
{
    egl_display *d = get_display(dpy, 1);
    const egl_config *c;
    if (!d || !(c = get_config(d, config)))
        return EGL_FALSE;
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    if (!config_attrib(c, attribute, value))
        return fail(EGL_BAD_ATTRIBUTE);
    return ok();
}

static egl_surface *new_surface(egl_display *d, const egl_config *c, int kind)
{
    egl_surface *s = (egl_surface *) calloc(1, sizeof *s);
    if (!s) {
        fail(EGL_BAD_ALLOC);
        return NULL;
    }
    s->magic = MAGIC_SURFACE;
    s->kind = kind;
    s->cfg = c;
    s->render_buffer = EGL_BACK_BUFFER;
    s->swap_behavior = EGL_BUFFER_PRESERVED;
    s->swap_interval = 1;
    s->handle = 0;
    return s;
}

static void add_surface(egl_display *d, egl_surface *s)
{
    s->next = d->surfaces;
    d->surfaces = s;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                     EGLNativeWindowType win,
                                                     const EGLint *attrib_list)
{
    egl_display *d = get_display(dpy, 1);
    const egl_config *c;
    egl_surface *s;
    screen_info scr;
    window_state ws;
    int i, have_w = 0, have_h = 0;

    if (!d || !(c = get_config(d, config)))
        return EGL_NO_SURFACE;
    if (win != -1 && (win == 0 || !get_window_state(win, &ws))) {
        fail(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }
    if (!(s = new_surface(d, c, SURF_WINDOW)))
        return EGL_NO_SURFACE;
    s->handle = win;

    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLint a = attrib_list[i], v = attrib_list[i + 1];
        switch (a) {
        case EGL_RENDER_BUFFER:
            if (v != EGL_BACK_BUFFER && v != EGL_SINGLE_BUFFER)
                goto bad_attr;
            s->render_buffer = v;
            break;
        case EGL_VG_COLORSPACE:
        case EGL_VG_ALPHA_FORMAT:
            break;              /* OpenVG only, ignored */
        case EGL_FLIP_FIRST_RISCOS:       s->flip_first = (v != 0); break;
        case EGL_SCREEN_BANKS_RISCOS:
            if (win != -1 || v < 0 || v == 1 || v > MAX_BANKS)
                goto bad_attr;
            s->want_banks = v;
            break;
        case EGL_WORK_AREA_X_RISCOS:      s->wa_x = v; break;
        case EGL_WORK_AREA_Y_RISCOS:      s->wa_y = v; break;
        case EGL_WORK_AREA_WIDTH_RISCOS:  s->w = v; have_w = 1; break;
        case EGL_WORK_AREA_HEIGHT_RISCOS: s->h = v; have_h = 1; break;
        default:
            goto bad_attr;
        }
    }
    if (have_w || have_h) {
        if (!have_w || !have_h || s->w < 1 || s->h < 1 || win == -1)
            goto bad_attr;
        s->fixed = 1;
    }

    read_screen(&scr);
    if (update_window_buffer(s, &scr) < 0) {
        free(s);
        return EGL_NO_SURFACE;
    }
    add_surface(d, s);
    ok();
    return (EGLSurface) s;

bad_attr:
    free(s);
    fail(EGL_BAD_ATTRIBUTE);
    return EGL_NO_SURFACE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                                      const EGLint *attrib_list)
{
    egl_display *d = get_display(dpy, 1);
    const egl_config *c;
    egl_surface *s;
    int i, w = 0, h = 0, largest = 0;

    if (!d || !(c = get_config(d, config)))
        return EGL_NO_SURFACE;
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLint a = attrib_list[i], v = attrib_list[i + 1];
        switch (a) {
        case EGL_WIDTH:  w = v; break;
        case EGL_HEIGHT: h = v; break;
        case EGL_LARGEST_PBUFFER: largest = v; break;
        case EGL_TEXTURE_FORMAT:
        case EGL_TEXTURE_TARGET:
            if (v != EGL_NO_TEXTURE) {
                fail(EGL_BAD_MATCH);
                return EGL_NO_SURFACE;
            }
            break;
        case EGL_MIPMAP_TEXTURE:
        case EGL_VG_COLORSPACE:
        case EGL_VG_ALPHA_FORMAT:
            break;
        default:
            fail(EGL_BAD_ATTRIBUTE);
            return EGL_NO_SURFACE;
        }
    }
    if (w < 0 || h < 0) {
        fail(EGL_BAD_PARAMETER);
        return EGL_NO_SURFACE;
    }
    if (w > MAX_PBUFFER || h > MAX_PBUFFER) {
        if (!largest) {
            fail(EGL_BAD_MATCH);
            return EGL_NO_SURFACE;
        }
        if (w > MAX_PBUFFER) w = MAX_PBUFFER;
        if (h > MAX_PBUFFER) h = MAX_PBUFFER;
    }
    if (!(s = new_surface(d, c, SURF_PBUFFER)))
        return EGL_NO_SURFACE;
    /* OSMesa can't bind a 0x0 buffer; keep 1x1 of storage but report 0. */
    s->w = w > 0 ? w : 1;
    s->h = h > 0 ? h : 1;
    s->stride = s->w;
    s->mem = calloc((size_t) s->w * s->h, 4);
    if (!s->mem) {
        free(s);
        fail(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }
    s->pixels = s->mem;
    s->wa_x = w;                /* reported size */
    s->wa_y = h;
    add_surface(d, s);
    ok();
    return (EGLSurface) s;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                                     EGLNativePixmapType pixmap,
                                                     const EGLint *attrib_list)
{
    egl_display *d = get_display(dpy, 1);
    const egl_config *c;
    egl_surface *s;
    int i, w, h, layout;
    void *px;

    if (!d || !(c = get_config(d, config)))
        return EGL_NO_SURFACE;
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        if (attrib_list[i] != EGL_VG_COLORSPACE && attrib_list[i] != EGL_VG_ALPHA_FORMAT) {
            fail(EGL_BAD_ATTRIBUTE);
            return EGL_NO_SURFACE;
        }
    }
    if (!pixmap_info(pixmap, &w, &h, &layout, &px)) {
        fail(EGL_BAD_NATIVE_PIXMAP);
        return EGL_NO_SURFACE;
    }
    if (layout != c->layout) {
        fail(EGL_BAD_MATCH);
        return EGL_NO_SURFACE;
    }
    if (!(s = new_surface(d, c, SURF_PIXMAP)))
        return EGL_NO_SURFACE;
    s->render_buffer = EGL_SINGLE_BUFFER;
    s->w = w;
    s->h = h;
    s->stride = w;
    s->pixels = px;
    add_surface(d, s);
    ok();
    return (EGLSurface) s;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
    egl_display *d = get_display(dpy, 1);
    egl_surface *s;
    if (!d || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (s->current)
        s->destroy_pending = 1;
    else
        unlink_surface(d, s);
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                              EGLint attribute, EGLint *value)
{
    egl_display *d = get_display(dpy, 1);
    egl_surface *s;
    if (!d || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    switch (attribute) {
    case EGL_CONFIG_ID:        *value = s->cfg->id; break;
    case EGL_WIDTH:            *value = s->kind == SURF_PBUFFER ? s->wa_x : s->w; break;
    case EGL_HEIGHT:           *value = s->kind == SURF_PBUFFER ? s->wa_y : s->h; break;
    case EGL_LARGEST_PBUFFER:  if (s->kind == SURF_PBUFFER) *value = EGL_FALSE; break;
    case EGL_RENDER_BUFFER:
        *value = (s->kind == SURF_PIXMAP || s->direct) ? EGL_SINGLE_BUFFER : EGL_BACK_BUFFER;
        break;
    case EGL_SWAP_BEHAVIOR:    *value = s->swap_behavior; break;
    case EGL_SCREEN_BANKS_RISCOS: *value = s->banks; break;
    case EGL_MULTISAMPLE_RESOLVE: *value = EGL_MULTISAMPLE_RESOLVE_DEFAULT; break;
    case EGL_HORIZONTAL_RESOLUTION:
    case EGL_VERTICAL_RESOLUTION:
    case EGL_PIXEL_ASPECT_RATIO: *value = EGL_UNKNOWN; break;
    case EGL_TEXTURE_FORMAT:
    case EGL_TEXTURE_TARGET:   if (s->kind == SURF_PBUFFER) *value = EGL_NO_TEXTURE; break;
    case EGL_MIPMAP_TEXTURE:
    case EGL_MIPMAP_LEVEL:     if (s->kind == SURF_PBUFFER) *value = 0; break;
    case EGL_VG_ALPHA_FORMAT:  *value = EGL_VG_ALPHA_FORMAT_NONPRE; break;
    case EGL_VG_COLORSPACE:    *value = EGL_VG_COLORSPACE_sRGB; break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                                               EGLint attribute, EGLint value)
{
    egl_display *d = get_display(dpy, 1);
    egl_surface *s;
    if (!d || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    switch (attribute) {
    case EGL_SWAP_BEHAVIOR:
        if (value != EGL_BUFFER_PRESERVED && value != EGL_BUFFER_DESTROYED)
            return fail(EGL_BAD_PARAMETER);
        s->swap_behavior = value;
        if (value == EGL_BUFFER_PRESERVED && s->handle == -1 && !s->no_banks) {
            s->no_banks = 1;            /* screen banks can't preserve: use a sprite */
            if (s->banks) {
                screen_info scr;
                read_screen(&scr);
                if (update_window_buffer(s, &scr) < 0)
                    return EGL_FALSE;
                if (s == cur_surf && cur_ctx && !bind(cur_ctx, s))
                    return fail(EGL_BAD_ALLOC);
            }
        }
        break;
    case EGL_MIPMAP_LEVEL:
        break;
    case EGL_MULTISAMPLE_RESOLVE:
        if (value != EGL_MULTISAMPLE_RESOLVE_DEFAULT)
            return fail(EGL_BAD_MATCH);
        break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
    egl_display *d = get_display(dpy, 1);
    (void) buffer;
    if (!d || !get_surface(d, surface))
        return EGL_FALSE;
    return fail(EGL_BAD_MATCH);         /* no bind-to-texture configs */
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
    return eglBindTexImage(dpy, surface, buffer);
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api)
{
    if (api != EGL_OPENGL_API)
        return fail(EGL_BAD_PARAMETER); /* OpenGL ES / OpenVG not (yet) */
    bound_api = api;
    return ok();
}

EGLAPI EGLenum EGLAPIENTRY eglQueryAPI(void)
{
    return bound_api;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitClient(void)
{
    if (cur_ctx)
        glFinish();
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL(void)
{
    return eglWaitClient();
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative(EGLint engine)
{
    if (engine != EGL_CORE_NATIVE_ENGINE)
        return fail(EGL_BAD_PARAMETER);
    return ok();                        /* RISC OS drawing is synchronous */
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread(void)
{
    if (cur_ctx)
        eglMakeCurrent((EGLDisplay) &display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    bound_api = EGL_OPENGL_API;
    return ok();
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype,
        EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list)
{
    egl_display *d = get_display(dpy, 1);
    (void) buftype; (void) buffer; (void) attrib_list;
    if (d && get_config(d, config))
        fail(EGL_BAD_PARAMETER);        /* only OpenVG images exist for this */
    return EGL_NO_SURFACE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
    if (!get_display(dpy, 1))
        return EGL_FALSE;
    if (!cur_ctx)
        return fail(EGL_BAD_CONTEXT);
    if (!cur_surf)
        return fail(EGL_BAD_SURFACE);
    if (interval < 0) interval = 0;
    if (interval > MAX_SWAP_INTERVAL) interval = MAX_SWAP_INTERVAL;
    cur_surf->swap_interval = interval;
    return ok();
}

EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                               EGLContext share_context,
                                               const EGLint *attrib_list)
{
    egl_display *d = get_display(dpy, 1);
    const egl_config *c;
    egl_context *ctx, *share = NULL;
    EGLint major = 1, minor = 0, flags = 0;
    EGLint profile = EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR;
    int attribs[20], i, n = 0, explicit_version = 0, explicit_profile = 0;

    if (!d || !(c = get_config(d, config)))
        return EGL_NO_CONTEXT;
    if (bound_api != EGL_OPENGL_API) {
        fail(EGL_BAD_MATCH);
        return EGL_NO_CONTEXT;
    }
    if (share_context != EGL_NO_CONTEXT && !(share = get_context(d, share_context)))
        return EGL_NO_CONTEXT;

    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLint a = attrib_list[i], v = attrib_list[i + 1];
        switch (a) {
        case EGL_CONTEXT_MAJOR_VERSION_KHR:     /* = EGL_CONTEXT_CLIENT_VERSION */
            major = v; explicit_version = 1; break;
        case EGL_CONTEXT_MINOR_VERSION_KHR:
            minor = v; explicit_version = 1; break;
        case EGL_CONTEXT_FLAGS_KHR:
            flags = v; break;
        case EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR:
            profile = v; explicit_profile = 1; break;
        case EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR:
            if (v != EGL_NO_RESET_NOTIFICATION_KHR) {
                fail(EGL_BAD_MATCH);
                return EGL_NO_CONTEXT;
            }
            break;
        default:
            fail(EGL_BAD_ATTRIBUTE);
            return EGL_NO_CONTEXT;
        }
    }
    if (flags & ~(EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR |
                  EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR |
                  EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR)) {
        fail(EGL_BAD_ATTRIBUTE);
        return EGL_NO_CONTEXT;
    }
    if (flags & EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR) {
        fail(EGL_BAD_MATCH);            /* no robust access in classic swrast */
        return EGL_NO_CONTEXT;
    }
    if (profile != EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
        profile != EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR) {
        fail(EGL_BAD_MATCH);
        return EGL_NO_CONTEXT;
    }

    attribs[n++] = OSMESA_FORMAT;
    attribs[n++] = c->layout == LAYOUT_TRGB ? OSMESA_BGRA : OSMESA_RGBA;
    attribs[n++] = OSMESA_DEPTH_BITS;   attribs[n++] = c->depth;
    attribs[n++] = OSMESA_STENCIL_BITS; attribs[n++] = c->stencil;
    attribs[n++] = OSMESA_ACCUM_BITS;   attribs[n++] = 0;
    /* The profile only applies to GL 3.2+ (EGL_KHR_create_context). */
    attribs[n++] = OSMESA_PROFILE;
    attribs[n++] = (explicit_profile && profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
                    (major > 3 || (major == 3 && minor >= 2)))
                   ? OSMESA_CORE_PROFILE : OSMESA_COMPAT_PROFILE;
    if (explicit_version && (major > 2 || (major == 2 && minor > 1))) {
        attribs[n++] = OSMESA_CONTEXT_MAJOR_VERSION; attribs[n++] = major;
        attribs[n++] = OSMESA_CONTEXT_MINOR_VERSION; attribs[n++] = minor;
    }
    attribs[n++] = 0;

    ctx = (egl_context *) calloc(1, sizeof *ctx);
    if (!ctx) {
        fail(EGL_BAD_ALLOC);
        return EGL_NO_CONTEXT;
    }
    ctx->om = OSMesaCreateContextAttribs(attribs, share ? share->om : NULL);
    if (!ctx->om) {
        free(ctx);
        fail(EGL_BAD_MATCH);            /* e.g. GL 3.x asked for: this is 2.1 */
        return EGL_NO_CONTEXT;
    }
    ctx->magic = MAGIC_CONTEXT;
    ctx->cfg = c;
    ctx->next = d->contexts;
    d->contexts = ctx;
    ok();
    return (EGLContext) ctx;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay dpy, EGLContext context)
{
    egl_display *d = get_display(dpy, 1);
    egl_context *c;
    if (!d || !(c = get_context(d, context)))
        return EGL_FALSE;
    if (c->current)
        c->destroy_pending = 1;
    else
        unlink_context(d, c);
    return ok();
}

static void release_current(egl_display *d)
{
    egl_context *c = cur_ctx;
    egl_surface *s = cur_surf;
    cur_ctx = NULL;
    cur_surf = NULL;
    if (c) {
        c->current = 0;
        if (c->destroy_pending) unlink_context(d, c);
    }
    if (s) {
        s->current = 0;
        if (s->destroy_pending) unlink_surface(d, s);
    }
}

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                             EGLSurface read, EGLContext context)
{
    egl_display *d;
    egl_context *c;
    egl_surface *s;

    if (context == EGL_NO_CONTEXT && draw == EGL_NO_SURFACE && read == EGL_NO_SURFACE) {
        if (!(d = get_display(dpy, 0)))
            return EGL_FALSE;
        release_current(d);
        return ok();
    }
    if (!(d = get_display(dpy, 1)))
        return EGL_FALSE;
    if (context == EGL_NO_CONTEXT)
        return fail(EGL_BAD_MATCH);
    if (!(c = get_context(d, context)))
        return EGL_FALSE;
    if (draw == EGL_NO_SURFACE || read == EGL_NO_SURFACE)
        return fail(EGL_BAD_MATCH);     /* no EGL_KHR_surfaceless_context */
    if (!(s = get_surface(d, draw)) || !get_surface(d, read))
        return EGL_FALSE;
    if (draw != read)
        return fail(EGL_BAD_MATCH);     /* OSMesa has one buffer per context */
    if (s->cfg->layout != c->cfg->layout)
        return fail(EGL_BAD_MATCH);

    if (s->kind == SURF_WINDOW) {
        screen_info scr;
        read_screen(&scr);
        if (update_window_buffer(s, &scr) < 0)
            return EGL_FALSE;
    }
    if (!bind(c, s))
        return fail(EGL_BAD_ALLOC);

    if (c != cur_ctx || s != cur_surf) {
        egl_context *oc = cur_ctx;
        egl_surface *os = cur_surf;
        cur_ctx = c;
        cur_surf = s;
        c->current = 1;
        s->current = 1;
        if (oc && oc != c) {
            oc->current = 0;
            if (oc->destroy_pending) unlink_context(d, oc);
        }
        if (os && os != s) {
            os->current = 0;
            if (os->destroy_pending) unlink_surface(d, os);
        }
    }
    return ok();
}

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void)
{
    return cur_ctx ? (EGLContext) cur_ctx : EGL_NO_CONTEXT;
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint readdraw)
{
    if (readdraw != EGL_READ && readdraw != EGL_DRAW) {
        fail(EGL_BAD_PARAMETER);
        return EGL_NO_SURFACE;
    }
    return cur_surf ? (EGLSurface) cur_surf : EGL_NO_SURFACE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void)
{
    return cur_ctx ? (EGLDisplay) &display : EGL_NO_DISPLAY;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryContext(EGLDisplay dpy, EGLContext context,
                                              EGLint attribute, EGLint *value)
{
    egl_display *d = get_display(dpy, 1);
    egl_context *c;
    if (!d || !(c = get_context(d, context)))
        return EGL_FALSE;
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    switch (attribute) {
    case EGL_CONFIG_ID:              *value = c->cfg->id; break;
    case EGL_CONTEXT_CLIENT_TYPE:    *value = EGL_OPENGL_API; break;
    case EGL_CONTEXT_CLIENT_VERSION: *value = 0; break;    /* GL ES only */
    case EGL_RENDER_BUFFER:
        if (!c->current || !cur_surf) *value = EGL_NONE;
        else eglQuerySurface(dpy, (EGLSurface) cur_surf, EGL_RENDER_BUFFER, value);
        break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    egl_display *d = get_display(dpy, 1);
    egl_surface *s;
    screen_info scr;
    int changed;

    if (!d || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (s != cur_surf || !cur_ctx)
        return fail(EGL_BAD_SURFACE);
    if (s->kind != SURF_WINDOW)
        return ok();                    /* no effect on pbuffers/pixmaps */

    glFinish();
    read_screen(&scr);
    present(d, s, &scr);

    /* Window resized or screen mode changed: the next frame gets a new
       buffer (the one just shown was complete). */
    changed = update_window_buffer(s, &scr);
    if (changed < 0)
        return EGL_FALSE;
    if (s->banks)
        changed = 1;                    /* now drawing into another bank */
    if (changed && !bind(cur_ctx, s))
        return fail(EGL_BAD_ALLOC);
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers(EGLDisplay dpy, EGLSurface surface,
                                             EGLNativePixmapType target)
{
    egl_display *d = get_display(dpy, 1);
    egl_surface *s;
    int w, h, layout, x, y, cw, ch;
    void *px;

    if (!d || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!pixmap_info(target, &w, &h, &layout, &px))
        return fail(EGL_BAD_NATIVE_PIXMAP);
    if (s == cur_surf && cur_ctx)
        glFinish();
    cw = w < s->w ? w : s->w;
    ch = h < s->h ? h : s->h;
    for (y = 0; y < ch; y++) {
        const unsigned int *src = (const unsigned int *) s->pixels + (size_t) y * s->stride;
        unsigned int *dst = (unsigned int *) px + (size_t) y * w;
        if (layout == s->cfg->layout) {
            memcpy(dst, src, cw * 4);
        } else {
            for (x = 0; x < cw; x++) {
                unsigned int p = src[x];
                dst[x] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFF) | ((p & 0xFF) << 16);
            }
        }
    }
    return ok();
}

/* ------------------------------------------------------------------ */
/* EGL_RISCOS_wimp_window                                              */

EGLAPI EGLBoolean EGLAPIENTRY eglRedrawWindowRISCOS(EGLDisplay dpy, int *block)
{
    egl_display *d = get_display(dpy, 1);
    egl_surface *s;
    _kernel_swi_regs r;

    if (!d)
        return EGL_FALSE;
    if (!block)
        return fail(EGL_BAD_PARAMETER);
    for (s = d->surfaces; s; s = s->next)
        if (s->kind == SURF_WINDOW && s->handle == block[0] && !s->destroy_pending)
            break;
    if (!s)
        return fail(EGL_BAD_NATIVE_WINDOW);
    r.r[1] = (int) block;
    if (_kernel_swi(Wimp_RedrawWindow, &r, &r) != NULL)
        return fail(EGL_BAD_NATIVE_WINDOW);
    plot_loop(d, block[0], NULL, block, r.r[0]);
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglPlotSurfaceRISCOS(EGLDisplay dpy, EGLSurface surface,
                                                   const int *block)
{
    egl_display *d = get_display(dpy, 1);
    egl_surface *s;
    screen_info scr;

    if (!d || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!block)
        return fail(EGL_BAD_PARAMETER);
    if (s->kind != SURF_WINDOW || s->handle == -1)
        return fail(EGL_BAD_SURFACE);
    read_screen(&scr);
    {
        os_rect clip;
        clip.x0 = block[7]; clip.y0 = block[8]; clip.x1 = block[9]; clip.y1 = block[10];
        plot_rectangle(s, block, &scr, &clip);
    }
    return ok();
}

/* ------------------------------------------------------------------ */
/* eglGetProcAddress                                                   */

#define F(name) { #name, (__eglMustCastToProperFunctionPointerType) name }
static const struct {
    const char *name;
    __eglMustCastToProperFunctionPointerType func;
} egl_functions[] = {
    F(eglBindAPI), F(eglBindTexImage), F(eglChooseConfig), F(eglCopyBuffers),
    F(eglCreateContext), F(eglCreatePbufferFromClientBuffer),
    F(eglCreatePbufferSurface), F(eglCreatePixmapSurface),
    F(eglCreateWindowSurface), F(eglDestroyContext), F(eglDestroySurface),
    F(eglGetConfigAttrib), F(eglGetConfigs), F(eglGetCurrentContext),
    F(eglGetCurrentDisplay), F(eglGetCurrentSurface), F(eglGetDisplay),
    F(eglGetError), F(eglGetProcAddress), F(eglInitialize), F(eglMakeCurrent),
    F(eglQueryAPI), F(eglQueryContext), F(eglQueryString), F(eglQuerySurface),
    F(eglReleaseTexImage), F(eglReleaseThread), F(eglSurfaceAttrib),
    F(eglSwapBuffers), F(eglSwapInterval), F(eglTerminate), F(eglWaitClient),
    F(eglWaitGL), F(eglWaitNative),
    F(eglRedrawWindowRISCOS), F(eglPlotSurfaceRISCOS),
};
#undef F

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *procname)
{
    size_t i;
    if (!procname)
        return NULL;
    if (procname[0] == 'e' && procname[1] == 'g' && procname[2] == 'l') {
        for (i = 0; i < sizeof egl_functions / sizeof egl_functions[0]; i++)
            if (strcmp(procname, egl_functions[i].name) == 0)
                return egl_functions[i].func;
        return NULL;
    }
    return (__eglMustCastToProperFunctionPointerType) OSMesaGetProcAddress(procname);
}
