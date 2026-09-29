/*
 * egl_internal.h - what the parts of riscos-mesa's EGL share: limits, SWI
 * numbers, the object types, the lock, and the helpers one part uses from
 * another. MIT licence (see LICENSE).
 *
 * The library is one compilation unit: egl_riscos.c includes this, then
 * each file in parts/. Every part also includes this header itself (the
 * include guard makes that a no-op in the build), so an editor or a tool
 * such as clangd can make sense of a part on its own. Everything is
 * static: nothing here is visible to programs linking libEGL.a.
 */
#ifndef EGL_INTERNAL_H
#define EGL_INTERNAL_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>

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
    "EGL_KHR_get_all_proc_addresses EGL_KHR_image EGL_KHR_image_base " \
    "EGL_KHR_image_pixmap EGL_KHR_lock_surface EGL_KHR_lock_surface2 " \
    "EGL_KHR_lock_surface3 EGL_KHR_partial_update EGL_KHR_reusable_sync " \
    "EGL_KHR_surfaceless_context EGL_KHR_swap_buffers_with_damage EGL_KHR_wait_sync " \
    "EGL_RISCOS_overlay EGL_RISCOS_wimp_window"
/* Client extensions: eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS) */
#define EGL_RISCOS_CLIENT_EXTENSIONS \
    "EGL_EXT_client_extensions EGL_EXT_platform_base " \
    "EGL_KHR_client_get_all_proc_addresses EGL_KHR_debug EGL_RISCOS_platform_wimp"

#define MAGIC_DISPLAY 0x444C4745   /* "EGLD" */
#define MAGIC_SURFACE 0x534C4745   /* "EGLS" */
#define MAGIC_CONTEXT 0x434C4745   /* "EGLC" */
#define MAGIC_SYNC    0x594C4745   /* "EGLY" */
#define MAGIC_IMAGE   0x494C4745   /* "EGLI" */
#define MAX_DAMAGE    16           /* more rectangles than this: use their bounds */

#define LAYOUT_TBGR 0              /* 0x00BBGGRR: R,G,B,X in memory = OSMESA_RGBA */
#define LAYOUT_TRGB 1              /* 0x00RRGGBB: B,G,R,X in memory = OSMESA_BGRA */
#define MODEFLAG_TRGB 0x4000

/* Native window handles that aren't Wimp windows */
#define HANDLE_SCREEN   (-1)       /* the whole screen (EGL_RISCOS_SCREEN_WINDOW) */
#define HANDLE_DISPMANX (-3)       /* a DispmanX element (libbcm_host) */

/* OS_SpriteOp reason codes: +256 sprite given by name in a user area,
   +512 by pointer */
#define SPRITEOP_CREATE     (256 + 15)
#define SPRITEOP_PUT_USER   (512 + 34)     /* PutSpriteUserCoords */
#define SPRITEOP_PUT_SCALED (512 + 52)     /* PutSpriteScaled */

/* OS_Byte reason codes */
#define OSBYTE_WAIT_VSYNC    19
#define OSBYTE_DRAW_BANK     112           /* screen bank the VDU draws into */
#define OSBYTE_DISPLAY_BANK  113           /* screen bank the display shows */
#define OSBYTE_VSYNC_COUNT   176           /* counts down once a vsync */

#define WINDOW_FLAG_OPEN (1 << 16)         /* Wimp window flags: the window is open */
#define SWI_X_BIT 0x20000                  /* the "X" (return errors) SWI bit */

/* Largest pbuffer: OSMesa's largest buffer (Mesa's SWRAST_MAX_WIDTH/HEIGHT,
   set by patches/mesa riscos-size-limit). The EGL host harness checks
   they agree. */
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
#ifndef OS_SWINumberFromString
#define OS_SWINumberFromString 0x39
#endif
#ifndef Wimp_ReadSysInfo
#define Wimp_ReadSysInfo    0x400F2
#endif

/* ------------------------------------------------------------------ */
/* Objects                                                             */

typedef struct egl_config {
    EGLint id;
    EGLint depth, stencil;
    int layout;
} egl_config;

enum { SURF_WINDOW, SURF_PBUFFER, SURF_PIXMAP };

/* egl_surface.ovl_state (parts/overlay.c) */
#define OVL_OFF      0          /* no overlay (yet): the sprite is plotted */
#define OVL_ON       1          /* an overlay exists */
#define OVL_FAILED   2          /* gave up until the size or mode changes */

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
    int handle;                 /* Wimp window handle, HANDLE_SCREEN or HANDLE_DISPMANX */
    int dmx;                    /* DispmanX element (handle HANDLE_DISPMANX) */
    int fixed;                  /* work area rectangle given */
    int wa_x, wa_y;             /* its top left, OS units */
    int pb_w, pb_h;             /* pbuffer: the size asked for (0x0 is kept as 1x1) */
    int pb_largest;             /* pbuffer: EGL_LARGEST_PBUFFER as given */
    int direct;                 /* rendering into screen memory */
    int banks;                  /* > 0: flipping between this many screen banks */
    int draw_bank;              /* bank being drawn (1..banks) */
    int rw, rh;                 /* EGL_RENDER_WIDTH/HEIGHT_RISCOS: render size, scaled to the window or screen (0 = follow it) */
    int bank_vsync;             /* vsync counter (OS_Byte 176) at the last bank switch, -1 none */
    void *bank_addr[MAX_BANKS + 1];
    int no_banks;               /* don't try screen banks (failed, or preserved contents wanted) */
    int want_banks;             /* banks asked for at creation: 0 (sprite plot), 2 or 3 */
    int *area;                  /* malloc'd sprite area */
    int *sprite;                /* sprite in it */
    int sprite_mode;            /* mode word / selector used to make it */
    int sprite_eig;             /* the screen's xeig << 4 | yeig then (a selector's
                                   address stays the same when its eigs change) */
    int sprite_h;               /* rows in the sprite: > h when padded (see MIN_SPRITE_BYTES) */
    /* hardware overlay (parts/overlay.c) */
    int ovl_want;               /* EGL_OVERLAY_RISCOS: 1 program asked, 0 program refused, -1 unset (EGL$Overlay decides) */
    int ovl_state;              /* OVL_OFF, OVL_ON, OVL_FAILED */
    int ovl_id, ovl_type;       /* VideoOverlay ID (0 = none); 0 Z-Order, 1 Basic */
    int ovl_banks, ovl_next, ovl_last;  /* buffers; next to write; last shown (-1 none) */
    int ovl_shown;              /* the overlay is showing the surface */
    int ovl_w, ovl_h, ovl_mode; /* the size and screen mode it was made for */
    int ovl_placed[4];          /* scroll x/y and size last given to SetPosition/SetScale */
    int ovl_vsync;              /* the vsync counter at the last buffer switch */
    int ovl_run, ovl_swap_cs;   /* swaps in a row (each soon after the last), time of the last */
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
    OSMesaBuffer buf;           /* what OSMesa draws into: pixels + depth/stencil */
    int bound;                  /* bindings to current contexts (as draw or read) */
    struct egl_thread *thread;  /* the thread whose contexts bind it */
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
    struct egl_thread *thread;  /* the thread it is current in, or NULL */
    struct egl_surface *draw, *read;    /* while current (NULL: surfaceless) */
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

typedef struct egl_image {
    EGLint magic;
    const void *sprite;         /* the native pixmap it was made from */
    void *pixels;
    int w, h;
    int layout;                 /* LAYOUT_TBGR or LAYOUT_TRGB */
    EGLLabelKHR label;
    struct egl_image *next;
} egl_image;

typedef struct egl_display {
    EGLint magic;
    int initialised;
    egl_config configs[8];
    int nconfigs;
    egl_surface *surfaces;
    egl_context *contexts;
    egl_sync *syncs;
    egl_image *images;
    EGLLabelKHR label;
} egl_display;


/* Per-thread state (EGL 1.4 section 3.1): made the first time a thread
   calls EGL, freed by eglReleaseThread and at thread exit. */
typedef struct egl_thread {
    EGLint error;               /* eglGetError */
    EGLenum api;                /* eglBindAPI */
    egl_context *ctx[2];        /* current context for OpenGL [0] and OpenGL ES [1] */
    egl_context *active;        /* the one OSMesa renders with (see current.c) */
    EGLLabelKHR label;          /* EGL_KHR_debug: eglLabelObjectKHR(THREAD) */
} egl_thread;
#define API_SLOT(api) ((api) == EGL_OPENGL_API ? 0 : 1)

/* ------------------------------------------------------------------ */
/* The screen and the Wimp (parts/screen.c)                            */

typedef struct screen_info {
    int flags, xeig, yeig, log2bpp;
    int width, height;          /* pixels */
    void *start;
    int line_length;            /* bytes */
} screen_info;

typedef struct window_state {
    int handle;
    int x0, y0, x1, y1;         /* visible area, screen OS units */
    int scroll_x, scroll_y;
    int behind, flags;
} window_state;

typedef struct { int x0, y0, x1, y1; } os_rect;    /* screen OS units, x1/y1 exclusive */

/* ------------------------------------------------------------------ */
/* Globals, the lock and errors (egl_riscos.c)                         */

static egl_display display;             /* the one display (initialised in egl_riscos.c) */
static const char *egl_cmd;             /* EGL_KHR_debug: the command being run */
static EGLLabelKHR egl_obj_label;       /* ... and the label of the object it found */
static EGLDEBUGPROCKHR debug_callback;
static int debug_enabled[4];

static egl_thread *thr(void);
static EGLBoolean fail(EGLint error);   /* set the thread's error; returns EGL_FALSE */
static EGLBoolean ok(void);             /* clear it; returns EGL_TRUE */
static int egl_enter(const char *cmd);
static void egl_leave(int *entered);
static int egl_lock(void);

/* Every public function starts with ENTER(): it takes the library's lock
   (recursive: the debug callback and libbcm_host may call back into EGL),
   which is released when the function returns (GCC's cleanup attribute),
   and records the function's name for EGL_KHR_debug. LOCK() takes the
   lock alone, for code Mesa calls. Helpers that take no lock themselves
   must only be called with it held. */
#define ENTER() int egl_entered __attribute__((cleanup(egl_leave), unused)) = egl_enter(__func__)
#define LOCK() int egl_locked __attribute__((cleanup(egl_leave), unused)) = egl_lock()

/* ------------------------------------------------------------------ */
/* Helpers one part uses from another, by the part that defines them   */

/* parts/screen.c */
static void dmx_plot(const egl_surface *surf, const screen_info *s,
                     const riscos_dmx_placement *pl, int base_x, int top_os,
                     os_rect clip);
static int vdu_variable(int var);
static void read_screen(screen_info *s);
static int screen_layout(const screen_info *s);
static int read_mode_variable(int mode, int var, int *value);
static int sprite_mode_for(int layout, const screen_info *s);
static int vsync_counter(void);
static int vsyncs_since(int then);
static void wait_vsyncs(int n);
static int get_window_state(int handle, window_state *ws);
static int wanted_size(const egl_surface *surf, const screen_info *s, int *w, int *h);
static os_rect redraw_clip(const int *block);
static int next_redraw_rect(int *block);
static int window_surface_in(const egl_surface *surf, int handle);
static void shown_size(const egl_surface *surf, const window_state *ws, const screen_info *s,
                       int *w, int *h);
static void shown_area(const egl_surface *surf, const window_state *ws, const screen_info *s,
                       int box[4]);
static void plot_rectangle(const egl_surface *surf, const int *block, const screen_info *s,
                           const os_rect *clip);
static void plot_loop(egl_display *d, int handle, egl_surface *only, int *block, int more);
static void dmx_window_loop(const egl_surface *surf, const screen_info *s,
                            const riscos_dmx_placement *pl, int *block, int more);
static void present(egl_display *d, egl_surface *surf, const screen_info *s,
                    const EGLint *rects, int n);

/* parts/buffers.c */
static void free_buffers(egl_surface *surf);
static int update_window_buffer(egl_surface *surf, const screen_info *s);
static int pixmap_info(void *pixmap, int *w, int *h, int *layout, void **pixels);

/* parts/overlay.c */
static void ovl_destroy(egl_surface *surf);
static int ovl_update(egl_display *d, egl_surface *surf, const screen_info *s, int new_frame);
static void ovl_redraw_rectangle(egl_surface *surf, int *block);
static void ovl_check_all(egl_display *d);

/* parts/validation.c */
static egl_display *get_display(EGLDisplay dpy, int need_init);
static const egl_config *get_config(egl_display *d, EGLConfig config);
static egl_surface *get_surface(egl_display *d, EGLSurface surface);
static egl_context *get_context(egl_display *d, EGLContext context);
static void unlink_surface(egl_display *d, egl_surface *surf);
static void unlink_context(egl_display *d, egl_context *ctx);

/* parts/configs.c */
static int config_attrib(const egl_config *c, EGLint attrib, EGLint *value);
static void make_configs(egl_display *d);
static int choose_configs(egl_display *d, const EGLint *attrib_list, const egl_config **match);

/* parts/current.c */
static egl_context *cur_context(void);
static int activate(egl_thread *t, egl_context *c);
static int surface_changed(egl_surface *s);
static void release_slot(egl_display *d, egl_thread *t, int slot);
static int make_current(egl_display *d, egl_thread *t, egl_context *c,
                        egl_surface *draw, egl_surface *read);

/* parts/api.c */
static EGLSurface create_window_surface(EGLDisplay dpy, EGLConfig config,
                                        EGLNativeWindowType win, const EGLint *attrib_list);
static EGLSurface create_pixmap_surface(EGLDisplay dpy, EGLConfig config,
                                        void *pixmap, const EGLint *attrib_list);

/* parts/extensions.c */
static GLboolean image_lookup(void *image, OSMesaImage *desc);
static void destroy_images(egl_display *d);

#endif /* EGL_INTERNAL_H */
