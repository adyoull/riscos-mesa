/*
 * v3dm.h - riscos-mesa's frontend for Mesa's v3d driver on native RISC OS
 * (through RISC3D). PROTOTYPE (v3d branch): the interface may change before
 * it goes behind riscos-mesa's EGL.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#ifndef V3DM_H
#define V3DM_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct v3dm_context v3dm_context;

#define V3DM_API_GL    0          /* OpenGL, compatibility profile */
#define V3DM_API_GLES1 1
#define V3DM_API_GLES2 2          /* OpenGL ES 2.0 and later */

/* Opens the GPU through RISC3D and starts Mesa's driver on it: 1, or 0 (see
   v3dm_error). v3dm_create does it when needed. */
int v3dm_init(void);
void v3dm_shutdown(void);
const char *v3dm_error(void);
/* the DRM driver name of the GPU RISC3D lists, if this build has a Mesa
   driver for it; else NULL */
const char *v3dm_device_name(void);
/* the GPU module's DRM driver name, as text */
const char *v3dm_device(void);
/* the driver's name for the GPU, once started */
const char *v3dm_renderer(void);

/* a context; depth: with a 24-bit depth and 8-bit stencil buffer */
v3dm_context *v3dm_create(int api, int major, int minor, int depth);
/* makes c current, drawing into an off-screen RGBA frame of this size
   (made again if the size changes); NULL: none current */
int v3dm_make_current(v3dm_context *c, unsigned width, unsigned height);
void v3dm_destroy(v3dm_context *c);
/* c's frame (c current), finished, as R,G,B,A bytes: rows row_bytes
   apart, the top row first or (top_first 0) last. 0, or -1 */
int v3dm_read(v3dm_context *c, void *pixels, int row_bytes, int top_first);
/* the formats of c's frame, as text */
const char *v3dm_formats(v3dm_context *c);

#ifdef __cplusplus
}
#endif
#endif
