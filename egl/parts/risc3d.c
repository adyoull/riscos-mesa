/*
 * egl/parts/risc3d.c - contexts drawn by RISC3D, a hardware path, instead
 * of OSMesa.
 *
 * Where the RISC3D module has a driver registered (on RISC OS on Linux:
 * Linux's graphics driver) and RISC3D$Disable isn't set, eglCreateContext
 * gives an OpenGL (up to 2.1, compatibility), OpenGL ES 1.1 (RISC3D 0.4
 * and its rol-3d) or OpenGL ES 2 context to RISC3D; anywhere else, or if RISC3D can't make one, OSMesa draws as
 * before. A program sees the same EGL and GL either way.
 *
 * - The gl* entry points are glapi's stubs. For a RISC3D context the
 *   dispatch table is made from RISC3D's list of functions (by name,
 *   OSMesaMakeDispatchRISCOS), with a few of EGL's own in front; it is set
 *   when the context becomes current, so a call costs no more than before.
 * - eglSwapBuffers takes the finished frame with r3d_swap into the window
 *   surface's sprite (or screen memory), then shows it as before.
 *   EGL$Overlapped (a system variable, read when the context is made) set
 *   to 1 uses r3d_swap_overlapped instead: no wait for the GPU, a frame
 *   of latency.
 * - glFinish (also eglWaitClient/eglWaitGL, which call it) copies the
 *   frame into a pbuffer or pixmap surface being drawn, so its pixels are
 *   there as with OSMesa; so does eglCopyBuffers.
 * - glEGLImageTargetTexture2DOES on a RISC3D context copies the sprite into
 *   the texture at the call (RISC3D can't draw from RISC OS memory): later
 *   changes to the sprite need the call again. Images over the program's
 *   memory (EGL_RISCOS_image_memory, the GPU library only) are used in
 *   place, by glEGLImageTargetRenderbufferStorageOES too. A program that relies on
 *   the texture following the sprite (zero copy) asks for an OSMesa
 *   context with EGL_CONTEXT_SOFTWARE_RISCOS.
 * - Not given to RISC3D: OpenGL ES 3, OpenGL 3 and core profiles,
 *   and contexts sharing objects with another context (each RISC3D context
 *   is its own; sharing with a RISC3D context is EGL_BAD_MATCH).
 * - Zero-copy window surfaces: where RISC3D can put frames on a display
 *   plane, a window surface's frames go there (below), not read back.
 * - EGL_CONTEXT_SOFTWARE_RISCOS (EGL_RISCOS_renderer) asks for OSMesa.
 * - One thread draws: RISC3D's current context is global.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */

#include "r3d.h"


static void *r3dw_table;                /* the dispatch table, made once */
/* RISC3D's current context in the calling thread (each thread has its
   own: RISC3D client 0.12, and the GPU library) */
#define r3dw_current (thr()->active && thr()->active->r3d ? thr()->active->r3d : NULL)
static void (*r3dw_glFinish)(void);
static void (*r3dw_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                 const void *);
static void (*r3dw_glPixelStorei)(GLenum, GLint);

static void (*r3dw_find(const char *name))(void)
{
    const struct r3d_gl_function *f;
    for (f = r3d_functions(); f->name; f++)
        if (!strcmp(f->name, name))
            return f->fn;
    return NULL;
}

/* A frame's pixels into a surface: r3d gives &BBGGRR words, top row first,
   w * h of them; the surface may have longer rows (screen memory) or the
   other colour order. */
static int r3dw_frame_into(egl_surface *s, int overlapped)
{
    static unsigned int *tmp;
    static size_t tmp_words;
    size_t words = (size_t) s->w * s->h, bytes = words * 4;
    unsigned int *dst = (unsigned int *) s->pixels;
    int x, y, r;

    if (!s->pixels || s->w <= 0 || s->h <= 0)
        return 0;
    if (r3d_in_place(s->pixels))        /* drawn there already: wait for it */
        return r3d_swap(s->pixels, bytes);
    if (s->stride == s->w && s->cfg->layout == LAYOUT_TBGR)
        return overlapped ? r3d_swap_overlapped(dst, bytes) : r3d_swap(dst, bytes);
    if (tmp_words < words) {
        unsigned int *n = (unsigned int *) realloc(tmp, bytes);
        if (!n)
            return -1;
        tmp = n;
        tmp_words = words;
    }
    r = overlapped ? r3d_swap_overlapped(tmp, bytes) : r3d_swap(tmp, bytes);
    if (r < 0)
        return r;
    for (y = 0; y < s->h; y++) {
        const unsigned int *src = tmp + (size_t) y * s->w;
        unsigned int *row = dst + (size_t) y * s->stride;
        if (s->cfg->layout == LAYOUT_TBGR)
            memcpy(row, src, (size_t) s->w * 4);
        else
            for (x = 0; x < s->w; x++) {
                unsigned int p = src[x];
                row[x] = (p & 0xFF00FF00u) | ((p & 0xFF) << 16) | ((p >> 16) & 0xFF);
            }
    }
    return r;
}

/* the current RISC3D context's draw surface, if not a window: its pixels
   brought up to date */
static void r3dw_copy_back(void)
{
    egl_context *c = thr()->active;
    if (c && c->r3d && c->draw && c->draw->kind != SURF_WINDOW)
        r3dw_frame_into(c->draw, 0);
}

static void APIENTRY r3dw_finish(void)
{
    if (r3dw_glFinish)
        r3dw_glFinish();
    r3dw_copy_back();
}

/* GL_OES_EGL_image on a RISC3D context: an image over the program's
   memory (EGL_RISCOS_image_memory) is the GPU library's own, used in
   place; a sprite's pixels are copied now */
static void APIENTRY r3dw_image_target(GLenum target, void *image)
{
    OSMesaImage m;
    void *gpu = gpu_image_lookup(image);
    if (gpu) {
        r3d_image_target(0, target, gpu);
        return;
    }
    if (target != GL_TEXTURE_2D || !r3dw_glTexImage2D || !image_lookup(image, &m))
        return;
    if (m.format == OSMESA_BGRA) {
        /* GL_BGRA isn't in OpenGL ES 2: swap to R,G,B,x first */
        unsigned int *rgba = (unsigned int *) malloc((size_t) m.width * m.height * 4);
        int x, y;
        if (!rgba)
            return;
        for (y = 0; y < m.height; y++) {
            const unsigned int *src = (const unsigned int *) ((const char *) m.pixels + (size_t) y * m.row_bytes);
            for (x = 0; x < m.width; x++) {
                unsigned int p = src[x];
                rgba[(size_t) y * m.width + x] = (p & 0xFF00FF00u) | ((p & 0xFF) << 16) | ((p >> 16) & 0xFF);
            }
        }
        r3dw_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        r3dw_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m.width, m.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        free(rgba);
    } else {
        /* rows a multiple of 4 bytes: one image, rows m.row_bytes apart */
        if (r3dw_glPixelStorei && m.row_bytes == m.width * 4)
            r3dw_glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (m.row_bytes == m.width * 4) {
            r3dw_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m.width, m.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                              m.pixels);
        } else {
            unsigned int *rgba = (unsigned int *) malloc((size_t) m.width * m.height * 4);
            int y;
            if (!rgba)
                return;
            for (y = 0; y < m.height; y++)
                memcpy(rgba + (size_t) y * m.width, (const char *) m.pixels + (size_t) y * m.row_bytes,
                       (size_t) m.width * 4);
            r3dw_glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m.width, m.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                              rgba);
            free(rgba);
        }
    }
}

/* glEGLImageTargetRenderbufferStorageOES on a RISC3D context: images over
   the program's memory only (the GPU draws straight into it); with a
   sprite's image it does nothing */
static void APIENTRY r3dw_image_storage(GLenum target, void *image)
{
    void *gpu = gpu_image_lookup(image);
    if (gpu)
        r3d_image_target(1, target, gpu);
}

/* glGetString on a RISC3D context: RISC3D's answer, but GL_EXTENSIONS
   always lists GL_OES_EGL_image, whose function EGL gives
   (r3dw_image_target) whatever the renderer has */
static const GLubyte *(*r3dw_glGetString)(GLenum);
static const GLubyte *APIENTRY r3dw_get_string(GLenum name)
{
    static const GLubyte *from;
    static char *with;
    const GLubyte *e = r3dw_glGetString ? r3dw_glGetString(name) : NULL;
    const char *p;
    size_t n;

    if (name != GL_EXTENSIONS || !e)
        return e;
    for (p = (const char *) e; (p = strstr(p, "GL_OES_EGL_image")) != NULL; p += 16)
        if ((p == (const char *) e || p[-1] == ' ') && (p[16] == ' ' || p[16] == 0))
            return e;                   /* listed already */
    if (e != from || !with) {
        n = strlen((const char *) e);
        free(with);
        with = (char *) malloc(n + 18);
        if (!with)
            return e;
        memcpy(with, e, n);
        strcpy(with + n, n && e[n - 1] != ' ' ? " GL_OES_EGL_image" : "GL_OES_EGL_image");
        from = e;
    }
    return (const GLubyte *) with;
}

/* RISC3D's functions, with EGL's own in front where they differ */
static void *r3dw_dispatch(void)
{
    static const OSMesaDispatchEntry own[] = {
        { "glFinish", (void (*)(void)) r3dw_finish },
        { "glEGLImageTargetTexture2DOES", (void (*)(void)) r3dw_image_target },
        { "glEGLImageTargetRenderbufferStorageOES", (void (*)(void)) r3dw_image_storage },
        { "glGetString", (void (*)(void)) r3dw_get_string },
    };
    OSMesaDispatchEntry *list;
    const struct r3d_gl_function *fns;
    int n, i;

    if (r3dw_table)
        return r3dw_table;
    r3dw_glFinish = r3dw_find("glFinish");
    r3dw_glTexImage2D = (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                  const void *)) r3dw_find("glTexImage2D");
    r3dw_glPixelStorei = (void (*)(GLenum, GLint)) r3dw_find("glPixelStorei");
    r3dw_glGetString = (const GLubyte *(*)(GLenum)) r3dw_find("glGetString");
    fns = r3d_functions();
    for (n = 0; fns[n].name; n++)
        ;
    list = (OSMesaDispatchEntry *) malloc((n + sizeof own / sizeof own[0]) * sizeof *list);
    if (!list)
        return NULL;
    /* later entries win: RISC3D's first, then EGL's */
    for (i = 0; i < n; i++) {
        list[i].name = fns[i].name;
        list[i].fn = fns[i].fn;
    }
    for (i = 0; i < (int) (sizeof own / sizeof own[0]); i++)
        list[n + i] = own[i];
    r3dw_table = OSMesaMakeDispatchRISCOS(list, n + (int) (sizeof own / sizeof own[0]));
    free(list);
    return r3dw_table;
}

/* eglCreateContext: a RISC3D context for this request, or NULL (OSMesa) */
static struct r3d_context *r3dw_create(EGLenum api, const context_request *q, const egl_context *share)
{
    struct r3d_context *r = NULL;

    if (q->software || (share && !share->r3d))
        return NULL;                    /* (OSMesa contexts share with OSMesa ones) */
    if (share) {
        /* sharing with a RISC3D context: the GPU library's contexts can
           (r3d_can_share), the command stream's can't (eglCreateContext
           says EGL_BAD_MATCH before this) */
        if (api == EGL_OPENGL_ES_API)
            r = r3d_create_shared(q->es == 1 ? R3D_API_GLES1 : R3D_API_GLES2,
                                  q->es == 3 ? q->major : 0, q->es == 3 ? q->minor : 0, share->r3d);
        else
            r = r3d_create_shared(R3D_API_GL, q->explicit_version && q->major >= 3 ? q->major : 0,
                                  q->explicit_version && q->major >= 3 ? q->minor : 0, share->r3d);
    } else if (api == EGL_OPENGL_ES_API) {
        if (riscos_mesa_pick_renderer(api, q->major, q->minor) != RISCOS_MESA_RISC3D)
            return NULL;
        if (q->es == 3)
            r = r3d_create_version(R3D_API_GLES2, q->major, q->minor);     /* RISC3D 0.5 */
        else if (q->es == 2)
            r = r3d_create();           /* the highest it has, ES 3.1 on a Pi 4, as Mesa's EGL */
        else if (q->es == 1)
            r = r3d_create_es1();       /* RISC3D 0.4 (rol-3d 0.4): else NULL, OSMesa */
    } else if (!q->es) {
        /* core profiles are 3.2 and later, so the version says it all */
        if (riscos_mesa_pick_renderer(api, q->explicit_version ? q->major : 0,
                                      q->explicit_version ? q->minor : 0) != RISCOS_MESA_RISC3D)
            return NULL;
        if (q->explicit_version && q->major >= 3)
            r = r3d_create_version(R3D_API_GL, q->major, q->minor);        /* RISC3D 0.5 */
        else
            r = r3d_create_gl();        /* the highest it has, compatibility profile */
    }
    if (r && !r3dw_dispatch()) {
        r3d_destroy(r);
        r = NULL;
    }
    return r;
}

/* EGL$Overlapped, read when a context is made: 1 for overlapped swaps.
   With OS_ReadVarVal, not getenv (see ovl_user_setting). */
static int r3dw_want_overlapped(void)
{
    char v[4];
    _kernel_swi_regs r;
    r.r[0] = (int) "EGL$Overlapped";
    r.r[1] = (int) v;
    r.r[2] = (int) sizeof v - 1;
    r.r[3] = 0;
    r.r[4] = 3;
    if (_kernel_swi(OS_ReadVarVal, &r, &r) != NULL || r.r[2] < 0 || r.r[2] >= (int) sizeof v)
        return 0;
    v[r.r[2]] = 0;
    return strcmp(v, "1") == 0;
}

/* EGL_VERSION: says when RISC3D draws OpenGL and GLES contexts here */
static const char *r3dw_version(void)
{
    return riscos_mesa_pick_renderer(EGL_OPENGL_API, 0, 0) == RISCOS_MESA_RISC3D
           ? "1.4 riscos-mesa (OSMesa; RISC3D for OpenGL and OpenGL ES)" : EGL_RISCOS_VERSION;
}

/* EGL_OPENGL_ES3_BIT in configs' EGL_RENDERABLE_TYPE: only when RISC3D
   gives ES 3.0, which is known by making a context (once). */
static int r3dw_es3(void)
{
    static int known = -1;
    struct r3d_context *r;
    if (riscos_mesa_pick_renderer(EGL_OPENGL_ES_API, 3, 0) != RISCOS_MESA_RISC3D)
        return 0;
    if (known < 0) {
        r = r3d_create_version(R3D_API_GLES2, 3, 0);
        known = r != NULL;
        if (r)
            r3d_destroy(r);
    }
    return known;
}

/* With the GPU library in the program (natively, through V3DRender 0.11),
   a window surface's frames are drawn straight into its own memory, as
   Mesa 20.3.5 draws them: the sprite EGL plots, screen memory (direct),
   or each screen bank in turn. Nothing is read back: eglSwapBuffers only
   waits for the frame. Where it can't (not 4 KB aligned, rows not a
   multiple of 16 bytes, 0x00RRGGBB layout, an older GPU module), frames
   are read back as before. */
/* ------------------------------------------------------------------ */
/* The GPU's present (V3DRender 0.31: DRM_IOCTL_RISCOS_PRESENT)        */
/*                                                                     */
/* A visible-area window surface drawn by the GPU library is drawn in  */
/* the GPU's own tiles (faster to draw into than rows, and           */
/* glCopyTexSubImage2D from it stays on the GPU), and shown by the GPU */
/* copying it into the screen: the rectangles of the Wimp's update or  */
/* redraw loop, collected as plot_loop goes and copied when it ends.  */
/* To the Wimp it's an ordinary redraw, so menus and other windows     */
/* stay over it. The copy is waited for before the loop's caller goes  */
/* on: once the program polls the Wimp, a menu may be drawn there. If  */
/* the present is refused, the frame is read back and plotted as       */
/* before, and from the next frame the surface is drawn into its      */
/* sprite again (until the screen mode changes). EGL$Present 0: never. */

static void r3dw_place(egl_surface *s);

/* one key for a screen mode, as far as the present cares (never 0) */
static int r3dw_mode_key(const screen_info *scr)
{
    return ((scr->width & 0x3FFF) << 18 | (scr->height & 0x3FFF) << 4 | (scr->log2bpp & 15)) + 1;
}

static int r3dw_present_setting(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("EGL$Present");
        on = !(v && (v[0] == '0' || v[0] == 'n' || v[0] == 'N' ||
                     ((v[0] == 'o' || v[0] == 'O') && (v[1] == 'f' || v[1] == 'F'))));
    }
    return on;
}

/* s (the active RISC3D context's draw surface) can be shown by the GPU's
   present: a visible-area window surface shown as it is, with no other
   way of showing it, on a screen the module takes */
static int r3dw_present_wanted(egl_surface *s)
{
    screen_info scr;
    int r;
    if (!r3d_in_program() || !r3dw_present_setting())
        return 0;
    if (s->kind != SURF_WINDOW || s->handle <= 0 || s->fixed || s->dmx || s->banks ||
        s->direct || is_scaled(s) || s->ever_locked || s->ovl_want == 1 || s->ovl_shown ||
        s->r3d_ovl == R3O_ON || s->cfg->layout != LAYOUT_TBGR || !s->sprite)
        return 0;
    read_screen(&scr);
    if (s->r3d_pres_no == r3dw_mode_key(&scr))
        return 0;                       /* refused on this screen before */
    r = r3d_present(NULL, 0);
    if (r != 0 && r != -EBUSY)
        s->r3d_pres_no = r3dw_mode_key(&scr);   /* not asked again until the mode changes */
    return r == 0;
}

/* the rectangles collected for the present: screen pixels (6 ints each,
   as r3d_present takes them), and in OS units for a plot instead */
#define R3DW_PRESENT_RECTS 128
static struct {
    egl_surface *s;
    int handle, wa_dx, wa_dy;           /* the window; screen = work area + wa_d */
    int n;
    int px[R3DW_PRESENT_RECTS * 6];
    os_rect os[R3DW_PRESENT_RECTS];
} r3dw_pres;

/* plot_loop: one rectangle (OS units, screen) of s, which the GPU copies
   when the loop ends (block: the Wimp's) */
static void r3dw_collect(egl_surface *s, const int *block, const screen_info *scr, const os_rect *clip)
{
    int *p, top = scr->height << scr->yeig;
    if (clip->x0 >= clip->x1 || clip->y0 >= clip->y1)
        return;
    if (r3dw_pres.n && (r3dw_pres.s != s || r3dw_pres.n == R3DW_PRESENT_RECTS))
        r3dw_present_collected(&display);
    r3dw_pres.s = s;
    r3dw_pres.handle = block[0];
    r3dw_pres.wa_dx = block[1] - block[5];
    r3dw_pres.wa_dy = block[4] - block[6];
    p = r3dw_pres.px + r3dw_pres.n * 6;
    p[0] = clip->x0 >> scr->xeig;
    p[1] = (top - clip->y1) >> scr->yeig;
    p[2] = (clip->x1 - clip->x0) >> scr->xeig;
    p[3] = (clip->y1 - clip->y0) >> scr->yeig;
    p[4] = (clip->x0 - block[1]) >> scr->xeig;     /* from the visible area's top left */
    p[5] = (block[4] - clip->y1) >> scr->yeig;
    r3dw_pres.os[r3dw_pres.n++] = *clip;
}

/* the rectangles collected: copied by the GPU, or (refused, or the
   surface's context isn't current here) its frame plotted there */
static void r3dw_present_collected(egl_display *d)
{
    egl_context *c = thr()->active;
    egl_surface *s = r3dw_pres.s;
    os_rect all;
    int n = r3dw_pres.n, i, r, block[11];
    _kernel_swi_regs regs;

    if (!n)
        return;
    r3dw_pres.n = 0;
    r = c && c->r3d && c->draw == s ? r3d_present(r3dw_pres.px, n) : -ENOENT;
    if (r == 0)
        return;
    if (r != -EBUSY && r != -ENOENT) {
        screen_info scr;
        read_screen(&scr);
        s->r3d_pres_no = r3dw_mode_key(&scr);
        s->r3d_pres_drop = 1;           /* plotted from the next frame (api.c's swap) */
    }
    /* plot it instead: the frame read back into its sprite, if its
       context is here (otherwise the sprite as it is), then the same
       area updated as plot_loop does it */
    if (r != -ENOENT)
        r3dw_frame_into(s, 0);
    all = r3dw_pres.os[0];
    for (i = 1; i < n; i++) {
        if (r3dw_pres.os[i].x0 < all.x0) all.x0 = r3dw_pres.os[i].x0;
        if (r3dw_pres.os[i].y0 < all.y0) all.y0 = r3dw_pres.os[i].y0;
        if (r3dw_pres.os[i].x1 > all.x1) all.x1 = r3dw_pres.os[i].x1;
        if (r3dw_pres.os[i].y1 > all.y1) all.y1 = r3dw_pres.os[i].y1;
    }
    block[0] = r3dw_pres.handle;
    block[1] = all.x0 - r3dw_pres.wa_dx;
    block[2] = all.y0 - r3dw_pres.wa_dy;
    block[3] = all.x1 - r3dw_pres.wa_dx;
    block[4] = all.y1 - r3dw_pres.wa_dy;
    regs.r[1] = (int) block;
    s->r3d_pres = 0;                    /* (for plot_loop: plot it) */
    if (_kernel_swi(Wimp_UpdateWindow, &regs, &regs) == NULL)
        plot_loop(d, block[0], s, block, regs.r[0]);
    s->r3d_pres = 1;
}

/* eglSwapBuffers of a desktop window surface (the GPU's present, or the
   CPU's plot): timed as a full screen plot is (its swap interval from the
   last one, starting just after a vsync), so the copy, quicker than the
   display's scan, doesn't tear. Not for DispmanX's windows (libbcm_host
   polls the Wimp itself), screen banks or the screen itself (timed in
   present_fullscreen), or a surface on a hardware overlay (timed by its
   buffer switch). As through an overlay, the wait is up to one vsync, and
   eglSwapWouldWaitRISCOS says when it would be longer than SWAP_SOON_US,
   so a program (SDL's driver) can wait in Wimp_PollIdle instead and keep
   the desktop running (docs/DECISIONS.md). */
static int r3dw_window_timed(const egl_surface *s)
{
    return s->kind == SURF_WINDOW && s->handle > 0 && !s->dmx && !s->banks && !s->direct &&
           !s->ovl_shown && s->swap_interval > 0;
}

static void r3dw_present_vsync(egl_surface *s)
{
    if (!r3dw_window_timed(s))
        return;
    if (s->swap_interval > 0) {
        int n = plot_vsyncs_needed(s);
        if (n > 0) {
            wait_vsyncs(n);
            note_vsync();
        }
        s->plot_vsync = vsync_counter();
    }
}

/* the end of a swap: a surface whose present was refused goes back to
   being drawn into its sprite; on another screen mode, it's asked again */
static void r3dw_present_end_swap(egl_surface *s, const screen_info *scr)
{
    if (s->r3d_pres_drop) {
        s->r3d_pres_drop = 0;
        s->r3d_pres = 0;
        r3dw_place(s);
    } else if (!s->r3d_pres && s->r3d_pres_no && s->r3d_pres_no != r3dw_mode_key(scr)) {
        r3dw_place(s);
    }
}

static void r3dw_place(egl_surface *s)
{
    if (!r3d_in_program())
        return;
    if (s && r3dw_present_wanted(s) && r3d_draw_tiled(1, s->w, s->h)) {
        s->r3d_pres = 1;
        return;
    }
    if (s)
        s->r3d_pres = 0;
    if (s && s->kind == SURF_WINDOW && s->pixels && s->cfg->layout == LAYOUT_TBGR &&
        s->r3d_ovl != R3O_ON && r3d_draw_into(s->pixels, s->stride * 4, s->w, s->h))
        return;
    r3d_draw_into(NULL, 0, s ? s->w : 1, s ? s->h : 1);
}

/* s's memory is about to go or move: the GPU stops drawing into it */
static void r3dw_release_pixels(egl_surface *s)
{
    egl_context *c = thr()->active;
    if (c && c->r3d && s->pixels && r3d_in_place(s->pixels))
        r3d_draw_into(NULL, 0, s->w, s->h);
}

/* make c (a RISC3D context) the one drawing, after OSMesa's is released */
static int r3dw_activate(egl_context *c)
{
    egl_surface *s = c->draw;
    r3d_make_current(c->r3d);
    OSMesaSetDispatchRISCOS(r3dw_table);
    r3d_surface(s ? s->w : 1, s ? s->h : 1);
    r3dw_place(s);
    return 1;
}

/* a RISC3D context stops drawing */
static void r3dw_deactivate(void)
{
    r3d_make_current(NULL);
    OSMesaSetDispatchRISCOS(NULL);
}

/* ------------------------------------------------------------------ */
/* Zero-copy window surfaces (RISC3D 0.2: r3d_overlay)                 */
/*                                                                     */
/* Where the renderer can put a context's frames on a display plane   */
/* (RISC OS on Linux: rol-fb's overlays), a visible-area window       */
/* surface's frames go there instead of being read back and plotted.  */
/* The picture is under the desktop and shows where the desktop's     */
/* pixels are the key colour, so EGL paints the window's visible area */
/* in it (and repaints it in redraws), and anything over the window   */
/* clips the picture with nothing more to do. Only in a 16 million    */
/* colour mode (the key must be exact); not with a swap interval of 0 */
/* (the plane is paced by the display), EGL$Overlapped, a program     */
/* that set EGL_OVERLAY_RISCOS to EGL_FALSE, or EGL$Overlay off.      */


#ifndef ColourTrans_SetGCOL
#define ColourTrans_SetGCOL 0x40743
#endif
#ifndef OS_Plot
#define OS_Plot 0x45
#endif


static int r3dw_overlay_wanted(const egl_context *c, const egl_surface *s, const screen_info *scr)
{
    return s->kind == SURF_WINDOW && s->handle > 0 && !s->fixed && !s->dmx &&
           scr->log2bpp == 5 && s->swap_interval != 0 && !c->r3d_overlapped &&
           s->ovl_want != 0 && ovl_user_setting() >= 0;
}

/* Paint one rectangle of a redraw or update loop (block as Wimp gives it)
   in the surface's key colour: its visible area, clipped by the Wimp */
static void r3dw_paint_key(const egl_surface *s, const int *block)
{
    _kernel_swi_regs r;
    r.r[0] = (int) (s->r3d_key << 8);           /* &BBGGRR00 */
    r.r[3] = 0;
    r.r[4] = 0;
    _kernel_swi(ColourTrans_SetGCOL, &r, &r);
    r.r[0] = 4;                                 /* move */
    r.r[1] = block[1];
    r.r[2] = block[2];
    _kernel_swi(OS_Plot, &r, &r);
    r.r[0] = 96 + 5;                            /* rectangle fill, absolute */
    r.r[1] = block[3] - 1;
    r.r[2] = block[4] - 1;
    _kernel_swi(OS_Plot, &r, &r);
}

/* Paint the whole visible area of the surface's window in the key */
static void r3dw_paint_window(egl_surface *s, const window_state *ws)
{
    _kernel_swi_regs r;
    int block[11];
    block[0] = s->handle;
    block[1] = ws->scroll_x;                    /* work area coordinates */
    block[2] = ws->scroll_y - (ws->y1 - ws->y0);
    block[3] = ws->scroll_x + (ws->x1 - ws->x0);
    block[4] = ws->scroll_y;
    r.r[1] = (int) block;
    if (_kernel_swi(Wimp_UpdateWindow, &r, &r) != NULL)
        return;
    while (r.r[0]) {
        r3dw_paint_key(s, block);
        r.r[1] = (int) block;
        if (_kernel_swi(Wimp_GetRectangle, &r, &r) != NULL)
            break;
    }
}

/* Stop showing a surface on the plane: hide it there (as the context that
   placed it) and repaint the window from the surface's own pixels */
static void r3dw_overlay_off(egl_surface *s, int failed)
{
    unsigned key;
    if (s->r3d_ovl == R3O_ON && s->r3d_ovl_ctx) {
        if (s->r3d_ovl_ctx != r3dw_current)
            r3d_make_current(s->r3d_ovl_ctx);
        r3d_overlay(0, 0, 0, 0, &key);
        if (s->r3d_ovl_ctx != r3dw_current)
            r3d_make_current(r3dw_current);
    }
    s->r3d_ovl = failed ? R3O_FAILED : R3O_OFF;
    s->r3d_ovl_ctx = NULL;
}

/* eglSwapBuffers of a window surface on a RISC3D context: put the frame on
   the display plane. 1: done (nothing to plot); 0: not this way (read the
   frame back and plot it as usual); -1: as 0, and the whole window must be
   replotted (it showed the plane until now). */
static int r3dw_overlay_frame(egl_display *d, egl_context *c, egl_surface *s, const screen_info *scr)
{
    window_state ws;
    unsigned key;
    int x, y, w, h, was = s->r3d_ovl == R3O_ON;
    egl_surface *o;

    if (!r3dw_overlay_wanted(c, s, scr) || !get_window_state(s->handle, &ws) ||
        !(ws.flags & (1 << 16))) {
        if (was) {
            r3dw_overlay_off(s, 0);
            return -1;
        }
        s->r3d_ovl = R3O_OFF;
        return 0;
    }
    x = ws.x0 >> scr->xeig;
    y = ((scr->height << scr->yeig) - ws.y1) >> scr->yeig;
    w = (ws.x1 - ws.x0) >> scr->xeig;
    h = (ws.y1 - ws.y0) >> scr->yeig;
    if (s->r3d_ovl == R3O_FAILED && x == s->r3d_place[0] && y == s->r3d_place[1] &&
        w == s->r3d_place[2] && h == s->r3d_place[3] && scr->width == s->r3d_place[4])
        return 0;                       /* said no here before */
    if (!was || s->r3d_ovl_ctx != c->r3d || x != s->r3d_place[0] || y != s->r3d_place[1] ||
        w != s->r3d_place[2] || h != s->r3d_place[3]) {
        s->r3d_place[0] = x; s->r3d_place[1] = y; s->r3d_place[2] = w; s->r3d_place[3] = h;
        s->r3d_place[4] = scr->width;
        if (w <= 0 || h <= 0 || r3d_overlay(x, y, w, h, &key) != 1) {
            if (was)
                r3dw_overlay_off(s, 1);
            s->r3d_ovl = R3O_FAILED;
            return was ? -1 : 0;
        }
        /* one plane per context: a surface it showed before goes back to
           being plotted (its pixels are a frame old: repaint it all) */
        for (o = d->surfaces; o; o = o->next)
            if (o != s && o->r3d_ovl == R3O_ON && o->r3d_ovl_ctx == c->r3d) {
                o->r3d_ovl = R3O_OFF;
                o->r3d_ovl_ctx = NULL;
                if (o->kind == SURF_WINDOW)
                    present(d, o, scr, NULL, 0);
            }
        s->r3d_ovl = R3O_ON;
        s->r3d_ovl_ctx = c->r3d;
        s->r3d_key = key;
        r3dw_paint_window(s, &ws);
    }
    if (r3d_swap_overlay() < 0) {
        r3dw_overlay_off(s, 1);         /* stopped (a mode change, say) */
        return -1;
    }
    return 1;
}

/* eglDestroySurface / eglDestroyContext: nothing left on the plane */
static void r3dw_surface_gone(egl_surface *s)
{
    if (s->r3d_ovl == R3O_ON)
        r3dw_overlay_off(s, 0);
}

static void r3dw_context_gone(egl_display *d, struct r3d_context *r)
{
    egl_surface *s;
    for (s = d->surfaces; s; s = s->next)
        if (s->r3d_ovl_ctx == r) {
            s->r3d_ovl = R3O_OFF;       /* r3d_destroy takes the plane away */
            s->r3d_ovl_ctx = NULL;
        }
}
