/*
 * bcm_host.c - DispmanX compatibility for riscos-mesa (MIT licence).
 *
 * Programs written for the Raspberry Pi's Khronos stack create a DispmanX
 * element and hand EGL a pointer to an EGL_DISPMANX_WINDOW_T naming it.
 * Here an element is only a record of where on the screen the program wants
 * its output: libEGL asks for it (riscos_dispmanx.h) and plots the surface
 * there on each eglSwapBuffers, scaled from the source rectangle to the
 * destination. Removing an element (or exiting) redraws the desktop under
 * it. Layers, opacity, transforms and resources aren't supported.
 */
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>

#include "bcm_host.h"
#include <EGL/eglplatform.h>
#include <EGL/egl.h>
#include "EGL/eglext_brcm.h"
#include "riscos_dispmanx.h"

#ifndef OS_ValidateAddress
#define OS_ValidateAddress 0x3A
#endif
#ifndef Wimp_ForceRedraw
#define Wimp_ForceRedraw 0x400D1
#endif

#define MAX_ELEMENTS 16
#define ELEMENT_BASE 0x444D4500u        /* "\0EMD": element handles */
#define DISPLAY_BASE 0x444D4400u        /* display handles */

typedef struct {
    int used;
    DISPMANX_DISPLAY_HANDLE_T display;
    int32_t layer;
    VC_RECT_T dst, src;                 /* src in 16.16 fixed point */
    int visible;
    DISPMANX_RESOURCE_HANDLE_T resource;
} element;

static element elements[MAX_ELEMENTS];
static uint32_t next_update = 1;
static int atexit_done;

static void screen_size(int *w, int *h, int *xeig, int *yeig)
{
    static const int vars[] = { 11, 12, 4, 5, -1 };   /* X/YWindLimit, X/YEigFactor */
    int vals[4] = { 639, 479, 1, 1 };
    _kernel_swi_regs r;
    r.r[0] = (int) vars;
    r.r[1] = (int) vals;
    _kernel_swi(OS_ReadVduVariables, &r, &r);
    *w = vals[0] + 1;
    *h = vals[1] + 1;
    if (xeig) *xeig = vals[2];
    if (yeig) *yeig = vals[3];
}

/* Redraw the desktop under an element that has gone (does nothing
   outside the desktop). */
static void uncover(const element *e)
{
    _kernel_swi_regs r;
    int sw, sh, xe, ye;
    if (e->dst.width <= 0 || e->dst.height <= 0)
        return;
    screen_size(&sw, &sh, &xe, &ye);
    r.r[0] = -1;
    r.r[1] = e->dst.x << xe;
    r.r[2] = (sh - e->dst.y - e->dst.height) << ye;
    r.r[3] = (e->dst.x + e->dst.width) << xe;
    r.r[4] = (sh - e->dst.y) << ye;
    _kernel_swi(Wimp_ForceRedraw, &r, &r);
}

static void remove_all(void)
{
    int i;
    for (i = 0; i < MAX_ELEMENTS; i++)
        if (elements[i].used) {
            uncover(&elements[i]);
            elements[i].used = 0;
        }
}

static void setup_atexit(void)
{
    if (!atexit_done) {
        atexit(remove_all);         /* never leave the desktop painted over */
        atexit_done = 1;
    }
}

static element *find(DISPMANX_ELEMENT_HANDLE_T h)
{
    uint32_t i = h - ELEMENT_BASE - 1;
    if (h <= ELEMENT_BASE || i >= MAX_ELEMENTS || !elements[i].used)
        return NULL;
    return &elements[i];
}

void bcm_host_init(void)
{
    setup_atexit();
}

void bcm_host_deinit(void)
{
}

int32_t graphics_get_display_size(const uint16_t display_number,
                                  uint32_t *width, uint32_t *height)
{
    int w, h;
    (void) display_number;
    screen_size(&w, &h, NULL, NULL);
    if (width) *width = w;
    if (height) *height = h;
    return 0;
}

int vc_dispmanx_rect_set(VC_RECT_T *rect, uint32_t x_offset, uint32_t y_offset,
                         uint32_t width, uint32_t height)
{
    rect->x = x_offset;
    rect->y = y_offset;
    rect->width = width;
    rect->height = height;
    return 0;
}

DISPMANX_DISPLAY_HANDLE_T vc_dispmanx_display_open(uint32_t device)
{
    setup_atexit();
    return DISPLAY_BASE + (device & 0xFF);
}

DISPMANX_DISPLAY_HANDLE_T vc_dispmanx_display_open_mode(uint32_t device, uint32_t mode)
{
    (void) mode;
    return vc_dispmanx_display_open(device);
}

int vc_dispmanx_display_close(DISPMANX_DISPLAY_HANDLE_T display)
{
    int i;
    for (i = 0; i < MAX_ELEMENTS; i++)
        if (elements[i].used && elements[i].display == display) {
            uncover(&elements[i]);
            elements[i].used = 0;
        }
    return 0;
}

int vc_dispmanx_display_get_info(DISPMANX_DISPLAY_HANDLE_T display,
                                 DISPMANX_MODEINFO_T *pinfo)
{
    int w, h;
    if (!pinfo)
        return -1;
    screen_size(&w, &h, NULL, NULL);
    pinfo->width = w;
    pinfo->height = h;
    pinfo->transform = DISPMANX_NO_ROTATE;
    pinfo->input_format = VCOS_DISPLAY_INPUT_FORMAT_RGB888;
    pinfo->display_num = display - DISPLAY_BASE;
    return 0;
}

int vc_dispmanx_display_set_background(DISPMANX_UPDATE_HANDLE_T update,
                                       DISPMANX_DISPLAY_HANDLE_T display,
                                       uint8_t red, uint8_t green, uint8_t blue)
{
    (void) update; (void) display; (void) red; (void) green; (void) blue;
    return 0;
}

DISPMANX_UPDATE_HANDLE_T vc_dispmanx_update_start(int32_t priority)
{
    (void) priority;
    if (++next_update == 0)
        next_update = 1;
    return next_update;
}

/* Changes take effect at once, so an update only has to complete. */
int vc_dispmanx_update_submit(DISPMANX_UPDATE_HANDLE_T update,
                              DISPMANX_CALLBACK_FUNC_T cb_func, void *cb_arg)
{
    if (cb_func)
        cb_func(update, cb_arg);
    return 0;
}

int vc_dispmanx_update_submit_sync(DISPMANX_UPDATE_HANDLE_T update)
{
    (void) update;
    return 0;
}

DISPMANX_ELEMENT_HANDLE_T vc_dispmanx_element_add(DISPMANX_UPDATE_HANDLE_T update,
        DISPMANX_DISPLAY_HANDLE_T display, int32_t layer, const VC_RECT_T *dest_rect,
        DISPMANX_RESOURCE_HANDLE_T src, const VC_RECT_T *src_rect,
        DISPMANX_PROTECTION_T protection, VC_DISPMANX_ALPHA_T *alpha,
        DISPMANX_CLAMP_T *clamp, DISPMANX_TRANSFORM_T transform)
{
    int i, w, h;
    element *e;
    (void) update; (void) protection; (void) clamp; (void) transform;

    for (i = 0; i < MAX_ELEMENTS && elements[i].used; i++)
        ;
    if (i == MAX_ELEMENTS)
        return DISPMANX_NO_HANDLE;
    setup_atexit();
    e = &elements[i];
    memset(e, 0, sizeof *e);
    e->used = 1;
    e->display = display;
    e->layer = layer;
    e->resource = src;
    screen_size(&w, &h, NULL, NULL);
    if (dest_rect) {
        e->dst = *dest_rect;
    } else {
        e->dst.width = w;           /* no rectangle: the whole screen */
        e->dst.height = h;
    }
    if (src_rect)
        e->src = *src_rect;
    e->visible = !(alpha && (alpha->flags & 3) != DISPMANX_FLAGS_ALPHA_FROM_SOURCE &&
                   alpha->opacity == 0);
    return ELEMENT_BASE + i + 1;
}

int vc_dispmanx_element_change_attributes(DISPMANX_UPDATE_HANDLE_T update,
        DISPMANX_ELEMENT_HANDLE_T element_h, uint32_t change_flags, int32_t layer,
        uint8_t opacity, const VC_RECT_T *dest_rect, const VC_RECT_T *src_rect,
        DISPMANX_RESOURCE_HANDLE_T mask, DISPMANX_TRANSFORM_T transform)
{
    element *e = find(element_h);
    (void) update; (void) mask; (void) transform;
    if (!e)
        return -1;
    if (change_flags & ELEMENT_CHANGE_LAYER)
        e->layer = layer;
    if (change_flags & ELEMENT_CHANGE_OPACITY)
        e->visible = opacity != 0;
    if ((change_flags & ELEMENT_CHANGE_DEST_RECT) && dest_rect) {
        if (dest_rect->x != e->dst.x || dest_rect->y != e->dst.y ||
            dest_rect->width != e->dst.width || dest_rect->height != e->dst.height)
            uncover(e);             /* the old position shows the desktop again */
        e->dst = *dest_rect;
    }
    if ((change_flags & ELEMENT_CHANGE_SRC_RECT) && src_rect)
        e->src = *src_rect;
    if (!e->visible)
        uncover(e);
    return 0;
}

int vc_dispmanx_element_change_layer(DISPMANX_UPDATE_HANDLE_T update,
                                     DISPMANX_ELEMENT_HANDLE_T element_h, int32_t layer)
{
    return vc_dispmanx_element_change_attributes(update, element_h, ELEMENT_CHANGE_LAYER,
                                                 layer, 255, NULL, NULL, 0, 0);
}

int vc_dispmanx_element_remove(DISPMANX_UPDATE_HANDLE_T update,
                               DISPMANX_ELEMENT_HANDLE_T element_h)
{
    element *e = find(element_h);
    (void) update;
    if (!e)
        return -1;
    uncover(e);
    e->used = 0;
    return 0;
}

int vc_dispmanx_vsync_callback(DISPMANX_DISPLAY_HANDLE_T display,
                               DISPMANX_CALLBACK_FUNC_T cb_func, void *cb_arg)
{
    (void) display; (void) cb_func; (void) cb_arg;
    return -1;
}

/* ------------------------------------------------------------------ */
/* For libEGL                                                          */

int __riscos_dispmanx_window(const void *native_window, int *id, int *w, int *h)
{
    const EGL_DISPMANX_WINDOW_T *nw = (const EGL_DISPMANX_WINDOW_T *) native_window;
    _kernel_swi_regs r;
    int carry = 1;
    uintptr_t p = (uintptr_t) native_window;

    if (p < 0x8000 || (p & 3) != 0)
        return 0;
    r.r[0] = (int) p;
    r.r[1] = (int) (p + sizeof *nw);
    if (_kernel_swi_c(OS_ValidateAddress, &r, &r, &carry) != NULL || carry)
        return 0;                   /* not readable memory: not ours */
    if (!find(nw->element) || nw->width <= 0 || nw->height <= 0)
        return 0;
    *id = (int) nw->element;
    *w = nw->width;
    *h = nw->height;
    return 1;
}

int __riscos_dispmanx_placement(int id, riscos_dmx_placement *p)
{
    element *e = find((DISPMANX_ELEMENT_HANDLE_T) id);
    if (!e)
        return 0;
    p->visible = e->visible && e->resource == 0;
    p->x = e->dst.x;
    p->y = e->dst.y;
    p->w = e->dst.width;
    p->h = e->dst.height;
    p->src_x = e->src.x >> 16;
    p->src_y = e->src.y >> 16;
    p->src_w = e->src.width >> 16;
    p->src_h = e->src.height >> 16;
    return 1;
}

/* Broadcom's closest-match config choice (see EGL/eglext_brcm.h). */
EGLBoolean eglSaneChooseConfigBRCM(EGLDisplay dpy, const EGLint *attrib_list,
                                   EGLConfig *configs, EGLint config_size,
                                   EGLint *num_config)
{
    EGLint a[128];
    int i, n = 0;
    for (i = 0; attrib_list && attrib_list[i] != EGL_NONE && n < 126; i += 2) {
        if (attrib_list[i] == EGL_SAMPLES || attrib_list[i] == EGL_SAMPLE_BUFFERS)
            continue;
        a[n++] = attrib_list[i];
        a[n++] = attrib_list[i + 1];
    }
    a[n] = EGL_NONE;
    return eglChooseConfig(dpy, a, configs, config_size, num_config);
}
