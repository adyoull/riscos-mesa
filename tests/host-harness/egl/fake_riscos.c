/*
 * fake_riscos.c - just enough RISC OS for egl_riscos.c on a Linux host:
 * a 32bpp screen in memory, a few Wimp windows (one redraw rectangle each:
 * the visible area), OS_SpriteOp create/plot, mode and VDU variables.
 * Pointers go through 32-bit registers as on RISC OS, so everything that
 * reaches a SWI must live below 2 GB (the harness builds -no-pie, keeps
 * malloc on brk and runs its tests on a low stack).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <kernel.h>
#include <swis.h>
#include "fake_riscos.h"

fake_screen_t fake_screen;
fake_window_t fake_windows[FAKE_MAX_WINDOWS];
int fake_vsyncs, fake_update_calls, fake_redraw_calls, fake_plots;
int fake_force_redraws, fake_force_rect[5], fake_scaled_plots;
int fake_wimp_nulls, fake_wimp_script[16][2], fake_wimp_script_len;
int fake_wimp_polls, fake_wimp_keys_passed, fake_wimp_tasks;
void (*fake_wimp_hook)(int reason);
const char *fake_wimp_title;
static int wimp_next_handle = 0x7000, wimp_last_window, wimp_stage, wimp_step;

static _kernel_oserror err = { 1, "fake error" };
static int clip[4];                  /* graphics window, OS units x0,y0,x1,y1 (excl.) */

/* A 32bpp mode selector like OS_ScreenMode 1 would return. */
static int screen_selector[16];

static _kernel_oserror *error(const char *msg)
{
    snprintf(err.errmess, sizeof err.errmess, "%s", msg);
    return &err;
}

void fake_set_screen(int w, int h, int trgb, int log2bpp)
{
    free(fake_screen.mem);
    fake_screen.w = w;
    fake_screen.h = h;
    fake_screen.xeig = fake_screen.yeig = 1;
    fake_screen.log2bpp = log2bpp;
    fake_screen.flags = trgb ? 0x4000 : 0;
    fake_screen.line_length = w * 4;
    fake_screen.mem = calloc((size_t) w * h * 3, 4);
    fake_screen.da_size = fake_screen.da_max = w * h * 4;     /* one bank, can't grow */
    fake_screen.vdu_bank = fake_screen.display_bank = 1;
}

fake_window_t *fake_open_window(int handle, int x0, int y0, int x1, int y1, int sx, int sy)
{
    int i;
    for (i = 0; i < FAKE_MAX_WINDOWS; i++) {
        if (fake_windows[i].handle == 0 || fake_windows[i].handle == handle) {
            fake_windows[i].handle = handle;
            fake_windows[i].x0 = x0; fake_windows[i].y0 = y0;
            fake_windows[i].x1 = x1; fake_windows[i].y1 = y1;
            fake_windows[i].sx = sx; fake_windows[i].sy = sy;
            return &fake_windows[i];
        }
    }
    return NULL;
}

static fake_window_t *find_window(int handle)
{
    int i;
    for (i = 0; i < FAKE_MAX_WINDOWS; i++)
        if (fake_windows[i].handle == handle && handle != 0)
            return &fake_windows[i];
    return NULL;
}

unsigned int fake_bank_pixel(int bank, int x, int y_from_top)
{
    return fake_screen.mem[(size_t) (bank - 1) * fake_screen.w * fake_screen.h +
                           (size_t) y_from_top * (fake_screen.line_length / 4) + x];
}

unsigned int fake_screen_pixel(int x, int y_from_top)
{
    return fake_screen.mem[(size_t) y_from_top * (fake_screen.line_length / 4) + x];
}

/* Colour order of a sprite mode word or mode selector: 1 = TRGB. -1 if not 32bpp. */
static int mode_info(int mode, int *log2bpp, int *flags)
{
    if (mode == -1) {
        *log2bpp = fake_screen.log2bpp;
        *flags = fake_screen.flags;
        return 1;
    }
    if (mode & 1) {                                 /* sprite mode word */
        int type = (mode >> 27) & 15;
        if (type == 6) { *log2bpp = 5; *flags = 0; return 1; }
        if (type == 5) { *log2bpp = 4; *flags = 0; return 1; }
        return 0;
    }
    if (mode >= 256) {                              /* mode selector */
        const int *sel = (const int *) (long) mode;
        int i;
        if ((sel[0] & 0xFF) != 1) return 0;
        *log2bpp = sel[3];
        *flags = 0;
        for (i = 5; sel[i] != -1; i += 2)
            if (sel[i] == 0) *flags = sel[i + 1];
        return 1;
    }
    *log2bpp = 3;                                   /* old numbered mode: 8bpp */
    *flags = 0;
    return 1;
}

static _kernel_oserror *sprite_op(_kernel_swi_regs *r)
{
    int reason = r->r[0] & 255;

    if (reason == 15) {                             /* create sprite */
        int *area = (int *) (long) r->r[1];
        int w = r->r[4], h = r->r[5], mode = r->r[6], log2bpp, flags, size;
        int *spr;
        if ((r->r[0] & 0xF00) != 0x100) return error("create: user area only");
        if (!mode_info(mode, &log2bpp, &flags) || log2bpp != 5)
            return error("create: 32bpp only in the fake");
        size = 44 + w * h * 4;
        if (area[3] + size > area[0]) return error("Not enough room in sprite area");
        spr = (int *) ((char *) area + area[3]);
        memset(spr, 0, size);
        spr[0] = size;
        strncpy((char *) &spr[1], (const char *) (long) r->r[2], 12);
        spr[4] = w - 1;
        spr[5] = h - 1;
        spr[6] = 0;
        spr[7] = 31;
        spr[8] = 44;
        spr[9] = 44;
        spr[10] = mode;
        area[1]++;
        area[3] += size;
        return NULL;
    }
    if (reason == 34 || reason == 52) {             /* put sprite (scaled) */
        const int *spr = (const int *) (long) r->r[2];
        const int *f = reason == 52 ? (const int *) (long) r->r[6] : NULL;
        int w = spr[4] + 1, h = spr[5] + 1, log2bpp, flags, dx, dy, ow, oh, swap, sxe, sye;
        int px0 = r->r[3] >> fake_screen.xeig, py0 = r->r[4] >> fake_screen.yeig;
        const unsigned int *pix = (const unsigned int *) ((const char *) spr + spr[8]);
        if ((r->r[0] & 0xF00) != 0x200) return error("plot: pointer form only");
        mode_info(spr[10], &log2bpp, &flags);
        swap = ((flags ^ fake_screen.flags) & 0x4000) != 0;
        /* the sprite's own pixel size: a mode word's dpi, else the screen's */
        sxe = fake_screen.xeig; sye = fake_screen.yeig;
        if ((spr[10] & 1) && ((unsigned) spr[10] >> 27) != 0) {
            int xdpi = (spr[10] >> 1) & 0x1FFF, ydpi = (spr[10] >> 14) & 0x1FFF;
            sxe = xdpi >= 180 ? 0 : xdpi >= 90 ? 1 : 2;
            sye = ydpi >= 180 ? 0 : ydpi >= 90 ? 1 : 2;
        }
        ow = (int) (((long long) (w << sxe) * (f ? f[0] : 1) / (f ? f[2] : 1)) >> fake_screen.xeig);
        oh = (int) (((long long) (h << sye) * (f ? f[1] : 1) / (f ? f[3] : 1)) >> fake_screen.yeig);
        fake_plots++;
        if (reason == 52) fake_scaled_plots++;
        for (dy = 0; dy < oh; dy++) {               /* dy: rows from the top of the plot */
            int y_up = py0 + (oh - 1 - dy), oy = y_up << fake_screen.yeig;
            int sy = (int) ((long long) dy * h / oh);
            if (y_up < 0 || y_up >= fake_screen.h || oy < clip[1] || oy >= clip[3]) continue;
            for (dx = 0; dx < ow; dx++) {
                int x = px0 + dx, ox = x << fake_screen.xeig;
                unsigned int p = pix[(size_t) sy * w + (int) ((long long) dx * w / ow)];
                if (x < 0 || x >= fake_screen.w || ox < clip[0] || ox >= clip[2]) continue;
                if (swap) p = (p & 0xFF00FF00u) | ((p >> 16) & 0xFF) | ((p & 0xFF) << 16);
                if (x >= fake_watch[0] && x < fake_watch[2] &&
                    (fake_screen.h - 1 - y_up) >= fake_watch[1] && (fake_screen.h - 1 - y_up) < fake_watch[3] &&
                    (p & 0xFFFFFF) == (unsigned) fake_watch_value)
                    fake_watch_hits++;
                fake_screen.mem[(size_t) (fake_screen.h - 1 - y_up) * (fake_screen.line_length / 4) + x] = p;
            }
        }
        return NULL;
    }
    return error("SpriteOp reason not faked");
}

static void full_clip(void)
{
    clip[0] = 0;
    clip[1] = 0;
    clip[2] = fake_screen.w << fake_screen.xeig;
    clip[3] = fake_screen.h << fake_screen.yeig;
}

/* Fill in a redraw block for window w with one rectangle: the visible area
   clipped to the work area rectangle wa (NULL = everything). */
static int start_redraw(int *block, fake_window_t *w, const int *wa)
{
    int ox = w->x0 - w->sx, oy = w->y1 - w->sy;     /* work area origin on screen */
    int r0 = w->x0, r1 = w->y0, r2 = w->x1, r3 = w->y1;
    if (wa) {
        if (wa[0] + ox > r0) r0 = wa[0] + ox;
        if (wa[1] + oy > r1) r1 = wa[1] + oy;
        if (wa[2] + ox < r2) r2 = wa[2] + ox;
        if (wa[3] + oy < r3) r3 = wa[3] + oy;
    }
    block[1] = w->x0; block[2] = w->y0; block[3] = w->x1; block[4] = w->y1;
    block[5] = w->sx; block[6] = w->sy;
    block[7] = r0; block[8] = r1; block[9] = r2; block[10] = r3;
    if (r0 >= r2 || r1 >= r3)
        return 0;
    clip[0] = r0; clip[1] = r1; clip[2] = r2; clip[3] = r3;
    return 1;
}

_kernel_oserror *_kernel_swi(int no, _kernel_swi_regs *in, _kernel_swi_regs *out)
{
    _kernel_swi_regs r = *in;
    _kernel_oserror *e = NULL;
    fake_window_t *w;
    int *block;

    switch (no) {
    case OS_SpriteOp:
        e = sprite_op(&r);
        break;
    case OS_ReadVduVariables: {
        const int *vars = (const int *) (long) r.r[0];
        int *vals = (int *) (long) r.r[1];
        for (; *vars != -1; vars++, vals++) {
            switch (*vars) {
            case 0:   *vals = fake_screen.flags; break;
            case 4:   *vals = fake_screen.xeig; break;
            case 5:   *vals = fake_screen.yeig; break;
            case 6:   *vals = fake_screen.line_length; break;
            case 9:   *vals = fake_screen.log2bpp; break;
            case 11:  *vals = fake_screen.w - 1; break;
            case 12:  *vals = fake_screen.h - 1; break;
            case 7:   *vals = fake_screen.w * fake_screen.h * 4; break;
            case 148: *vals = (int) (long) (fake_screen.mem + (size_t) (fake_screen.vdu_bank - 1) *
                                            fake_screen.w * fake_screen.h); break;
            default:  *vals = 0;
            }
        }
        break;
    }
    case OS_ReadModeVariable: {
        int log2bpp, flags;
        if (!mode_info(r.r[0], &log2bpp, &flags)) { e = error("bad mode"); break; }
        r.r[2] = (r.r[1] == 9) ? log2bpp : (r.r[1] == 0) ? flags : 0;
        break;
    }
    case 0x5C:      /* OS_ReadDynamicArea */
        if (r.r[0] != 2) { e = error("only the screen area is faked"); break; }
        r.r[1] = fake_screen.da_size;
        r.r[2] = fake_screen.da_max;
        break;
    case 0x2A:      /* OS_ChangeDynamicArea */
        if (r.r[0] != 2) { e = error("only the screen area is faked"); break; }
        if (fake_screen.da_size + r.r[1] > fake_screen.da_max) { e = error("Area can't grow"); break; }
        fake_screen.da_size += r.r[1];
        break;
    case OS_ScreenMode:
        if (r.r[0] != 1) { e = error("ScreenMode reason not faked"); break; }
        screen_selector[0] = 1;
        screen_selector[1] = fake_screen.w;
        screen_selector[2] = fake_screen.h;
        screen_selector[3] = fake_screen.log2bpp;
        screen_selector[4] = 60;
        screen_selector[5] = 0;
        screen_selector[6] = fake_screen.flags;
        screen_selector[7] = -1;
        r.r[1] = (int) (long) screen_selector;
        break;
    case 0x400C0:   /* Wimp_Initialise */
        fake_wimp_tasks++;
        wimp_stage = wimp_step = 0;
        r.r[0] = 380; r.r[1] = 0x4A00;
        break;
    case 0x400DD:   /* Wimp_CloseDown */
        fake_wimp_tasks--;
        break;
    case 0x400C1:   /* Wimp_CreateWindow: opened by Wimp_OpenWindow */
        block = (int *) (long) r.r[1];
        if (block[14] & 0x100) fake_wimp_title = (const char *) (long) block[18];   /* indirected */
        r.r[0] = wimp_last_window = wimp_next_handle++;
        break;
    case 0x400C5:   /* Wimp_OpenWindow */
        block = (int *) (long) r.r[1];
        if (!fake_open_window(block[0], block[1], block[2], block[3], block[4], block[5], block[6]))
            e = error("too many windows");
        break;
    case 0x400C6:   /* Wimp_CloseWindow */
    case 0x400C3: { /* Wimp_DeleteWindow */
        fake_window_t *cw = find_window(*(int *) (long) r.r[1]);
        if (cw) cw->handle = 0;
        break;
    }
    case 0x400D2:   /* Wimp_SetCaretPosition */
        break;
    case 0x400DC:   /* Wimp_ProcessKey: a key the task didn't use */
        fake_wimp_keys_passed++;
        break;
    case 0x400C7:   /* Wimp_Poll */
    case 0x400E1: { /* Wimp_PollIdle */
        int reason;
        block = (int *) (long) r.r[1];
        memset(block, 0, 64);
        fake_wimp_polls++;
        if (wimp_stage == 0) { reason = 1; block[0] = wimp_last_window; wimp_stage = 1; }
        else if (wimp_stage == 1 && wimp_step < fake_wimp_nulls && !(r.r[0] & 1)) {
            reason = 0; wimp_step++;
        } else {
            if (wimp_stage == 1) { wimp_stage = 2; wimp_step = 0; }
            if (wimp_step < fake_wimp_script_len) {
                reason = fake_wimp_script[wimp_step][0];
                block[0] = wimp_last_window;
                if (reason == 8) block[6] = fake_wimp_script[wimp_step][1];
                if (reason == 2) {           /* Open_Window_Request: new width and height, OS units */
                    fake_window_t *ow = find_window(wimp_last_window);
                    int v = fake_wimp_script[wimp_step][1];
                    if (ow) {
                        block[1] = ow->x0; block[4] = ow->y1;
                        block[3] = ow->x0 + (v & 0xFFFF); block[2] = ow->y1 - (v >> 16);
                        block[5] = ow->sx; block[6] = ow->sy; block[7] = -1;
                    }
                }
                if (reason == 6) block[2] = 4, block[3] = wimp_last_window;
                wimp_step++;
            } else { reason = 3; block[0] = wimp_last_window; }
        }
        if (fake_wimp_hook) fake_wimp_hook(reason);
        r.r[0] = reason;
        break;
    }
    case 0x42:      /* OS_ReadMonotonicTime: centiseconds */
        r.r[0] = fake_wimp_polls * 2;
        break;
    case 0x400CB:   /* Wimp_GetWindowState */
        block = (int *) (long) r.r[1];
        if (!(w = find_window(block[0]))) { e = error("Illegal window handle"); break; }
        block[1] = w->x0; block[2] = w->y0; block[3] = w->x1; block[4] = w->y1;
        block[5] = w->sx; block[6] = w->sy; block[7] = -1; block[8] = 1 << 16;
        break;
    case 0x400C8:   /* Wimp_RedrawWindow */
        block = (int *) (long) r.r[1];
        if (!(w = find_window(block[0]))) { e = error("Illegal window handle"); break; }
        fake_redraw_calls++;
        r.r[0] = start_redraw(block, w, NULL);
        break;
    case 0x400C9: { /* Wimp_UpdateWindow */
        int wa[4];
        block = (int *) (long) r.r[1];
        if (!(w = find_window(block[0]))) { e = error("Illegal window handle"); break; }
        fake_update_calls++;
        memcpy(wa, &block[1], sizeof wa);
        r.r[0] = start_redraw(block, w, wa);
        break;
    }
    case 0x400D1:   /* Wimp_ForceRedraw */
        fake_force_redraws++;
        fake_force_rect[0] = r.r[0]; fake_force_rect[1] = r.r[1]; fake_force_rect[2] = r.r[2];
        fake_force_rect[3] = r.r[3]; fake_force_rect[4] = r.r[4];
        break;
    case 0x400CA:   /* Wimp_GetRectangle: only ever one rectangle */
        r.r[0] = 0;
        full_clip();
        break;
    default:
        e = error("SWI not faked");
    }
    if (out) *out = r;
    return e;
}

/* OS_ValidateAddress: carry clear if every page of r0..r1 is mapped. */
_kernel_oserror *_kernel_swi_c(int no, _kernel_swi_regs *in, _kernel_swi_regs *out, int *carry)
{
    if (no == 0x3A) {
        long pg = sysconf(_SC_PAGESIZE);
        unsigned long a = (unsigned long) (unsigned) in->r[0] & ~(pg - 1);
        unsigned long end = (unsigned long) (unsigned) in->r[1];
        unsigned char v;
        *carry = 0;
        for (; a < end; a += pg)
            if (mincore((void *) a, pg, &v) != 0) { *carry = 1; break; }
        if (out) *out = *in;
        return NULL;
    }
    *carry = 0;
    return _kernel_swi(no, in, out);
}

int _kernel_osbyte(int op, int x, int y)
{
    (void) y;
    int banks = fake_screen.da_size / (fake_screen.w * fake_screen.h * 4);
    if (op == 19) fake_vsyncs++;
    if ((op == 112 || op == 113) && x >= 0 && x <= banks) {
        if (x == 0) x = fake_screen.display_bank;
        if (op == 112) fake_screen.vdu_bank = x;
        else fake_screen.display_bank = x;
    }
    return 0;
}

/* VDU 24 (graphics window) only; four signed 16-bit inclusive coordinates. */
int fake_watch[4], fake_watch_value, fake_watch_hits;   /* pixels written in a rect with a value */

int _kernel_oswrch(int c)
{
    static int state = -1, bytes[8];
    if (state < 0) {
        if (c == 24) state = 0;
        return 0;
    }
    bytes[state++] = c & 0xFF;
    if (state == 8) {
        int i, v[4];
        for (i = 0; i < 4; i++)
            v[i] = (short) (bytes[2 * i] | (bytes[2 * i + 1] << 8));
        clip[0] = v[0]; clip[1] = v[1]; clip[2] = v[2] + 1; clip[3] = v[3] + 1;
        state = -1;
    }
    return 0;
}

void fake_reset_clip(void)
{
    full_clip();
}
