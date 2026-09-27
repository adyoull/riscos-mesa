/*
 * egl/parts/current.c - what is current: each thread's contexts, and
 * binding a context and its surfaces to OSMesa.
 * Part of egl_riscos.c: it is compiled by being included from there, as
 * one unit with the other parts (so their helpers stay private to the
 * library), not on its own. MIT licence (see LICENSE).
 *
 * EGL keeps, for each thread, one current context per client API (OpenGL
 * and OpenGL ES), each with a draw and a read surface. OSMesa has one
 * current context per thread, and GL calls go to it: that is the thread's
 * "active" context. Making a context current activates it; eglBindAPI
 * activates the newly bound API's context if the thread has one.
 *
 * Every surface has an OSMesaBuffer: its memory as the colour buffer,
 * with depth and stencil buffers of its own (from its config). A surface
 * can be bound to contexts of one thread only, as EGL says.
 */

/* ------------------------------------------------------------------ */
/* Buffers                                                             */

static GLenum osmesa_format(int layout)
{
    return layout == LAYOUT_TRGB ? OSMESA_BGRA : OSMESA_RGBA;
}

/* The surface's OSMesaBuffer, made the first time and pointed at the
   surface's current memory and size. */
static OSMesaBuffer surface_buffer(egl_surface *s)
{
    if (!s->buf) {
        s->buf = OSMesaCreateBuffer(osmesa_format(s->cfg->layout), s->cfg->depth,
                                    s->cfg->stencil);
        if (!s->buf)
            return NULL;
    }
    /* rows run top down; the row length is the surface's stride */
    if (!OSMesaBufferStorage(s->buf, s->pixels, s->w, s->h, s->stride, GL_FALSE))
        return NULL;
    return s->buf;
}

/* EGL_KHR_surfaceless_context: OSMesa always needs a buffer, so a context
   made current without a surface draws into a 1x1 stand-in (framebuffer
   objects are what such a context is for). */
static OSMesaBuffer standin_buffer(int layout)
{
    static OSMesaBuffer standin[2];
    static unsigned int pixel[2];
    if (!standin[layout]) {
        standin[layout] = OSMesaCreateBuffer(osmesa_format(layout), 0, 0);
        if (standin[layout])
            OSMesaBufferStorage(standin[layout], &pixel[layout], 1, 1, 1, GL_FALSE);
    }
    return standin[layout];
}

/* ------------------------------------------------------------------ */
/* Current contexts                                                    */

/* The calling thread's current context for the bound API */
static egl_context *cur_context(void)
{
    egl_thread *t = thr();
    return t->ctx[API_SLOT(t->api)];
}

/* Make c the context OSMesa renders with in thread t, bound to its draw
   and read surfaces (or the stand-in); NULL: none. */
static int activate(egl_thread *t, egl_context *c)
{
    OSMesaBuffer draw, read;

    if (!c) {
        if (t->active)
            OSMesaMakeCurrent(NULL, NULL, 0, 0, 0);
        t->active = NULL;
        return 1;
    }
    draw = c->draw ? surface_buffer(c->draw) : standin_buffer(c->cfg->layout);
    read = c->read ? surface_buffer(c->read) : standin_buffer(c->cfg->layout);
    if (!draw || !read || !OSMesaMakeCurrentBuffers(c->om, draw, read)) {
        activate(t, NULL);
        return 0;
    }
    t->active = c;
    return 1;
}

/* A surface's memory or size changed: rebind it if the active context uses
   it (other contexts pick it up when they are next activated). */
static int surface_changed(egl_surface *s)
{
    egl_thread *t = thr();
    if (t->active && (t->active->draw == s || t->active->read == s))
        return activate(t, t->active);
    return 1;
}

/* Drop a context's hold on its surfaces; destroy those waiting for it */
static void unbind_surfaces(egl_display *d, egl_context *c)
{
    egl_surface *s[2];
    int i;
    s[0] = c->draw;
    s[1] = c->read;
    c->draw = c->read = NULL;
    for (i = 0; i < 2; i++) {
        if (s[i] && --s[i]->bound == 0) {
            s[i]->thread = NULL;
            if (s[i]->destroy_pending)
                unlink_surface(d, s[i]);
        }
    }
}

/* Thread t's context for one API stops being current */
static void release_slot(egl_display *d, egl_thread *t, int slot)
{
    egl_context *c = t->ctx[slot];
    if (!c)
        return;
    /* EGL_KHR_context_flush_control */
    if (c == t->active && c->release_flush)
        glFlush();
    t->ctx[slot] = NULL;
    c->thread = NULL;
    if (t->active == c && !activate(t, t->ctx[1 - slot]))
        activate(t, NULL);
    unbind_surfaces(d, c);
    if (c->destroy_pending)
        unlink_context(d, c);
}

/* eglMakeCurrent's work, once everything has been checked */
static int make_current(egl_display *d, egl_thread *t, egl_context *c,
                        egl_surface *draw, egl_surface *read)
{
    const int slot = API_SLOT(c->api);

    if (t->ctx[slot] && t->ctx[slot] != c)
        release_slot(d, t, slot);
    else if (t->ctx[slot] == c)
        unbind_surfaces(d, c);
    c->draw = draw;
    c->read = read;
    if (draw) {
        draw->bound++;
        draw->thread = t;
    }
    if (read) {
        read->bound++;
        read->thread = t;
    }
    c->thread = t;
    t->ctx[slot] = c;
    return activate(t, c);
}
