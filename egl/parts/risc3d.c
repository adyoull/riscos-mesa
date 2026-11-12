/*
 * egl/parts/risc3d.c - contexts drawn by RISC3D, a hardware path, instead
 * of OSMesa.
 *
 * Where the RISC3D module has a driver registered (on RISC OS on Linux:
 * Linux's graphics driver) and RISC3D$Disable isn't set, eglCreateContext
 * gives an OpenGL (up to 2.1, compatibility) or OpenGL ES 2 context to
 * RISC3D; anywhere else, or if RISC3D can't make one, OSMesa draws as
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
 *   the texture following the sprite (zero copy) should set RISC3D$Disable.
 * - Not given to RISC3D: OpenGL ES 1 and 3, OpenGL 3 and core profiles,
 *   and contexts sharing objects with another context (each RISC3D context
 *   is its own; sharing with a RISC3D context is EGL_BAD_MATCH).
 * - One thread draws: RISC3D's current context is global.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */

#include "r3d.h"

/* from extensions.c, included later */
static GLboolean image_lookup(void *image, OSMesaImage *desc);

static void *r3dw_table;                /* the dispatch table, made once */
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

/* RISC3D's functions, with EGL's own in front where they differ */
static void *r3dw_dispatch(void)
{
    static const OSMesaDispatchEntry own[] = {
        { "glFinish", (void (*)(void)) r3dw_finish },
        { "glEGLImageTargetTexture2DOES", (void (*)(void)) r3dw_image_target },
    };
    OSMesaDispatchEntry *list;
    int n, i;

    if (r3dw_table)
        return r3dw_table;
    r3dw_glFinish = r3dw_find("glFinish");
    r3dw_glTexImage2D = (void (*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum,
                                  const void *)) r3dw_find("glTexImage2D");
    r3dw_glPixelStorei = (void (*)(GLenum, GLint)) r3dw_find("glPixelStorei");
    for (n = 0; r3d_gl_functions[n].name; n++)
        ;
    list = (OSMesaDispatchEntry *) malloc((n + 2) * sizeof *list);
    if (!list)
        return NULL;
    /* later entries win: RISC3D's first, then EGL's */
    for (i = 0; i < n; i++) {
        list[i].name = r3d_gl_functions[i].name;
        list[i].fn = r3d_gl_functions[i].fn;
    }
    list[n] = own[0];
    list[n + 1] = own[1];
    r3dw_table = OSMesaMakeDispatchRISCOS(list, n + 2);
    free(list);
    return r3dw_table;
}

/* eglCreateContext: a RISC3D context for this request, or NULL (OSMesa) */
static struct r3d_context *r3dw_create(EGLenum api, const context_request *q, const egl_context *share)
{
    struct r3d_context *r = NULL;

    if (share || !r3d_available())
        return NULL;
    if (api == EGL_OPENGL_ES_API) {
        if (q->es == 2)
            r = r3d_create();
    } else if (!q->es) {
        int core = q->explicit_profile && q->profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
                   (q->major > 3 || (q->major == 3 && q->minor >= 2));
        int beyond = q->explicit_version && (q->major > 2 || (q->major == 2 && q->minor > 1));
        if (!core && !beyond)
            r = r3d_create_gl();
    }
    if (r && !r3dw_dispatch()) {
        r3d_destroy(r);
        r = NULL;
    }
    return r;
}

/* EGL$Overlapped, read when a context is made */
static int r3dw_want_overlapped(void)
{
    const char *v = getenv("EGL$Overlapped");
    return v && v[0] == '1';
}

/* make c (a RISC3D context) the one drawing, after OSMesa's is released */
static int r3dw_activate(egl_context *c)
{
    egl_surface *s = c->draw;
    r3d_make_current(c->r3d);
    OSMesaSetDispatchRISCOS(r3dw_table);
    r3d_surface(s ? s->w : 1, s ? s->h : 1);
    return 1;
}

/* a RISC3D context stops drawing */
static void r3dw_deactivate(void)
{
    r3d_make_current(NULL);
    OSMesaSetDispatchRISCOS(NULL);
}
