/*
 * v3dm.c - riscos-mesa's frontend for Mesa's GPU drivers on native RISC
 * OS: a context and an off-screen framebuffer of a given size, as OSMesa
 * gives, but drawn by the GPU. The GPU's RISC OS driver module (V3DRender
 * on the Raspberry Pi 4) is found through RISC3D, and Mesa's driver for it
 * is chosen by the DRM driver name the module registered with RISC3D
 * (only v3d so far).
 *
 * PROTOTYPE (v3d branch). It follows the frontends Mesa carries for
 * systems without DRI (gallium/frontends/hgl, and the OSMesa frontend
 * Mesa removed in 25.1): one pipe screen on the module's file, a
 * pipe_frontend_screen, and for each context a
 * pipe_frontend_drawable whose colour and depth buffers are resources
 * of the GPU. The program reads the frame back with glReadPixels (or
 * v3dm_read), as with OSMesa.
 *
 * Copyright (c) 2026 Andrew Youll. MIT licence.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "frontend/api.h"
#include "state_tracker/st_context.h"
#include "util/u_atomic.h"
#include "util/box.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/simple_mtx.h"
#include "util/xmlconfig.h"
#include "v3d/v3d_screen.h"
#include "broadcom/common/v3d_limits.h"
#include "v3d/v3d_resource.h"
#include "v3d/v3d_bufmgr.h"

#include <errno.h>
#include "xf86drm.h"
#include "drm-uapi/v3d_drm.h"
#include "drm-uapi/drm_fourcc.h"
#include "frontend/winsys_handle.h"
#include "main/errors.h"
#include "drm_riscos.h"
#include "v3dm.h"

struct v3dm_buffer {
    struct pipe_frontend_drawable base;
    struct st_visual visual;
    unsigned width, height;
    struct pipe_resource *textures[ST_ATTACHMENT_COUNT];
    enum st_attachment_type main;       /* the colour buffer drawn into */
    void *requested;                    /* the caller's memory asked for (v3dm_draw_into) */
    void *target;                       /* the caller's memory it is, or NULL */
    int target_row_bytes;
    int tiled;                          /* in the GPU's tiles, for v3dm_present */
};

struct v3dm_context {
    struct st_context *st;
    struct v3dm_buffer *fb;
    struct st_visual visual;
    void *target;                       /* v3dm_draw_into's memory, or NULL */
    int target_row_bytes;
    int tiled;                          /* v3dm_draw_tiled: frames in tiles, shown by v3dm_present */
    /* frames used lately, kept for coming back to: EGL's screen banks
       take turns, each drawn into where it is */
#define V3DM_SPARE_FBS 3
    struct v3dm_buffer *spare[V3DM_SPARE_FBS];
    /* v3dm_present_scaled: the frame scaled by the GPU to the size it's
       shown at, in tiles, presented instead of the frame (NULL: none yet) */
    struct pipe_resource *scaled;
};

static struct {
    int fd;
    struct pipe_screen *screen;
    struct pipe_frontend_screen *fscreen;
    driOptionCache options, options_info;
    int tiled;                  /* V3DM$Tiled: colour buffers in tiles */
    int no_cached;              /* V3DM$Uncached, or a module before V3DRender 0.11 */
    int no_import;              /* V3DM$NoImport, or the module refused an import */
    int no_uncached;            /* a module before V3DRender 0.13 (no DRM_RISCOS_IMPORT_UNCACHED) */
    int trace;                  /* V3DM$Trace: frames made, chosen and finished, on stderr */
    int keep_depth;             /* V3DM$KeepDepth: v3dm_end_frame keeps depth/stencil (to compare) */
    int late_flush;             /* V3DM$LateFlush: v3dm_end_frame doesn't submit the frame (to compare) */
    uint32_t present_sync;      /* syncobj v3dm_present waits on (0: not made yet) */
    char error[160];
} dev;

static uint32_t fb_ids;

const char *v3dm_error(void)
{
    return dev.error;
}

/* ---- the framebuffer ---- */

/* ---- the colour buffer drawn into ----
   With V3DRender 0.11 the GPU draws, in rows, either straight into memory
   the caller has (DRM_IOCTL_RISCOS_IMPORT_MEMORY: an EGL surface's sprite
   or screen memory; no copy at all), or into a BO the CPU sees cached
   (DRM_RISCOS_BO_CPU_CACHED: read back at cached speed). The module
   cleans and invalidates the CPU's caches around the jobs. Older
   modules: an ordinary (uncached) buffer, as before. */

static struct pipe_resource *from_handle(const struct pipe_resource *t, uint32_t handle,
                                         unsigned stride)
{
    struct winsys_handle wh;
    memset(&wh, 0, sizeof wh);
    wh.type = WINSYS_HANDLE_TYPE_KMS;
    wh.handle = handle;
    wh.stride = stride;
    wh.modifier = DRM_FORMAT_MOD_LINEAR;
    wh.format = t->format;
    return dev.screen->resource_from_handle(dev.screen, t, &wh, PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
}

static void close_handle(uint32_t handle)
{
    struct drm_gem_close cl;
    memset(&cl, 0, sizeof cl);
    cl.handle = handle;
    drmIoctl(dev.fd, DRM_IOCTL_GEM_CLOSE, &cl);
}

/* DRM_IOCTL_RISCOS_IMPORT_MEMORY of im's memory; memory the CPU doesn't
   cache (screen memory) marked so (V3DRender 0.13: no cache cleaning and
   invalidating around jobs), or not on an older module */
static uintptr_t missing_page;         /* import_memory refused: the page with none behind it */

static int import_memory(struct drm_riscos_import_memory *im)
{
    im->flags = 0;
    missing_page = 0;
    if (!drm_riscos_memory_present((void *) (uintptr_t) im->address, im->size, &missing_page)) {
        if (dev.trace)
            fprintf(stderr, "v3dm: not mapped: %08x has no page behind it\n", (unsigned) missing_page);
        errno = EFAULT;
        return -1;
    }
    if (!dev.no_uncached && drm_riscos_cpu_uncached((void *) (uintptr_t) im->address, im->size)) {
        im->flags = DRM_RISCOS_IMPORT_UNCACHED;
        if (drmIoctl(dev.fd, DRM_IOCTL_RISCOS_IMPORT_MEMORY, im) == 0)
            return 0;
        if (errno != EINVAL)
            return -1;
        dev.no_uncached = 1;            /* before V3DRender 0.13 */
        im->flags = 0;
    }
    return drmIoctl(dev.fd, DRM_IOCTL_RISCOS_IMPORT_MEMORY, im);
}

static struct pipe_resource *colour_resource(struct v3dm_buffer *b, const struct pipe_resource *t)
{
    struct pipe_resource *r;
    unsigned stride = t->width0 * util_format_get_blocksize(t->format);
    if (b->tiled)
        return dev.screen->resource_create(dev.screen, t);     /* the GPU's own, in tiles */
    if (!dev.tiled && b->target) {
        /* the caller's memory: whole 4 KB pages (the caller pads it), rows
           a multiple of 16 bytes */
        struct drm_riscos_import_memory im;
        memset(&im, 0, sizeof im);
        im.address = (uintptr_t) b->target;
        im.size = (b->target_row_bytes * t->height0 + 4095) & ~4095u;
        if (!dev.no_import && !((uintptr_t) b->target & 4095) && !(b->target_row_bytes & 15) &&
            import_memory(&im) == 0) {
            r = from_handle(t, im.handle, b->target_row_bytes);
            if (r)
                return r;
            close_handle(im.handle);
        }
        b->target = NULL;               /* drawn into a buffer of its own, and copied */
    }
    if (!dev.no_cached) {
        struct drm_v3d_create_bo cb;
        memset(&cb, 0, sizeof cb);
        cb.size = (stride * t->height0 + 4095) & ~4095u;
        cb.flags = DRM_RISCOS_BO_CPU_CACHED;
        if (drmIoctl(dev.fd, DRM_IOCTL_V3D_CREATE_BO, &cb) == 0) {
            r = from_handle(t, cb.handle, stride);
            if (r)
                return r;
            close_handle(cb.handle);
        } else if (errno == EINVAL) {
            dev.no_cached = 1;          /* before V3DRender 0.11 */
        }
    }
    return dev.screen->resource_create(dev.screen, t);
}

/* b->textures[a], made: 1, or 0 if the visual has no such buffer (or it
   couldn't be made: textures[a] stays NULL) */
static int fb_make(struct v3dm_buffer *b, enum st_attachment_type a)
{
    struct pipe_resource t;
    memset(&t, 0, sizeof t);
    t.target = PIPE_TEXTURE_2D;
    t.width0 = b->width;
    t.height0 = b->height;
    t.depth0 = 1;
    t.array_size = 1;
    if (a == ST_ATTACHMENT_DEPTH_STENCIL) {
        t.format = b->visual.depth_stencil_format;
        t.bind = PIPE_BIND_DEPTH_STENCIL;
    } else {
        t.format = b->visual.color_format;
        t.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
        /* in rows (raster), not the GPU's tiles: the frame is read back as
           one sequential copy (or not at all), with no detiling by the CPU
           (V3DM$Tiled: tiles, to compare) */
        if (!dev.tiled && !(a == b->main && b->tiled))
            t.bind |= PIPE_BIND_LINEAR;
    }
    if (t.format == PIPE_FORMAT_NONE)
        return 0;
    b->textures[a] = a == b->main ? colour_resource(b, &t) : dev.screen->resource_create(dev.screen, &t);
    return 1;
}

static bool
fb_validate(struct st_context *st, struct pipe_frontend_drawable *drawable,
            const enum st_attachment_type *statts, unsigned count,
            struct pipe_resource **out, struct pipe_resource **resolve)
{
    struct v3dm_buffer *b = (struct v3dm_buffer *) drawable;
    unsigned i;
    (void) st;
    (void) resolve;
    for (i = 0; i < count; i++) {
        enum st_attachment_type a = statts[i];
        if (!b->textures[a] && !fb_make(b, a))
            continue;                   /* (no format: no such buffer) */
        if (!b->textures[a])
            return false;
        pipe_resource_reference(&out[i], b->textures[a]);
    }
    return true;
}

static bool
fb_flush_front(struct st_context *st, struct pipe_frontend_drawable *drawable,
               enum st_attachment_type statt)
{
    (void) st; (void) drawable; (void) statt;
    return true;            /* the program reads the frame back itself */
}

static struct v3dm_buffer *fb_create(const struct st_visual *visual, unsigned w, unsigned h,
                                     void *target, int target_row_bytes, int tiled)
{
    struct v3dm_buffer *b = CALLOC_STRUCT(v3dm_buffer);
    if (!b)
        return NULL;
    b->visual = *visual;
    b->width = w;
    b->height = h;
    b->main = visual->buffer_mask & ST_ATTACHMENT_BACK_LEFT_MASK ? ST_ATTACHMENT_BACK_LEFT
                                                                 : ST_ATTACHMENT_FRONT_LEFT;
    b->requested = b->target = target;
    b->target_row_bytes = target_row_bytes;
    b->tiled = !target && tiled;
    b->base.visual = &b->visual;
    b->base.flush_front = fb_flush_front;
    b->base.validate = fb_validate;
    p_atomic_set(&b->base.stamp, 1);
    b->base.ID = p_atomic_inc_return(&fb_ids);
    b->base.fscreen = dev.fscreen;
    return b;
}

static void fb_destroy(struct v3dm_buffer *b)
{
    unsigned i;
    if (!b)
        return;
    st_api_destroy_drawable(&b->base);  /* the state tracker lets go of it */
    for (i = 0; i < ST_ATTACHMENT_COUNT; i++)
        pipe_resource_reference(&b->textures[i], NULL);
    FREE(b);
}

/* ---- images over the caller's memory (EGL_RISCOS_image_memory) ----
   A colour buffer in rows over memory the program has (a
   shared memory buffer, say), mapped into the GPU by the module
   (DRM_IOCTL_RISCOS_IMPORT_MEMORY), for glEGLImageTargetRenderbuffer-
   StorageOES (the GPU draws straight into it) or glEGLImageTarget-
   Texture2DOES. The GPU's row 0 (GL's y = 0, the bottom) is the
   memory's first. Mesa asks the frontend what an image handle is
   (get_egl_image), from whatever thread draws. */

struct v3dm_image {
    struct pipe_resource *res;
    struct v3dm_image *next;
};

static simple_mtx_t images_lock = SIMPLE_MTX_INITIALIZER;
static struct v3dm_image *images;

static enum pipe_format fourcc_format(unsigned fourcc)
{
    switch (fourcc) {
    case DRM_FORMAT_ARGB8888: return PIPE_FORMAT_B8G8R8A8_UNORM;    /* B,G,R,A bytes (cairo's ARGB32) */
    case DRM_FORMAT_XRGB8888: return PIPE_FORMAT_B8G8R8X8_UNORM;
    case DRM_FORMAT_ABGR8888: return PIPE_FORMAT_R8G8B8A8_UNORM;    /* R,G,B,A bytes (a sprite's &BBGGRR) */
    case DRM_FORMAT_XBGR8888: return PIPE_FORMAT_R8G8B8X8_UNORM;
    default: return PIPE_FORMAT_NONE;
    }
}

static v3dm_image *image_make(void *pixels, int row_bytes, unsigned width, unsigned height, unsigned fourcc);

v3dm_image *v3dm_image_create(void *pixels, int row_bytes, unsigned width, unsigned height, unsigned fourcc)
{
    v3dm_image *m = image_make(pixels, row_bytes, width, height, fourcc);
    if (dev.trace && m)
        fprintf(stderr, "v3dm: image %p over %p, %ux%u, rows %d bytes\n", (void *) m, pixels, width, height,
                row_bytes);
    else if (dev.trace)
        fprintf(stderr, "v3dm: no image over %p (%s)\n", pixels, dev.error);
    return m;
}

static v3dm_image *image_make(void *pixels, int row_bytes, unsigned width, unsigned height, unsigned fourcc)
{
    struct drm_riscos_import_memory im;
    struct pipe_resource t, *r;
    struct v3dm_image *m;
    if (!v3dm_init())
        return NULL;
    memset(&t, 0, sizeof t);
    t.target = PIPE_TEXTURE_2D;
    t.format = fourcc_format(fourcc);
    t.width0 = width;
    t.height0 = height;
    t.depth0 = 1;
    t.array_size = 1;
    t.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_LINEAR;
    if (t.format == PIPE_FORMAT_NONE ||
        !dev.screen->is_format_supported(dev.screen, t.format, PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_RENDER_TARGET)) {
        snprintf(dev.error, sizeof dev.error, "the GPU can't draw into memory of format %08x", fourcc);
        return NULL;
    }
    if (!pixels || ((uintptr_t) pixels & 4095) || !width || !height || width > 4096 || height > 4096 ||
        row_bytes < (int) width * 4 || (row_bytes & 15)) {
        snprintf(dev.error, sizeof dev.error, "memory for the GPU: 4 KB aligned, at most 4096 x 4096, "
                 "rows a multiple of 16 bytes");
        return NULL;
    }
    if (dev.no_import) {
        snprintf(dev.error, sizeof dev.error, "the GPU module can't map memory (V3DM$NoImport, or refused before)");
        return NULL;
    }
    memset(&im, 0, sizeof im);
    im.address = (uintptr_t) pixels;
    im.size = ((size_t) row_bytes * height + 4095) & ~4095u;
    if (import_memory(&im)) {
        if (missing_page)
            snprintf(dev.error, sizeof dev.error, "the memory at %08x has no page behind it",
                     (unsigned) missing_page);
        else
            snprintf(dev.error, sizeof dev.error, "the GPU module wouldn't map the memory (%d)", errno);
        return NULL;
    }
    r = from_handle(&t, im.handle, (unsigned) row_bytes);
    if (!r) {
        close_handle(im.handle);
        snprintf(dev.error, sizeof dev.error, "Mesa's driver refused the mapped memory");
        return NULL;
    }
    m = CALLOC_STRUCT(v3dm_image);
    if (!m) {
        snprintf(dev.error, sizeof dev.error, "no memory");
        pipe_resource_reference(&r, NULL);
        return NULL;
    }
    m->res = r;
    simple_mtx_lock(&images_lock);
    m->next = images;
    images = m;
    simple_mtx_unlock(&images_lock);
    return m;
}

/* The image goes; its memory stays mapped while a renderbuffer or texture
   made from it is left (the resource is theirs too) */
void v3dm_image_destroy(v3dm_image *m)
{
    struct v3dm_image **p;
    simple_mtx_lock(&images_lock);
    for (p = &images; *p && *p != m; p = &(*p)->next)
        ;
    if (*p)
        *p = m->next;
    simple_mtx_unlock(&images_lock);
    if (!m)
        return;
    pipe_resource_reference(&m->res, NULL);
    FREE(m);
}

static bool get_egl_image(struct pipe_frontend_screen *fscreen, void *image, struct st_egl_image *out)
{
    struct v3dm_image *m;
    (void) fscreen;
    simple_mtx_lock(&images_lock);
    for (m = images; m && m != image; m = m->next)
        ;
    if (m) {
        pipe_resource_reference(&out->texture, m->res);
        out->format = m->res->format;
        out->level = 0;
        out->layer = 0;
        out->internalformat = 0x1908;   /* GL_RGBA */
    }
    simple_mtx_unlock(&images_lock);
    return m != NULL;
}

static bool validate_egl_image(struct pipe_frontend_screen *fscreen, void *image)
{
    struct v3dm_image *m;
    (void) fscreen;
    simple_mtx_lock(&images_lock);
    for (m = images; m && m != image; m = m->next)
        ;
    simple_mtx_unlock(&images_lock);
    return m != NULL;
}

/* ---- the device ---- */

static int get_param(struct pipe_frontend_screen *fscreen, enum st_manager_param param)
{
    (void) fscreen;
    return param == ST_MANAGER_BROKEN_INVALIDATE;
}

/* Mesa's driver for each kind of GPU module, by the DRM driver name the
   module gives (RISC3D_Info r6, or its DRM_IOCTL_VERSION) */
static struct pipe_screen *create_v3d(int fd, struct pipe_screen_config *config)
{
    return v3d_screen_create(fd, config, NULL);
}

static const struct {
    const char *name;
    struct pipe_screen *(*create)(int fd, struct pipe_screen_config *config);
} drivers[] = {
    { "v3d", create_v3d },              /* Raspberry Pi 4 and 400 (V3DRender) */
    /* another GPU's module: a row here, and its Mesa driver in build-v3d.sh */
};

int v3dm_init(void)
{
    struct pipe_screen_config config;
    const char *name;
    unsigned i;
    if (dev.screen)
        return 1;
    name = drm_riscos_device();
    if (!name) {
        snprintf(dev.error, sizeof dev.error, "no GPU: RISC3D isn't loaded, or lists no GPU driver module (DRM device)");
        return 0;
    }
    for (i = 0; i < sizeof drivers / sizeof drivers[0] && strcmp(drivers[i].name, name); i++)
        ;
    if (i == sizeof drivers / sizeof drivers[0]) {
        snprintf(dev.error, sizeof dev.error, "this build has no Mesa driver for the \"%s\" GPU", name);
        return 0;
    }
    dev.tiled = getenv("V3DM$Tiled") != NULL;
    dev.no_cached = dev.tiled || getenv("V3DM$Uncached") != NULL;
    dev.no_import = dev.tiled || getenv("V3DM$NoImport") != NULL;
    dev.trace = getenv("V3DM$Trace") != NULL;
    dev.keep_depth = getenv("V3DM$KeepDepth") != NULL;
    dev.late_flush = getenv("V3DM$LateFlush") != NULL;
    dev.fd = drm_riscos_open();
    if (dev.fd < 0) {
        snprintf(dev.error, sizeof dev.error, "the %s GPU's module wouldn't open", name);
        dev.fd = 0;
        return 0;
    }
    driParseOptionInfo(&dev.options_info, NULL, 0);
    memset(&config, 0, sizeof config);
    config.options = &dev.options;
    config.options_info = &dev.options_info;
    dev.screen = drivers[i].create(dev.fd, &config);
    if (!dev.screen) {
        snprintf(dev.error, sizeof dev.error, "Mesa's %s driver refused the GPU module's file", name);
        drm_riscos_close(dev.fd);
        dev.fd = 0;
        return 0;
    }
    dev.fscreen = CALLOC_STRUCT(pipe_frontend_screen);
    dev.fscreen->screen = dev.screen;
    dev.fscreen->get_param = get_param;
    dev.fscreen->get_egl_image = get_egl_image;
    dev.fscreen->validate_egl_image = validate_egl_image;
    return 1;
}

void v3dm_shutdown(void)
{
    if (!dev.screen)
        return;
    st_screen_destroy(dev.fscreen);
    FREE(dev.fscreen);
    dev.screen->destroy(dev.screen);     /* v3d: closes the module's file */
    dev.screen = NULL;
    dev.fscreen = NULL;
    dev.fd = 0;
}

const char *v3dm_device_name(void)
{
    const char *name = drm_riscos_device();
    unsigned i;
    for (i = 0; name && i < sizeof drivers / sizeof drivers[0]; i++)
        if (!strcmp(drivers[i].name, name))
            return name;
    return NULL;
}

const char *v3dm_device(void)
{
    static char text[64];
    const char *name = drm_riscos_device();
    if (!name)
        return "none";
    snprintf(text, sizeof text, "%s, through RISC3D", name);
    return text;
}

const char *v3dm_renderer(void)
{
    return dev.screen ? dev.screen->get_name(dev.screen) : NULL;
}

/* ---- formats ----
   The first of a list that the driver can use for this kind of buffer.
   (A format v3d's table doesn't know is accepted for the resource but
   stored by the GPU in a colour format: a depth store then never
   finishes. V3DRender found it: Z24_UNORM_S8_UINT, 15 Nov 2026.) */

static enum pipe_format pick_format(const enum pipe_format *list, unsigned bind)
{
    for (; *list != PIPE_FORMAT_NONE; list++)
        if (dev.screen->is_format_supported(dev.screen, *list, PIPE_TEXTURE_2D, 0, 0, bind))
            return *list;
    return PIPE_FORMAT_NONE;
}

static const enum pipe_format colour_formats[] = {
    PIPE_FORMAT_R8G8B8A8_UNORM, PIPE_FORMAT_B8G8R8A8_UNORM, PIPE_FORMAT_NONE
};
/* 16-bit colour (EGL's 5:6:5 configs): red at the top, as the GPU stores
   5:6:5 (V3DRender's present stores it in the screen's own order) */
static const enum pipe_format colour_formats_16[] = {
    PIPE_FORMAT_B5G6R5_UNORM, PIPE_FORMAT_NONE
};
static const enum pipe_format depth_formats[] = {
    PIPE_FORMAT_S8_UINT_Z24_UNORM, PIPE_FORMAT_X8Z24_UNORM, PIPE_FORMAT_Z16_UNORM, PIPE_FORMAT_NONE
};

/* ---- contexts ---- */

v3dm_context *v3dm_create(int api, int major, int minor, int depth, v3dm_context *share)
{
    return v3dm_create_format(api, major, minor, depth, share, 32);
}

v3dm_context *v3dm_create_format(int api, int major, int minor, int depth, v3dm_context *share,
                                 int colour_bits)
{
    struct st_context_attribs attribs;
    enum st_context_error err;
    v3dm_context *c;
    if (!v3dm_init())
        return NULL;
    c = CALLOC_STRUCT(v3dm_context);
    if (!c)
        return NULL;
    c->visual.color_format = pick_format(colour_bits == 16 ? colour_formats_16 : colour_formats,
                                         PIPE_BIND_RENDER_TARGET);
    c->visual.depth_stencil_format = depth ? pick_format(depth_formats, PIPE_BIND_DEPTH_STENCIL)
                                           : PIPE_FORMAT_NONE;
    if (c->visual.color_format == PIPE_FORMAT_NONE) {
        snprintf(dev.error, sizeof dev.error, "the GPU offers no %s render target",
                 colour_bits == 16 ? "RGB565" : "RGBA8");
        FREE(c);
        return NULL;
    }
    c->visual.accum_format = PIPE_FORMAT_NONE;
    c->visual.buffer_mask = ST_ATTACHMENT_FRONT_LEFT_MASK | ST_ATTACHMENT_BACK_LEFT_MASK |
                            (depth ? ST_ATTACHMENT_DEPTH_STENCIL_MASK : 0);
    memset(&attribs, 0, sizeof attribs);
    attribs.profile = api == V3DM_API_GLES1 ? API_OPENGLES :
                      api == V3DM_API_GLES2 ? API_OPENGLES2 : API_OPENGL_COMPAT;
    attribs.major = major;
    attribs.minor = minor;
    attribs.visual = c->visual;
    /* share: the objects (textures, buffers, programs...) of another
       context, which may be current in another thread */
    c->st = st_api_create_context(dev.fscreen, &attribs, &err, share ? share->st : NULL);
    if (!c->st) {
        snprintf(dev.error, sizeof dev.error, "st_api_create_context failed (%d)", (int) err);
        FREE(c);
        return NULL;
    }
    c->st->frontend_context = c;
    return c;
}

/* the GPU has finished drawing into b's colour buffer (it is mapped and
   unmapped for reading: v3d flushes the jobs writing it and waits) */
static int fb_finish(v3dm_context *c, struct v3dm_buffer *b)
{
    struct pipe_context *pipe = c->st->pipe;
    struct pipe_resource *res = b ? b->textures[b->main] : NULL;
    struct pipe_transfer *t = NULL;
    struct pipe_box box;
    if (!res)
        return -1;
    if (dev.trace)
        fprintf(stderr, "v3dm: finish frame %p (into %p)\n", (void *) b, b->target);
    st_context_flush(c->st, 0, NULL, NULL, NULL);
    u_box_2d(0, 0, 1, 1, &box);
    if (!pipe->texture_map(pipe, res, 0, PIPE_MAP_READ, &box, &t))
        return -1;
    pipe->texture_unmap(pipe, t);
    return 0;
}

static int fb_fits(const struct v3dm_buffer *b, unsigned w, unsigned h, void *target, int row_bytes,
                   int tiled)
{
    return b && b->width == w && b->height == h && b->requested == target &&
           (!target || b->target_row_bytes == row_bytes) && b->tiled == (!target && tiled);
}

/* The frame c->fb was, kept as a spare (the oldest spare goes) */
static struct v3dm_buffer *fb_keep(v3dm_context *c, struct v3dm_buffer *b)
{
    struct v3dm_buffer *gone = c->spare[V3DM_SPARE_FBS - 1];
    unsigned i;
    for (i = V3DM_SPARE_FBS - 1; i > 0; i--)
        c->spare[i] = c->spare[i - 1];
    c->spare[0] = b;
    return gone;
}

int v3dm_make_current(v3dm_context *c, unsigned width, unsigned height)
{
    struct v3dm_buffer *gone = NULL;
    unsigned i;
    int ok;
    if (!c)
        return st_api_make_current(NULL, NULL, NULL);
    if (!fb_fits(c->fb, width, height, c->target, c->target_row_bytes, c->tiled)) {
        struct v3dm_buffer *b = NULL;
        for (i = 0; i < V3DM_SPARE_FBS && !b; i++)
            if (fb_fits(c->spare[i], width, height, c->target, c->target_row_bytes, c->tiled)) {
                b = c->spare[i];
                for (; i + 1 < V3DM_SPARE_FBS; i++)
                    c->spare[i] = c->spare[i + 1];
                c->spare[V3DM_SPARE_FBS - 1] = NULL;
            }
        if (!b) {
            b = fb_create(&c->visual, width, height, c->target, c->target_row_bytes, c->tiled);
            if (!b)
                return 0;
            fb_make(b, b->main);        /* now: whether it draws into c->target is known */
            if (dev.trace)
                fprintf(stderr, "v3dm: new frame %p %ux%u, into %p\n", (void *) b, width, height, b->target);
        } else if (dev.trace) {
            fprintf(stderr, "v3dm: frame %p %ux%u again, into %p\n", (void *) b, width, height, b->target);
        }
        if (c->fb)
            gone = fb_keep(c, c->fb);
        c->fb = b;
    }
    ok = st_api_make_current(c->st, &c->fb->base, &c->fb->base);
    if (gone) {
        if (gone->target)
            fb_finish(c, gone);         /* nothing more goes into the caller's memory */
        fb_destroy(gone);
    }
    return ok;
}

/* Every frame that draws into the caller's memory finished and let go
   (the caller is about to free or move some of it) */
static void drop_targets(v3dm_context *c)
{
    unsigned i, j;
    if (c->fb && c->fb->target)
        fb_finish(c, c->fb);
    for (i = j = 0; i < V3DM_SPARE_FBS; i++) {
        struct v3dm_buffer *b = c->spare[i];
        c->spare[i] = NULL;
        if (b && b->target) {
            fb_finish(c, b);
            fb_destroy(b);
        } else if (b) {
            c->spare[j++] = b;
        }
    }
}

int v3dm_draw_into(v3dm_context *c, void *pixels, int row_bytes, unsigned width, unsigned height)
{
    if (!c)
        return 0;
    if (!pixels)
        drop_targets(c);
    c->target = pixels;
    c->target_row_bytes = pixels ? row_bytes : 0;
    c->tiled = 0;
    if (!v3dm_make_current(c, width, height))
        return 0;
    return pixels && c->fb->target == pixels;
}

int v3dm_draw_tiled(v3dm_context *c, int on, unsigned width, unsigned height)
{
    if (!c)
        return 0;
    if (on && c->target)
        drop_targets(c);
    if (on)
        c->target = NULL, c->target_row_bytes = 0;
    c->tiled = on != 0;
    return v3dm_make_current(c, width, height);
}

/* the DRM fourcc of a colour buffer's format, for V3DRender's present;
   0 if it has none */
static uint32_t present_fourcc(enum pipe_format f)
{
    switch (f) {
    case PIPE_FORMAT_R8G8B8A8_UNORM: case PIPE_FORMAT_R8G8B8A8_SRGB: return DRM_FORMAT_ABGR8888;
    case PIPE_FORMAT_R8G8B8X8_UNORM: case PIPE_FORMAT_R8G8B8X8_SRGB: return DRM_FORMAT_XBGR8888;
    case PIPE_FORMAT_B8G8R8A8_UNORM: case PIPE_FORMAT_B8G8R8A8_SRGB: return DRM_FORMAT_ARGB8888;
    case PIPE_FORMAT_B8G8R8X8_UNORM: case PIPE_FORMAT_B8G8R8X8_SRGB: return DRM_FORMAT_XRGB8888;
    case PIPE_FORMAT_B5G6R5_UNORM: return DRM_FORMAT_RGB565;
    default: return 0;
    }
}

/* V3DRender 0.31: the current frame copied by the GPU into the screen's
   rectangles (6 ints each: dst_x, dst_y, width, height, src_x, src_y),
   waiting until it's stored. n 0: only whether it would be taken. 0, or
   -errno (ENOTTY: not offered) */
/* c's frame scaled by the GPU to width x height (c->scaled, made or
   remade at that size): nearest when both are whole multiples of the
   frame's, else bilinear. The scaled frame, or NULL (couldn't be made) */
static struct pipe_resource *scale_frame(v3dm_context *c, struct pipe_resource *res,
                                         unsigned width, unsigned height)
{
    struct pipe_context *pipe = c->st->pipe;
    struct pipe_blit_info bi;
    if (!c->scaled || c->scaled->width0 != width || c->scaled->height0 != height ||
        c->scaled->format != res->format) {
        struct pipe_resource t;
        pipe_resource_reference(&c->scaled, NULL);
        memset(&t, 0, sizeof t);
        t.target = PIPE_TEXTURE_2D;
        t.format = res->format;
        t.width0 = width;
        t.height0 = height;
        t.depth0 = 1;
        t.array_size = 1;
        t.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
        c->scaled = dev.screen->resource_create(dev.screen, &t);
        if (!c->scaled)
            return NULL;
    }
    memset(&bi, 0, sizeof bi);
    bi.src.resource = res;
    bi.src.format = res->format;
    u_box_2d(0, 0, res->width0, res->height0, &bi.src.box);
    bi.dst.resource = c->scaled;
    bi.dst.format = c->scaled->format;
    u_box_2d(0, 0, width, height, &bi.dst.box);
    bi.mask = PIPE_MASK_RGBA;
    bi.filter = width % res->width0 == 0 && height % res->height0 == 0
                ? PIPE_TEX_FILTER_NEAREST : PIPE_TEX_FILTER_LINEAR;
    pipe->blit(pipe, &bi);
    return c->scaled;
}

int v3dm_present(v3dm_context *c, const int *rects, unsigned n)
{
    return v3dm_present_scaled(c, rects, n, 0, 0);
}

int v3dm_present_scaled(v3dm_context *c, const int *rects, unsigned n,
                        unsigned width, unsigned height)
{
    struct drm_riscos_present p;
    struct pipe_resource *res = c && c->fb ? c->fb->textures[c->fb->main] : NULL;
    unsigned done = 0;
    if (!c || !dev.fd)
        return -ENODEV;
    if (width > V3D_MAX_IMAGE_DIMENSION || height > V3D_MAX_IMAGE_DIMENSION)
        return -EINVAL;
    if (res && n && width && height && (width != res->width0 || height != res->height0)) {
        res = scale_frame(c, res, width, height);
        if (!res)
            return -ENOMEM;
    }
    memset(&p, 0, sizeof p);
    if (res) {
        struct v3d_resource *rsc = v3d_resource(res);
        const struct v3d_resource_slice *sl = &rsc->slices[0];
        p.handle = rsc->bo->handle;
        p.format = present_fourcc(res->format);
        p.width = res->width0;
        p.height = res->height0;
        switch (sl->tiling) {
        case V3D_TILING_UIF_NO_XOR:
            p.flags = DRM_RISCOS_PRESENT_SRC_UIF;
            p.uif_height_or_stride = sl->padded_height;
            break;
        case V3D_TILING_UIF_XOR:
            p.flags = DRM_RISCOS_PRESENT_SRC_UIF | DRM_RISCOS_PRESENT_SRC_UIF_XOR;
            p.uif_height_or_stride = sl->padded_height;
            break;
        case V3D_TILING_RASTER:
            p.uif_height_or_stride = sl->stride;
            break;
        default:
            return -EINVAL;             /* small frames in other tilings: plotted */
        }
        if (!p.format)
            return -EINVAL;
    } else if (n) {
        return -ENOENT;                 /* no frame drawn yet */
    }
    if (!n)
        return drmIoctl(dev.fd, DRM_IOCTL_RISCOS_PRESENT, &p) ? -errno : 0;
    if (!dev.present_sync && drmSyncobjCreate(dev.fd, 0, &dev.present_sync))
        return -errno;
    st_context_flush(c->st, 0, NULL, NULL, NULL);       /* its jobs submitted: present waits for them */
    p.out_sync = dev.present_sync;
    while (done < n) {
        unsigned k = n - done > DRM_RISCOS_PRESENT_MAX_RECTS ? DRM_RISCOS_PRESENT_MAX_RECTS : n - done;
        p.rect_count = k;
        p.rects = (uintptr_t) (rects + 6 * done);
        if (drmIoctl(dev.fd, DRM_IOCTL_RISCOS_PRESENT, &p))
            return done ? -EIO : -errno;
        done += k;
    }
    /* stored before the caller lets the Wimp draw anything else there */
    if (drmSyncobjWait(dev.fd, &dev.present_sync, 1, INT64_MAX, 0, NULL))
        return -EIO;
    if (dev.trace)
        fprintf(stderr, "v3dm: presented frame %p, %u rectangles\n", (void *) c->fb, n);
    return 0;
}

/* The end of c's frame (eglSwapBuffers): its depth and stencil are
   undefined after a swap (EGL 1.4, 3.9.3), so the frame's last job doesn't
   store them, and the next frame doesn't load them (as Linux's DRI
   frontend does: __DRI2_FLUSH_INVALIDATE_ANCILLARY). V3DM$KeepDepth:
   kept, to compare.
   Then the frame's jobs are submitted, so the GPU draws it while the swap
   waits for the vsync, rather than from when it's shown (V3DM$LateFlush:
   as before, to compare). */
void v3dm_end_frame(v3dm_context *c)
{
    struct pipe_context *pipe;
    if (!c || !c->fb)
        return;
    pipe = c->st->pipe;
    if (!dev.keep_depth && pipe->invalidate_resource && c->fb->textures[ST_ATTACHMENT_DEPTH_STENCIL])
        pipe->invalidate_resource(pipe, c->fb->textures[ST_ATTACHMENT_DEPTH_STENCIL]);
    if (!dev.late_flush)
        st_context_flush(c->st, 0, NULL, NULL, NULL);
}

/* Buffers the GPU module refused since this was last asked (v3d_bo_alloc
   gave stand-ins: what needed them wasn't drawn): GL_OUT_OF_MEMORY in the
   current context, for its next glGetError. The count. */
int v3dm_report_oom(void)
{
    struct st_context *st;
    int n;
    if (!dev.screen)
        return 0;
    n = p_atomic_xchg(&v3d_screen(dev.screen)->riscos_oom, 0);
    if (n > 0 && (st = st_api_get_current()) != NULL)
        _mesa_error(st->ctx, GL_OUT_OF_MEMORY, "the GPU's memory is full (%d buffers refused)", n);
    return n;
}

int v3dm_finish(v3dm_context *c)
{
    return c ? fb_finish(c, c->fb) : -1;
}

void v3dm_destroy(v3dm_context *c)
{
    if (!c)
        return;
    drop_targets(c);                    /* the caller may free its memory after this */
    /* (st_destroy_context binds c while it works and then binds again
       whatever this thread had current, so c mustn't be that) */
    if (st_api_get_current() == c->st)
        st_api_make_current(NULL, NULL, NULL);
    st_context_flush(c->st, 0, NULL, NULL, NULL);
    st_destroy_context(c->st);
    fb_destroy(c->fb);
    pipe_resource_reference(&c->scaled, NULL);
    {
        unsigned i;
        for (i = 0; i < V3DM_SPARE_FBS; i++)
            fb_destroy(c->spare[i]);
    }
    FREE(c);
}

/* Copying out of the GPU's buffers, which the CPU sees uncached: reads
   are slow, and slowest a word at a time, so 128 bytes go at once through
   VFP's 16 upper double registers (VFPv3-D32: every ARMv7 target here;
   d16-d31 need no saving). Then the rest a word at a time. */
static void copy_from_gpu(void *dst, const void *src, size_t n)
{
#if defined(__arm__) && defined(__ARM_FP)
    unsigned char *d = (unsigned char *) dst;
    const unsigned char *s = (const unsigned char *) src;
    if ((((uintptr_t) d | (uintptr_t) s) & 7) == 0) {
        while (n >= 128) {
            __asm__ volatile("vldmia %0!, {d16-d31}\n\tvstmia %1!, {d16-d31}"
                             : "+r"(s), "+r"(d) : : "memory",
                               "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
                               "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31");
            n -= 128;
        }
    }
    memcpy(d, s, n);
#else
    memcpy(dst, src, n);
#endif
}

/* The frame is a window-system framebuffer (Mesa draws it with y
   flipped), so its row 0 is the top one. */
int v3dm_read(v3dm_context *c, void *pixels, int row_bytes, int top_first)
{
    struct pipe_context *pipe;
    struct pipe_resource *res;
    struct pipe_transfer *t = NULL;
    struct pipe_box box;
    const unsigned char *src;
    unsigned y, x, w, h;
    int bgra;
    if (!c || !c->fb)
        return -1;
    res = c->fb->textures[ST_ATTACHMENT_BACK_LEFT] ? c->fb->textures[ST_ATTACHMENT_BACK_LEFT]
                                                   : c->fb->textures[ST_ATTACHMENT_FRONT_LEFT];
    if (!res)
        return -1;
    w = c->fb->width;
    h = c->fb->height;
    bgra = res->format == PIPE_FORMAT_B8G8R8A8_UNORM;
    st_context_flush(c->st, 0, NULL, NULL, NULL);
    pipe = c->st->pipe;
    u_box_2d(0, 0, w, h, &box);
    src = (const unsigned char *) pipe->texture_map(pipe, res, 0, PIPE_MAP_READ, &box, &t);
    if (!src)
        return -1;
    if (res->format != PIPE_FORMAT_R8G8B8A8_UNORM && !bgra) {
        /* 16-bit colour: each row unpacked to R,G,B,A bytes (through a
           cached copy: the frame is uncached) */
        unsigned bytes = w * util_format_get_blocksize(res->format);
        unsigned char *tmp = (unsigned char *) malloc(bytes);
        if (!tmp) {
            pipe->texture_unmap(pipe, t);
            return -1;
        }
        for (y = 0; y < h; y++) {
            copy_from_gpu(tmp, src + (size_t) y * t->stride, bytes);
            util_format_unpack_rgba_8unorm_rect(res->format,
                                                (uint8_t *) pixels + (size_t) (top_first ? y : h - 1 - y) * row_bytes,
                                                (unsigned) row_bytes, tmp, bytes, w, 1);
        }
        free(tmp);
        pipe->texture_unmap(pipe, t);
        return 0;
    }
    if (!bgra && top_first && t->stride == w * 4 && (unsigned) row_bytes == w * 4) {
        copy_from_gpu(pixels, src, (size_t) w * h * 4);     /* the whole frame at once */
        pipe->texture_unmap(pipe, t);
        return 0;
    }
    for (y = 0; y < h; y++) {
        const unsigned char *row = src + (size_t) y * t->stride;
        unsigned char *out = (unsigned char *) pixels + (size_t) (top_first ? y : h - 1 - y) * row_bytes;
        copy_from_gpu(out, row, (size_t) w * 4);
        if (bgra) {                     /* then R and B swapped, in cached memory */
            uint32_t *p = (uint32_t *) out;
            for (x = 0; x < w; x++)
                p[x] = (p[x] & 0xFF00FF00u) | ((p[x] & 0xFF) << 16) | ((p[x] >> 16) & 0xFF);
        }
    }
    pipe->texture_unmap(pipe, t);
    return 0;
}

const char *v3dm_formats(v3dm_context *c)
{
    static char text[96];
    if (!c)
        return "";
    snprintf(text, sizeof text, "colour %s, depth/stencil %s",
             util_format_short_name(c->visual.color_format),
             util_format_short_name(c->visual.depth_stencil_format));
    return text;
}
