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
#include <stdio.h>
#include <string.h>

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "frontend/api.h"
#include "state_tracker/st_context.h"
#include "util/u_atomic.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/xmlconfig.h"
#include "v3d/v3d_screen.h"

#include "drm_riscos.h"
#include "v3dm.h"

struct v3dm_buffer {
    struct pipe_frontend_drawable base;
    struct st_visual visual;
    unsigned width, height;
    struct pipe_resource *textures[ST_ATTACHMENT_COUNT];
};

struct v3dm_context {
    struct st_context *st;
    struct v3dm_buffer *fb;
    struct st_visual visual;
};

static struct {
    int fd;
    struct pipe_screen *screen;
    struct pipe_frontend_screen *fscreen;
    driOptionCache options, options_info;
    char error[160];
} dev;

static uint32_t fb_ids;

const char *v3dm_error(void)
{
    return dev.error;
}

/* ---- the framebuffer ---- */

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
        if (!b->textures[a]) {
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
            }
            if (t.format == PIPE_FORMAT_NONE)
                continue;
            b->textures[a] = dev.screen->resource_create(dev.screen, &t);
            if (!b->textures[a])
                return false;
        }
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

static struct v3dm_buffer *fb_create(const struct st_visual *visual, unsigned w, unsigned h)
{
    struct v3dm_buffer *b = CALLOC_STRUCT(v3dm_buffer);
    if (!b)
        return NULL;
    b->visual = *visual;
    b->width = w;
    b->height = h;
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
    for (i = 0; i < ST_ATTACHMENT_COUNT; i++)
        pipe_resource_reference(&b->textures[i], NULL);
    FREE(b);
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
        snprintf(dev.error, sizeof dev.error, "no GPU driver module is loaded (RISC3D lists no DRM device, and V3DRender isn't loaded)");
        return 0;
    }
    for (i = 0; i < sizeof drivers / sizeof drivers[0] && strcmp(drivers[i].name, name); i++)
        ;
    if (i == sizeof drivers / sizeof drivers[0]) {
        snprintf(dev.error, sizeof dev.error, "this build has no Mesa driver for the \"%s\" GPU", name);
        return 0;
    }
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

const char *v3dm_device(void)
{
    static char text[64];
    const char *name = drm_riscos_device();
    if (!name)
        return "none";
    snprintf(text, sizeof text, "%s, opened %s", name,
             drm_riscos_via_risc3d() ? "through RISC3D" : "directly (V3DRender_Open)");
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
static const enum pipe_format depth_formats[] = {
    PIPE_FORMAT_S8_UINT_Z24_UNORM, PIPE_FORMAT_X8Z24_UNORM, PIPE_FORMAT_Z16_UNORM, PIPE_FORMAT_NONE
};

/* ---- contexts ---- */

v3dm_context *v3dm_create(int api, int major, int minor, int depth)
{
    struct st_context_attribs attribs;
    enum st_context_error err;
    v3dm_context *c;
    if (!v3dm_init())
        return NULL;
    c = CALLOC_STRUCT(v3dm_context);
    if (!c)
        return NULL;
    c->visual.color_format = pick_format(colour_formats, PIPE_BIND_RENDER_TARGET);
    c->visual.depth_stencil_format = depth ? pick_format(depth_formats, PIPE_BIND_DEPTH_STENCIL)
                                           : PIPE_FORMAT_NONE;
    if (c->visual.color_format == PIPE_FORMAT_NONE) {
        snprintf(dev.error, sizeof dev.error, "the GPU offers no RGBA8 render target");
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
    c->st = st_api_create_context(dev.fscreen, &attribs, &err, NULL);
    if (!c->st) {
        snprintf(dev.error, sizeof dev.error, "st_api_create_context failed (%d)", (int) err);
        FREE(c);
        return NULL;
    }
    c->st->frontend_context = c;
    return c;
}

int v3dm_make_current(v3dm_context *c, unsigned width, unsigned height)
{
    if (!c)
        return st_api_make_current(NULL, NULL, NULL);
    if (!c->fb || c->fb->width != width || c->fb->height != height) {
        struct v3dm_buffer *old = c->fb;
        c->fb = fb_create(&c->visual, width, height);
        if (!c->fb)
            return 0;
        fb_destroy(old);
    }
    return st_api_make_current(c->st, &c->fb->base, &c->fb->base);
}

void v3dm_destroy(v3dm_context *c)
{
    if (!c)
        return;
    st_api_make_current(NULL, NULL, NULL);
    st_context_flush(c->st, 0, NULL, NULL, NULL);
    st_destroy_context(c->st);
    fb_destroy(c->fb);
    FREE(c);
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
