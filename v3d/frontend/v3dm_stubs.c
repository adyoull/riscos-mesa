/*
 * v3dm_stubs.c - what Mesa's v3d driver refers to but riscos-mesa's
 * frontend never uses on RISC OS. renderonly (a separate display device
 * importing the GPU's buffers, as on Linux with KMS) is only reached with
 * the renderonly object v3dm.c doesn't give, and Mesa leaves its code out
 * of libgallium where the system has no KMS.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <stddef.h>

struct renderonly;
struct renderonly_scanout;
struct pipe_resource;
struct winsys_handle;

void renderonly_scanout_destroy(struct renderonly_scanout *scanout, struct renderonly *ro);
struct renderonly_scanout *renderonly_create_gpu_import_for_resource(struct pipe_resource *rsc,
                                                                     struct renderonly *ro,
                                                                     struct winsys_handle *out_handle);

void renderonly_scanout_destroy(struct renderonly_scanout *scanout, struct renderonly *ro)
{
    (void) scanout; (void) ro;
}

struct renderonly_scanout *renderonly_create_gpu_import_for_resource(struct pipe_resource *rsc,
                                                                     struct renderonly *ro,
                                                                     struct winsys_handle *out_handle)
{
    (void) rsc; (void) ro; (void) out_handle;
    return NULL;
}
