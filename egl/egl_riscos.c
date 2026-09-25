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
 * Extensions: see EGL_RISCOS_EXTENSIONS / EGL_RISCOS_CLIENT_EXTENSIONS
 * below and the table in README.md.
 * See include/EGL/eglext_riscos.h and README.md for the RISC OS details.
 *
 * Not thread safe: all EGL and GL calls must come from one thread (the
 * normal case on RISC OS). OSMesa has no "release current", so after
 * eglMakeCurrent(dpy, NO_SURFACE, NO_SURFACE, NO_CONTEXT) GL calls still
 * reach the last context; don't make them.
 */
#include <stdlib.h>
#include <string.h>

#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_riscos.h>
#include <GL/osmesa.h>

#include <kernel.h>
#include <swis.h>

/* DispmanX compatibility: libbcm_host provides these when it's linked. */
#include "riscos_dispmanx.h"
#pragma weak __riscos_dispmanx_window
#pragma weak __riscos_dispmanx_placement

#ifndef OSMESA_ES1_PROFILE                  /* riscos-mesa's Mesa patch */
#define OSMESA_ES1_PROFILE 0x1001
#define OSMESA_ES2_PROFILE 0x1002
#endif
#define RENDERABLE_BITS (EGL_OPENGL_BIT | EGL_OPENGL_ES_BIT | EGL_OPENGL_ES2_BIT)

#define EGL_RISCOS_VENDOR  "riscos-mesa"
#define EGL_RISCOS_VERSION "1.4 riscos-mesa (OSMesa)"
#define EGL_RISCOS_EXTENSIONS \
    "EGL_EXT_buffer_age EGL_EXT_swap_buffers_with_damage " \
    "EGL_KHR_context_flush_control EGL_KHR_create_context EGL_KHR_fence_sync " \
    "EGL_KHR_get_all_proc_addresses EGL_KHR_lock_surface EGL_KHR_lock_surface2 " \
    "EGL_KHR_lock_surface3 EGL_KHR_partial_update EGL_KHR_reusable_sync " \
    "EGL_KHR_surfaceless_context EGL_KHR_swap_buffers_with_damage EGL_KHR_wait_sync " \
    "EGL_RISCOS_wimp_window"
/* Client extensions: eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS) */
#define EGL_RISCOS_CLIENT_EXTENSIONS \
    "EGL_EXT_client_extensions EGL_EXT_platform_base " \
    "EGL_KHR_client_get_all_proc_addresses EGL_KHR_debug EGL_RISCOS_platform_wimp"

#define MAGIC_DISPLAY 0x444C4745   /* "EGLD" */
#define MAGIC_SURFACE 0x534C4745   /* "EGLS" */
#define MAGIC_CONTEXT 0x434C4745   /* "EGLC" */
#define MAGIC_SYNC    0x594C4745   /* "EGLY" */
#define MAX_DAMAGE    16           /* more rectangles than this: use their bounds */

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
    int handle;                 /* Wimp window handle, -1 = screen, -3 = DispmanX element */
    int dmx;                    /* DispmanX element (handle -3) */
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
    /* buffer age (EGL_EXT_buffer_age), partial update, locking */
    int swaps;                  /* swaps since this buffer was (re)made */
    int age_queried;            /* since the last swap */
    int n_damage;               /* eglSetDamageRegionKHR for this frame: -1 none */
    EGLint damage[MAX_DAMAGE * 4];
    int locked;                 /* EGL_KHR_lock_surface */
    int ever_locked;
    /* bookkeeping */
    int current;
    int destroy_pending;
    EGLLabelKHR label;
    struct egl_surface *next;
} egl_surface;

typedef struct egl_context {
    EGLint magic;
    const egl_config *cfg;
    OSMesaContext om;
    EGLenum api;                /* EGL_OPENGL_API or EGL_OPENGL_ES_API */
    int es;                     /* OpenGL ES version: 1 or 2 (0 = desktop GL) */
    int release_flush;          /* EGL_KHR_context_flush_control */
    int surfaceless;            /* has been current without a surface */
    int had_surface;            /* has been current with one */
    int current;
    int destroy_pending;
    EGLLabelKHR label;
    struct egl_context *next;
} egl_context;

typedef struct egl_sync {
    EGLint magic;
    EGLenum type;               /* EGL_SYNC_FENCE_KHR or EGL_SYNC_REUSABLE_KHR */
    EGLint status;              /* EGL_SIGNALED_KHR / EGL_UNSIGNALED_KHR */
    EGLLabelKHR label;
    struct egl_sync *next;
} egl_sync;

typedef struct egl_display {
    EGLint magic;
    int initialised;
    egl_config configs[8];
    int nconfigs;
    egl_surface *surfaces;
    egl_context *contexts;
    egl_sync *syncs;
    EGLLabelKHR label;
} egl_display;

static egl_display display = { MAGIC_DISPLAY, 0, {{0, 0, 0, 0}}, 0, NULL, NULL, NULL, NULL };
static EGLint last_error = EGL_SUCCESS;
static EGLenum bound_api = EGL_OPENGL_API;
static egl_context *cur_ctx;
static egl_surface *cur_surf;

/* EGL_KHR_debug: every public function records its name on entry, and the
   validation helpers record the label of the object they found. */
static const char *egl_cmd = "";
static EGLLabelKHR egl_obj_label, thread_label;
static EGLDEBUGPROCKHR debug_callback;
static int debug_enabled[4] = { 1, 1, 0, 0 };   /* critical, error, warn, info */
#define ENTER() (egl_cmd = __func__, egl_obj_label = NULL)

static const char *error_name(EGLint e)
{
    static const char *names[] = {
        "EGL_SUCCESS", "EGL_NOT_INITIALIZED", "EGL_BAD_ACCESS", "EGL_BAD_ALLOC",
        "EGL_BAD_ATTRIBUTE", "EGL_BAD_CONFIG", "EGL_BAD_CONTEXT",
        "EGL_BAD_CURRENT_SURFACE", "EGL_BAD_DISPLAY", "EGL_BAD_MATCH",
        "EGL_BAD_NATIVE_PIXMAP", "EGL_BAD_NATIVE_WINDOW", "EGL_BAD_PARAMETER",
        "EGL_BAD_SURFACE", "EGL_CONTEXT_LOST"
    };
    if (e >= EGL_SUCCESS && e <= EGL_CONTEXT_LOST)
        return names[e - EGL_SUCCESS];
    return "EGL error";
}

static EGLBoolean fail(EGLint error)
{
    last_error = error;
    if (debug_callback && debug_enabled[error == EGL_BAD_ALLOC ? 0 : 1])
        debug_callback(error, egl_cmd,
                       error == EGL_BAD_ALLOC ? EGL_DEBUG_MSG_CRITICAL_KHR : EGL_DEBUG_MSG_ERROR_KHR,
                       thread_label, egl_obj_label, error_name(error));
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
    } else if (surf->fixed || surf->dmx) {
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

/* Clip damage rectangles (x, y, w, h in pixels from the bottom left, as
   eglSwapBuffersWithDamageKHR gives them) to the surface and turn them into
   OS unit offsets from its top left: out[i] = x0, y0, x1, y1 with y going up
   (so y values are <= 0). More than MAX_DAMAGE become their bounding box.
   Returns the number of rectangles, or -1 for "the whole surface". */
static int damage_rects(const egl_surface *surf, const EGLint *rects, int n,
                        const screen_info *s, os_rect *out)
{
    int i, m = 0, over = 0;
    os_rect box = { 0, 0, 0, 0 };

    if (!rects || n <= 0)
        return -1;
    for (i = 0; i < n; i++) {
        int x0 = rects[i * 4], y0 = rects[i * 4 + 1];
        int x1 = x0 + rects[i * 4 + 2], y1 = y0 + rects[i * 4 + 3];
        os_rect r;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > surf->w) x1 = surf->w;
        if (y1 > surf->h) y1 = surf->h;
        if (x0 >= x1 || y0 >= y1)
            continue;
        r.x0 = x0 << s->xeig;
        r.x1 = x1 << s->xeig;
        r.y0 = (y0 - surf->h) << s->yeig;
        r.y1 = (y1 - surf->h) << s->yeig;
        if (m == 0 && !over) {
            box = r;
        } else {
            if (r.x0 < box.x0) box.x0 = r.x0;
            if (r.y0 < box.y0) box.y0 = r.y0;
            if (r.x1 > box.x1) box.x1 = r.x1;
            if (r.y1 > box.y1) box.y1 = r.y1;
        }
        if (m < MAX_DAMAGE)
            out[m++] = r;
        else
            over = 1;
    }
    if (over) {
        out[0] = box;
        return 1;
    }
    return m;
}

/* DispmanX compatibility: after the vsync wait, plot the surface (its
   source rectangle) scaled to the element's destination rectangle. */
static void present_dmx(egl_surface *surf, const screen_info *s)
{
    riscos_dmx_placement pl;
    _kernel_swi_regs r;
    int factors[4], i, sw, sh, sxe, sye;
    long long top, y;
    os_rect clip, all;

    if (!__riscos_dispmanx_placement || !__riscos_dispmanx_placement(surf->dmx, &pl))
        return;                         /* element gone */
    for (i = 0; i < surf->swap_interval; i++)
        _kernel_osbyte(19, 0, 0);
    if (!pl.visible || !surf->sprite || pl.w <= 0 || pl.h <= 0)
        return;
    sw = pl.src_w > 0 ? pl.src_w : surf->w;
    sh = pl.src_h > 0 ? pl.src_h : surf->h;
    sxe = surf->sprite_mode == SPRITE_MODE_TYPE6 ? 1 : s->xeig;   /* 90 dpi = eig 1 */
    sye = surf->sprite_mode == SPRITE_MODE_TYPE6 ? 1 : s->yeig;
    factors[0] = pl.w << s->xeig;       /* x: multiply, then divide */
    factors[1] = pl.h << s->yeig;
    factors[2] = sw << sxe;
    factors[3] = sh << sye;

    all.x0 = 0; all.y0 = 0;
    all.x1 = s->width << s->xeig; all.y1 = s->height << s->yeig;
    clip.x0 = pl.x << s->xeig;
    clip.x1 = (pl.x + pl.w) << s->xeig;
    clip.y1 = (s->height - pl.y) << s->yeig;
    clip.y0 = (s->height - pl.y - pl.h) << s->yeig;
    if (clip.x0 < 0) clip.x0 = 0;
    if (clip.y0 < 0) clip.y0 = 0;
    if (clip.x1 > all.x1) clip.x1 = all.x1;
    if (clip.y1 > all.y1) clip.y1 = all.y1;
    if (clip.x0 >= clip.x1 || clip.y0 >= clip.y1)
        return;

    /* The source rectangle's top left lands on the destination's top left;
       the sprite's padding rows (see MIN_SPRITE_BYTES) fall outside. */
    top = ((long long) (s->height - pl.y) << s->yeig) +
          ((long long) (pl.src_y << sye) * factors[1]) / factors[3];
    /* whole screen pixels, so the top row lands on the destination's top */
    y = top - (((((long long) (surf->sprite_h << sye) * factors[1]) / factors[3])
                >> s->yeig) << s->yeig);
    set_graphics_window(&clip);
    r.r[0] = 512 + 52;                  /* PutSpriteScaled */
    r.r[1] = (int) surf->area;
    r.r[2] = (int) surf->sprite;
    r.r[3] = (int) ((pl.x << s->xeig) -
                    ((long long) (pl.src_x << sxe) * factors[0]) / factors[2]);
    r.r[4] = (int) y;
    r.r[5] = 0;
    r.r[6] = (int) factors;
    r.r[7] = 0;
    _kernel_swi(OS_SpriteOp, &r, &r);
    set_graphics_window(&all);
}

/* Show a finished frame of a window surface: all of it, or only the damaged
   rectangles (rects/n as for eglSwapBuffersWithDamageKHR; NULL = all). */
static void present(egl_display *d, egl_surface *surf, const screen_info *s,
                    const EGLint *rects, int n)
{
    _kernel_swi_regs r;
    int block[11];
    os_rect dmg[MAX_DAMAGE];
    int i, nd;

    if (surf->dmx) {
        present_dmx(surf, s);           /* always the whole surface */
        return;
    }
    nd = damage_rects(surf, rects, n, s, dmg);
    if (nd == 0 && !surf->banks)
        return;                         /* all rectangles empty: nothing changed */

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
            int top = s->height << s->yeig;
            all.x0 = 0; all.y0 = 0;
            all.x1 = s->width << s->xeig; all.y1 = top;
            if (nd < 0) {
                plot_rectangle(surf, NULL, s, &all);
                return;
            }
            for (i = 0; i < nd; i++) {
                os_rect c;
                c.x0 = dmg[i].x0; c.x1 = dmg[i].x1;
                c.y0 = top + dmg[i].y0; c.y1 = top + dmg[i].y1;
                set_graphics_window(&c);
                plot_rectangle(surf, NULL, s, &c);
            }
            set_graphics_window(&all);
        }
        return;
    }

    /* Update the part of the work area the surface covers (or each damaged
       part of it). */
    {
        int left, top;
        if (surf->fixed) {
            left = surf->wa_x;
            top = surf->wa_y;
        } else {
            window_state ws;
            if (!get_window_state(surf->handle, &ws))
                return;
            left = ws.scroll_x;
            top = ws.scroll_y;
        }
        for (i = 0; i < (nd < 0 ? 1 : nd); i++) {
            block[0] = surf->handle;
            if (nd < 0) {
                block[1] = left;
                block[2] = top - (surf->h << s->yeig);
                block[3] = left + (surf->w << s->xeig);
                block[4] = top;
            } else {
                block[1] = left + dmg[i].x0;
                block[2] = top + dmg[i].y0;
                block[3] = left + dmg[i].x1;
                block[4] = top + dmg[i].y1;
            }
            r.r[1] = (int) block;
            if (_kernel_swi(Wimp_UpdateWindow, &r, &r) != NULL)
                return;
            plot_loop(d, surf->handle, surf, block, r.r[0]);
        }
    }
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
            surf->swaps = 0;
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
        surf->swaps = 0;
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
    surf->swaps = 0;
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
    egl_obj_label = d->label;
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
        if (s == (egl_surface *) surface && !s->destroy_pending) {
            egl_obj_label = s->label;
            return s;
        }
    fail(EGL_BAD_SURFACE);
    return NULL;
}

static egl_context *get_context(egl_display *d, EGLContext context)
{
    egl_context *c;
    for (c = d->contexts; c; c = c->next)
        if (c == (egl_context *) context && !c->destroy_pending) {
            egl_obj_label = c->label;
            return c;
        }
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
    case EGL_CONFORMANT:               v = RENDERABLE_BITS; break;
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
    case EGL_RENDERABLE_TYPE:          v = RENDERABLE_BITS; break;
    case EGL_SAMPLE_BUFFERS:
    case EGL_SAMPLES:                  v = 0; break;
    case EGL_STENCIL_SIZE:             v = c->stencil; break;
    case EGL_SURFACE_TYPE:
        v = EGL_WINDOW_BIT | EGL_PBUFFER_BIT | EGL_PIXMAP_BIT |
            EGL_SWAP_BEHAVIOR_PRESERVED_BIT | EGL_LOCK_SURFACE_BIT_KHR;
        break;
    case EGL_MATCH_FORMAT_KHR:
        /* EXACT = B,G,R,A bytes in memory, which is 0x00RRGGBB */
        v = (c->layout == LAYOUT_TRGB) ? EGL_FORMAT_RGBA_8888_EXACT_KHR : EGL_FORMAT_RGBA_8888_KHR;
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

enum { M_ATLEAST, M_EXACT, M_MASK, M_IGNORE, M_FORMAT };

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
    { EGL_MATCH_FORMAT_KHR,       EGL_DONT_CARE,  M_FORMAT },
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
    ENTER();
    if (display_id != EGL_DEFAULT_DISPLAY)
        return EGL_NO_DISPLAY;
    return (EGLDisplay) &display;
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    egl_display *d;
    ENTER();
    if (!(d = get_display(dpy, 0)))
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
    egl_display *d;
    egl_surface *s, *sn;
    egl_context *c, *cn;
    egl_sync *y, *yn;

    ENTER();
    if (!(d = get_display(dpy, 0)))
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
    for (y = d->syncs; y; y = yn) {
        yn = y->next;
        y->magic = 0;
        free(y);
    }
    d->syncs = NULL;
    d->initialised = 0;
    return ok();
}

EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name)
{
    ENTER();
    if (dpy == EGL_NO_DISPLAY && name == EGL_EXTENSIONS) {
        last_error = EGL_SUCCESS;       /* EGL_EXT_client_extensions */
        return EGL_RISCOS_CLIENT_EXTENSIONS;
    }
    if (!get_display(dpy, 1))
        return NULL;
    last_error = EGL_SUCCESS;
    switch (name) {
    case EGL_VENDOR:      return EGL_RISCOS_VENDOR;
    case EGL_VERSION:     return EGL_RISCOS_VERSION;
    case EGL_EXTENSIONS:  return EGL_RISCOS_EXTENSIONS;
    case EGL_CLIENT_APIS: return "OpenGL OpenGL_ES";
    }
    fail(EGL_BAD_PARAMETER);
    return NULL;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                                            EGLint config_size, EGLint *num_config)
{
    egl_display *d;
    int i, n;

    ENTER();
    if (!(d = get_display(dpy, 1)))
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
    egl_display *d;
    EGLint want[NCRITERIA];
    const egl_config *match[8];
    int i, j, n = 0, by_id = 0;
    EGLint want_id = 0;
    int pixmap_layout = -1;

    ENTER();
    if (!(d = get_display(dpy, 1)))
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
                case M_FORMAT:  /* RGBA_8888 also matches the EXACT ones */
                    good = have == want[j] ||
                           (want[j] == EGL_FORMAT_RGBA_8888_KHR &&
                            have == EGL_FORMAT_RGBA_8888_EXACT_KHR);
                    break;
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
    egl_display *d;
    const egl_config *c;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(c = get_config(d, config)))
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
    (void) d;
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
    s->n_damage = -1;
    return s;
}

static void add_surface(egl_display *d, egl_surface *s)
{
    s->next = d->surfaces;
    d->surfaces = s;
}

static EGLSurface create_window_surface(EGLDisplay dpy, EGLConfig config,
                                        EGLNativeWindowType win, const EGLint *attrib_list)
{
    egl_display *d;
    const egl_config *c;
    egl_surface *s;
    screen_info scr;
    window_state ws;
    int i, have_w = 0, have_h = 0, dmx = 0, dmx_w = 0, dmx_h = 0;

    if (!(d = get_display(dpy, 1)) || !(c = get_config(d, config)))
        return EGL_NO_SURFACE;
    if (win != -1 && win != 0 && __riscos_dispmanx_window &&
        __riscos_dispmanx_window((const void *) win, &dmx, &dmx_w, &dmx_h)) {
        /* a pointer to an EGL_DISPMANX_WINDOW_T (DispmanX compatibility) */
    } else if (win != -1 && (win == 0 || !get_window_state(win, &ws))) {
        fail(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }
    if (!(s = new_surface(d, c, SURF_WINDOW)))
        return EGL_NO_SURFACE;
    s->handle = win;
    if (dmx) {
        s->handle = -3;
        s->dmx = dmx;
        s->w = dmx_w;
        s->h = dmx_h;
    }

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
            if (s->handle != -1 || v < 0 || v == 1 || v > MAX_BANKS)
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
        if (!have_w || !have_h || s->w < 1 || s->h < 1 || s->handle < 0)
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

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                     EGLNativeWindowType win,
                                                     const EGLint *attrib_list)
{
    ENTER();
    return create_window_surface(dpy, config, win, attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                                      const EGLint *attrib_list)
{
    egl_display *d;
    const egl_config *c;
    egl_surface *s;
    int i, w = 0, h = 0, largest = 0;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(c = get_config(d, config)))
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

static EGLSurface create_pixmap_surface(EGLDisplay dpy, EGLConfig config,
                                        void *pixmap, const EGLint *attrib_list)
{
    egl_display *d;
    const egl_config *c;
    egl_surface *s;
    int i, w, h, layout;
    void *px;

    if (!(d = get_display(dpy, 1)) || !(c = get_config(d, config)))
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

EGLAPI EGLSurface EGLAPIENTRY eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                                     EGLNativePixmapType pixmap,
                                                     const EGLint *attrib_list)
{
    ENTER();
    return create_pixmap_surface(dpy, config, (void *) pixmap, attrib_list);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
    egl_display *d;
    egl_surface *s;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (s->current)
        s->destroy_pending = 1;
    else
        unlink_surface(d, s);
    return ok();
}

/* eglQuerySurface and eglQuerySurface64KHR. */
static EGLBoolean query_surface(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                                EGLAttribKHR *value)
{
    egl_display *d;
    egl_surface *s;
    int trgb;

    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    trgb = s->cfg->layout == LAYOUT_TRGB;
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

    case EGL_BUFFER_AGE_EXT:
        /* How many frames old the back buffer's contents are (0 = unknown).
           A sprite or the screen itself keeps the last frame: 1. Screen
           banks hold the frame from 'banks' swaps ago. */
        if (s != cur_surf || !cur_ctx)
            return fail(EGL_BAD_SURFACE);
        if (s->kind != SURF_WINDOW || s->swaps == 0)
            *value = 0;
        else if (s->banks)
            *value = s->swaps >= s->banks ? s->banks : 0;
        else
            *value = 1;
        s->age_queried = 1;
        break;

    case EGL_BITMAP_POINTER_KHR:
    case EGL_BITMAP_PITCH_KHR:
    case EGL_BITMAP_ORIGIN_KHR:
    case EGL_BITMAP_PIXEL_RED_OFFSET_KHR:
    case EGL_BITMAP_PIXEL_GREEN_OFFSET_KHR:
    case EGL_BITMAP_PIXEL_BLUE_OFFSET_KHR:
    case EGL_BITMAP_PIXEL_ALPHA_OFFSET_KHR:
    case EGL_BITMAP_PIXEL_LUMINANCE_OFFSET_KHR:
    case EGL_BITMAP_PIXEL_SIZE_KHR:
        if (!s->locked)
            return fail(EGL_BAD_ACCESS);
        switch (attribute) {
        case EGL_BITMAP_POINTER_KHR:  *value = (EGLAttribKHR) s->pixels; break;
        case EGL_BITMAP_PITCH_KHR:    *value = s->stride * 4; break;
        case EGL_BITMAP_ORIGIN_KHR:   *value = EGL_UPPER_LEFT_KHR; break;
        case EGL_BITMAP_PIXEL_RED_OFFSET_KHR:   *value = trgb ? 16 : 0; break;
        case EGL_BITMAP_PIXEL_GREEN_OFFSET_KHR: *value = 8; break;
        case EGL_BITMAP_PIXEL_BLUE_OFFSET_KHR:  *value = trgb ? 0 : 16; break;
        case EGL_BITMAP_PIXEL_ALPHA_OFFSET_KHR: *value = 24; break;
        case EGL_BITMAP_PIXEL_LUMINANCE_OFFSET_KHR: *value = 0; break;
        case EGL_BITMAP_PIXEL_SIZE_KHR: *value = 32; break;
        }
        break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                              EGLint attribute, EGLint *value)
{
    EGLAttribKHR v;
    ENTER();
    if (!value) {
        egl_display *d = get_display(dpy, 1);
        if (d && get_surface(d, surface))
            fail(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    v = *value;                 /* unchanged for attributes that don't apply */
    if (!query_surface(dpy, surface, attribute, &v))
        return EGL_FALSE;
    *value = (EGLint) v;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface64KHR(EGLDisplay dpy, EGLSurface surface,
                                                   EGLint attribute, EGLAttribKHR *value)
{
    ENTER();
    return query_surface(dpy, surface, attribute, value);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                                               EGLint attribute, EGLint value)
{
    egl_display *d;
    egl_surface *s;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
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

static EGLBoolean tex_image(EGLDisplay dpy, EGLSurface surface)
{
    egl_display *d;
    if (!(d = get_display(dpy, 1)) || !get_surface(d, surface))
        return EGL_FALSE;
    return fail(EGL_BAD_MATCH);         /* no bind-to-texture configs */
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
    ENTER();
    (void) buffer;
    return tex_image(dpy, surface);
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
    ENTER();
    (void) buffer;
    return tex_image(dpy, surface);
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api)
{
    ENTER();
    if (api != EGL_OPENGL_API && api != EGL_OPENGL_ES_API)
        return fail(EGL_BAD_PARAMETER); /* no OpenVG */
    bound_api = api;
    return ok();
}

EGLAPI EGLenum EGLAPIENTRY eglQueryAPI(void)
{
    ENTER();
    return bound_api;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitClient(void)
{
    ENTER();
    if (cur_ctx)
        glFinish();
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL(void)
{
    ENTER();
    if (cur_ctx)
        glFinish();
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative(EGLint engine)
{
    ENTER();
    if (engine != EGL_CORE_NATIVE_ENGINE)
        return fail(EGL_BAD_PARAMETER);
    return ok();                        /* RISC OS drawing is synchronous */
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread(void)
{
    ENTER();
    if (cur_ctx)
        eglMakeCurrent((EGLDisplay) &display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    bound_api = EGL_OPENGL_API;
    return ok();
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype,
        EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list)
{
    egl_display *d;
    ENTER();
    (void) buftype; (void) buffer; (void) attrib_list;
    if ((d = get_display(dpy, 1)) && get_config(d, config))
        fail(EGL_BAD_PARAMETER);        /* only OpenVG images exist for this */
    return EGL_NO_SURFACE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
    ENTER();
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
    egl_display *d;
    const egl_config *c;
    egl_context *ctx, *share = NULL;
    EGLint major = 1, minor = 0, flags = 0;
    EGLint profile = EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR;
    int attribs[20], i, n = 0, explicit_version = 0, explicit_profile = 0;
    int release_flush = 1, es = 0;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(c = get_config(d, config)))
        return EGL_NO_CONTEXT;
    if (bound_api != EGL_OPENGL_API && bound_api != EGL_OPENGL_ES_API) {
        fail(EGL_BAD_MATCH);
        return EGL_NO_CONTEXT;
    }
    if (share_context != EGL_NO_CONTEXT && !(share = get_context(d, share_context)))
        return EGL_NO_CONTEXT;
    if (share && share->api != bound_api) {
        fail(EGL_BAD_MATCH);            /* can't share between GL and GLES */
        return EGL_NO_CONTEXT;
    }

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
        case EGL_CONTEXT_RELEASE_BEHAVIOR_KHR:  /* EGL_KHR_context_flush_control */
            if (v != EGL_CONTEXT_RELEASE_BEHAVIOR_FLUSH_KHR &&
                v != EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR) {
                fail(EGL_BAD_ATTRIBUTE);
                return EGL_NO_CONTEXT;
            }
            release_flush = (v == EGL_CONTEXT_RELEASE_BEHAVIOR_FLUSH_KHR);
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
    if (bound_api == EGL_OPENGL_ES_API) {
        /* EGL_CONTEXT_CLIENT_VERSION (default 1); classic swrast gives
           ES 1.1 and ES 2.0 */
        if (explicit_profile || (flags & EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR)) {
            fail(EGL_BAD_ATTRIBUTE);    /* desktop GL only */
            return EGL_NO_CONTEXT;
        }
        if (major == 1 && (minor == 0 || minor == 1))
            es = 1;
        else if (major == 2 && minor == 0)
            es = 2;
        else {
            fail(EGL_BAD_MATCH);        /* ES 3.x: not with this renderer */
            return EGL_NO_CONTEXT;
        }
    }

    attribs[n++] = OSMESA_FORMAT;
    attribs[n++] = c->layout == LAYOUT_TRGB ? OSMESA_BGRA : OSMESA_RGBA;
    attribs[n++] = OSMESA_DEPTH_BITS;   attribs[n++] = c->depth;
    attribs[n++] = OSMESA_STENCIL_BITS; attribs[n++] = c->stencil;
    attribs[n++] = OSMESA_ACCUM_BITS;   attribs[n++] = 0;
    /* The profile only applies to GL 3.2+ (EGL_KHR_create_context). */
    attribs[n++] = OSMESA_PROFILE;
    if (es)
        attribs[n++] = es == 1 ? OSMESA_ES1_PROFILE : OSMESA_ES2_PROFILE;
    else
        attribs[n++] = (explicit_profile && profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
                        (major > 3 || (major == 3 && minor >= 2)))
                       ? OSMESA_CORE_PROFILE : OSMESA_COMPAT_PROFILE;
    if (!es && explicit_version && (major > 2 || (major == 2 && minor > 1))) {
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
        fail(EGL_BAD_MATCH);            /* e.g. GL 3.x asked for: this is 2.1 / ES 2.0 */
        return EGL_NO_CONTEXT;
    }
    ctx->magic = MAGIC_CONTEXT;
    ctx->cfg = c;
    ctx->api = bound_api;
    ctx->es = es;
    ctx->release_flush = release_flush;
    ctx->next = d->contexts;
    d->contexts = ctx;
    ok();
    return (EGLContext) ctx;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay dpy, EGLContext context)
{
    egl_display *d;
    egl_context *c;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(c = get_context(d, context)))
        return EGL_FALSE;
    if (c->current)
        c->destroy_pending = 1;
    else
        unlink_context(d, c);
    return ok();
}

/* EGL_KHR_context_flush_control: flush a context that stops being current
   (unless it was created with EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR). */
static void flush_on_release(egl_context *c)
{
    if (c && c->release_flush)
        glFlush();
}

static void release_current(egl_display *d)
{
    egl_context *c = cur_ctx;
    egl_surface *s = cur_surf;
    flush_on_release(c);
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

/* EGL_KHR_surfaceless_context: OSMesa always needs a buffer, so a context
   made current without a surface draws into this (framebuffer objects are
   what such a context is for). */
static unsigned int surfaceless_pixel;

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                             EGLSurface read, EGLContext context)
{
    egl_display *d;
    egl_context *c;
    egl_surface *s = NULL;

    ENTER();
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
    if ((draw == EGL_NO_SURFACE) != (read == EGL_NO_SURFACE))
        return fail(EGL_BAD_MATCH);
    if (draw != EGL_NO_SURFACE) {
        if (!(s = get_surface(d, draw)) || !get_surface(d, read))
            return EGL_FALSE;
        if (draw != read)
            return fail(EGL_BAD_MATCH);     /* OSMesa has one buffer per context */
        if (s->cfg->layout != c->cfg->layout)
            return fail(EGL_BAD_MATCH);
        if (s->locked)
            return fail(EGL_BAD_ACCESS);    /* EGL_KHR_lock_surface */
    }

    if (c != cur_ctx)
        flush_on_release(cur_ctx);

    if (s) {
        if (s->kind == SURF_WINDOW) {
            screen_info scr;
            read_screen(&scr);
            if (update_window_buffer(s, &scr) < 0)
                return EGL_FALSE;
        }
        if (!bind(c, s))
            return fail(EGL_BAD_ALLOC);
        if (c->surfaceless && !c->had_surface) {
            /* Mesa sizes the viewport and scissor box the first time a
               context is bound, which was to the 1x1 stand-in: fix them. */
            glViewport(0, 0, s->w, s->h);
            glScissor(0, 0, s->w, s->h);
        }
        c->had_surface = 1;
    } else {
        if (!OSMesaMakeCurrent(c->om, &surfaceless_pixel, GL_UNSIGNED_BYTE, 1, 1))
            return fail(EGL_BAD_ALLOC);
        OSMesaPixelStore(OSMESA_Y_UP, 0);
        OSMesaPixelStore(OSMESA_ROW_LENGTH, 1);
        c->surfaceless = 1;
    }

    if (c != cur_ctx || s != cur_surf) {
        egl_context *oc = cur_ctx;
        egl_surface *os = cur_surf;
        cur_ctx = c;
        cur_surf = s;
        c->current = 1;
        if (s)
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

/* One context is current at a time (OSMesa has one). EGL keeps one per
   client API, so report it only for its own API. */
static egl_context *current_for_api(void)
{
    return (cur_ctx && cur_ctx->api == bound_api) ? cur_ctx : NULL;
}

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void)
{
    ENTER();
    return current_for_api() ? (EGLContext) cur_ctx : EGL_NO_CONTEXT;
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint readdraw)
{
    ENTER();
    if (readdraw != EGL_READ && readdraw != EGL_DRAW) {
        fail(EGL_BAD_PARAMETER);
        return EGL_NO_SURFACE;
    }
    return (current_for_api() && cur_surf) ? (EGLSurface) cur_surf : EGL_NO_SURFACE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void)
{
    ENTER();
    return current_for_api() ? (EGLDisplay) &display : EGL_NO_DISPLAY;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryContext(EGLDisplay dpy, EGLContext context,
                                              EGLint attribute, EGLint *value)
{
    egl_display *d;
    egl_context *c;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(c = get_context(d, context)))
        return EGL_FALSE;
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    switch (attribute) {
    case EGL_CONFIG_ID:              *value = c->cfg->id; break;
    case EGL_CONTEXT_CLIENT_TYPE:    *value = c->api; break;
    case EGL_CONTEXT_CLIENT_VERSION: *value = c->es; break;   /* ES only; 0 for GL */
    case EGL_RENDER_BUFFER:
        if (!c->current || !cur_surf)
            *value = EGL_NONE;
        else
            *value = (cur_surf->kind == SURF_PIXMAP || cur_surf->direct)
                     ? EGL_SINGLE_BUFFER : EGL_BACK_BUFFER;
        break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    return ok();
}

/* eglSwapBuffers and the with-damage versions. */
static EGLBoolean swap(EGLDisplay dpy, EGLSurface surface, const EGLint *rects, EGLint n)
{
    egl_display *d;
    egl_surface *s;
    screen_info scr;
    int changed, is_current;

    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (n < 0 || (n > 0 && !rects))
        return fail(EGL_BAD_PARAMETER);
    if (s->locked)
        return fail(EGL_BAD_ACCESS);
    is_current = (s == cur_surf && cur_ctx);
    /* A lockable window surface written through eglLockSurfaceKHR can be
       shown without a context current to it. */
    if (!is_current && !(s->kind == SURF_WINDOW && s->ever_locked && !s->current))
        return fail(EGL_BAD_SURFACE);
    if (s->kind != SURF_WINDOW)
        return ok();                    /* no effect on pbuffers/pixmaps */

    /* EGL_KHR_partial_update: the damage region set for this frame is what
       changed, unless the swap gives its own. */
    if (n == 0 && s->n_damage > 0) {
        rects = s->damage;
        n = s->n_damage;
    }

    if (is_current)
        glFinish();
    read_screen(&scr);
    present(d, s, &scr, n > 0 ? rects : NULL, n);
    s->swaps++;
    s->age_queried = 0;
    s->n_damage = -1;

    /* Window resized or screen mode changed: the next frame gets a new
       buffer (the one just shown was complete). */
    changed = update_window_buffer(s, &scr);
    if (changed < 0)
        return EGL_FALSE;
    if (s->banks)
        changed = 1;                    /* now drawing into another bank */
    if (changed && is_current && !bind(cur_ctx, s))
        return fail(EGL_BAD_ALLOC);
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    ENTER();
    return swap(dpy, surface, NULL, 0);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffersWithDamageKHR(EGLDisplay dpy, EGLSurface surface,
                                                          const EGLint *rects, EGLint n_rects)
{
    ENTER();
    return swap(dpy, surface, rects, n_rects);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surface,
                                                          const EGLint *rects, EGLint n_rects)
{
    ENTER();
    return swap(dpy, surface, rects, n_rects);
}

EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers(EGLDisplay dpy, EGLSurface surface,
                                             EGLNativePixmapType target)
{
    egl_display *d;
    egl_surface *s;
    int w, h, layout, x, y, cw, ch;
    void *px;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
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
/* EGL_KHR_partial_update                                              */

EGLAPI EGLBoolean EGLAPIENTRY eglSetDamageRegionKHR(EGLDisplay dpy, EGLSurface surface,
                                                    EGLint *rects, EGLint n_rects)
{
    egl_display *d;
    egl_surface *s;
    int i;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (s != cur_surf || !cur_ctx || s->kind != SURF_WINDOW)
        return fail(EGL_BAD_MATCH);
    if (n_rects < 0 || (n_rects > 0 && !rects))
        return fail(EGL_BAD_PARAMETER);
    if (!s->age_queried || s->n_damage >= 0)
        return fail(EGL_BAD_ACCESS);    /* age not asked for, or already set */
    if (n_rects == 0) {
        s->n_damage = 0;                /* whole surface */
        return ok();
    }
    if (n_rects > MAX_DAMAGE) {
        /* keep their bounding box */
        int x0 = rects[0], y0 = rects[1];
        int x1 = x0 + rects[2], y1 = y0 + rects[3];
        for (i = 1; i < n_rects; i++) {
            if (rects[i * 4] < x0) x0 = rects[i * 4];
            if (rects[i * 4 + 1] < y0) y0 = rects[i * 4 + 1];
            if (rects[i * 4] + rects[i * 4 + 2] > x1) x1 = rects[i * 4] + rects[i * 4 + 2];
            if (rects[i * 4 + 1] + rects[i * 4 + 3] > y1) y1 = rects[i * 4 + 1] + rects[i * 4 + 3];
        }
        s->damage[0] = x0; s->damage[1] = y0;
        s->damage[2] = x1 - x0; s->damage[3] = y1 - y0;
        s->n_damage = 1;
        return ok();
    }
    memcpy(s->damage, rects, n_rects * 4 * sizeof rects[0]);
    s->n_damage = n_rects;
    return ok();
}

/* ------------------------------------------------------------------ */
/* EGL_KHR_lock_surface3                                               */

EGLAPI EGLBoolean EGLAPIENTRY eglLockSurfaceKHR(EGLDisplay dpy, EGLSurface surface,
                                                const EGLint *attrib_list)
{
    egl_display *d;
    egl_surface *s;
    int i;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLint a = attrib_list[i], v = attrib_list[i + 1];
        if (a == EGL_MAP_PRESERVE_PIXELS_KHR)
            continue;                   /* contents are always kept */
        if (a == EGL_LOCK_USAGE_HINT_KHR &&
            !(v & ~(EGL_READ_SURFACE_BIT_KHR | EGL_WRITE_SURFACE_BIT_KHR)))
            continue;
        return fail(EGL_BAD_ATTRIBUTE);
    }
    if (s->locked || s->current)
        return fail(EGL_BAD_ACCESS);
    if (s->kind == SURF_WINDOW) {
        screen_info scr;
        read_screen(&scr);              /* the window may have changed size */
        if (update_window_buffer(s, &scr) < 0)
            return EGL_FALSE;
    }
    s->locked = 1;
    s->ever_locked = 1;
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglUnlockSurfaceKHR(EGLDisplay dpy, EGLSurface surface)
{
    egl_display *d;
    egl_surface *s;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!s->locked)
        return fail(EGL_BAD_ACCESS);
    s->locked = 0;
    return ok();
}

/* ------------------------------------------------------------------ */
/* EGL_KHR_fence_sync, EGL_KHR_reusable_sync, EGL_KHR_wait_sync         */
/* Rendering is done by the CPU, in order: a fence is signalled as soon as
   it's made (after glFinish). There is one thread, so nothing can signal
   a reusable sync while eglClientWaitSyncKHR waits for it: waiting on an
   unsignalled one returns EGL_TIMEOUT_EXPIRED_KHR at once. */

static egl_sync *get_sync(egl_display *d, EGLSyncKHR sync)
{
    egl_sync *y;
    for (y = d->syncs; y; y = y->next)
        if (y == (egl_sync *) sync) {
            egl_obj_label = y->label;
            return y;
        }
    fail(EGL_BAD_PARAMETER);
    return NULL;
}

EGLAPI EGLSyncKHR EGLAPIENTRY eglCreateSyncKHR(EGLDisplay dpy, EGLenum type,
                                               const EGLint *attrib_list)
{
    egl_display *d;
    egl_sync *y;

    ENTER();
    if (!(d = get_display(dpy, 1)))
        return EGL_NO_SYNC_KHR;
    if (attrib_list && attrib_list[0] != EGL_NONE) {
        fail(EGL_BAD_ATTRIBUTE);
        return EGL_NO_SYNC_KHR;
    }
    if (type != EGL_SYNC_FENCE_KHR && type != EGL_SYNC_REUSABLE_KHR) {
        fail(EGL_BAD_ATTRIBUTE);
        return EGL_NO_SYNC_KHR;
    }
    if (type == EGL_SYNC_FENCE_KHR && !cur_ctx) {
        fail(EGL_BAD_MATCH);
        return EGL_NO_SYNC_KHR;
    }
    y = (egl_sync *) calloc(1, sizeof *y);
    if (!y) {
        fail(EGL_BAD_ALLOC);
        return EGL_NO_SYNC_KHR;
    }
    y->magic = MAGIC_SYNC;
    y->type = type;
    if (type == EGL_SYNC_FENCE_KHR) {
        glFinish();
        y->status = EGL_SIGNALED_KHR;
    } else {
        y->status = EGL_UNSIGNALED_KHR;
    }
    y->next = d->syncs;
    d->syncs = y;
    ok();
    return (EGLSyncKHR) y;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySyncKHR(EGLDisplay dpy, EGLSyncKHR sync)
{
    egl_display *d;
    egl_sync *y, **p;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(y = get_sync(d, sync)))
        return EGL_FALSE;
    for (p = &d->syncs; *p; p = &(*p)->next)
        if (*p == y) {
            *p = y->next;
            break;
        }
    y->magic = 0;
    free(y);
    return ok();
}

EGLAPI EGLint EGLAPIENTRY eglClientWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync,
                                               EGLint flags, EGLTimeKHR timeout)
{
    egl_display *d;
    egl_sync *y;
    (void) timeout;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(y = get_sync(d, sync)))
        return EGL_FALSE;
    if ((flags & EGL_SYNC_FLUSH_COMMANDS_BIT_KHR) && cur_ctx)
        glFlush();
    ok();
    return y->status == EGL_SIGNALED_KHR ? EGL_CONDITION_SATISFIED_KHR : EGL_TIMEOUT_EXPIRED_KHR;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetSyncAttribKHR(EGLDisplay dpy, EGLSyncKHR sync,
                                                  EGLint attribute, EGLint *value)
{
    egl_display *d;
    egl_sync *y;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(y = get_sync(d, sync)))
        return EGL_FALSE;
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    switch (attribute) {
    case EGL_SYNC_TYPE_KHR:   *value = y->type; break;
    case EGL_SYNC_STATUS_KHR: *value = y->status; break;
    case EGL_SYNC_CONDITION_KHR:
        if (y->type != EGL_SYNC_FENCE_KHR)
            return fail(EGL_BAD_ATTRIBUTE);
        *value = EGL_SYNC_PRIOR_COMMANDS_COMPLETE_KHR;
        break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglSignalSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLenum mode)
{
    egl_display *d;
    egl_sync *y;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(y = get_sync(d, sync)))
        return EGL_FALSE;
    if (y->type != EGL_SYNC_REUSABLE_KHR)
        return fail(EGL_BAD_MATCH);
    if (mode != EGL_SIGNALED_KHR && mode != EGL_UNSIGNALED_KHR)
        return fail(EGL_BAD_PARAMETER);
    y->status = mode;
    return ok();
}

EGLAPI EGLint EGLAPIENTRY eglWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags)
{
    egl_display *d;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !get_sync(d, sync))
        return EGL_FALSE;
    if (!cur_ctx)
        return fail(EGL_BAD_MATCH);
    if (flags != 0)
        return fail(EGL_BAD_PARAMETER);
    return ok();                        /* GL runs in order on the CPU anyway */
}

/* ------------------------------------------------------------------ */
/* EGL_EXT_platform_base with EGL_RISCOS_platform_wimp                 */

EGLAPI EGLDisplay EGLAPIENTRY eglGetPlatformDisplayEXT(EGLenum platform, void *native_display,
                                                       const EGLint *attrib_list)
{
    ENTER();
    if (platform != EGL_PLATFORM_RISCOS) {
        fail(EGL_BAD_PARAMETER);
        return EGL_NO_DISPLAY;
    }
    if (attrib_list && attrib_list[0] != EGL_NONE) {
        fail(EGL_BAD_ATTRIBUTE);
        return EGL_NO_DISPLAY;
    }
    if (native_display != NULL) {
        fail(EGL_BAD_PARAMETER);        /* only the default display (the screen) */
        return EGL_NO_DISPLAY;
    }
    ok();
    return (EGLDisplay) &display;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config,
                                                                void *native_window,
                                                                const EGLint *attrib_list)
{
    ENTER();
    /* native_window points to the Wimp window handle (or to -1) */
    if (!native_window) {
        egl_display *d = get_display(dpy, 1);
        if (d && get_config(d, config))
            fail(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }
    return create_window_surface(dpy, config, *(const EGLNativeWindowType *) native_window,
                                 attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformPixmapSurfaceEXT(EGLDisplay dpy, EGLConfig config,
                                                                void *native_pixmap,
                                                                const EGLint *attrib_list)
{
    ENTER();
    /* native_pixmap is the sprite header pointer itself */
    return create_pixmap_surface(dpy, config, native_pixmap, attrib_list);
}

/* ------------------------------------------------------------------ */
/* EGL_KHR_debug                                                       */

EGLAPI EGLint EGLAPIENTRY eglDebugMessageControlKHR(EGLDEBUGPROCKHR callback,
                                                    const EGLAttrib *attrib_list)
{
    int i, en[4];
    ENTER();
    memcpy(en, debug_enabled, sizeof en);
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLAttrib a = attrib_list[i];
        if (a < EGL_DEBUG_MSG_CRITICAL_KHR || a > EGL_DEBUG_MSG_INFO_KHR) {
            fail(EGL_BAD_ATTRIBUTE);
            return EGL_BAD_ATTRIBUTE;
        }
        en[a - EGL_DEBUG_MSG_CRITICAL_KHR] = attrib_list[i + 1] != EGL_FALSE;
    }
    memcpy(debug_enabled, en, sizeof en);
    debug_callback = callback;
    ok();
    return EGL_SUCCESS;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryDebugKHR(EGLint attribute, EGLAttrib *value)
{
    ENTER();
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    if (attribute >= EGL_DEBUG_MSG_CRITICAL_KHR && attribute <= EGL_DEBUG_MSG_INFO_KHR)
        *value = debug_enabled[attribute - EGL_DEBUG_MSG_CRITICAL_KHR] ? EGL_TRUE : EGL_FALSE;
    else if (attribute == EGL_DEBUG_CALLBACK_KHR)
        *value = (EGLAttrib) debug_callback;
    else
        return fail(EGL_BAD_ATTRIBUTE);
    return ok();
}

EGLAPI EGLint EGLAPIENTRY eglLabelObjectKHR(EGLDisplay dpy, EGLenum objectType,
                                            EGLObjectKHR object, EGLLabelKHR label)
{
    egl_display *d;
    ENTER();
    if (objectType == EGL_OBJECT_THREAD_KHR) {
        thread_label = label;
        ok();
        return EGL_SUCCESS;
    }
    if (!(d = get_display(dpy, 0)))
        return EGL_BAD_DISPLAY;
    switch (objectType) {
    case EGL_OBJECT_DISPLAY_KHR:
        if (object != (EGLObjectKHR) dpy)
            break;
        d->label = label;
        ok();
        return EGL_SUCCESS;
    case EGL_OBJECT_CONTEXT_KHR:
    case EGL_OBJECT_SURFACE_KHR:
    case EGL_OBJECT_SYNC_KHR:
        if (!d->initialised) {
            fail(EGL_NOT_INITIALIZED);
            return EGL_NOT_INITIALIZED;
        }
        if (objectType == EGL_OBJECT_CONTEXT_KHR) {
            egl_context *c = get_context(d, (EGLContext) object);
            if (!c) break;
            c->label = label;
        } else if (objectType == EGL_OBJECT_SURFACE_KHR) {
            egl_surface *s = get_surface(d, (EGLSurface) object);
            if (!s) break;
            s->label = label;
        } else {
            egl_sync *y = get_sync(d, (EGLSyncKHR) object);
            if (!y) break;
            y->label = label;
        }
        ok();
        return EGL_SUCCESS;
    }
    fail(EGL_BAD_PARAMETER);
    return EGL_BAD_PARAMETER;
}

/* ------------------------------------------------------------------ */
/* EGL_RISCOS_wimp_window                                              */

EGLAPI EGLBoolean EGLAPIENTRY eglRedrawWindowRISCOS(EGLDisplay dpy, int *block)
{
    egl_display *d;
    egl_surface *s;
    _kernel_swi_regs r;

    ENTER();
    if (!(d = get_display(dpy, 1)))
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
    egl_display *d;
    egl_surface *s;
    screen_info scr;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!block)
        return fail(EGL_BAD_PARAMETER);
    if (s->kind != SURF_WINDOW || s->handle < 0)
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
    /* extensions */
    F(eglClientWaitSyncKHR), F(eglCreatePlatformPixmapSurfaceEXT),
    F(eglCreatePlatformWindowSurfaceEXT), F(eglCreateSyncKHR),
    F(eglDebugMessageControlKHR), F(eglDestroySyncKHR), F(eglGetPlatformDisplayEXT),
    F(eglGetSyncAttribKHR), F(eglLabelObjectKHR), F(eglLockSurfaceKHR),
    F(eglQueryDebugKHR), F(eglQuerySurface64KHR), F(eglSetDamageRegionKHR),
    F(eglSignalSyncKHR), F(eglSwapBuffersWithDamageEXT), F(eglSwapBuffersWithDamageKHR),
    F(eglUnlockSurfaceKHR), F(eglWaitSyncKHR),
    F(eglRedrawWindowRISCOS), F(eglPlotSurfaceRISCOS),
};
#undef F

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *procname)
{
    size_t i;
    ENTER();
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
