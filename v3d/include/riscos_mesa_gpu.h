/*
 * riscos_mesa_gpu.h - the GPU library (Mesa 26.x with its GPU drivers,
 * v3d/gpu) as riscos-mesa's libOSMesa sees it.
 *
 * On native RISC OS the GPU's driver module (V3DRender on the Pi 4)
 * registers with RISC3D as a DRM device, which takes no command stream:
 * the program runs Mesa's driver for it itself. This library is that
 * Mesa, with every symbol hidden but riscos_mesa_gpu(), so that it lives
 * beside Mesa 20.3.5 (the software renderer) in one program. libOSMesa's
 * RISC3D switch (osmesa/r3d_switch.c) gives its contexts to EGL and to
 * OSMesa's routing in the shape of RISC3D's client, as RISC3D's own
 * client does on RISC OS on Linux. Nothing else uses this header.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#ifndef RISCOS_MESA_GPU_H
#define RISCOS_MESA_GPU_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RISCOS_MESA_GPU_VERSION 8

/* a gl function by its GL name, as RISC3D's client lists its own */
struct riscos_mesa_gpu_function { const char *name; void (*fn)(void); };

struct riscos_mesa_gpu {
    int version;                        /* RISCOS_MESA_GPU_VERSION */
    /* the DRM driver name of the GPU RISC3D lists ("v3d") if this library
       has a Mesa driver for it, else NULL */
    const char *(*device)(void);
    /* a context: api as RISC3D's (R3D_API_GLES2 0, R3D_API_GL 1,
       R3D_API_GLES1 2); major 0: the highest the GPU gives (OpenGL:
       compatibility profile). NULL: see error() */
    void *(*create)(int api, int major, int minor);
    void (*destroy)(void *context);
    /* drawing into an off-screen frame of this size (made again when the
       size changes); context NULL: none current. 1, or 0 */
    int (*make_current)(void *context, int width, int height);
    /* the current frame, finished, as R,G,B,A bytes (&BBGGRR words),
       rows row_bytes apart, the top row first or (top_first 0) last.
       0, or -1 */
    int (*read)(void *context, void *pixels, int row_bytes, int top_first);
    /* the gl entry points, ending with a NULL name: they draw with the
       current context */
    const struct riscos_mesa_gpu_function *functions;
    const char *(*error)(void);
    /* (version 2) the context, made current at this size, draws straight
       into the caller's memory (R,G,B,A bytes, rows row_bytes apart, the
       top first; 4 KB aligned with whole pages to its end, rows a
       multiple of 16 bytes): 1, or 0 if it can't and draws into its own
       frame as before. pixels NULL: its own frame. The memory must stay
       put until then, or until destroy. */
    int (*draw_into)(void *context, void *pixels, int row_bytes, int width, int height);
    /* (version 2) the frame is finished. 0, or -1 */
    int (*finish)(void *context);
    /* (version 3) create, sharing share's objects (textures, buffers,
       renderbuffers, framebuffers, programs...): the same api. Contexts
       may be current in different threads at once: each thread's current
       context is its own (Mesa's glapi keeps one per thread once a second
       thread makes one current). NULL: see error() */
    void *(*create_shared)(int api, int major, int minor, void *share);
    /* (version 4) an image over the caller's memory (EGL_RISCOS_image_memory):
       rows row_bytes apart (a multiple of 16), the first GL's bottom row,
       4 KB aligned with whole pages to its end; fourcc a DRM format
       (ARGB8888: B,G,R,A bytes; XRGB8888; ABGR8888: R,G,B,A; XBGR8888).
       It can be used in any of the library's contexts. NULL: see error() */
    void *(*image_create)(void *pixels, int row_bytes, int width, int height, unsigned fourcc);
    void (*image_destroy)(void *image);
    /* (version 4) glEGLImageTargetTexture2DOES (renderbuffer 0) or
       glEGLImageTargetRenderbufferStorageOES (1) with such an image, in the
       current context */
    void (*image_target)(int renderbuffer, unsigned target, void *image);
    /* (version 5) on: the context, made current at this size, draws into
       a frame of its own in the GPU's tiles, to be shown with present
       (read still reads it); off: its own frame in rows, as before.
       1, or 0 */
    int (*draw_tiled)(void *context, int on, int width, int height);
    /* (version 5, V3DRender 0.31) the context's current frame copied by
       the GPU into the screen, into n rectangles of 6 ints (dst_x, dst_y,
       width, height, src_x, src_y: screen pixels from the screen's top
       left, and the offset in the frame, top row first), returning once
       it is stored. n 0: only whether it would be taken. 0, or -errno:
       ENOTTY not offered, EINVAL not this screen or frame, EBUSY not just
       now (plot it this time) */
    int (*present)(void *context, const int *rects, int n);
    /* (version 6) any gl entry point by its GL name, as Mesa's own
       GetProcAddress gives it, including the extension functions that
       aren't in functions (glActiveStencilFaceEXT, say); NULL if Mesa 26
       doesn't know the name. libOSMesa fills its list from this. */
    void (*(*get_proc)(const char *name))(void);
    /* (version 7) create_shared (share NULL: none) with a colour buffer of
       32 bits (RGBA8) or 16 (RGB565: EGL's 5:6:5 configs). read still
       gives R,G,B,A bytes. NULL: see error() */
    void *(*create_format)(int api, int major, int minor, void *share, int colour_bits);
    /* (version 8) present, of the frame scaled by the GPU to width x
       height first (the size it's shown at: the rectangles' source
       offsets are in that), when that isn't the frame's own size; 0 x 0:
       as present. Nearest when both are whole multiples, else bilinear */
    int (*present_scaled)(void *context, const int *rects, int n, int width, int height);
    /* (version 8) the end of the context's frame (eglSwapBuffers, before
       it's shown or read): its depth and stencil, undefined after a swap,
       aren't stored */
    void (*end_frame)(void *context);
};

/* the library's one exported name */
const struct riscos_mesa_gpu *riscos_mesa_gpu(void);

#ifdef __cplusplus
}
#endif
#endif
