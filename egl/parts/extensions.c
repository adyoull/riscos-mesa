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
    if (s != cur_surf || !cur_ctx || s->kind != SURF_WINDOW)
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
    if (s->locked || s->current)
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
   it's made (after glFinish). There is one thread, so nothing can signal
   a reusable sync while eglClientWaitSyncKHR waits for it: waiting on an
   unsignalled one returns EGL_TIMEOUT_EXPIRED_KHR at once. */

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
    if (type == EGL_SYNC_FENCE_KHR && !cur_ctx) {
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
    if ((flags & EGL_SYNC_FLUSH_COMMANDS_BIT_KHR) && cur_ctx)
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
    if (!cur_ctx)
        return fail(EGL_BAD_MATCH);
    if (flags != 0)
        return fail(EGL_BAD_PARAMETER);
    return ok();                        /* GL runs in order on the CPU anyway */
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
    return create_window_surface(dpy, config, *(const EGLNativeWindowType *) native_window,
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
        thread_label = label;
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
    r.r[1] = (int) block;
    if (_kernel_swi(Wimp_RedrawWindow, &r, &r) != NULL)
        return fail(EGL_BAD_NATIVE_WINDOW);
    plot_loop(d, block[0], NULL, block, r.r[0]);
    return ok();
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
    F(eglClientWaitSyncKHR), F(eglCreatePlatformPixmapSurfaceEXT),
    F(eglCreatePlatformWindowSurfaceEXT), F(eglCreateSyncKHR),
    F(eglDebugMessageControlKHR), F(eglDestroySyncKHR), F(eglGetPlatformDisplayEXT),
    F(eglGetSyncAttribKHR), F(eglLabelObjectKHR), F(eglLockSurfaceKHR),
    F(eglQueryDebugKHR), F(eglQuerySurface64KHR), F(eglSetDamageRegionKHR),
    F(eglSignalSyncKHR), F(eglSwapBuffersWithDamageEXT), F(eglSwapBuffersWithDamageKHR),
    F(eglUnlockSurfaceKHR), F(eglWaitSyncKHR),
    F(eglRedrawWindowRISCOS), F(eglPlotSurfaceRISCOS),
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
