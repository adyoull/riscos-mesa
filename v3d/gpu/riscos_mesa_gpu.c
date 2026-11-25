/*
 * riscos_mesa_gpu.c - the GPU library's one exported name (see
 * v3d/include/riscos_mesa_gpu.h): the frontend (v3d/frontend/v3dm.c) in
 * the shape libOSMesa's RISC3D switch expects.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <stddef.h>
#include <errno.h>

#include "riscos_mesa_gpu.h"
#include "v3dm.h"

/* gpu_functions.c (made by the build from the library's gl* entry points) */
extern const struct riscos_mesa_gpu_function riscos_mesa_gpu_functions[];

static const char *device(void)
{
    return v3dm_device_name();
}

static void *create_format(int api, int major, int minor, void *share, int colour_bits)
{
    int a = api == 1 ? V3DM_API_GL : api == 2 ? V3DM_API_GLES1 : V3DM_API_GLES2;
    if (major <= 0) {                   /* the highest the GPU gives */
        major = a == V3DM_API_GLES1 ? 1 : a == V3DM_API_GLES2 ? 2 : 1;
        minor = a == V3DM_API_GLES1 ? 1 : 0;
    }
    return v3dm_create_format(a, major, minor, 1, (v3dm_context *) share, colour_bits == 16 ? 16 : 32);
}

static void *create_shared(int api, int major, int minor, void *share)
{
    return create_format(api, major, minor, share, 32);
}

static void *create(int api, int major, int minor)
{
    return create_shared(api, major, minor, NULL);
}

static void destroy(void *context)
{
    v3dm_destroy((v3dm_context *) context);
}

static int make_current(void *context, int width, int height)
{
    if (width <= 0 || height <= 0)
        width = height = 1;
    return v3dm_make_current((v3dm_context *) context, (unsigned) width, (unsigned) height);
}

static int read_frame(void *context, void *pixels, int row_bytes, int top_first)
{
    return v3dm_read((v3dm_context *) context, pixels, row_bytes, top_first);
}

static int draw_into(void *context, void *pixels, int row_bytes, int width, int height)
{
    if (width <= 0 || height <= 0)
        return 0;
    return v3dm_draw_into((v3dm_context *) context, pixels, row_bytes, (unsigned) width, (unsigned) height);
}

static int finish(void *context)
{
    return v3dm_finish((v3dm_context *) context);
}

static void *image_create(void *pixels, int row_bytes, int width, int height, unsigned fourcc)
{
    if (width <= 0 || height <= 0)
        return NULL;
    return v3dm_image_create(pixels, row_bytes, (unsigned) width, (unsigned) height, fourcc);
}

static void image_destroy(void *image)
{
    v3dm_image_destroy((v3dm_image *) image);
}

/* (Mesa's glapi; these two aren't among the library's static gl entry
   points, so they are found by name, through the current dispatch) */
void *_mesa_glapi_get_proc_address(const char *name);

static void image_target(int renderbuffer, unsigned target, void *image)
{
    static void (*texture)(unsigned, void *), (*storage)(unsigned, void *);
    void (*fn)(unsigned, void *);
    if (!texture) {
        storage = (void (*)(unsigned, void *)) _mesa_glapi_get_proc_address("glEGLImageTargetRenderbufferStorageOES");
        texture = (void (*)(unsigned, void *)) _mesa_glapi_get_proc_address("glEGLImageTargetTexture2DOES");
    }
    fn = renderbuffer ? storage : texture;
    if (fn)
        fn(target, image);
}

static int draw_tiled(void *context, int on, int width, int height)
{
    if (width <= 0 || height <= 0)
        width = height = 1;
    return v3dm_draw_tiled((v3dm_context *) context, on, (unsigned) width, (unsigned) height);
}

static int present(void *context, const int *rects, int n)
{
    return n < 0 ? -EINVAL : v3dm_present((v3dm_context *) context, rects, (unsigned) n);
}

static int present_scaled(void *context, const int *rects, int n, int width, int height)
{
    if (n < 0 || width < 0 || height < 0)
        return -EINVAL;
    return v3dm_present_scaled((v3dm_context *) context, rects, (unsigned) n,
                               (unsigned) width, (unsigned) height);
}

static void end_frame(void *context)
{
    v3dm_end_frame((v3dm_context *) context);
}

/* glGetError, in riscos_mesa_gpu_functions (tools/gpu-functions.py): a
   buffer the GPU module refused is GL_OUT_OF_MEMORY */
unsigned glGetError(void);
unsigned riscos_mesa_gpu_glGetError(void);
unsigned riscos_mesa_gpu_glGetError(void)
{
    v3dm_report_oom();
    return glGetError();
}

static void (*get_proc(const char *name))(void)
{
    return (void (*)(void)) _mesa_glapi_get_proc_address(name);
}

static const struct riscos_mesa_gpu gpu = {
    RISCOS_MESA_GPU_VERSION, device, create, destroy, make_current, read_frame,
    riscos_mesa_gpu_functions, v3dm_error, draw_into, finish, create_shared,
    image_create, image_destroy, image_target, draw_tiled, present, get_proc, create_format,
    present_scaled, end_frame,
};

__attribute__((visibility("default")))
const struct riscos_mesa_gpu *riscos_mesa_gpu(void)
{
    return &gpu;
}
