/*
 * riscos_wimpwin.c - the smallest Wimp task with one window (see the .h).
 *
 * Part of riscos-mesa. MIT licence (see LICENCES.txt).
 */
#define EGL_EGLEXT_PROTOTYPES 1     /* eglRedrawWindowRISCOS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>
#include <EGL/egl.h>
#include <EGL/eglext_riscos.h>
#include "riscos_wimpwin.h"

#define TASK 0x4B534154             /* "TASK" */

static int task;

static int vdu_var(int var)
{
    int in[2] = { var, -1 }, out[1] = { 0 };
    _kernel_swi_regs r;
    r.r[0] = (int) in; r.r[1] = (int) out;
    _kernel_swi(OS_ReadVduVariables, &r, &r);
    return out[0];
}

int rw_init(const char *task_name)
{
    static const int messages[] = { 0 };     /* Message_Quit only */
    _kernel_swi_regs r;
    if (task) return 1;
    r.r[0] = 380; r.r[1] = TASK; r.r[2] = (int) task_name; r.r[3] = (int) messages;
    if (_kernel_swi(Wimp_Initialise, &r, &r) != NULL)
        return 0;
    task = r.r[1];
    return 1;
}

int rw_open(rw_window *w, const char *title, int x, int y, int width, int height)
{
    int wb[23], block[8];
    int xeig = vdu_var(4), yeig = vdu_var(5);
    int sw = (vdu_var(11) + 1) << xeig, sh = (vdu_var(12) + 1) << yeig;
    int ow = width << xeig, oh = height << yeig, x0, y1;
    _kernel_swi_regs r;

    memset(w, 0, sizeof *w);
    w->width = width; w->height = height;
    strncpy(w->title, title, sizeof w->title - 1);
    x0 = x < 0 ? (sw - ow) / 2 : x << xeig;
    y1 = y < 0 ? sh - (sh - oh) / 2 : sh - (y << yeig);
    memset(wb, 0, sizeof wb);
    wb[0] = x0; wb[1] = y1 - oh; wb[2] = x0 + ow; wb[3] = y1;
    wb[6] = -1;                                  /* on top */
    wb[7] = (int) 0xAF000002u;                   /* moveable; back, close, title, toggle, adjust */
    wb[8] = 7 | (2 << 8) | (7 << 16) | (4 << 24);
    wb[9] = 3 | (1 << 8) | (12 << 16);
    wb[10] = 0; wb[11] = -(sh > oh ? sh : oh);   /* work area: up to the screen size */
    wb[12] = sw > ow ? sw : ow; wb[13] = 0;
    wb[14] = 0x07000119;                         /* indirected text title */
    wb[16] = 1;
    wb[18] = (int) w->title; wb[19] = -1; wb[20] = sizeof w->title;
    r.r[1] = (int) wb;
    if (_kernel_swi(Wimp_CreateWindow, &r, &r) != NULL)
        return 0;
    w->handle = r.r[0];
    block[0] = w->handle;
    memcpy(&block[1], wb, 7 * sizeof(int));
    r.r[1] = (int) block;
    _kernel_swi(Wimp_OpenWindow, &r, &r);
    return 1;
}

void rw_set_title(rw_window *w, const char *title)
{
    _kernel_swi_regs r;
    strncpy(w->title, title, sizeof w->title - 1);
    r.r[0] = w->handle; r.r[1] = TASK; r.r[2] = 3;        /* redraw the title bar */
    _kernel_swi(Wimp_ForceRedraw, &r, &r);
}

void rw_pass_key(int key)
{
    _kernel_swi_regs r;
    r.r[0] = key;
    _kernel_swi(Wimp_ProcessKey, &r, &r);
}

int rw_poll(rw_window *w, EGLDisplay dpy, int want_idle, int *key)
{
    int block[64];
    _kernel_swi_regs r;

    r.r[0] = want_idle ? 0 : 1;                  /* mask null events when not animating */
    r.r[1] = (int) block;
    if (_kernel_swi(Wimp_Poll, &r, &r) != NULL)
        return RW_CLOSE;
    switch (r.r[0]) {
    case 0:                                      /* Null_Reason_Code */
        return RW_IDLE;
    case 1:                                      /* Redraw_Window_Request */
        if (dpy == EGL_NO_DISPLAY || !eglRedrawWindowRISCOS(dpy, block)) {
            r.r[1] = (int) block;
            _kernel_swi(Wimp_RedrawWindow, &r, &r);
            while (r.r[0]) { r.r[1] = (int) block; _kernel_swi(Wimp_GetRectangle, &r, &r); }
        }
        return RW_NONE;
    case 2:                                      /* Open_Window_Request */
        r.r[1] = (int) block;
        _kernel_swi(Wimp_OpenWindow, &r, &r);
        return RW_OPEN;
    case 3:                                      /* Close_Window_Request */
        return block[0] == w->handle ? RW_CLOSE : RW_NONE;
    case 6:                                      /* Mouse_Click: take the input focus */
        if (block[3] == w->handle) {
            r.r[0] = w->handle; r.r[1] = -1; r.r[2] = 0; r.r[3] = 0;
            r.r[4] = 1 << 25; r.r[5] = -1;      /* invisible caret */
            _kernel_swi(Wimp_SetCaretPosition, &r, &r);
        }
        return RW_NONE;
    case 8:                                      /* Key_Pressed */
        if (key) { *key = block[6]; return RW_KEY; }
        rw_pass_key(block[6]);
        return RW_NONE;
    case 17: case 18:                            /* Message_Quit */
        return block[4] == 0 ? RW_CLOSE : RW_NONE;
    }
    return RW_NONE;
}

void rw_close(rw_window *w)
{
    _kernel_swi_regs r;
    int block[1];
    if (!w->handle) return;
    block[0] = w->handle;
    r.r[1] = (int) block;
    _kernel_swi(Wimp_DeleteWindow, &r, &r);
    w->handle = 0;
}

void rw_end(void)
{
    _kernel_swi_regs r;
    if (!task) return;
    r.r[0] = task; r.r[1] = TASK;
    _kernel_swi(Wimp_CloseDown, &r, &r);
    task = 0;
}

static FILE *out;              /* the output file, when redirected */
static long out_seen;

int rw_redirect_output(const char *var)
{
    const char *o = getenv(var);
    if (!o || !*o || (out = freopen(o, "w+", stdout)) == NULL)
        return 0;
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (freopen(o, "a", stderr) != NULL)
        setvbuf(stderr, NULL, _IONBF, 0);
    return 1;
}

void rw_show_output(rw_window *w, const char *base)
{
    char buf[256], *p, *q;
    long end, from;
    size_t n;
    if (!out) return;
    fflush(stdout);
    end = ftell(stdout);
    if (end <= out_seen) return;
    from = end - (long) sizeof buf + 1;
    if (from < 0) from = 0;
    fseek(stdout, from, SEEK_SET);
    n = fread(buf, 1, (size_t) (end - from), stdout);
    fseek(stdout, 0, SEEK_END);
    out_seen = end;
    buf[n] = 0;
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    p = strrchr(buf, '\n');
    p = p ? p + 1 : buf;
    for (q = p; *q; q++) if ((unsigned char) *q < ' ') *q = ' ';
    if (*p) {
        char t[128];
        snprintf(t, sizeof t, "%.40s: %.80s", base, p);
        rw_set_title(w, t);
    }
}

unsigned rw_millis(void)
{
    _kernel_swi_regs r;
    _kernel_swi(OS_ReadMonotonicTime, &r, &r);
    return (unsigned) r.r[0] * 10u;
}
