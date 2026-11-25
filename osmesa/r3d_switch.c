/*
 * osmesa/r3d_switch.c - RISC3D's client API (risc3d/lib/r3d.h) for EGL
 * and OSMesa's routing, whichever kind of driver RISC3D has.
 *
 * RISC3D is the interface GPU drivers register with. Two kinds:
 * - A command-stream driver (Linux3D, on RISC OS on Linux): RISC3D's own
 *   client (risc3d/lib/r3d_gl.c, compiled with r3d_stream_names.h under
 *   r3ds_ names) writes the stream and Linux's Mesa draws.
 * - A DRM device (a GPU module: V3DRender on the Pi 4, natively): it
 *   takes no command stream, so the program runs Mesa's driver for it
 *   itself: the GPU library (libMesaGPU, v3d/include/riscos_mesa_gpu.h),
 *   Mesa 26.x with every symbol hidden, which reaches the module only
 *   through RISC3D (RISC3D_Info, RISC3D_Open, the ioctl SWI RISC3D names).
 * Which one is decided once, at the first call: the GPU library when it
 * is linked in (a weak reference: static programs link it if they want
 * it; the shared libOSMesa needs it) and RISC3D's driver is a DRM device
 * it has a Mesa driver for; else the command stream. RISC3D$Disable
 * turns both off. EGL (egl/parts/risc3d.c) and OSMesa's routing
 * (risc3d_route.c) call the r3d_ functions below either way.
 *
 * With the GPU library, each context draws into an off-screen frame the
 * size r3d_surface or r3d_target gives. r3d_swap reads the finished frame
 * back (&BBGGRR words, the top row first), as the command stream's does;
 * there are no display planes (r3d_overlay says no) and no overlapped
 * swaps (r3d_swap_overlapped is r3d_swap). r3d_target's buffer gets the
 * frame at glFinish and glFlush and at r3d_target_copy.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */

#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>
#include <GL/gl.h>
#include <GL/osmesa.h>
#include "r3d.h"
#include "r3d_switch.h"
#include "riscos_mesa_gpu.h"

/* ---- RISC3D's client, under other names (r3d_stream_names.h) ---- */
extern const struct r3d_gl_function r3ds_gl_functions[];
int r3ds_available(void);
struct r3d_context *r3ds_create(void);
struct r3d_context *r3ds_create_gl(void);
struct r3d_context *r3ds_create_es1(void);
struct r3d_context *r3ds_create_version(int api, int major, int minor);
const char *r3ds_target(struct r3d_context *, void *buffer, int width, int height, int format,
                        int row_length, int y_up);
int r3ds_driver_flags(void);
void r3ds_uploads(int as_memory);
int r3ds_target_copy(struct r3d_context *);
void r3ds_make_current(struct r3d_context *);
void r3ds_destroy(struct r3d_context *);
const char *r3ds_open(void);
const char *r3ds_open_gl(void);
const char *r3ds_open_es1(void);
void r3ds_close(void);
void r3ds_surface(int width, int height);
int r3ds_swap(void *pixels, size_t bytes);
int r3ds_swap_overlapped(void *pixels, size_t bytes);
int r3ds_overlay(int x, int y, int width, int height, unsigned *key);
int r3ds_swap_overlay(void);
void r3ds_flush(void);
int r3ds_stats(unsigned long long v[4]);
const char *r3ds_error(void);
struct r3d_context *r3ds_create_shared(int api, int major, int minor, struct r3d_context *share);
int r3ds_can_share(void);

/* ---- the GPU library, if linked in ---- */
extern const struct riscos_mesa_gpu *riscos_mesa_gpu(void) __attribute__((weak));

/* A system variable set to anything but 0, false or no (as pick.c's) */
static int var_set(const char *name)
{
    char v[8];
    int i;
    _kernel_swi_regs r;
    r.r[0] = (int) name;
    r.r[1] = (int) v;
    r.r[2] = (int) sizeof v - 1;
    r.r[3] = 0;
    r.r[4] = 3;
    if (_kernel_swi(OS_ReadVarVal, &r, &r) != NULL || r.r[2] <= 0 || r.r[2] >= (int) sizeof v)
        return r.r[2] >= (int) sizeof v;
    v[r.r[2]] = 0;
    for (i = 0; v[i]; i++)
        if (v[i] >= 'A' && v[i] <= 'Z')
            v[i] += 'a' - 'A';
    return strcmp(v, "0") && strcmp(v, "false") && strcmp(v, "no");
}

static const struct riscos_mesa_gpu *gpu;       /* non-NULL: the GPU library draws */

static void decide(void)
{
    static int decided;
    const struct riscos_mesa_gpu *g;
    if (decided)
        return;
    decided = 1;
    if (!riscos_mesa_gpu || var_set("RISC3D$Disable"))
        return;
    g = riscos_mesa_gpu();
    if (g && g->version >= 1 && g->device())
        gpu = g;
}

int r3d_in_program(void)
{
    decide();
    return gpu != NULL;
}

/* ---- the GPU library's contexts ---- */

struct r3d_context {
    void *c;                    /* the GPU library's */
    int w, h;                   /* the frame's size */
    int sized;                  /* r3d_surface has given it a size (its viewport set to fit) */
    void *into;                 /* r3d_draw_into's memory, when the GPU draws there */
    void *buffer;               /* r3d_target's, or NULL */
    int format, row_length, y_up;
    unsigned char *rgba;        /* a frame read back, for r3d_target's buffer */
    size_t rgba_bytes;
};

/* Each thread's current context (with the GPU library several threads
   can draw at once, each with its own). EGL's calls are
   serialised by its lock; GL calls (glFinish, glFlush below) aren't. */
static pthread_once_t cur_once = PTHREAD_ONCE_INIT;
static pthread_key_t cur_key;

static void cur_make_key(void)
{
    pthread_key_create(&cur_key, NULL);
}

static struct r3d_context *cur_get(void)
{
    pthread_once(&cur_once, cur_make_key);
    return (struct r3d_context *) pthread_getspecific(cur_key);
}

static void cur_set(struct r3d_context *r)
{
    pthread_once(&cur_once, cur_make_key);
    pthread_setspecific(cur_key, r);
}

#define cur (cur_get())

static struct r3d_context *only;

static struct r3d_context *gpu_create_format(int api, int major, int minor, struct r3d_context *share,
                                             int colour_bits)
{
    struct r3d_context *r = (struct r3d_context *) calloc(1, sizeof *r);
    if (!r)
        return NULL;
    if (gpu->version >= 7)
        r->c = gpu->create_format(api, major, minor, share ? share->c : NULL, colour_bits);
    else if (share)
        r->c = gpu->version >= 3 ? gpu->create_shared(api, major, minor, share->c) : NULL;
    else
        r->c = gpu->create(api, major, minor);
    if (!r->c) {
        free(r);
        return NULL;
    }
    r->w = r->h = 1;
    return r;
}

static struct r3d_context *gpu_create_shared(int api, int major, int minor, struct r3d_context *share)
{
    return gpu_create_format(api, major, minor, share, 32);
}

static struct r3d_context *gpu_create(int api, int major, int minor)
{
    return gpu_create_shared(api, major, minor, NULL);
}

/* the frame into r3d_target's buffer, in its format, row length and order */
static int copy_to_target(struct r3d_context *r)
{
    size_t need;
    int x, y, stride;
    if (!r || !r->buffer)
        return 0;
    if (r->format == OSMESA_RGBA)       /* straight into the buffer: its layout is the frame's */
        return gpu->read(r->c, r->buffer, (r->row_length > 0 ? r->row_length : r->w) * 4, !r->y_up)
               ? -1 : 0;
    need = (size_t) r->w * r->h * 4;
    if (r->rgba_bytes < need) {
        unsigned char *n = (unsigned char *) realloc(r->rgba, need);
        if (!n)
            return -1;
        r->rgba = n;
        r->rgba_bytes = need;
    }
    if (gpu->read(r->c, r->rgba, r->w * 4, !r->y_up))   /* the buffer's row order */
        return -1;
    stride = r->row_length > 0 ? r->row_length : r->w;
    for (y = 0; y < r->h; y++) {
        const unsigned char *s = r->rgba + (size_t) y * r->w * 4;
        switch (r->format) {
        case OSMESA_RGBA:
            memcpy((unsigned char *) r->buffer + (size_t) y * stride * 4, s, (size_t) r->w * 4);
            break;
        case OSMESA_BGRA: case OSMESA_ARGB: {
            unsigned char *d = (unsigned char *) r->buffer + (size_t) y * stride * 4;
            for (x = 0; x < r->w; x++, s += 4, d += 4) {
                if (r->format == OSMESA_BGRA) {
                    d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];
                } else {
                    d[0] = s[3]; d[1] = s[0]; d[2] = s[1]; d[3] = s[2];
                }
            }
            break;
        }
        case OSMESA_RGB: case OSMESA_BGR: {
            unsigned char *d = (unsigned char *) r->buffer + (size_t) y * stride * 3;
            int bgr = r->format == OSMESA_BGR;
            for (x = 0; x < r->w; x++, s += 4, d += 3) {
                d[0] = s[bgr ? 2 : 0]; d[1] = s[1]; d[2] = s[bgr ? 0 : 2];
            }
            break;
        }
        case OSMESA_RGB_565: {
            unsigned short *d = (unsigned short *) r->buffer + (size_t) y * stride;
            for (x = 0; x < r->w; x++, s += 4)
                d[x] = (unsigned short) (((s[0] >> 3) << 11) | ((s[1] >> 2) << 5) | (s[2] >> 3));
            break;
        }
        default:
            return -1;
        }
    }
    return 0;
}

/* glFinish and glFlush: the frame into r3d_target's buffer too */
static void (*gpu_glFinish)(void), (*gpu_glFlush)(void);

static void APIENTRY switch_glFinish(void)
{
    gpu_glFinish();
    copy_to_target(cur);
}

static void APIENTRY switch_glFlush(void)
{
    gpu_glFlush();
    copy_to_target(cur);
}

/* Mesa 20.3.5's dispatch (the software renderer's, which EGL's and
   OSMesa's dispatch tables are made from), for every GL name it has */
unsigned int _glapi_get_dispatch_table_size(void);
const char *_glapi_get_proc_name(unsigned int offset);

static int gpu_function_cmp(const void *key, const void *entry)
{
    return strcmp((const char *) key, ((const struct riscos_mesa_gpu_function *) entry)->name);
}

const struct r3d_gl_function *r3d_functions(void)
{
    static struct r3d_gl_function *list;
    const struct riscos_mesa_gpu_function *f;
    unsigned int slots = 0, off;
    int n, total;
    decide();
    if (!gpu)
        return r3ds_gl_functions;
    if (list)
        return list;
    for (n = 0; gpu->functions[n].name; n++)
        ;
    /* (version 6) room for the GPU library's entry points that aren't in
       its list, found by name below */
    if (gpu->version >= 6)
        slots = _glapi_get_dispatch_table_size();
    list = (struct r3d_gl_function *) calloc((size_t) n + slots + 1, sizeof *list);
    if (!list) {
        static const struct r3d_gl_function none[1] = { { NULL, NULL } };
        return none;
    }
    for (n = 0, f = gpu->functions; f->name; f++, n++) {
        list[n].name = f->name;
        list[n].fn = f->fn;
        if (!strcmp(f->name, "glFinish")) {
            gpu_glFinish = f->fn;
            list[n].fn = (void (*)(void)) switch_glFinish;
        } else if (!strcmp(f->name, "glFlush")) {
            gpu_glFlush = f->fn;
            list[n].fn = (void (*)(void)) switch_glFlush;
        }
    }
    if (!gpu_glFinish || !gpu_glFlush) {        /* can't be: the library has both */
        free(list);
        list = NULL;
        return r3ds_gl_functions;
    }
    /* Its list has only the functions Mesa 26 has entry points for by
       name; extension functions (glActiveStencilFaceEXT, ...) are only
       had through GetProcAddress. Without them, a GPU context's call went
       to the software renderer's function and did nothing (David Sharp:
       GL_EXT_stencil_two_side's back face never set). So every name Mesa
       20.3.5's dispatch has that isn't in the list is asked for. (The
       list is sorted by name: tools/gpu-functions.py.) */
    total = n;
    for (off = 0; off < slots; off++) {
        const char *name = _glapi_get_proc_name(off);
        void (*fn)(void);
        if (!name || strncmp(name, "gl", 2) ||
            bsearch(name, gpu->functions, (size_t) n, sizeof *gpu->functions, gpu_function_cmp))
            continue;
        fn = gpu->get_proc(name);
        if (fn) {
            list[total].name = name;
            list[total].fn = fn;
            total++;
        }
    }
    return list;
}

/* ---- the client's API ---- */

int r3d_available(void)
{
    decide();
    return gpu ? 1 : r3ds_available();
}

int r3d_driver_flags(void)
{
    decide();
    return gpu ? R3D_DRIVER_DRM : r3ds_driver_flags();
}

struct r3d_context *r3d_create_version(int api, int major, int minor)
{
    decide();
    return gpu ? gpu_create(api, major, minor) : r3ds_create_version(api, major, minor);
}

struct r3d_context *r3d_create_shared(int api, int major, int minor, struct r3d_context *share)
{
    decide();
    if (!share)
        return r3d_create_version(api, major, minor);
    /* the command stream: RISC3D client 0.12, with RISC3D 0.06 on and (on
       RISC OS on Linux) Linux3D 0.06 and rol-3d 0.20 on */
    return gpu ? gpu_create_shared(api, major, minor, share) : r3ds_create_shared(api, major, minor, share);
}

/* As r3d_create_shared (share NULL: none), the colour buffer 16 bits
   (RGB565) or 32. Where that can't be (the command stream, a GPU library
   before version 7) the context is 32 bits: its frame is read back as
   R,G,B,A all the same. */
struct r3d_context *r3d_create_format(int api, int major, int minor, struct r3d_context *share,
                                      int colour_bits)
{
    decide();
    if (gpu)
        return gpu_create_format(api, major, minor, share, colour_bits);
    return share ? r3ds_create_shared(api, major, minor, share) : r3ds_create_version(api, major, minor);
}

int r3d_can_share(void)
{
    decide();
    return gpu ? gpu->version >= 3 : r3ds_can_share();
}

/* images over the program's memory: the GPU library only (version 4) */
int r3d_can_image(void)
{
    decide();
    return gpu && gpu->version >= 4;
}

void *r3d_image_create(void *pixels, int row_bytes, int width, int height, unsigned fourcc)
{
    return r3d_can_image() ? gpu->image_create(pixels, row_bytes, width, height, fourcc) : NULL;
}

void r3d_image_destroy(void *image)
{
    if (image && r3d_can_image())
        gpu->image_destroy(image);
}

void r3d_image_target(int renderbuffer, unsigned target, void *image)
{
    if (r3d_can_image() && cur)
        gpu->image_target(renderbuffer, target, image);
}

/* frames in tiles, shown by the GPU's present: the GPU library only
   (version 5) */
int r3d_draw_tiled(int on, int width, int height)
{
    decide();
    if (!gpu || gpu->version < 5 || !cur)
        return 0;
    cur->into = NULL;
    if (width > 0 && height > 0) {
        cur->w = width;
        cur->h = height;
    }
    return gpu->draw_tiled(cur->c, on, cur->w, cur->h);
}

int r3d_present(const int *rects, int n)
{
    decide();
    if (!gpu || gpu->version < 5)
        return -ENOTTY;
    if (!cur)
        return -EINVAL;
    return gpu->present(cur->c, rects, n);
}

void r3d_end_frame(void)
{
    decide();
    if (gpu && gpu->version >= 8 && cur)
        gpu->end_frame(cur->c);
}

int r3d_present_scaled(const int *rects, int n, int width, int height)
{
    decide();
    if (!width && !height)
        return r3d_present(rects, n);
    if (!gpu || gpu->version < 8)
        return -ENOTTY;
    if (!cur)
        return -EINVAL;
    return gpu->present_scaled(cur->c, rects, n, width, height);
}

struct r3d_context *r3d_create(void)
{
    decide();
    return gpu ? gpu_create(R3D_API_GLES2, 0, 0) : r3ds_create();
}

struct r3d_context *r3d_create_gl(void)
{
    decide();
    return gpu ? gpu_create(R3D_API_GL, 0, 0) : r3ds_create_gl();
}

struct r3d_context *r3d_create_es1(void)
{
    decide();
    return gpu ? gpu_create(R3D_API_GLES1, 0, 0) : r3ds_create_es1();
}

void r3d_make_current(struct r3d_context *r)
{
    decide();
    if (!gpu) {
        r3ds_make_current(r);
        return;
    }
    if (cur && cur != r && cur->into && gpu->version >= 2)
        gpu->finish(cur->c);            /* its frame is complete in the caller's memory */
    cur_set(r);
    if (r)
        gpu->make_current(r->c, r->w, r->h);
    else
        gpu->make_current(NULL, 0, 0);
}

/* a gl function of the GPU library's, by name (NULL if it hasn't it) */
static void (*gpu_gl(const char *name))(void)
{
    const struct riscos_mesa_gpu_function *f;
    for (f = gpu->functions; f->name; f++)
        if (!strcmp(f->name, name))
            return f->fn;
    return NULL;
}

void r3d_surface(int width, int height)
{
    decide();
    if (!gpu) {
        r3ds_surface(width, height);
        return;
    }
    if (!cur || width <= 0 || height <= 0)
        return;
    if (cur->w != width || cur->h != height) {
        cur->w = width;
        cur->h = height;
        gpu->make_current(cur->c, width, height);
    }
    /* The first surface sets the viewport and scissor box to fit, as the
       command stream's client does (and as GL does when a context is
       first made current). Mesa set them at the context's first
       make_current, at the 1x1 it had before it had a surface, so a
       program that never calls glViewport drew into one pixel. */
    if (!cur->sized && (width > 1 || height > 1)) {    /* (1x1: EGL's "no surface") */
        static void (*viewport)(int, int, int, int), (*scissor)(int, int, int, int);
        cur->sized = 1;
        if (!viewport) {
            viewport = (void (*)(int, int, int, int)) gpu_gl("glViewport");
            scissor = (void (*)(int, int, int, int)) gpu_gl("glScissor");
        }
        if (viewport && scissor) {
            viewport(0, 0, width, height);
            scissor(0, 0, width, height);
        }
    }
}

void r3d_destroy(struct r3d_context *r)
{
    decide();
    if (!gpu) {
        r3ds_destroy(r);
        return;
    }
    if (!r)
        return;
    if (cur == r) {
        gpu->make_current(NULL, 0, 0);
        cur_set(NULL);
    }
    gpu->destroy(r->c);
    free(r->rgba);
    free(r);
}

int r3d_draw_into(void *pixels, int row_bytes, int width, int height)
{
    decide();
    if (!gpu || gpu->version < 2 || !cur)
        return 0;
    cur->into = NULL;
    if (gpu->draw_into(cur->c, pixels, row_bytes, width, height))
        cur->into = pixels;
    if (width > 0 && height > 0) {
        cur->w = width;
        cur->h = height;
    }
    return cur->into != NULL;
}

int r3d_in_place(const void *pixels)
{
    decide();
    return gpu && cur && cur->into && cur->into == pixels;
}

int r3d_swap(void *pixels, size_t bytes)
{
    size_t need;
    decide();
    if (!gpu)
        return r3ds_swap(pixels, bytes);
    if (!cur)
        return -1;
    need = (size_t) cur->w * cur->h * 4;
    if (cur->into && cur->into == pixels)       /* already there: only wait for it */
        return gpu->finish(cur->c) ? -1 : (int) need;
    if (bytes < need || gpu->read(cur->c, pixels, cur->w * 4, 1))
        return -1;
    return (int) need;
}

int r3d_swap_overlapped(void *pixels, size_t bytes)
{
    decide();
    return gpu ? r3d_swap(pixels, bytes) : r3ds_swap_overlapped(pixels, bytes);
}

int r3d_overlay(int x, int y, int width, int height, unsigned *key)
{
    decide();
    return gpu ? (cur ? 0 : -1) : r3ds_overlay(x, y, width, height, key);
}

int r3d_swap_overlay(void)
{
    decide();
    return gpu ? -1 : r3ds_swap_overlay();
}

const char *r3d_target(struct r3d_context *r, void *buffer, int width, int height, int format,
                       int row_length, int y_up)
{
    decide();
    if (!gpu)
        return r3ds_target(r, buffer, width, height, format, row_length, y_up);
    if (!r)
        return "no context";
    switch (format) {
    case OSMESA_RGBA: case OSMESA_BGRA: case OSMESA_ARGB: case OSMESA_RGB: case OSMESA_BGR:
    case OSMESA_RGB_565:
        break;
    default:
        return "the GPU can't draw into a buffer of this format";
    }
    r->buffer = buffer;
    if (!buffer)
        return NULL;
    r->format = format;
    r->row_length = row_length;
    r->y_up = y_up;
    if (r->w != width || r->h != height) {
        r->w = width;
        r->h = height;
        if (cur == r)
            gpu->make_current(r->c, width, height);
    }
    return NULL;
}

int r3d_target_copy(struct r3d_context *r)
{
    decide();
    if (!gpu)
        return r3ds_target_copy(r);
    return r == cur ? copy_to_target(r) : 0;
}

void r3d_uploads(int as_memory)
{
    decide();
    if (!gpu)
        r3ds_uploads(as_memory);
}

void r3d_flush(void)
{
    decide();
    if (!gpu)
        r3ds_flush();
    else if (cur) {
        r3d_functions();
        if (gpu_glFlush)
            gpu_glFlush();
    }
}

int r3d_stats(unsigned long long v[4])
{
    decide();
    return gpu ? -1 : r3ds_stats(v);
}

const char *r3d_error(void)
{
    decide();
    return gpu ? gpu->error() : r3ds_error();
}

/* the simple form: one context, made current */
static const char *open_one(int api)
{
    if (!only)
        only = gpu_create(api, 0, 0);
    if (!only)
        return gpu->error();
    r3d_make_current(only);
    return NULL;
}

const char *r3d_open(void)
{
    decide();
    return gpu ? open_one(R3D_API_GLES2) : r3ds_open();
}

const char *r3d_open_gl(void)
{
    decide();
    return gpu ? open_one(R3D_API_GL) : r3ds_open_gl();
}

const char *r3d_open_es1(void)
{
    decide();
    return gpu ? open_one(R3D_API_GLES1) : r3ds_open_es1();
}

void r3d_close(void)
{
    decide();
    if (!gpu) {
        r3ds_close();
        return;
    }
    r3d_destroy(only);
    only = NULL;
}
