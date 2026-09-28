/*
 * egl/parts/validation.c - checking handles and attribute lists; linking objects.
 * Part of egl_riscos.c: it is compiled by being included from there, as
 * one unit with the other parts (so their helpers stay private to the
 * library), not on its own. MIT licence (see LICENSE).
 */

/* ------------------------------------------------------------------ */
/* Validation                                                          */

static egl_display *get_display(EGLDisplay dpy, int need_init)
{
    egl_display *d = (egl_display *) dpy;
    if (d != &display) {
        fail(EGL_BAD_DISPLAY);
        return NULL;
    }
    egl_obj_label = d->label;
    if (need_init && !d->initialised) {
        fail(EGL_NOT_INITIALIZED);
        return NULL;
    }
    return d;
}

static const egl_config *get_config(egl_display *d, EGLConfig config)
{
    const egl_config *c = (const egl_config *) config;
    if (c < d->configs || c >= d->configs + d->nconfigs) {
        fail(EGL_BAD_CONFIG);
        return NULL;
    }
    return c;
}

static egl_surface *get_surface(egl_display *d, EGLSurface surface)
{
    egl_surface *s;
    for (s = d->surfaces; s; s = s->next)
        if (s == (egl_surface *) surface && !s->destroy_pending) {
            egl_obj_label = s->label;
            return s;
        }
    fail(EGL_BAD_SURFACE);
    return NULL;
}

static egl_context *get_context(egl_display *d, EGLContext context)
{
    egl_context *c;
    for (c = d->contexts; c; c = c->next)
        if (c == (egl_context *) context && !c->destroy_pending) {
            egl_obj_label = c->label;
            return c;
        }
    fail(EGL_BAD_CONTEXT);
    return NULL;
}

static void unlink_surface(egl_display *d, egl_surface *surf)
{
    egl_surface **p;
    for (p = &d->surfaces; *p; p = &(*p)->next)
        if (*p == surf) {
            *p = surf->next;
            break;
        }
    ovl_destroy(surf);
    free_buffers(surf);
    OSMesaDestroyBuffer(surf->buf);
    surf->magic = 0;
    free(surf);
}

static void unlink_context(egl_display *d, egl_context *ctx)
{
    egl_context **p;
    for (p = &d->contexts; *p; p = &(*p)->next)
        if (*p == ctx) {
            *p = ctx->next;
            break;
        }
    OSMesaDestroyContext(ctx->om);
    ctx->magic = 0;
    free(ctx);
}
