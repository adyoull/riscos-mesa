/*
 * egl/parts/extensions.c - the extensions: partial update, lock surface, sync objects, platform, debug, EGL_RISCOS_wimp_window, eglGetProcAddress.
 * Part of egl_riscos.c: it is compiled by being included from there, as
 * one unit with the other parts (so their helpers stay private to the
 * library), not on its own. MIT licence (see LICENSE).
 */

/* ------------------------------------------------------------------ */
/* EGL_KHR_partial_update                                              */

EGLAPI EGLBoolean EGLAPIENTRY eglSetDamageRegionKHR(EGLDisplay dpy, EGLSurface surface,
                                                    EGLint *rects, EGLint n_rects)
{
    egl_display *d;
    egl_surface *s;
    int i;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!cur_context() || cur_context()->draw != s || s->kind != SURF_WINDOW ||
        s->swap_behavior == EGL_BUFFER_PRESERVED)
        return fail(EGL_BAD_MATCH);
    if (n_rects < 0 || (n_rects > 0 && !rects))
        return fail(EGL_BAD_PARAMETER);
    if (!s->age_queried || s->n_damage >= 0)
        return fail(EGL_BAD_ACCESS);    /* age not asked for, or already set */
    if (n_rects == 0) {
        s->n_damage = 0;                /* whole surface */
        return ok();
    }
    if (n_rects > MAX_DAMAGE) {
        /* keep their bounding box */
        int x0 = rects[0], y0 = rects[1];
        int x1 = x0 + rects[2], y1 = y0 + rects[3];
        for (i = 1; i < n_rects; i++) {
            if (rects[i * 4] < x0) x0 = rects[i * 4];
            if (rects[i * 4 + 1] < y0) y0 = rects[i * 4 + 1];
            if (rects[i * 4] + rects[i * 4 + 2] > x1) x1 = rects[i * 4] + rects[i * 4 + 2];
            if (rects[i * 4 + 1] + rects[i * 4 + 3] > y1) y1 = rects[i * 4 + 1] + rects[i * 4 + 3];
        }
        s->damage[0] = x0; s->damage[1] = y0;
        s->damage[2] = x1 - x0; s->damage[3] = y1 - y0;
        s->n_damage = 1;
        return ok();
    }
    memcpy(s->damage, rects, n_rects * 4 * sizeof rects[0]);
    s->n_damage = n_rects;
    return ok();
}

/* ------------------------------------------------------------------ */
/* EGL_KHR_lock_surface3                                               */

EGLAPI EGLBoolean EGLAPIENTRY eglLockSurfaceKHR(EGLDisplay dpy, EGLSurface surface,
                                                const EGLint *attrib_list)
{
    egl_display *d;
    egl_surface *s;
    int i;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLint a = attrib_list[i], v = attrib_list[i + 1];
        if (a == EGL_MAP_PRESERVE_PIXELS_KHR)
            continue;                   /* contents are always kept */
        if (a == EGL_LOCK_USAGE_HINT_KHR &&
            !(v & ~(EGL_READ_SURFACE_BIT_KHR | EGL_WRITE_SURFACE_BIT_KHR)))
            continue;
        return fail(EGL_BAD_ATTRIBUTE);
    }
    if (s->locked || s->bound)
        return fail(EGL_BAD_ACCESS);
    if (s->kind == SURF_WINDOW) {
        screen_info scr;
        read_screen(&scr);              /* the window may have changed size */
        if (update_window_buffer(s, &scr) < 0)
            return EGL_FALSE;
    }
    s->locked = 1;
    s->ever_locked = 1;
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglUnlockSurfaceKHR(EGLDisplay dpy, EGLSurface surface)
{
    egl_display *d;
    egl_surface *s;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!s->locked)
        return fail(EGL_BAD_ACCESS);
    s->locked = 0;
    return ok();
}

/* ------------------------------------------------------------------ */
/* EGL_KHR_fence_sync, EGL_KHR_reusable_sync, EGL_KHR_wait_sync         */
/* Rendering is done by the CPU, in order: a fence is signalled as soon as
   it's made (after glFinish). eglClientWaitSyncKHR doesn't wait: on an
   unsignalled reusable sync it returns EGL_TIMEOUT_EXPIRED_KHR at once,
   whatever the timeout. (Another thread could signal it meanwhile; a
   program that relies on that has to poll. Blocking would also stop the
   whole desktop, which is cooperatively multitasked.) */

static egl_sync *get_sync(egl_display *d, EGLSyncKHR sync)
{
    egl_sync *y;
    for (y = d->syncs; y; y = y->next)
        if (y == (egl_sync *) sync) {
            egl_obj_label = y->label;
            return y;
        }
    fail(EGL_BAD_PARAMETER);
    return NULL;
}

EGLAPI EGLSyncKHR EGLAPIENTRY eglCreateSyncKHR(EGLDisplay dpy, EGLenum type,
                                               const EGLint *attrib_list)
{
    egl_display *d;
    egl_sync *y;

    ENTER();
    if (!(d = get_display(dpy, 1)))
        return EGL_NO_SYNC_KHR;
    if (attrib_list && attrib_list[0] != EGL_NONE) {
        fail(EGL_BAD_ATTRIBUTE);
        return EGL_NO_SYNC_KHR;
    }
    if (type != EGL_SYNC_FENCE_KHR && type != EGL_SYNC_REUSABLE_KHR) {
        fail(EGL_BAD_ATTRIBUTE);
        return EGL_NO_SYNC_KHR;
    }
    if (type == EGL_SYNC_FENCE_KHR && !cur_context()) {
        fail(EGL_BAD_MATCH);
        return EGL_NO_SYNC_KHR;
    }
    y = (egl_sync *) calloc(1, sizeof *y);
    if (!y) {
        fail(EGL_BAD_ALLOC);
        return EGL_NO_SYNC_KHR;
    }
    y->magic = MAGIC_SYNC;
    y->type = type;
    if (type == EGL_SYNC_FENCE_KHR) {
        glFinish();
        y->status = EGL_SIGNALED_KHR;
    } else {
        y->status = EGL_UNSIGNALED_KHR;
    }
    y->next = d->syncs;
    d->syncs = y;
    ok();
    return (EGLSyncKHR) y;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySyncKHR(EGLDisplay dpy, EGLSyncKHR sync)
{
    egl_display *d;
    egl_sync *y, **p;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(y = get_sync(d, sync)))
        return EGL_FALSE;
    for (p = &d->syncs; *p; p = &(*p)->next)
        if (*p == y) {
            *p = y->next;
            break;
        }
    y->magic = 0;
    free(y);
    return ok();
}

EGLAPI EGLint EGLAPIENTRY eglClientWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync,
                                               EGLint flags, EGLTimeKHR timeout)
{
    egl_display *d;
    egl_sync *y;
    (void) timeout;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(y = get_sync(d, sync)))
        return EGL_FALSE;
    if ((flags & EGL_SYNC_FLUSH_COMMANDS_BIT_KHR) && thr()->active)
        glFlush();
    ok();
    return y->status == EGL_SIGNALED_KHR ? EGL_CONDITION_SATISFIED_KHR : EGL_TIMEOUT_EXPIRED_KHR;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetSyncAttribKHR(EGLDisplay dpy, EGLSyncKHR sync,
                                                  EGLint attribute, EGLint *value)
{
    egl_display *d;
    egl_sync *y;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(y = get_sync(d, sync)))
        return EGL_FALSE;
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    switch (attribute) {
    case EGL_SYNC_TYPE_KHR:   *value = y->type; break;
    case EGL_SYNC_STATUS_KHR: *value = y->status; break;
    case EGL_SYNC_CONDITION_KHR:
        if (y->type != EGL_SYNC_FENCE_KHR)
            return fail(EGL_BAD_ATTRIBUTE);
        *value = EGL_SYNC_PRIOR_COMMANDS_COMPLETE_KHR;
        break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglSignalSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLenum mode)
{
    egl_display *d;
    egl_sync *y;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(y = get_sync(d, sync)))
        return EGL_FALSE;
    if (y->type != EGL_SYNC_REUSABLE_KHR)
        return fail(EGL_BAD_MATCH);
    if (mode != EGL_SIGNALED_KHR && mode != EGL_UNSIGNALED_KHR)
        return fail(EGL_BAD_PARAMETER);
    y->status = mode;
    return ok();
}

EGLAPI EGLint EGLAPIENTRY eglWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR sync, EGLint flags)
{
    egl_display *d;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !get_sync(d, sync))
        return EGL_FALSE;
    if (!cur_context())
        return fail(EGL_BAD_MATCH);
    if (flags != 0)
        return fail(EGL_BAD_PARAMETER);
    return ok();                        /* GL runs in order on the CPU anyway */
}

/* ------------------------------------------------------------------ */
/* EGL_KHR_image_base, EGL_KHR_image_pixmap                            */
/* An image is a 32bpp sprite (the same native pixmaps as pixmap
   surfaces). It holds no copy: a GL texture made from it with
   glEGLImageTargetTexture2DOES (GL_OES_EGL_image) uses the sprite's pixels
   in place, so whatever the program writes into the sprite shows at the
   next draw. Mesa asks this library what an image handle is through
   image_lookup, registered with OSMesaSetImageLookup by eglInitialize.
   The sprite must stay in memory while a texture uses it, even after the
   image is destroyed. */

static egl_image *get_image(egl_display *d, EGLImageKHR image)
{
    egl_image *m;
    for (m = d->images; m; m = m->next)
        if (m == (egl_image *) image) {
            egl_obj_label = m->label;
            return m;
        }
    fail(EGL_BAD_PARAMETER);
    return NULL;
}

/* OSMesaImageLookupFunc: called by Mesa from glEGLImageTargetTexture2DOES,
   on the GL thread, so it takes the lock: another thread may be creating or
   destroying images at the same time. */
static GLboolean image_lookup(void *image, OSMesaImage *desc)
{
    egl_image *m;
    LOCK();
    if (!display.initialised)
        return GL_FALSE;
    for (m = display.images; m; m = m->next)
        if (m == (egl_image *) image) {
            desc->pixels = m->pixels;
            desc->width = m->w;
            desc->height = m->h;
            desc->row_bytes = m->w * 4;
            desc->format = m->layout == LAYOUT_TRGB ? OSMESA_BGRA : OSMESA_RGBA;
            return GL_TRUE;
        }
    return GL_FALSE;
}

EGLAPI EGLImageKHR EGLAPIENTRY eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target,
                                                 EGLClientBuffer buffer, const EGLint *attrib_list)
{
    egl_display *d;
    egl_image *m;
    int i, w, h, layout;
    void *pixels;

    ENTER();
    if (!(d = get_display(dpy, 1)))
        return EGL_NO_IMAGE_KHR;
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2)
        if (attrib_list[i] != EGL_IMAGE_PRESERVED_KHR ||
            (attrib_list[i + 1] != EGL_TRUE && attrib_list[i + 1] != EGL_FALSE)) {
            fail(EGL_BAD_PARAMETER);
            return EGL_NO_IMAGE_KHR;
        }
    /* EGL_KHR_image_pixmap: sprites only, and no context */
    if (target != EGL_NATIVE_PIXMAP_KHR || ctx != EGL_NO_CONTEXT ||
        !pixmap_info((void *) buffer, &w, &h, &layout, &pixels)) {
        fail(EGL_BAD_PARAMETER);
        return EGL_NO_IMAGE_KHR;
    }
    for (m = d->images; m; m = m->next)
        if (m->sprite == (const void *) buffer) {
            fail(EGL_BAD_ACCESS);       /* already an image */
            return EGL_NO_IMAGE_KHR;
        }
    m = (egl_image *) calloc(1, sizeof *m);
    if (!m) {
        fail(EGL_BAD_ALLOC);
        return EGL_NO_IMAGE_KHR;
    }
    m->magic = MAGIC_IMAGE;
    m->sprite = (const void *) buffer;
    m->pixels = pixels;
    m->w = w;
    m->h = h;
    m->layout = layout;
    m->next = d->images;
    d->images = m;
    ok();
    return (EGLImageKHR) m;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR image)
{
    egl_display *d;
    egl_image *m, **p;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(m = get_image(d, image)))
        return EGL_FALSE;
    for (p = &d->images; *p != m; p = &(*p)->next)
        ;
    *p = m->next;
    m->magic = 0;
    free(m);
    return ok();
}

static void destroy_images(egl_display *d)
{
    egl_image *m, *mn;
    for (m = d->images; m; m = mn) {
        mn = m->next;
        m->magic = 0;
        free(m);
    }
    d->images = NULL;
}

/* ------------------------------------------------------------------ */
/* EGL_EXT_platform_base with EGL_RISCOS_platform_wimp                 */

EGLAPI EGLDisplay EGLAPIENTRY eglGetPlatformDisplayEXT(EGLenum platform, void *native_display,
                                                       const EGLint *attrib_list)
{
    ENTER();
    if (platform != EGL_PLATFORM_RISCOS) {
        fail(EGL_BAD_PARAMETER);
        return EGL_NO_DISPLAY;
    }
    if (attrib_list && attrib_list[0] != EGL_NONE) {
        fail(EGL_BAD_ATTRIBUTE);
        return EGL_NO_DISPLAY;
    }
    if (native_display != NULL) {
        fail(EGL_BAD_PARAMETER);        /* only the default display (the screen) */
        return EGL_NO_DISPLAY;
    }
    ok();
    return (EGLDisplay) &display;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformWindowSurfaceEXT(EGLDisplay dpy, EGLConfig config,
                                                                void *native_window,
                                                                const EGLint *attrib_list)
{
    ENTER();
    /* native_window points to the Wimp window handle (or to -1) */
    if (!native_window) {
        egl_display *d = get_display(dpy, 1);
        if (d && get_config(d, config))
            fail(EGL_BAD_NATIVE_WINDOW);
        return EGL_NO_SURFACE;
    }
    /* an int (EGLNativeWindowType is an int on RISC OS, but not in every
       build of the headers: read exactly the int the extension documents) */
    return create_window_surface(dpy, config,
                                 (EGLNativeWindowType) (long) *(const int *) native_window,
                                 attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePlatformPixmapSurfaceEXT(EGLDisplay dpy, EGLConfig config,
                                                                void *native_pixmap,
                                                                const EGLint *attrib_list)
{
    ENTER();
    /* native_pixmap is the sprite header pointer itself */
    return create_pixmap_surface(dpy, config, native_pixmap, attrib_list);
}

/* ------------------------------------------------------------------ */
/* EGL_KHR_debug                                                       */

EGLAPI EGLint EGLAPIENTRY eglDebugMessageControlKHR(EGLDEBUGPROCKHR callback,
                                                    const EGLAttrib *attrib_list)
{
    int i, en[4];
    ENTER();
    memcpy(en, debug_enabled, sizeof en);
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLAttrib a = attrib_list[i];
        if (a < EGL_DEBUG_MSG_CRITICAL_KHR || a > EGL_DEBUG_MSG_INFO_KHR) {
            fail(EGL_BAD_ATTRIBUTE);
            return EGL_BAD_ATTRIBUTE;
        }
        en[a - EGL_DEBUG_MSG_CRITICAL_KHR] = attrib_list[i + 1] != EGL_FALSE;
    }
    memcpy(debug_enabled, en, sizeof en);
    debug_callback = callback;
    ok();
    return EGL_SUCCESS;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryDebugKHR(EGLint attribute, EGLAttrib *value)
{
    ENTER();
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    if (attribute >= EGL_DEBUG_MSG_CRITICAL_KHR && attribute <= EGL_DEBUG_MSG_INFO_KHR)
        *value = debug_enabled[attribute - EGL_DEBUG_MSG_CRITICAL_KHR] ? EGL_TRUE : EGL_FALSE;
    else if (attribute == EGL_DEBUG_CALLBACK_KHR)
        *value = (EGLAttrib) debug_callback;
    else
        return fail(EGL_BAD_ATTRIBUTE);
    return ok();
}

EGLAPI EGLint EGLAPIENTRY eglLabelObjectKHR(EGLDisplay dpy, EGLenum objectType,
                                            EGLObjectKHR object, EGLLabelKHR label)
{
    egl_display *d;
    ENTER();
    if (objectType == EGL_OBJECT_THREAD_KHR) {
        thr()->label = label;
        ok();
        return EGL_SUCCESS;
    }
    if (!(d = get_display(dpy, 0)))
        return EGL_BAD_DISPLAY;
    switch (objectType) {
    case EGL_OBJECT_DISPLAY_KHR:
        if (object != (EGLObjectKHR) dpy)
            break;
        d->label = label;
        ok();
        return EGL_SUCCESS;
    case EGL_OBJECT_CONTEXT_KHR:
    case EGL_OBJECT_SURFACE_KHR:
    case EGL_OBJECT_SYNC_KHR:
    case EGL_OBJECT_IMAGE_KHR:
        if (!d->initialised) {
            fail(EGL_NOT_INITIALIZED);
            return EGL_NOT_INITIALIZED;
        }
        if (objectType == EGL_OBJECT_CONTEXT_KHR) {
            egl_context *c = get_context(d, (EGLContext) object);
            if (!c) break;
            c->label = label;
        } else if (objectType == EGL_OBJECT_SURFACE_KHR) {
            egl_surface *s = get_surface(d, (EGLSurface) object);
            if (!s) break;
            s->label = label;
        } else if (objectType == EGL_OBJECT_IMAGE_KHR) {
            egl_image *m = get_image(d, (EGLImageKHR) object);
            if (!m) break;
            m->label = label;
        } else {
            egl_sync *y = get_sync(d, (EGLSyncKHR) object);
            if (!y) break;
            y->label = label;
        }
        ok();
        return EGL_SUCCESS;
    }
    fail(EGL_BAD_PARAMETER);
    return EGL_BAD_PARAMETER;
}

/* ------------------------------------------------------------------ */
/* EGL_RISCOS_wimp_window                                              */

EGLAPI EGLBoolean EGLAPIENTRY eglRedrawWindowRISCOS(EGLDisplay dpy, int *block)
{
    egl_display *d;
    egl_surface *s;
    _kernel_swi_regs r;

    ENTER();
    if (!(d = get_display(dpy, 1)))
        return EGL_FALSE;
    if (!block)
        return fail(EGL_BAD_PARAMETER);
    for (s = d->surfaces; s; s = s->next)
        if (s->kind == SURF_WINDOW && s->handle == block[0] && !s->destroy_pending)
            break;
    if (!s) {
        /* DispmanX compatibility in window mode: the window shows elements. */
        riscos_dmx_placement pl;
        screen_info scr;
        for (s = d->surfaces; s; s = s->next) {
            memset(&pl, 0, sizeof pl);
            if (s->kind == SURF_WINDOW && s->dmx && !s->destroy_pending &&
                __riscos_dispmanx_placement && __riscos_dispmanx_placement(s->dmx, &pl) &&
                pl.window == block[0])
                break;
        }
        if (!s)
            return fail(EGL_BAD_NATIVE_WINDOW);
        r.r[1] = (int) block;
        if (_kernel_swi(Wimp_RedrawWindow, &r, &r) != NULL)
            return fail(EGL_BAD_NATIVE_WINDOW);
        read_screen(&scr);
        if (pl.visible && s->sprite)
            dmx_window_loop(s, &scr, &pl, block, r.r[0]);
        else
            while (r.r[0]) { r.r[1] = (int) block; _kernel_swi(Wimp_GetRectangle, &r, &r); }
        return ok();
    }
    ovl_check_all(d);                   /* overlays: something may cover them now */
    r.r[1] = (int) block;
    if (_kernel_swi(Wimp_RedrawWindow, &r, &r) != NULL)
        return fail(EGL_BAD_NATIVE_WINDOW);
    plot_loop(d, block[0], NULL, block, r.r[0]);
    return ok();
}

/* EGL_RISCOS_overlay: a program that stops calling eglSwapBuffers (a
   paused video) calls this on null events, so its overlays still hide
   when a window or menu covers them and come back afterwards. */
EGLAPI EGLBoolean EGLAPIENTRY eglCheckOverlaysRISCOS(EGLDisplay dpy)
{
    egl_display *d;
    ENTER();
    if (!(d = get_display(dpy, 1)))
        return EGL_FALSE;
    ovl_check_all(d);
    return ok();
}

/* EGL_RISCOS_overlay: would eglSwapBuffers on this surface block right now
   waiting for a vsync that a later swap wouldn't need? True for a surface
   shown through an overlay, or a full screen surface with screen banks,
   when fewer than its swap interval of vsyncs have passed since the last
   switch. A program with other work (decoding the next video frame) does
   that and swaps on its next pass instead (riscos-ffmpeg's Reel found the
   blocking wait cost a 60 fps player most of a frame). Always false where
   waiting is part of showing the frame (full screen single buffer: the
   plot is timed to the vsync) or where there is no wait (a plotted
   window, swap interval 0). */
EGLAPI EGLBoolean EGLAPIENTRY eglSwapWouldWaitRISCOS(EGLDisplay dpy, EGLSurface surface)
{
    egl_display *d;
    egl_surface *s;
    int wait = 0;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (s->kind == SURF_WINDOW && s->swap_interval > 0) {
        if (s->handle == -1 && s->banks)
            wait = s->bank_vsync >= 0 && vsyncs_since(s->bank_vsync) < s->swap_interval;
        else if (s->handle >= 0 && s->ovl_shown)
            wait = vsync_counter() == s->ovl_vsync;
    }
    ok();
    return wait ? EGL_TRUE : EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglPlotSurfaceRISCOS(EGLDisplay dpy, EGLSurface surface,
                                                   const int *block)
{
    egl_display *d;
    egl_surface *s;
    screen_info scr;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!block)
        return fail(EGL_BAD_PARAMETER);
    if (s->kind != SURF_WINDOW || s->handle < 0)
        return fail(EGL_BAD_SURFACE);
    read_screen(&scr);
    if (s->ovl_shown) {
        ovl_redraw_rectangle(s, (int *) block);   /* shown through its overlay */
        return ok();
    }
    {
        os_rect clip;
        clip.x0 = block[7]; clip.y0 = block[8]; clip.x1 = block[9]; clip.y1 = block[10];
        plot_rectangle(s, block, &scr, &clip);
    }
    return ok();
}

/* ------------------------------------------------------------------ */
/* eglGetProcAddress                                                   */

#define F(name) { #name, (__eglMustCastToProperFunctionPointerType) name }
static const struct {
    const char *name;
    __eglMustCastToProperFunctionPointerType func;
} egl_functions[] = {
    F(eglBindAPI), F(eglBindTexImage), F(eglChooseConfig), F(eglCopyBuffers),
    F(eglCreateContext), F(eglCreatePbufferFromClientBuffer),
    F(eglCreatePbufferSurface), F(eglCreatePixmapSurface),
    F(eglCreateWindowSurface), F(eglDestroyContext), F(eglDestroySurface),
    F(eglGetConfigAttrib), F(eglGetConfigs), F(eglGetCurrentContext),
    F(eglGetCurrentDisplay), F(eglGetCurrentSurface), F(eglGetDisplay),
    F(eglGetError), F(eglGetProcAddress), F(eglInitialize), F(eglMakeCurrent),
    F(eglQueryAPI), F(eglQueryContext), F(eglQueryString), F(eglQuerySurface),
    F(eglReleaseTexImage), F(eglReleaseThread), F(eglSurfaceAttrib),
    F(eglSwapBuffers), F(eglSwapInterval), F(eglTerminate), F(eglWaitClient),
    F(eglWaitGL), F(eglWaitNative),
    /* extensions */
    F(eglClientWaitSyncKHR), F(eglCreateImageKHR), F(eglCreatePlatformPixmapSurfaceEXT),
    F(eglCreatePlatformWindowSurfaceEXT), F(eglCreateSyncKHR),
    F(eglDebugMessageControlKHR), F(eglDestroyImageKHR), F(eglDestroySyncKHR),
    F(eglGetPlatformDisplayEXT),
    F(eglGetSyncAttribKHR), F(eglLabelObjectKHR), F(eglLockSurfaceKHR),
    F(eglQueryDebugKHR), F(eglQuerySurface64KHR), F(eglSetDamageRegionKHR),
    F(eglSignalSyncKHR), F(eglSwapBuffersWithDamageEXT), F(eglSwapBuffersWithDamageKHR),
    F(eglUnlockSurfaceKHR), F(eglWaitSyncKHR),
    F(eglRedrawWindowRISCOS), F(eglPlotSurfaceRISCOS), F(eglCheckOverlaysRISCOS),
    F(eglSwapWouldWaitRISCOS),
};
#undef F

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY eglGetProcAddress(const char *procname)
{
    size_t i;
    ENTER();
    if (!procname)
        return NULL;
    if (procname[0] == 'e' && procname[1] == 'g' && procname[2] == 'l') {
        for (i = 0; i < sizeof egl_functions / sizeof egl_functions[0]; i++)
            if (strcmp(procname, egl_functions[i].name) == 0)
                return egl_functions[i].func;
        return NULL;
    }
    return (__eglMustCastToProperFunctionPointerType) OSMesaGetProcAddress(procname);
}
