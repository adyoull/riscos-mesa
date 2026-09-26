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
 * Source layout: this file has the common definitions (objects, limits,
 * SWI numbers) and includes the rest, which is in egl/parts/ by topic:
 *   screen.c      the screen, Wimp windows, plotting frames (swap/present)
 *   buffers.c     sprites, screen banks, pbuffer memory, pixmaps
 *   validation.c  checking handles and attribute lists
 *   configs.c     the configs and eglChooseConfig matching
 *   api.c         the EGL 1.4 entry points
 *   extensions.c  partial update, lock surface, sync objects, platform,
 *                 debug, EGL_RISCOS_wimp_window, eglGetProcAddress
 * They are one compilation unit (build only this file), so the helpers
 * shared between them stay static and invisible to programs linking
 * libEGL.a.
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
#pragma weak __riscos_dispmanx_swapped

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

/* 32bpp sprite type 6 (0x00BBGGRR) at the screen's resolution, so it plots
   pixel for pixel: 90 dpi in a normal (EX1 EY1) mode, 180 dpi in a high
   resolution (EX0 EY0) one. */
#define SPRITE_MODE_TYPE6_EIG(xeig, yeig) \
    (1 | ((180 >> (xeig)) << 1) | ((180 >> (yeig)) << 14) | (6 << 27))

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
/* The EGL spec's initial API is OpenGL ES when it's supported (code written
   for the Pi's Khronos stack relies on it); desktop GL code binds
   EGL_OPENGL_API. */
static EGLenum bound_api = EGL_OPENGL_ES_API;
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
/* The rest of the library, one unit with this file (see the list in   */
/* the comment at the top).                                            */

#include "parts/screen.c"
#include "parts/buffers.c"
#include "parts/validation.c"
#include "parts/configs.c"
#include "parts/api.c"
#include "parts/extensions.c"
