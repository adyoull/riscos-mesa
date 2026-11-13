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
 *   changes to the sprite need the call again. A program that relies on
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

/* from extensions.c, included later */
static GLboolean image_lookup(void *image, OSMesaImage *desc);

static void *r3dw_table;                /* the dispatch table, made once */
static struct r3d_context *r3dw_current;        /* RISC3D's current context (r3dw_activate) */
static void (*r3dw_glFinish)(void);
static void (*r3dw_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                 const void *);
static void (*r3dw_glPixelStorei)(GLenum, GLint);

static void (*r3dw_find(const char *name))(void)
{
    const struct r3d_gl_function *f;
    for (f = r3d_gl_functions; f->name; f++)
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

/* GL_OES_EGL_image on a RISC3D context: the sprite's pixels, copied now */
static void APIENTRY r3dw_image_target(GLenum target, void *image)
{
    OSMesaImage m;
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
        { "glGetString", (void (*)(void)) r3dw_get_string },
    };
    OSMesaDispatchEntry *list;
    int n, i;

    if (r3dw_table)
        return r3dw_table;
    r3dw_glFinish = r3dw_find("glFinish");
    r3dw_glTexImage2D = (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                  const void *)) r3dw_find("glTexImage2D");
    r3dw_glPixelStorei = (void (*)(GLenum, GLint)) r3dw_find("glPixelStorei");
    r3dw_glGetString = (const GLubyte *(*)(GLenum)) r3dw_find("glGetString");
    for (n = 0; r3d_gl_functions[n].name; n++)
        ;
    list = (OSMesaDispatchEntry *) malloc((n + sizeof own / sizeof own[0]) * sizeof *list);
    if (!list)
        return NULL;
    /* later entries win: RISC3D's first, then EGL's */
    for (i = 0; i < n; i++) {
        list[i].name = r3d_gl_functions[i].name;
        list[i].fn = r3d_gl_functions[i].fn;
    }
    for (i = 0; i < (int) (sizeof own / sizeof own[0]); i++)
        list[n + i] = own[i];
    r3dw_table = OSMesaMakeDispatchRISCOS(list, n + (int) (sizeof own / sizeof own[0]));
    free(list);
    return r3dw_table;
}

/* LIBGL_ALWAYS_SOFTWARE (a system variable, as Mesa's environment
   variable) set to anything but 0, false or no: always OSMesa.
   With OS_ReadVarVal, not getenv (see ovl_user_setting). */
static int r3dw_always_software(void)
{
    char v[8];
    int i;
    _kernel_swi_regs r;
    r.r[0] = (int) "LIBGL_ALWAYS_SOFTWARE";
    r.r[1] = (int) v;
    r.r[2] = (int) sizeof v - 1;
    r.r[3] = 0;
    r.r[4] = 3;
    if (_kernel_swi(OS_ReadVarVal, &r, &r) != NULL || r.r[2] <= 0 || r.r[2] >= (int) sizeof v)
        return r.r[2] >= (int) sizeof v;    /* too long to be "0": set */
    v[r.r[2]] = 0;
    for (i = 0; v[i]; i++)
        if (v[i] >= 'A' && v[i] <= 'Z')
            v[i] += 'a' - 'A';
    return strcmp(v, "0") && strcmp(v, "false") && strcmp(v, "no");
}

/* riscos-mesa's one choice of renderer (eglext_riscos.h), for EGL, SDL
   and later OSMesa: api EGL_OPENGL_API or EGL_OPENGL_ES_API, the version
   asked for (0.0: none, GL 2.1 or GLES 1). In order:
   1. LIBGL_ALWAYS_SOFTWARE or RISC3D$Disable set: software.
   2. RISC3D has a driver and draws this version (GL up to 2.1, GLES 1.1
      and 2.0): RISC3D. (GLES 1.1 needs RISC3D 0.4's rol-3d too: if it
      can't make one, EGL uses OSMesa all the same.)
   3. (Reserved: a GPU driven in the program, RISCOS_MESA_V3D.)
   4. Software: OSMesa, which refuses what it can't do, as before. */
EGLAPI int EGLAPIENTRY riscos_mesa_pick_renderer(unsigned int api, int major, int minor)
{
    int ok;
    if (r3dw_always_software() || !r3d_available())
        return RISCOS_MESA_SOFTWARE;
    if (api == EGL_OPENGL_ES_API)
        ok = major <= 1 || (major == 2 && minor == 0);
    else if (api == EGL_OPENGL_API)
        ok = major < 2 || (major == 2 && minor <= 1);
    else
        ok = 0;
    return ok ? RISCOS_MESA_RISC3D : RISCOS_MESA_SOFTWARE;
}

/* eglCreateContext: a RISC3D context for this request, or NULL (OSMesa) */
static struct r3d_context *r3dw_create(EGLenum api, const context_request *q, const egl_context *share)
{
    struct r3d_context *r = NULL;

    if (share || q->software)
        return NULL;
    if (api == EGL_OPENGL_ES_API) {
        if (riscos_mesa_pick_renderer(api, q->es, 0) != RISCOS_MESA_RISC3D)
            return NULL;
        if (q->es == 2)
            r = r3d_create();
        else if (q->es == 1)
            r = r3d_create_es1();       /* RISC3D 0.4 (rol-3d 0.4): else NULL, OSMesa */
    } else if (!q->es) {
        /* core profiles are 3.2 and later, so the version says it all */
        if (riscos_mesa_pick_renderer(api, q->explicit_version ? q->major : 0,
                                      q->explicit_version ? q->minor : 0) != RISCOS_MESA_RISC3D)
            return NULL;
        r = r3d_create_gl();
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
           ? "1.4 riscos-mesa (OSMesa; RISC3D for GL 2.1, GLES 1.1 and GLES 2)" : EGL_RISCOS_VERSION;
}

/* make c (a RISC3D context) the one drawing, after OSMesa's is released */
static int r3dw_activate(egl_context *c)
{
    egl_surface *s = c->draw;
    r3d_make_current(c->r3d);
    r3dw_current = c->r3d;
    OSMesaSetDispatchRISCOS(r3dw_table);
    r3d_surface(s ? s->w : 1, s ? s->h : 1);
    return 1;
}

/* a RISC3D context stops drawing */
static void r3dw_deactivate(void)
{
    r3d_make_current(NULL);
    r3dw_current = NULL;
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
