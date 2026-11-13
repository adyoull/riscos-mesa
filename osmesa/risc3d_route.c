/*
 * osmesa/risc3d_route.c - OSMesa contexts drawn by RISC3D, a hardware
 * path, where it has a driver: programs that draw with OSMesa directly
 * (OSMesaCreateContext, OSMesaMakeCurrent on their own buffer) use the
 * GPU too, unchanged. In libOSMesa on RISC OS; Mesa patch
 * riscos-osmesa-risc3d calls these hooks (osmesa_riscos.h).
 *
 * - When OSMesa makes a context, it also asks riscos_mesa_pick_renderer
 *   (osmesa/pick.c). If that says RISC3D, a RISC3D context is made beside
 *   the software one, for the same API: OpenGL (compatibility), GLES 1.1
 *   or GLES 2. The software context stays: it is what draws if RISC3D
 *   can't (below). Not routed: accumulation buffers (RISC3D has none),
 *   colour index, core profiles, and EGL's own contexts
 *   (OSMESA_SOFTWARE_RISCOS).
 * - OSMesaMakeCurrent on a buffer RISC3D can draw into (8 bits a
 *   component, OSMesa's six colour formats, either row order, any row
 *   length) makes the RISC3D context current with r3d_target: the buffer
 *   is its framebuffer 0, and the frame is copied into it at glFinish and
 *   glFlush (when anything was drawn since), when another context or
 *   buffer is made current, and for OSMesaGetColorBuffer. The gl* entry
 *   points go to RISC3D through a dispatch table made from its list of
 *   functions (OSMesaMakeDispatchRISCOS), as EGL does.
 * - Anything else (other component types, r3d_target refusing) draws in
 *   software, as before.
 * - A program that writes into its buffer between frames itself would see
 *   that overwritten at the next copy; LIBGL_ALWAYS_SOFTWARE or
 *   RISC3D$Disable keep it in software.
 * - One thread draws: RISC3D's current context is global.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */

#include <stdlib.h>
#include <string.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include <EGL/egl.h>
#include <EGL/eglext_riscos.h>
#include "r3d.h"

/* the hooks (as in Mesa's osmesa_riscos.h, which isn't installed) */
void osmesa_r3d_create(OSMesaContext o, int profile, int major, int minor, GLenum format, int accum_bits);
void osmesa_r3d_destroy(OSMesaContext o);
int osmesa_r3d_make_current(OSMesaContext o, void *buffer, GLenum type, int width, int height,
                            GLenum format, int row_length, int y_up);
OSMesaContext osmesa_r3d_current(void);
void osmesa_r3d_retarget(OSMesaContext o, int row_length, int y_up);
int osmesa_r3d_size(OSMesaContext o, int *width, int *height);
int osmesa_r3d_color_buffer(OSMesaContext o, int *width, int *height, void **buffer);
int osmesa_r3d_depth_buffer(OSMesaContext o, int *width, int *height, int *bytes_per_value, void **buffer);

struct route {
    OSMesaContext o;
    struct r3d_context *r;
    void *buffer;                       /* r3d_target's, or NULL */
    int width, height, row_length, y_up;
    GLenum format;
    unsigned int *depth;                /* OSMesaGetDepthBuffer's copy */
    struct route *next;
};
static struct route *routes, *cur;
static void *table;                     /* RISC3D's functions behind the gl* entry points */
static int nested;                      /* in our own OSMesaMakeCurrent(NULL) */

static struct route *find(OSMesaContext o)
{
    struct route *t;
    for (t = routes; t; t = t->next)
        if (t->o == o)
            return t;
    return NULL;
}

static void *dispatch(void)
{
    OSMesaDispatchEntry *list;
    int n;
    if (table)
        return table;
    for (n = 0; r3d_gl_functions[n].name; n++)
        ;
    list = (OSMesaDispatchEntry *) malloc((size_t) (n ? n : 1) * sizeof *list);
    if (!list)
        return NULL;
    for (n = 0; r3d_gl_functions[n].name; n++) {
        list[n].name = r3d_gl_functions[n].name;
        list[n].fn = r3d_gl_functions[n].fn;
    }
    table = OSMesaMakeDispatchRISCOS(list, n);
    free(list);
    return table;
}

void osmesa_r3d_create(OSMesaContext o, int profile, int major, int minor, GLenum format, int accum_bits)
{
    struct route *t;
    struct r3d_context *r;
    unsigned int api;

    if (accum_bits > 0 || format == OSMESA_COLOR_INDEX || profile == OSMESA_CORE_PROFILE)
        return;
    if (profile == OSMESA_ES1_PROFILE) {
        api = EGL_OPENGL_ES_API; major = 1; minor = 1;
    } else if (profile == OSMESA_ES2_PROFILE) {
        api = EGL_OPENGL_ES_API; major = 2; minor = 0;
    } else {
        api = EGL_OPENGL_API;           /* the software context took the version */
    }
    if (riscos_mesa_pick_renderer(api, major, minor) != RISCOS_MESA_RISC3D || !dispatch())
        return;
    r = profile == OSMESA_ES1_PROFILE ? r3d_create_es1() :
        profile == OSMESA_ES2_PROFILE ? r3d_create() : r3d_create_gl();
    if (!r)
        return;
    t = (struct route *) calloc(1, sizeof *t);
    if (!t) {
        r3d_destroy(r);
        return;
    }
    t->o = o;
    t->r = r;
    t->next = routes;
    routes = t;
}

/* RISC3D lets go: the frame into the buffer, and nothing of RISC3D's current */
static void release(void)
{
    if (!cur)
        return;
    r3d_target_copy(cur->r);
    OSMesaSetDispatchRISCOS(NULL);
    r3d_make_current(NULL);
    cur = NULL;
}

void osmesa_r3d_destroy(OSMesaContext o)
{
    struct route **p, *t;
    for (p = &routes; (t = *p) != NULL; p = &t->next)
        if (t->o == o) {
            if (cur == t)
                release();
            *p = t->next;
            r3d_destroy(t->r);
            free(t->depth);
            free(t);
            return;
        }
}

static int format_ok(GLenum format, GLenum type)
{
    switch (format) {
    case OSMESA_RGBA: case OSMESA_BGRA: case OSMESA_ARGB: case OSMESA_RGB: case OSMESA_BGR:
        return type == GL_UNSIGNED_BYTE;
    case OSMESA_RGB_565:
        return type == GL_UNSIGNED_SHORT_5_6_5;
    default:
        return 0;
    }
}

int osmesa_r3d_make_current(OSMesaContext o, void *buffer, GLenum type, int width, int height,
                            GLenum format, int row_length, int y_up)
{
    struct route *t;

    if (nested)
        return 0;
    t = o ? find(o) : NULL;
    if (!t || !buffer || !format_ok(format, type)) {
        release();                      /* software from here */
        return 0;
    }
    if (cur && cur != t)
        release();
    if (cur == t)
        r3d_target_copy(t->r);          /* the frame so far into the old buffer */
    /* the software context mustn't stay current under RISC3D's table */
    nested = 1;
    OSMesaMakeCurrent(NULL, NULL, 0, 0, 0);
    nested = 0;
    r3d_make_current(t->r);
    if (r3d_target(t->r, buffer, width, height, (int) format, row_length, y_up)) {
        r3d_make_current(NULL);         /* refused: draw this one in software */
        cur = NULL;
        return 0;
    }
    OSMesaSetDispatchRISCOS(dispatch());
    t->buffer = buffer;
    t->width = width;
    t->height = height;
    t->format = format;
    t->row_length = row_length;
    t->y_up = y_up;
    cur = t;
    return 1;
}

OSMesaContext osmesa_r3d_current(void)
{
    return cur ? cur->o : NULL;
}

/* OSMesaPixelStore on the context RISC3D is drawing */
void osmesa_r3d_retarget(OSMesaContext o, int row_length, int y_up)
{
    struct route *t = find(o);
    if (!t || t != cur || !t->buffer)
        return;
    r3d_target_copy(t->r);
    t->row_length = row_length;
    t->y_up = y_up;
    if (r3d_target(t->r, t->buffer, t->width, t->height, (int) t->format, row_length, y_up)) {
        /* a refusal can't happen for a change of row length or order alone;
           if it does, nothing more is drawn into the buffer */
        t->buffer = NULL;
    }
}

int osmesa_r3d_size(OSMesaContext o, int *width, int *height)
{
    struct route *t = o ? find(o) : NULL;
    if (!t || t != cur)
        return 0;
    *width = t->width;
    *height = t->height;
    return 1;
}

int osmesa_r3d_color_buffer(OSMesaContext o, int *width, int *height, void **buffer)
{
    struct route *t = o ? find(o) : NULL;
    if (!t || !t->buffer)
        return 0;
    r3d_target_copy(t->r);
    *width = t->width;
    *height = t->height;
    *buffer = t->buffer;
    return 1;
}

/* read back when asked for, into a copy kept with the context */
int osmesa_r3d_depth_buffer(OSMesaContext o, int *width, int *height, int *bytes_per_value, void **buffer)
{
    struct route *t = o ? find(o) : NULL;
    const struct r3d_gl_function *f;
    void (*readpixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *) = NULL;

    if (!t || t != cur)
        return 0;
    for (f = r3d_gl_functions; f->name && !readpixels; f++)
        if (!strcmp(f->name, "glReadPixels"))
            readpixels = (void (*)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) f->fn;
    if (!readpixels)
        return 0;
    free(t->depth);
    t->depth = (unsigned int *) malloc((size_t) t->width * t->height * 4);
    if (!t->depth)
        return 0;
    /* 4-byte values: the program's pack alignment can't pad the rows */
    readpixels(0, 0, t->width, t->height, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, t->depth);
    *width = t->width;
    *height = t->height;
    *bytes_per_value = 4;
    *buffer = t->depth;
    return 1;
}
