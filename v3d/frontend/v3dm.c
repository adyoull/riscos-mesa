/*
 * v3dm.c - riscos-mesa's frontend for Mesa's v3d driver on native RISC OS
 * (V3DRender): a context and an off-screen framebuffer of a given size,
 * as OSMesa gives, but drawn by the Raspberry Pi 4's GPU.
 *
 * PROTOTYPE (v3d branch). It follows the frontends Mesa carries for
 * systems without DRI (gallium/frontends/hgl, and the OSMesa frontend
 * Mesa removed in 25.1): one pipe screen from v3d_screen_create on
 * V3DRender's file, a pipe_frontend_screen, and for each context a
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

int v3dm_init(void)
{
    struct pipe_screen_config config;
    if (dev.screen)
        return 1;
    if (!drm_riscos_available()) {
        snprintf(dev.error, sizeof dev.error, "V3DRender isn't loaded (or isn't driving a V3D)");
        return 0;
    }
    dev.fd = drm_riscos_open();
    if (dev.fd < 0) {
        snprintf(dev.error, sizeof dev.error, "V3DRender_Open failed");
        return 0;
    }
    driParseOptionInfo(&dev.options_info, NULL, 0);
    memset(&config, 0, sizeof config);
    config.options = &dev.options;
    config.options_info = &dev.options_info;
    dev.screen = v3d_screen_create(dev.fd, &config, NULL);
    if (!dev.screen) {
        snprintf(dev.error, sizeof dev.error, "Mesa's v3d driver refused V3DRender's file");
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
    dev.screen->destroy(dev.screen);     /* closes V3DRender's file */
    dev.screen = NULL;
    dev.fscreen = NULL;
    dev.fd = 0;
}

const char *v3dm_renderer(void)
{
    return dev.screen ? dev.screen->get_name(dev.screen) : NULL;
}

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
    c->visual.color_format = PIPE_FORMAT_R8G8B8A8_UNORM;
    c->visual.depth_stencil_format = depth ? PIPE_FORMAT_Z24_UNORM_S8_UINT : PIPE_FORMAT_NONE;
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
