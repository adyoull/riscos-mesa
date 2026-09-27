/*
 * egl/parts/api.c - the EGL 1.4 entry points.
 * Part of egl_riscos.c: it is compiled by being included from there, as
 * one unit with the other parts (so their helpers stay private to the
 * library), not on its own. MIT licence (see LICENSE).
 */

/* ------------------------------------------------------------------ */
/* EGL 1.4 API                                                         */

EGLAPI EGLint EGLAPIENTRY eglGetError(void)
{
    EGLint e = last_error;
    last_error = EGL_SUCCESS;
    return e;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetDisplay(EGLNativeDisplayType display_id)
{
    ENTER();
    if (display_id != EGL_DEFAULT_DISPLAY)
        return EGL_NO_DISPLAY;
    return (EGLDisplay) &display;
}

EGLAPI EGLBoolean EGLAPIENTRY eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    egl_display *d;
    ENTER();
    if (!(d = get_display(dpy, 0)))
        return EGL_FALSE;
    if (!d->initialised) {
        make_configs(d);
        d->initialised = 1;
        OSMesaSetImageLookup(image_lookup);     /* GL_OES_EGL_image */
    }
    if (major) *major = 1;
    if (minor) *minor = 4;
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglTerminate(EGLDisplay dpy)
{
    egl_display *d;
    egl_surface *s, *sn;
    egl_context *c, *cn;
    egl_sync *y, *yn;

    ENTER();
    if (!(d = get_display(dpy, 0)))
        return EGL_FALSE;
    for (s = d->surfaces; s; s = sn) {
        sn = s->next;
        if (s->current) s->destroy_pending = 1;
        else unlink_surface(d, s);
    }
    for (c = d->contexts; c; c = cn) {
        cn = c->next;
        if (c->current) c->destroy_pending = 1;
        else unlink_context(d, c);
    }
    for (y = d->syncs; y; y = yn) {
        yn = y->next;
        y->magic = 0;
        free(y);
    }
    d->syncs = NULL;
    destroy_images(d);
    d->initialised = 0;
    return ok();
}

EGLAPI const char *EGLAPIENTRY eglQueryString(EGLDisplay dpy, EGLint name)
{
    ENTER();
    if (dpy == EGL_NO_DISPLAY && name == EGL_EXTENSIONS) {
        last_error = EGL_SUCCESS;       /* EGL_EXT_client_extensions */
        return EGL_RISCOS_CLIENT_EXTENSIONS;
    }
    if (!get_display(dpy, 1))
        return NULL;
    last_error = EGL_SUCCESS;
    switch (name) {
    case EGL_VENDOR:      return EGL_RISCOS_VENDOR;
    case EGL_VERSION:     return EGL_RISCOS_VERSION;
    case EGL_EXTENSIONS:  return EGL_RISCOS_EXTENSIONS;
    case EGL_CLIENT_APIS: return "OpenGL OpenGL_ES";
    }
    fail(EGL_BAD_PARAMETER);
    return NULL;
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigs(EGLDisplay dpy, EGLConfig *configs,
                                            EGLint config_size, EGLint *num_config)
{
    egl_display *d;
    int i, n;

    ENTER();
    if (!(d = get_display(dpy, 1)))
        return EGL_FALSE;
    if (!num_config)
        return fail(EGL_BAD_PARAMETER);
    n = d->nconfigs;
    if (configs) {
        if (n > config_size) n = config_size;
        if (n < 0) n = 0;
        for (i = 0; i < n; i++)
            configs[i] = (EGLConfig) &d->configs[i];
    }
    *num_config = n;
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list,
                                              EGLConfig *configs, EGLint config_size,
                                              EGLint *num_config)
{
    egl_display *d;
    EGLint want[NCRITERIA];
    const egl_config *match[8];
    int i, j, n = 0, by_id = 0;
    EGLint want_id = 0;
    int pixmap_layout = -1;

    ENTER();
    if (!(d = get_display(dpy, 1)))
        return EGL_FALSE;
    if (!num_config)
        return fail(EGL_BAD_PARAMETER);

    for (j = 0; j < NCRITERIA; j++)
        want[j] = criteria[j].def;
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLint a = attrib_list[i], v = attrib_list[i + 1];
        if (a == EGL_MATCH_NATIVE_PIXMAP) {
            int w, h;
            void *px;
            if (v == EGL_NONE || !pixmap_info((void *) v, &w, &h, &pixmap_layout, &px))
                return fail(EGL_BAD_NATIVE_PIXMAP);
            continue;
        }
        for (j = 0; j < NCRITERIA; j++)
            if (criteria[j].attrib == a)
                break;
        if (j == NCRITERIA)
            return fail(EGL_BAD_ATTRIBUTE);
        want[j] = v;
        if (a == EGL_CONFIG_ID && v != EGL_DONT_CARE) {
            by_id = 1;
            want_id = v;
        }
    }

    for (i = 0; i < d->nconfigs; i++) {
        const egl_config *c = &d->configs[i];
        int good = 1;
        if (by_id) {
            good = (c->id == want_id);          /* all else ignored */
        } else {
            for (j = 0; j < NCRITERIA && good; j++) {
                EGLint have;
                if (criteria[j].match == M_IGNORE || want[j] == EGL_DONT_CARE)
                    continue;
                config_attrib(c, criteria[j].attrib, &have);
                switch (criteria[j].match) {
                case M_ATLEAST: good = have >= want[j]; break;
                case M_EXACT:   good = have == want[j]; break;
                case M_MASK:    good = (have & want[j]) == want[j]; break;
                case M_FORMAT:  /* RGBA_8888 also matches the EXACT ones */
                    good = have == want[j] ||
                           (want[j] == EGL_FORMAT_RGBA_8888_KHR &&
                            have == EGL_FORMAT_RGBA_8888_EXACT_KHR);
                    break;
                }
            }
            if (pixmap_layout >= 0 && c->layout != pixmap_layout)
                good = 0;
        }
        if (good)
            match[n++] = c;
    }
    qsort(match, n, sizeof match[0], config_compare);
    if (configs) {
        if (n > config_size) n = config_size;
        if (n < 0) n = 0;
        for (i = 0; i < n; i++)
            configs[i] = (EGLConfig) match[i];
    }
    *num_config = n;
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                                 EGLint attribute, EGLint *value)
{
    egl_display *d;
    const egl_config *c;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(c = get_config(d, config)))
        return EGL_FALSE;
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    if (!config_attrib(c, attribute, value))
        return fail(EGL_BAD_ATTRIBUTE);
    return ok();
}

static egl_surface *new_surface(egl_display *d, const egl_config *c, int kind)
{
    egl_surface *s = (egl_surface *) calloc(1, sizeof *s);
    (void) d;
    if (!s) {
        fail(EGL_BAD_ALLOC);
        return NULL;
    }
    s->magic = MAGIC_SURFACE;
    s->kind = kind;
    s->cfg = c;
    s->render_buffer = EGL_BACK_BUFFER;
    s->swap_behavior = EGL_BUFFER_PRESERVED;
    s->swap_interval = 1;
    s->handle = 0;
    s->n_damage = -1;
    return s;
}

/* Surfaces are kept in creation order: for work area surfaces in the same
   window that is their stacking order (later ones on top). */
static void add_surface(egl_display *d, egl_surface *s)
{
    egl_surface **p = &d->surfaces;
    while (*p)
        p = &(*p)->next;
    s->next = NULL;
    *p = s;
}

static EGLSurface create_window_surface(EGLDisplay dpy, EGLConfig config,
                                        EGLNativeWindowType win, const EGLint *attrib_list)
{
    egl_display *d;
    const egl_config *c;
    egl_surface *s;
    screen_info scr;
    window_state ws;
    int i, have_w = 0, have_h = 0, dmx = 0, dmx_w = 0, dmx_h = 0;

    if (!(d = get_display(dpy, 1)) || !(c = get_config(d, config)))
        return EGL_NO_SURFACE;
    /* The RISC OS native types come first: -1 (the screen) or a Wimp window
       handle. Only something that is neither is tried as a DispmanX window
       (a pointer to an EGL_DISPMANX_WINDOW_T, when libbcm_host is linked):
       the compatibility layer lets Raspberry Pi programs run, it doesn't
       change the native calls. */
    if (win != -1 && (win == 0 || !get_window_state(win, &ws))) {
        if (win == 0 || !__riscos_dispmanx_window ||
            !__riscos_dispmanx_window((const void *) win, &dmx, &dmx_w, &dmx_h)) {
            fail(EGL_BAD_NATIVE_WINDOW);
            return EGL_NO_SURFACE;
        }
    }
    if (!(s = new_surface(d, c, SURF_WINDOW)))
        return EGL_NO_SURFACE;
    s->handle = win;
    if (dmx) {
        s->handle = -3;
        s->dmx = dmx;
        s->w = dmx_w;
        s->h = dmx_h;
    }

    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLint a = attrib_list[i], v = attrib_list[i + 1];
        switch (a) {
        case EGL_RENDER_BUFFER:
            if (v != EGL_BACK_BUFFER && v != EGL_SINGLE_BUFFER)
                goto bad_attr;
            s->render_buffer = v;
            break;
        case EGL_VG_COLORSPACE:
        case EGL_VG_ALPHA_FORMAT:
            break;              /* OpenVG only, ignored */
        case EGL_SCREEN_BANKS_RISCOS:
            if (s->handle != -1 || v < 0 || v == 1 || v > MAX_BANKS)
                goto bad_attr;
            s->want_banks = v;
            break;
        case EGL_WORK_AREA_X_RISCOS:      s->wa_x = v; break;
        case EGL_WORK_AREA_Y_RISCOS:      s->wa_y = v; break;
        case EGL_WORK_AREA_WIDTH_RISCOS:  s->w = v; have_w = 1; break;
        case EGL_WORK_AREA_HEIGHT_RISCOS: s->h = v; have_h = 1; break;
        default:
            goto bad_attr;
        }
    }
    if (have_w || have_h) {
        if (!have_w || !have_h || s->w < 1 || s->h < 1 || s->handle < 0)
            goto bad_attr;
        s->fixed = 1;
    }

    read_screen(&scr);
    if (update_window_buffer(s, &scr) < 0) {
        free(s);
        return EGL_NO_SURFACE;
    }
    add_surface(d, s);
    ok();
    return (EGLSurface) s;

bad_attr:
    free(s);
    fail(EGL_BAD_ATTRIBUTE);
    return EGL_NO_SURFACE;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                     EGLNativeWindowType win,
                                                     const EGLint *attrib_list)
{
    ENTER();
    return create_window_surface(dpy, config, win, attrib_list);
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                                      const EGLint *attrib_list)
{
    egl_display *d;
    const egl_config *c;
    egl_surface *s;
    int i, w = 0, h = 0, largest = 0;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(c = get_config(d, config)))
        return EGL_NO_SURFACE;
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLint a = attrib_list[i], v = attrib_list[i + 1];
        switch (a) {
        case EGL_WIDTH:  w = v; break;
        case EGL_HEIGHT: h = v; break;
        case EGL_LARGEST_PBUFFER: largest = v; break;
        case EGL_TEXTURE_FORMAT:
        case EGL_TEXTURE_TARGET:
            if (v != EGL_NO_TEXTURE) {
                fail(EGL_BAD_MATCH);
                return EGL_NO_SURFACE;
            }
            break;
        case EGL_MIPMAP_TEXTURE:
        case EGL_VG_COLORSPACE:
        case EGL_VG_ALPHA_FORMAT:
            break;
        default:
            fail(EGL_BAD_ATTRIBUTE);
            return EGL_NO_SURFACE;
        }
    }
    if (w < 0 || h < 0) {
        fail(EGL_BAD_PARAMETER);
        return EGL_NO_SURFACE;
    }
    if (w > MAX_PBUFFER || h > MAX_PBUFFER) {
        if (!largest) {
            fail(EGL_BAD_MATCH);
            return EGL_NO_SURFACE;
        }
        if (w > MAX_PBUFFER) w = MAX_PBUFFER;
        if (h > MAX_PBUFFER) h = MAX_PBUFFER;
    }
    if (!(s = new_surface(d, c, SURF_PBUFFER)))
        return EGL_NO_SURFACE;
    /* OSMesa can't bind a 0x0 buffer; keep 1x1 of storage but report 0. */
    s->w = w > 0 ? w : 1;
    s->h = h > 0 ? h : 1;
    s->stride = s->w;
    s->mem = calloc((size_t) s->w * s->h, 4);
    if (!s->mem) {
        free(s);
        fail(EGL_BAD_ALLOC);
        return EGL_NO_SURFACE;
    }
    s->pixels = s->mem;
    s->wa_x = w;                /* reported size */
    s->wa_y = h;
    add_surface(d, s);
    ok();
    return (EGLSurface) s;
}

static EGLSurface create_pixmap_surface(EGLDisplay dpy, EGLConfig config,
                                        void *pixmap, const EGLint *attrib_list)
{
    egl_display *d;
    const egl_config *c;
    egl_surface *s;
    int i, w, h, layout;
    void *px;

    if (!(d = get_display(dpy, 1)) || !(c = get_config(d, config)))
        return EGL_NO_SURFACE;
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        if (attrib_list[i] != EGL_VG_COLORSPACE && attrib_list[i] != EGL_VG_ALPHA_FORMAT) {
            fail(EGL_BAD_ATTRIBUTE);
            return EGL_NO_SURFACE;
        }
    }
    if (!pixmap_info(pixmap, &w, &h, &layout, &px)) {
        fail(EGL_BAD_NATIVE_PIXMAP);
        return EGL_NO_SURFACE;
    }
    if (layout != c->layout) {
        fail(EGL_BAD_MATCH);
        return EGL_NO_SURFACE;
    }
    if (!(s = new_surface(d, c, SURF_PIXMAP)))
        return EGL_NO_SURFACE;
    s->render_buffer = EGL_SINGLE_BUFFER;
    s->w = w;
    s->h = h;
    s->stride = w;
    s->pixels = px;
    add_surface(d, s);
    ok();
    return (EGLSurface) s;
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config,
                                                     EGLNativePixmapType pixmap,
                                                     const EGLint *attrib_list)
{
    ENTER();
    return create_pixmap_surface(dpy, config, (void *) pixmap, attrib_list);
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroySurface(EGLDisplay dpy, EGLSurface surface)
{
    egl_display *d;
    egl_surface *s;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (s->current)
        s->destroy_pending = 1;
    else
        unlink_surface(d, s);
    return ok();
}

/* eglQuerySurface and eglQuerySurface64KHR. */
static EGLBoolean query_surface(EGLDisplay dpy, EGLSurface surface, EGLint attribute,
                                EGLAttribKHR *value)
{
    egl_display *d;
    egl_surface *s;
    int trgb;

    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    trgb = s->cfg->layout == LAYOUT_TRGB;
    switch (attribute) {
    case EGL_CONFIG_ID:        *value = s->cfg->id; break;
    case EGL_WIDTH:            *value = s->kind == SURF_PBUFFER ? s->wa_x : s->w; break;
    case EGL_HEIGHT:           *value = s->kind == SURF_PBUFFER ? s->wa_y : s->h; break;
    case EGL_LARGEST_PBUFFER:  if (s->kind == SURF_PBUFFER) *value = EGL_FALSE; break;
    case EGL_RENDER_BUFFER:
        *value = (s->kind == SURF_PIXMAP || s->direct) ? EGL_SINGLE_BUFFER : EGL_BACK_BUFFER;
        break;
    case EGL_SWAP_BEHAVIOR:    *value = s->swap_behavior; break;
    case EGL_SCREEN_BANKS_RISCOS: *value = s->banks; break;
    case EGL_MULTISAMPLE_RESOLVE: *value = EGL_MULTISAMPLE_RESOLVE_DEFAULT; break;
    case EGL_HORIZONTAL_RESOLUTION:
    case EGL_VERTICAL_RESOLUTION:
    case EGL_PIXEL_ASPECT_RATIO: *value = EGL_UNKNOWN; break;
    case EGL_TEXTURE_FORMAT:
    case EGL_TEXTURE_TARGET:   if (s->kind == SURF_PBUFFER) *value = EGL_NO_TEXTURE; break;
    case EGL_MIPMAP_TEXTURE:
    case EGL_MIPMAP_LEVEL:     if (s->kind == SURF_PBUFFER) *value = 0; break;
    case EGL_VG_ALPHA_FORMAT:  *value = EGL_VG_ALPHA_FORMAT_NONPRE; break;
    case EGL_VG_COLORSPACE:    *value = EGL_VG_COLORSPACE_sRGB; break;

    case EGL_BUFFER_AGE_EXT:
        /* How many frames old the back buffer's contents are (0 = unknown).
           A sprite or the screen itself keeps the last frame: 1. Screen
           banks hold the frame from 'banks' swaps ago. */
        if (s != cur_surf || !cur_ctx)
            return fail(EGL_BAD_SURFACE);
        if (s->kind != SURF_WINDOW || s->swaps == 0)
            *value = 0;
        else if (s->banks)
            *value = s->swaps >= s->banks ? s->banks : 0;
        else
            *value = 1;
        s->age_queried = 1;
        break;

    case EGL_BITMAP_POINTER_KHR:
    case EGL_BITMAP_PITCH_KHR:
    case EGL_BITMAP_ORIGIN_KHR:
    case EGL_BITMAP_PIXEL_RED_OFFSET_KHR:
    case EGL_BITMAP_PIXEL_GREEN_OFFSET_KHR:
    case EGL_BITMAP_PIXEL_BLUE_OFFSET_KHR:
    case EGL_BITMAP_PIXEL_ALPHA_OFFSET_KHR:
    case EGL_BITMAP_PIXEL_LUMINANCE_OFFSET_KHR:
    case EGL_BITMAP_PIXEL_SIZE_KHR:
        if (!s->locked)
            return fail(EGL_BAD_ACCESS);
        switch (attribute) {
        case EGL_BITMAP_POINTER_KHR:  *value = (EGLAttribKHR) s->pixels; break;
        case EGL_BITMAP_PITCH_KHR:    *value = s->stride * 4; break;
        case EGL_BITMAP_ORIGIN_KHR:   *value = EGL_UPPER_LEFT_KHR; break;
        case EGL_BITMAP_PIXEL_RED_OFFSET_KHR:   *value = trgb ? 16 : 0; break;
        case EGL_BITMAP_PIXEL_GREEN_OFFSET_KHR: *value = 8; break;
        case EGL_BITMAP_PIXEL_BLUE_OFFSET_KHR:  *value = trgb ? 0 : 16; break;
        case EGL_BITMAP_PIXEL_ALPHA_OFFSET_KHR: *value = 24; break;
        case EGL_BITMAP_PIXEL_LUMINANCE_OFFSET_KHR: *value = 0; break;
        case EGL_BITMAP_PIXEL_SIZE_KHR: *value = 32; break;
        }
        break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                              EGLint attribute, EGLint *value)
{
    EGLAttribKHR v;
    ENTER();
    if (!value) {
        egl_display *d = get_display(dpy, 1);
        if (d && get_surface(d, surface))
            fail(EGL_BAD_PARAMETER);
        return EGL_FALSE;
    }
    v = *value;                 /* unchanged for attributes that don't apply */
    if (!query_surface(dpy, surface, attribute, &v))
        return EGL_FALSE;
    *value = (EGLint) v;
    return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQuerySurface64KHR(EGLDisplay dpy, EGLSurface surface,
                                                   EGLint attribute, EGLAttribKHR *value)
{
    ENTER();
    return query_surface(dpy, surface, attribute, value);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                                               EGLint attribute, EGLint value)
{
    egl_display *d;
    egl_surface *s;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    switch (attribute) {
    case EGL_SWAP_BEHAVIOR:
        if (value != EGL_BUFFER_PRESERVED && value != EGL_BUFFER_DESTROYED)
            return fail(EGL_BAD_PARAMETER);
        s->swap_behavior = value;
        if (value == EGL_BUFFER_PRESERVED && s->handle == -1 && !s->no_banks) {
            s->no_banks = 1;            /* screen banks can't preserve: use a sprite */
            if (s->banks) {
                screen_info scr;
                read_screen(&scr);
                if (update_window_buffer(s, &scr) < 0)
                    return EGL_FALSE;
                if (s == cur_surf && cur_ctx && !bind(cur_ctx, s))
                    return fail(EGL_BAD_ALLOC);
            }
        }
        break;
    case EGL_MIPMAP_LEVEL:
        break;
    case EGL_MULTISAMPLE_RESOLVE:
        if (value != EGL_MULTISAMPLE_RESOLVE_DEFAULT)
            return fail(EGL_BAD_MATCH);
        break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    return ok();
}

static EGLBoolean tex_image(EGLDisplay dpy, EGLSurface surface)
{
    egl_display *d;
    if (!(d = get_display(dpy, 1)) || !get_surface(d, surface))
        return EGL_FALSE;
    return fail(EGL_BAD_MATCH);         /* no bind-to-texture configs */
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
    ENTER();
    (void) buffer;
    return tex_image(dpy, surface);
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
    ENTER();
    (void) buffer;
    return tex_image(dpy, surface);
}

EGLAPI EGLBoolean EGLAPIENTRY eglBindAPI(EGLenum api)
{
    ENTER();
    if (api != EGL_OPENGL_API && api != EGL_OPENGL_ES_API)
        return fail(EGL_BAD_PARAMETER); /* no OpenVG */
    bound_api = api;
    return ok();
}

EGLAPI EGLenum EGLAPIENTRY eglQueryAPI(void)
{
    ENTER();
    return bound_api;
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitClient(void)
{
    ENTER();
    if (cur_ctx)
        glFinish();
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitGL(void)
{
    ENTER();
    if (cur_ctx)
        glFinish();
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglWaitNative(EGLint engine)
{
    ENTER();
    if (engine != EGL_CORE_NATIVE_ENGINE)
        return fail(EGL_BAD_PARAMETER);
    return ok();                        /* RISC OS drawing is synchronous */
}

EGLAPI EGLBoolean EGLAPIENTRY eglReleaseThread(void)
{
    ENTER();
    if (cur_ctx)
        eglMakeCurrent((EGLDisplay) &display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    bound_api = EGL_OPENGL_ES_API;
    return ok();
}

EGLAPI EGLSurface EGLAPIENTRY eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype,
        EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list)
{
    egl_display *d;
    ENTER();
    (void) buftype; (void) buffer; (void) attrib_list;
    if ((d = get_display(dpy, 1)) && get_config(d, config))
        fail(EGL_BAD_PARAMETER);        /* only OpenVG images exist for this */
    return EGL_NO_SURFACE;
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
    ENTER();
    if (!get_display(dpy, 1))
        return EGL_FALSE;
    if (!cur_ctx)
        return fail(EGL_BAD_CONTEXT);
    if (!cur_surf)
        return fail(EGL_BAD_SURFACE);
    if (interval < 0) interval = 0;
    if (interval > MAX_SWAP_INTERVAL) interval = MAX_SWAP_INTERVAL;
    cur_surf->swap_interval = interval;
    return ok();
}

EGLAPI EGLContext EGLAPIENTRY eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                               EGLContext share_context,
                                               const EGLint *attrib_list)
{
    egl_display *d;
    const egl_config *c;
    egl_context *ctx, *share = NULL;
    EGLint major = 1, minor = 0, flags = 0;
    EGLint profile = EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR;
    int attribs[20], i, n = 0, explicit_version = 0, explicit_profile = 0;
    int release_flush = 1, es = 0;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(c = get_config(d, config)))
        return EGL_NO_CONTEXT;
    if (bound_api != EGL_OPENGL_API && bound_api != EGL_OPENGL_ES_API) {
        fail(EGL_BAD_MATCH);
        return EGL_NO_CONTEXT;
    }
    if (share_context != EGL_NO_CONTEXT && !(share = get_context(d, share_context)))
        return EGL_NO_CONTEXT;
    if (share && share->api != bound_api) {
        fail(EGL_BAD_MATCH);            /* can't share between GL and GLES */
        return EGL_NO_CONTEXT;
    }

    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE; i += 2) {
        EGLint a = attrib_list[i], v = attrib_list[i + 1];
        switch (a) {
        case EGL_CONTEXT_MAJOR_VERSION_KHR:     /* = EGL_CONTEXT_CLIENT_VERSION */
            major = v; explicit_version = 1; break;
        case EGL_CONTEXT_MINOR_VERSION_KHR:
            minor = v; explicit_version = 1; break;
        case EGL_CONTEXT_FLAGS_KHR:
            flags = v; break;
        case EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR:
            profile = v; explicit_profile = 1; break;
        case EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY_KHR:
            if (v != EGL_NO_RESET_NOTIFICATION_KHR) {
                fail(EGL_BAD_MATCH);
                return EGL_NO_CONTEXT;
            }
            break;
        case EGL_CONTEXT_RELEASE_BEHAVIOR_KHR:  /* EGL_KHR_context_flush_control */
            if (v != EGL_CONTEXT_RELEASE_BEHAVIOR_FLUSH_KHR &&
                v != EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR) {
                fail(EGL_BAD_ATTRIBUTE);
                return EGL_NO_CONTEXT;
            }
            release_flush = (v == EGL_CONTEXT_RELEASE_BEHAVIOR_FLUSH_KHR);
            break;
        default:
            fail(EGL_BAD_ATTRIBUTE);
            return EGL_NO_CONTEXT;
        }
    }
    if (flags & ~(EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR |
                  EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR |
                  EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR)) {
        fail(EGL_BAD_ATTRIBUTE);
        return EGL_NO_CONTEXT;
    }
    if (flags & EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR) {
        fail(EGL_BAD_MATCH);            /* no robust access in classic swrast */
        return EGL_NO_CONTEXT;
    }
    if (profile != EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
        profile != EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR) {
        fail(EGL_BAD_MATCH);
        return EGL_NO_CONTEXT;
    }
    if (bound_api == EGL_OPENGL_ES_API) {
        /* EGL_CONTEXT_CLIENT_VERSION (default 1); classic swrast gives
           ES 1.1 and ES 2.0 */
        if (explicit_profile || (flags & EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR)) {
            fail(EGL_BAD_ATTRIBUTE);    /* desktop GL only */
            return EGL_NO_CONTEXT;
        }
        if (major == 1 && (minor == 0 || minor == 1))
            es = 1;
        else if (major == 2 && minor == 0)
            es = 2;
        else {
            fail(EGL_BAD_MATCH);        /* ES 3.x: not with this renderer */
            return EGL_NO_CONTEXT;
        }
    }

    attribs[n++] = OSMESA_FORMAT;
    attribs[n++] = c->layout == LAYOUT_TRGB ? OSMESA_BGRA : OSMESA_RGBA;
    attribs[n++] = OSMESA_DEPTH_BITS;   attribs[n++] = c->depth;
    attribs[n++] = OSMESA_STENCIL_BITS; attribs[n++] = c->stencil;
    attribs[n++] = OSMESA_ACCUM_BITS;   attribs[n++] = 0;
    /* The profile only applies to GL 3.2+ (EGL_KHR_create_context). */
    attribs[n++] = OSMESA_PROFILE;
    if (es)
        attribs[n++] = es == 1 ? OSMESA_ES1_PROFILE : OSMESA_ES2_PROFILE;
    else
        attribs[n++] = (explicit_profile && profile == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR &&
                        (major > 3 || (major == 3 && minor >= 2)))
                       ? OSMESA_CORE_PROFILE : OSMESA_COMPAT_PROFILE;
    if (!es && explicit_version && (major > 2 || (major == 2 && minor > 1))) {
        attribs[n++] = OSMESA_CONTEXT_MAJOR_VERSION; attribs[n++] = major;
        attribs[n++] = OSMESA_CONTEXT_MINOR_VERSION; attribs[n++] = minor;
    }
    attribs[n++] = 0;

    ctx = (egl_context *) calloc(1, sizeof *ctx);
    if (!ctx) {
        fail(EGL_BAD_ALLOC);
        return EGL_NO_CONTEXT;
    }
    ctx->om = OSMesaCreateContextAttribs(attribs, share ? share->om : NULL);
    if (!ctx->om) {
        free(ctx);
        fail(EGL_BAD_MATCH);            /* e.g. GL 3.x asked for: this is 2.1 / ES 2.0 */
        return EGL_NO_CONTEXT;
    }
    ctx->magic = MAGIC_CONTEXT;
    ctx->cfg = c;
    ctx->api = bound_api;
    ctx->es = es;
    ctx->release_flush = release_flush;
    ctx->next = d->contexts;
    d->contexts = ctx;
    ok();
    return (EGLContext) ctx;
}

EGLAPI EGLBoolean EGLAPIENTRY eglDestroyContext(EGLDisplay dpy, EGLContext context)
{
    egl_display *d;
    egl_context *c;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(c = get_context(d, context)))
        return EGL_FALSE;
    if (c->current)
        c->destroy_pending = 1;
    else
        unlink_context(d, c);
    return ok();
}

/* EGL_KHR_context_flush_control: flush a context that stops being current
   (unless it was created with EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR). */
static void flush_on_release(egl_context *c)
{
    if (c && c->release_flush)
        glFlush();
}

static void release_current(egl_display *d)
{
    egl_context *c = cur_ctx;
    egl_surface *s = cur_surf;
    flush_on_release(c);
    cur_ctx = NULL;
    cur_surf = NULL;
    if (c) {
        c->current = 0;
        if (c->destroy_pending) unlink_context(d, c);
    }
    if (s) {
        s->current = 0;
        if (s->destroy_pending) unlink_surface(d, s);
    }
}

/* EGL_KHR_surfaceless_context: OSMesa always needs a buffer, so a context
   made current without a surface draws into this (framebuffer objects are
   what such a context is for). */
static unsigned int surfaceless_pixel;

EGLAPI EGLBoolean EGLAPIENTRY eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                             EGLSurface read, EGLContext context)
{
    egl_display *d;
    egl_context *c;
    egl_surface *s = NULL;

    ENTER();
    if (context == EGL_NO_CONTEXT && draw == EGL_NO_SURFACE && read == EGL_NO_SURFACE) {
        if (!(d = get_display(dpy, 0)))
            return EGL_FALSE;
        release_current(d);
        return ok();
    }
    if (!(d = get_display(dpy, 1)))
        return EGL_FALSE;
    if (context == EGL_NO_CONTEXT)
        return fail(EGL_BAD_MATCH);
    if (!(c = get_context(d, context)))
        return EGL_FALSE;
    if ((draw == EGL_NO_SURFACE) != (read == EGL_NO_SURFACE))
        return fail(EGL_BAD_MATCH);
    if (draw != EGL_NO_SURFACE) {
        if (!(s = get_surface(d, draw)) || !get_surface(d, read))
            return EGL_FALSE;
        if (draw != read)
            return fail(EGL_BAD_MATCH);     /* OSMesa has one buffer per context */
        if (s->cfg->layout != c->cfg->layout)
            return fail(EGL_BAD_MATCH);
        if (s->locked)
            return fail(EGL_BAD_ACCESS);    /* EGL_KHR_lock_surface */
    }

    if (c != cur_ctx)
        flush_on_release(cur_ctx);

    if (s) {
        if (s->kind == SURF_WINDOW) {
            screen_info scr;
            read_screen(&scr);
            if (update_window_buffer(s, &scr) < 0)
                return EGL_FALSE;
        }
        if (!bind(c, s))
            return fail(EGL_BAD_ALLOC);
        if (c->surfaceless && !c->had_surface) {
            /* Mesa sizes the viewport and scissor box the first time a
               context is bound, which was to the 1x1 stand-in: fix them. */
            glViewport(0, 0, s->w, s->h);
            glScissor(0, 0, s->w, s->h);
        }
        c->had_surface = 1;
    } else {
        if (!OSMesaMakeCurrent(c->om, &surfaceless_pixel, GL_UNSIGNED_BYTE, 1, 1))
            return fail(EGL_BAD_ALLOC);
        OSMesaPixelStore(OSMESA_Y_UP, 0);
        OSMesaPixelStore(OSMESA_ROW_LENGTH, 1);
        c->surfaceless = 1;
    }

    if (c != cur_ctx || s != cur_surf) {
        egl_context *oc = cur_ctx;
        egl_surface *os = cur_surf;
        cur_ctx = c;
        cur_surf = s;
        c->current = 1;
        if (s)
            s->current = 1;
        if (oc && oc != c) {
            oc->current = 0;
            if (oc->destroy_pending) unlink_context(d, oc);
        }
        if (os && os != s) {
            os->current = 0;
            if (os->destroy_pending) unlink_surface(d, os);
        }
    }
    return ok();
}

/* One context is current at a time (OSMesa has one). EGL keeps one per
   client API, so report it only for its own API. */
static egl_context *current_for_api(void)
{
    return (cur_ctx && cur_ctx->api == bound_api) ? cur_ctx : NULL;
}

EGLAPI EGLContext EGLAPIENTRY eglGetCurrentContext(void)
{
    ENTER();
    return current_for_api() ? (EGLContext) cur_ctx : EGL_NO_CONTEXT;
}

EGLAPI EGLSurface EGLAPIENTRY eglGetCurrentSurface(EGLint readdraw)
{
    ENTER();
    if (readdraw != EGL_READ && readdraw != EGL_DRAW) {
        fail(EGL_BAD_PARAMETER);
        return EGL_NO_SURFACE;
    }
    return (current_for_api() && cur_surf) ? (EGLSurface) cur_surf : EGL_NO_SURFACE;
}

EGLAPI EGLDisplay EGLAPIENTRY eglGetCurrentDisplay(void)
{
    ENTER();
    return current_for_api() ? (EGLDisplay) &display : EGL_NO_DISPLAY;
}

EGLAPI EGLBoolean EGLAPIENTRY eglQueryContext(EGLDisplay dpy, EGLContext context,
                                              EGLint attribute, EGLint *value)
{
    egl_display *d;
    egl_context *c;
    ENTER();
    if (!(d = get_display(dpy, 1)) || !(c = get_context(d, context)))
        return EGL_FALSE;
    if (!value)
        return fail(EGL_BAD_PARAMETER);
    switch (attribute) {
    case EGL_CONFIG_ID:              *value = c->cfg->id; break;
    case EGL_CONTEXT_CLIENT_TYPE:    *value = c->api; break;
    case EGL_CONTEXT_CLIENT_VERSION: *value = c->es; break;   /* ES only; 0 for GL */
    case EGL_RENDER_BUFFER:
        if (!c->current || !cur_surf)
            *value = EGL_NONE;
        else
            *value = (cur_surf->kind == SURF_PIXMAP || cur_surf->direct)
                     ? EGL_SINGLE_BUFFER : EGL_BACK_BUFFER;
        break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    return ok();
}

/* eglSwapBuffers and the with-damage versions. */
static EGLBoolean swap(EGLDisplay dpy, EGLSurface surface, const EGLint *rects, EGLint n)
{
    egl_display *d;
    egl_surface *s;
    screen_info scr;
    int changed, is_current;

    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (n < 0 || (n > 0 && !rects))
        return fail(EGL_BAD_PARAMETER);
    if (s->locked)
        return fail(EGL_BAD_ACCESS);
    is_current = (s == cur_surf && cur_ctx);
    /* A lockable window surface written through eglLockSurfaceKHR can be
       shown without a context current to it. */
    if (!is_current && !(s->kind == SURF_WINDOW && s->ever_locked && !s->current))
        return fail(EGL_BAD_SURFACE);
    if (s->kind != SURF_WINDOW)
        return ok();                    /* no effect on pbuffers/pixmaps */

    /* EGL_KHR_partial_update: the damage region set for this frame is what
       changed, unless the swap gives its own. */
    if (n == 0 && s->n_damage > 0) {
        rects = s->damage;
        n = s->n_damage;
    }

    if (is_current)
        glFinish();
    read_screen(&scr);
    present(d, s, &scr, n > 0 ? rects : NULL, n);
    s->swaps++;
    s->age_queried = 0;
    s->n_damage = -1;

    /* Window resized or screen mode changed: the next frame gets a new
       buffer (the one just shown was complete). */
    changed = update_window_buffer(s, &scr);
    if (changed < 0)
        return EGL_FALSE;
    if (s->banks)
        changed = 1;                    /* now drawing into another bank */
    if (changed && is_current && !bind(cur_ctx, s))
        return fail(EGL_BAD_ALLOC);
    /* DispmanX compatibility in window mode: libbcm_host polls the Wimp
       here, last, so the program multitasks (and may exit if its window is
       closed, with EGL's state complete). */
    if (s->dmx && __riscos_dispmanx_swapped)
        __riscos_dispmanx_swapped();
    return ok();
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffers(EGLDisplay dpy, EGLSurface surface)
{
    ENTER();
    return swap(dpy, surface, NULL, 0);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffersWithDamageKHR(EGLDisplay dpy, EGLSurface surface,
                                                          const EGLint *rects, EGLint n_rects)
{
    ENTER();
    return swap(dpy, surface, rects, n_rects);
}

EGLAPI EGLBoolean EGLAPIENTRY eglSwapBuffersWithDamageEXT(EGLDisplay dpy, EGLSurface surface,
                                                          const EGLint *rects, EGLint n_rects)
{
    ENTER();
    return swap(dpy, surface, rects, n_rects);
}

EGLAPI EGLBoolean EGLAPIENTRY eglCopyBuffers(EGLDisplay dpy, EGLSurface surface,
                                             EGLNativePixmapType target)
{
    egl_display *d;
    egl_surface *s;
    int w, h, layout, x, y, cw, ch;
    void *px;

    ENTER();
    if (!(d = get_display(dpy, 1)) || !(s = get_surface(d, surface)))
        return EGL_FALSE;
    if (!pixmap_info(target, &w, &h, &layout, &px))
        return fail(EGL_BAD_NATIVE_PIXMAP);
    if (s == cur_surf && cur_ctx)
        glFinish();
    cw = w < s->w ? w : s->w;
    ch = h < s->h ? h : s->h;
    for (y = 0; y < ch; y++) {
        const unsigned int *src = (const unsigned int *) s->pixels + (size_t) y * s->stride;
        unsigned int *dst = (unsigned int *) px + (size_t) y * w;
        if (layout == s->cfg->layout) {
            memcpy(dst, src, cw * 4);
        } else {
            for (x = 0; x < cw; x++) {
                unsigned int p = src[x];
                dst[x] = (p & 0xFF00FF00u) | ((p >> 16) & 0xFF) | ((p & 0xFF) << 16);
            }
        }
    }
    return ok();
}
