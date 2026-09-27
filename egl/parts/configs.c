/*
 * egl/parts/configs.c - the configs offered and eglChooseConfig matching.
 * Part of egl_riscos.c: it is compiled by being included from there, as
 * one unit with the other parts (so their helpers stay private to the
 * library), not on its own. MIT licence (see LICENSE).
 */

/* ------------------------------------------------------------------ */
/* Configs                                                             */

static int config_attrib(const egl_config *c, EGLint attrib, EGLint *value)
{
    EGLint v;
    switch (attrib) {
    case EGL_BUFFER_SIZE:              v = 32; break;
    case EGL_RED_SIZE:
    case EGL_GREEN_SIZE:
    case EGL_BLUE_SIZE:
    case EGL_ALPHA_SIZE:               v = 8; break;
    case EGL_LUMINANCE_SIZE:           v = 0; break;
    case EGL_ALPHA_MASK_SIZE:          v = 0; break;
    case EGL_BIND_TO_TEXTURE_RGB:
    case EGL_BIND_TO_TEXTURE_RGBA:     v = EGL_FALSE; break;
    case EGL_COLOR_BUFFER_TYPE:        v = EGL_RGB_BUFFER; break;
    case EGL_CONFIG_CAVEAT:            v = EGL_NONE; break;
    case EGL_CONFIG_ID:                v = c->id; break;
    /* Not claimed: EGL_CONFORMANT says contexts of these APIs pass Khronos
       conformance tests, and neither this EGL nor Mesa's classic software
       renderer has been through them. */
    case EGL_CONFORMANT:               v = 0; break;
    case EGL_DEPTH_SIZE:               v = c->depth; break;
    case EGL_LEVEL:                    v = 0; break;
    case EGL_MAX_PBUFFER_WIDTH:
    case EGL_MAX_PBUFFER_HEIGHT:       v = MAX_PBUFFER; break;
    case EGL_MAX_PBUFFER_PIXELS:       v = MAX_PBUFFER * MAX_PBUFFER; break;
    case EGL_MAX_SWAP_INTERVAL:        v = MAX_SWAP_INTERVAL; break;
    case EGL_MIN_SWAP_INTERVAL:        v = 0; break;
    case EGL_NATIVE_RENDERABLE:        v = EGL_TRUE; break;
    case EGL_NATIVE_VISUAL_ID:
        v = (c->layout == LAYOUT_TRGB) ? EGL_RISCOS_VISUAL_TRGB : EGL_RISCOS_VISUAL_TBGR;
        break;
    case EGL_NATIVE_VISUAL_TYPE:       v = EGL_NONE; break;
    case EGL_RENDERABLE_TYPE:          v = RENDERABLE_BITS; break;
    case EGL_SAMPLE_BUFFERS:
    case EGL_SAMPLES:                  v = 0; break;
    case EGL_STENCIL_SIZE:             v = c->stencil; break;
    case EGL_SURFACE_TYPE:
        v = EGL_WINDOW_BIT | EGL_PBUFFER_BIT | EGL_PIXMAP_BIT |
            EGL_SWAP_BEHAVIOR_PRESERVED_BIT | EGL_LOCK_SURFACE_BIT_KHR;
        break;
    case EGL_MATCH_FORMAT_KHR:
        /* EXACT = B,G,R,A bytes in memory, which is 0x00RRGGBB */
        v = (c->layout == LAYOUT_TRGB) ? EGL_FORMAT_RGBA_8888_EXACT_KHR : EGL_FORMAT_RGBA_8888_KHR;
        break;
    case EGL_TRANSPARENT_TYPE:         v = EGL_NONE; break;
    case EGL_TRANSPARENT_RED_VALUE:
    case EGL_TRANSPARENT_GREEN_VALUE:
    case EGL_TRANSPARENT_BLUE_VALUE:   v = 0; break;
    default:
        return 0;
    }
    *value = v;
    return 1;
}

enum { M_ATLEAST, M_EXACT, M_MASK, M_IGNORE, M_FORMAT };

static const struct {
    EGLint attrib, def;
    int match;
} criteria[] = {
    { EGL_BUFFER_SIZE,            0,              M_ATLEAST },
    { EGL_RED_SIZE,               0,              M_ATLEAST },
    { EGL_GREEN_SIZE,             0,              M_ATLEAST },
    { EGL_BLUE_SIZE,              0,              M_ATLEAST },
    { EGL_LUMINANCE_SIZE,         0,              M_ATLEAST },
    { EGL_ALPHA_SIZE,             0,              M_ATLEAST },
    { EGL_ALPHA_MASK_SIZE,        0,              M_ATLEAST },
    { EGL_BIND_TO_TEXTURE_RGB,    EGL_DONT_CARE,  M_EXACT },
    { EGL_BIND_TO_TEXTURE_RGBA,   EGL_DONT_CARE,  M_EXACT },
    { EGL_COLOR_BUFFER_TYPE,      EGL_RGB_BUFFER, M_EXACT },
    { EGL_CONFIG_CAVEAT,          EGL_DONT_CARE,  M_EXACT },
    { EGL_CONFIG_ID,              EGL_DONT_CARE,  M_EXACT },
    { EGL_CONFORMANT,             0,              M_MASK },
    { EGL_DEPTH_SIZE,             0,              M_ATLEAST },
    { EGL_LEVEL,                  0,              M_EXACT },
    { EGL_MAX_PBUFFER_WIDTH,      0,              M_IGNORE },
    { EGL_MAX_PBUFFER_HEIGHT,     0,              M_IGNORE },
    { EGL_MAX_PBUFFER_PIXELS,     0,              M_IGNORE },
    { EGL_MATCH_FORMAT_KHR,       EGL_DONT_CARE,  M_FORMAT },
    { EGL_MAX_SWAP_INTERVAL,      EGL_DONT_CARE,  M_EXACT },
    { EGL_MIN_SWAP_INTERVAL,      EGL_DONT_CARE,  M_EXACT },
    { EGL_NATIVE_RENDERABLE,      EGL_DONT_CARE,  M_EXACT },
    { EGL_NATIVE_VISUAL_ID,       0,              M_IGNORE },
    { EGL_NATIVE_VISUAL_TYPE,     EGL_DONT_CARE,  M_EXACT },
    { EGL_RENDERABLE_TYPE,        EGL_OPENGL_ES_BIT, M_MASK },
    { EGL_SAMPLE_BUFFERS,         0,              M_ATLEAST },
    { EGL_SAMPLES,                0,              M_ATLEAST },
    { EGL_STENCIL_SIZE,           0,              M_ATLEAST },
    { EGL_SURFACE_TYPE,           EGL_WINDOW_BIT, M_MASK },
    { EGL_TRANSPARENT_TYPE,       EGL_NONE,       M_EXACT },
    { EGL_TRANSPARENT_RED_VALUE,  EGL_DONT_CARE,  M_EXACT },
    { EGL_TRANSPARENT_GREEN_VALUE, EGL_DONT_CARE, M_EXACT },
    { EGL_TRANSPARENT_BLUE_VALUE, EGL_DONT_CARE,  M_EXACT },
};
#define NCRITERIA ((int) (sizeof criteria / sizeof criteria[0]))

/* eglChooseConfig: values that aren't one of an attribute's allowed ones
   are EGL_BAD_ATTRIBUTE (EGL 1.4 section 3.4.1.2) */
static int config_value_ok(EGLint attrib, EGLint v)
{
    if (v == EGL_DONT_CARE)
        return 1;
    switch (attrib) {
    case EGL_BIND_TO_TEXTURE_RGB:
    case EGL_BIND_TO_TEXTURE_RGBA:
    case EGL_NATIVE_RENDERABLE:
        return v == EGL_TRUE || v == EGL_FALSE;
    case EGL_COLOR_BUFFER_TYPE:
        return v == EGL_RGB_BUFFER || v == EGL_LUMINANCE_BUFFER;
    case EGL_CONFIG_CAVEAT:
        return v == EGL_NONE || v == EGL_SLOW_CONFIG || v == EGL_NON_CONFORMANT_CONFIG;
    case EGL_TRANSPARENT_TYPE:
        return v == EGL_NONE || v == EGL_TRANSPARENT_RGB;
    default:
        return 1;
    }
}

static int config_compare(const void *a, const void *b)
{
    /* EGL 1.4 section 3.4.1.2 sort order; everything before depth is
       identical for our configs. */
    const egl_config *x = *(const egl_config * const *) a;
    const egl_config *y = *(const egl_config * const *) b;
    if (x->depth != y->depth)
        return x->depth - y->depth;
    if (x->stencil != y->stencil)
        return x->stencil - y->stencil;
    return x->id - y->id;
}

static void make_configs(egl_display *d)
{
    static const EGLint ds[4][2] = { { 0, 0 }, { 16, 0 }, { 24, 0 }, { 24, 8 } };
    screen_info s;
    int first, i, l, n = 0;

    read_screen(&s);
    first = (screen_layout(&s) == LAYOUT_TRGB) ? LAYOUT_TRGB : LAYOUT_TBGR;
    for (l = 0; l < 2; l++) {
        for (i = 0; i < 4; i++) {
            d->configs[n].id = n + 1;
            d->configs[n].depth = ds[i][0];
            d->configs[n].stencil = ds[i][1];
            d->configs[n].layout = l == 0 ? first : 1 - first;
            n++;
        }
    }
    d->nconfigs = n;
}
