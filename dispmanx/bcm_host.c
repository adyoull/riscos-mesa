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
 *
 * Window mode (the default in the desktop): the "display" is a desktop
 * window rather than the screen. graphics_get_display_size reports the
 * window's size (640 pixels wide unless set otherwise), so the program
 * renders that many pixels, and libEGL plots its elements into the window.
 * The library is a Wimp task: it polls the Wimp after each eglSwapBuffers,
 * so the program multitasks, and closing the window ends the program.
 * <App>$Display (App: the program's application directory, e.g.
 * HelloTeapot) or DispmanX$Display chooses: "full" = the whole screen as on
 * the Pi; "WxH" or "W" = a window of that size.
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
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/eglext_riscos.h>
#include <stdio.h>
#include <unixlib/local.h>

#ifndef OS_ValidateAddress
#define OS_ValidateAddress 0x3A
#endif
#ifndef Wimp_ForceRedraw
#define Wimp_ForceRedraw 0x400D1
#endif
#ifndef Wimp_ReadSysInfo
#define Wimp_ReadSysInfo 0x400F2
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

/* Window mode (see the top of the file). */
#define DEFAULT_WINDOW_WIDTH 640
static int mode_chosen;
static int wimp_task, wimp_window;   /* 0 = full screen mode */
static int disp_w, disp_h;           /* the display (the window), pixels */
static int wscale = 1;               /* 2 in EX0 EY0 modes, as a 90 dpi mode shows it */
static int closing;
static char app_name[64];
static char win_title[80];
static void finish_window(void);

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
    if (wimp_window) {                  /* window mode: clear that part of the window */
        r.r[0] = wimp_window;
        r.r[1] = (e->dst.x * wscale) << xe;
        r.r[2] = -(((e->dst.y + e->dst.height) * wscale) << ye);
        r.r[3] = ((e->dst.x + e->dst.width) * wscale) << xe;
        r.r[4] = -((e->dst.y * wscale) << ye);
        _kernel_swi(Wimp_ForceRedraw, &r, &r);
        return;
    }
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
    closing = 1;
    for (i = 0; i < MAX_ELEMENTS; i++)
        if (elements[i].used) {
            uncover(&elements[i]);
            elements[i].used = 0;
        }
    finish_window();
}

static void setup_atexit(void)
{
    if (!atexit_done) {
        atexit(remove_all);         /* never leave the desktop painted over */
        atexit_done = 1;
    }
}

/* The program's name, from its application directory (...!HelloTeapot.x). */
extern char *program_invocation_name;
static void find_app_name(void)
{
    char ro[256], canon[256], *leaf, *end;
    const char *path = program_invocation_name;
    _kernel_swi_regs r;

    strcpy(app_name, "DispmanX");
    if (!path || !*path)
        return;
    if (strchr(path, '/') && __riscosify_std(path, 0, ro, sizeof ro, NULL))
        path = ro;
    r.r[0] = 37; r.r[1] = (int) path; r.r[2] = (int) canon;
    r.r[3] = 0; r.r[4] = 0; r.r[5] = sizeof canon;
    if (_kernel_swi(OS_FSControl, &r, &r) != NULL)
        return;
    if (!(end = strrchr(canon, '.')))
        return;
    *end = 0;
    leaf = strrchr(canon, '.');
    leaf = leaf ? leaf + 1 : canon;
    if (leaf[0] == '!' && leaf[1])
        snprintf(app_name, sizeof app_name, "%s", leaf + 1);
}

static void finish_window(void)
{
    _kernel_swi_regs r;
    int block[1];
    if (wimp_window) {
        block[0] = wimp_window;
        r.r[1] = (int) block;
        _kernel_swi(Wimp_DeleteWindow, &r, &r);
        wimp_window = 0;
    }
    if (wimp_task) {
        r.r[0] = wimp_task;
        r.r[1] = 0x4B534154;                     /* "TASK" */
        _kernel_swi(Wimp_CloseDown, &r, &r);
        wimp_task = 0;
    }
}

/* Decide between window mode and full screen, once. */
static void choose_mode(void)
{
    static const int messages[] = { 0 };
    char var[80], val[32];
    const char *v;
    int sw, sh, xe, ye, w = DEFAULT_WINDOW_WIDTH, h = 0, wb[23], block[8];
    _kernel_swi_regs r;

    if (mode_chosen)
        return;
    mode_chosen = 1;
    find_app_name();
    snprintf(var, sizeof var, "%s$Display", app_name);
    v = getenv(var);
    if (!v || !*v)
        v = getenv("DispmanX$Display");
    if (v && *v) {
        snprintf(val, sizeof val, "%s", v);
        if (strncmp(val, "full", 4) == 0 || strncmp(val, "Full", 4) == 0)
            return;                                  /* the whole screen, as on the Pi */
        w = atoi(val);
        if (strchr(val, 'x')) h = atoi(strchr(val, 'x') + 1);
        if (w < 16) w = DEFAULT_WINDOW_WIDTH;
    }
    /* Only in the desktop: outside it, the whole screen. */
    r.r[0] = 0;
    if (_kernel_swi(Wimp_ReadSysInfo, &r, &r) != NULL || r.r[0] == 0)
        return;
    screen_size(&sw, &sh, &xe, &ye);
    if (w > sw) w = sw;
    if (h <= 0) h = w * sh / sw;                     /* the screen's shape */
    if (h > sh - 64) h = sh - 64;
    wscale = (xe == 0 && ye == 0 && 2 * w <= sw && 2 * h <= sh - 64) ? 2 : 1;
    r.r[0] = 380; r.r[1] = 0x4B534154; r.r[2] = (int) app_name; r.r[3] = (int) messages;
    if (_kernel_swi(Wimp_Initialise, &r, &r) != NULL)
        return;
    wimp_task = r.r[1];

    snprintf(win_title, sizeof win_title, "%s", app_name);
    memset(wb, 0, sizeof wb);
    {
        int ow = (w * wscale) << xe, oh = (h * wscale) << ye;
        int x0 = ((sw << xe) - ow) / 2, y0 = ((sh << ye) - oh) / 2;
        wb[0] = x0; wb[1] = y0; wb[2] = x0 + ow; wb[3] = y0 + oh;
        wb[10] = 0; wb[11] = -oh; wb[12] = ow; wb[13] = 0;   /* work area = the display */
    }
    wb[6] = -1;
    wb[7] = (int) 0x87000002u;                   /* moveable; back, close, title */
    wb[8] = 7 | (2 << 8) | (7 << 16) | (0 << 24);  /* work area background black */
    wb[9] = 3 | (1 << 8) | (12 << 16);
    wb[14] = 0x07000119;                         /* indirected text title */
    wb[16] = 1;
    wb[18] = (int) win_title; wb[19] = -1; wb[20] = sizeof win_title;
    r.r[1] = (int) wb;
    if (_kernel_swi(Wimp_CreateWindow, &r, &r) != NULL) {
        finish_window();
        return;
    }
    wimp_window = r.r[0];
    block[0] = wimp_window;
    memcpy(&block[1], wb, 7 * sizeof(int));
    r.r[1] = (int) block;
    _kernel_swi(Wimp_OpenWindow, &r, &r);
    disp_w = w;
    disp_h = h;
}

/* The display's size: the window in window mode, else the screen. */
static void display_size(int *w, int *h)
{
    choose_mode();
    if (wimp_window) {
        *w = disp_w;
        *h = disp_h;
    } else {
        screen_size(w, h, NULL, NULL);
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
    choose_mode();
}

void bcm_host_deinit(void)
{
}

int32_t graphics_get_display_size(const uint16_t display_number,
                                  uint32_t *width, uint32_t *height)
{
    int w, h;
    (void) display_number;
    display_size(&w, &h);
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
    display_size(&w, &h);
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
    display_size(&w, &h);
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
    p->window = wimp_window;            /* 0: the screen */
    p->x = e->dst.x * wscale;
    p->y = e->dst.y * wscale;
    p->w = e->dst.width * wscale;
    p->h = e->dst.height * wscale;
    p->src_x = e->src.x >> 16;
    p->src_y = e->src.y >> 16;
    p->src_w = e->src.width >> 16;
    p->src_h = e->src.height >> 16;
    return 1;
}

/* Window mode: after each frame, handle the Wimp's events until there are
   none left, so the program multitasks. Closing the window (or the desktop
   quitting) ends the program: Pi programs have no other way to be told. */
void __riscos_dispmanx_swapped(void)
{
    int block[64];
    _kernel_swi_regs r;

    if (!wimp_task || closing)
        return;
    for (;;) {
        r.r[0] = 0;                              /* null events on: return at once */
        r.r[1] = (int) block;
        if (_kernel_swi(Wimp_Poll, &r, &r) != NULL)
            return;
        switch (r.r[0]) {
        case 0:                                  /* nothing else to do */
            return;
        case 1:                                  /* Redraw_Window_Request */
            if (!eglRedrawWindowRISCOS(eglGetDisplay(EGL_DEFAULT_DISPLAY), block)) {
                r.r[1] = (int) block;            /* nothing to show: background only */
                if (_kernel_swi(Wimp_RedrawWindow, &r, &r) == NULL)
                    while (r.r[0]) { r.r[1] = (int) block; _kernel_swi(Wimp_GetRectangle, &r, &r); }
            }
            break;
        case 2:                                  /* Open_Window_Request */
            r.r[1] = (int) block;
            _kernel_swi(Wimp_OpenWindow, &r, &r);
            break;
        case 3:                                  /* Close_Window_Request */
            if (block[0] == wimp_window) {
                closing = 1;
                exit(0);
            }
            break;
        case 8:                                  /* Key_Pressed: the program reads the keyboard itself */
            r.r[0] = block[6];
            _kernel_swi(Wimp_ProcessKey, &r, &r);
            break;
        case 17: case 18:                        /* Message_Quit */
            if (block[4] == 0) {
                closing = 1;
                exit(0);
            }
            break;
        }
    }
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
