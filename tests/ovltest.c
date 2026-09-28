/*
 * ovltest - measures what RISC OS hardware overlays (the VideoOverlay
 * module) can do on this machine, before riscos-mesa's EGL uses them.
 *
 *   ovltest vet     T1  which formats and sizes the machine offers
 *   ovltest bw      T2  how fast overlay memory is to write and read
 *   ovltest cache   T3  whether a frame written through the cache shows
 *   ovltest map     T4  whether buffers can stay mapped across Wimp_Poll
 *   ovltest tear    T5  whether buffer switches tear, and what they cost
 *   ovltest scale   T6  scaling quality and limits, resizing the window
 *   ovltest desk    T7  menus, windows, iconising and mode changes
 *   ovltest yuv     T8  YV12 colours, chroma placement and copy cost
 *
 * Each test is a Wimp task with one window: the overlay at the top and a
 * text panel below it with instructions and results. Everything shown in
 * the panel is also added to the file "ovlresults" in the current
 * directory (the Obey files set it to this directory), so the results can
 * be sent back as they are.
 *
 * The VideoOverlay SWIs, from the ROOL wiki (read 2026-09-28): Create
 * &59CC0, Destroy &59CC1, DisplayBuffer &59CC2, MapBuffer &59CC3,
 * UnmapBuffer &59CC4, DiscardBuffer &59CC5, Vet &59CC6, SetScale &59CC7,
 * SetWindow &59CC8, SetPosition &59CC9, RedrawWindow &59CCA. The numbers
 * are looked up by name at start-up (OS_SWINumberFromString) and logged.
 *
 * Written for riscos-ffmpeg's handoff 2026-09-28-mesa-egl-videooverlay.
 * Part of riscos-mesa. MIT licence (see LICENCES.txt).
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>
#include "hrtime.h"

/* ------------------------------------------------------------------ */
/* Small helpers                                                        */

#define PANEL_LINES 12
#define LINE_H      32                 /* OS units per text line */
#define PANEL_H     (PANEL_LINES * LINE_H + 16)

static FILE *logfile;
static char panel[PANEL_LINES][100];
static int panel_dirty;

static _kernel_oserror *swi(int no, _kernel_swi_regs *r)
{
    return _kernel_swi(no, r, r);
}

/* Show a line in the panel and add it to ovlresults. */
static void say(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    int i;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (logfile) { fprintf(logfile, "%s\n", line); fflush(logfile); }
    for (i = 0; i < PANEL_LINES - 1; i++)
        memcpy(panel[i], panel[i + 1], sizeof panel[i]);
    snprintf(panel[PANEL_LINES - 1], sizeof panel[0], "%.99s", line);
    panel_dirty = 1;
}

/* Log only (long tables), without scrolling the panel. */
static void note(const char *fmt, ...)
{
    va_list ap;
    if (!logfile) return;
    va_start(ap, fmt);
    vfprintf(logfile, fmt, ap);
    va_end(ap);
    fputc('\n', logfile);
    fflush(logfile);
}

static const char *err_text(_kernel_oserror *e)
{
    static char s[300];
    if (!e) return "ok";
    snprintf(s, sizeof s, "error &%X: %s", e->errnum, e->errmess);
    return s;
}

/* Durations (how long a test runs) use this clock. On the host test
   harness OVLTEST_VIRTUAL_MS makes each Wimp_Poll count as that many
   milliseconds, so the scripted runs don't depend on the host's speed.
   Measurements always use hr_seconds(). */
static int virtual_ms, polls;
static double now_ms(void)
{
    if (virtual_ms) return (double) polls * virtual_ms;
    return hr_seconds() * 1000.0;
}

static int mode_var(int var)
{
    _kernel_swi_regs r;
    r.r[0] = -1; r.r[1] = var;
    swi(OS_ReadModeVariable, &r);
    return r.r[2];
}

static int vdu_var(int var)
{
    int in[2] = { var, -1 }, out[1] = { 0 };
    _kernel_swi_regs r;
    r.r[0] = (int) in; r.r[1] = (int) out;
    swi(OS_ReadVduVariables, &r);
    return out[0];
}

/* ------------------------------------------------------------------ */
/* VideoOverlay                                                         */

enum { OV_CREATE, OV_DESTROY, OV_DISPLAY, OV_MAP, OV_UNMAP, OV_DISCARD, OV_VET,
       OV_SCALE, OV_WINDOW, OV_POSITION, OV_REDRAW, OV_COUNT };
static struct { const char *name; int no; } ov_swi[OV_COUNT] = {
    { "VideoOverlay_Create", 0x59CC0 },     { "VideoOverlay_Destroy", 0x59CC1 },
    { "VideoOverlay_DisplayBuffer", 0x59CC2 }, { "VideoOverlay_MapBuffer", 0x59CC3 },
    { "VideoOverlay_UnmapBuffer", 0x59CC4 }, { "VideoOverlay_DiscardBuffer", 0x59CC5 },
    { "VideoOverlay_Vet", 0x59CC6 },        { "VideoOverlay_SetScale", 0x59CC7 },
    { "VideoOverlay_SetWindow", 0x59CC8 },  { "VideoOverlay_SetPosition", 0x59CC9 },
    { "VideoOverlay_RedrawWindow", 0x59CCA },
};

/* A pixel format: Log2BPP, ModeFlags, NColour for the mode selector. */
typedef struct {
    const char *name;
    int log2bpp, flags, ncolour;
    int planes;                 /* 1 RGB, 2 NV12, 3 YV12/YV16 */
    int cw, ch;                 /* chroma subsampling (YUV) */
} fmt_t;

#define FOURCC(a, b, c, d) ((a) | (b) << 8 | (c) << 16 | (d) << 24)
/* YCbCr ModeFlags (Extended Framebuffer Format Specification): bits
   12-13 = 2 (YCbCr), bit 14 = video range, bit 15 = BT.709. */
#define YCC      0x2000
#define YCC_VID  0x4000
#define YCC_709  0x8000

static const fmt_t fmt_tbgr = { "TBGR32", 5, 0x0000, -1, 1, 1, 1 };
static const fmt_t formats[] = {
    { "TBGR32", 5, 0x0000, -1, 1, 1, 1 },
    { "TRGB32", 5, 0x4000, -1, 1, 1, 1 },
    { "ABGR32", 5, 0x8000, -1, 1, 1, 1 },
    { "ARGB32", 5, 0xC000, -1, 1, 1, 1 },
    { "RGB565", 4, 0x4080, 65535, 1, 1, 1 },
    { "BGR565", 4, 0x0080, 65535, 1, 1, 1 },
    { "YV12 601 full",  7, YCC,                     FOURCC('Y','V','1','2'), 3, 2, 2 },
    { "YV12 601 video", 7, YCC | YCC_VID,           FOURCC('Y','V','1','2'), 3, 2, 2 },
    { "YV12 709 full",  7, YCC | YCC_709,           FOURCC('Y','V','1','2'), 3, 2, 2 },
    { "YV12 709 video", 7, YCC | YCC_VID | YCC_709, FOURCC('Y','V','1','2'), 3, 2, 2 },
    { "YV16 601 full",  7, YCC,                     FOURCC('Y','V','1','6'), 3, 2, 1 },
    { "YV16 601 video", 7, YCC | YCC_VID,           FOURCC('Y','V','1','6'), 3, 2, 1 },
    { "YV16 709 full",  7, YCC | YCC_709,           FOURCC('Y','V','1','6'), 3, 2, 1 },
    { "YV16 709 video", 7, YCC | YCC_VID | YCC_709, FOURCC('Y','V','1','6'), 3, 2, 1 },
    { "NV12 601 full",  7, YCC,                     FOURCC('N','V','1','2'), 2, 2, 2 },
    { "NV12 601 video", 7, YCC | YCC_VID,           FOURCC('N','V','1','2'), 2, 2, 2 },
    { "NV12 709 full",  7, YCC | YCC_709,           FOURCC('N','V','1','2'), 2, 2, 2 },
    { "NV12 709 video", 7, YCC | YCC_VID | YCC_709, FOURCC('N','V','1','2'), 2, 2, 2 },
};
#define NFORMATS ((int) (sizeof formats / sizeof formats[0]))

typedef struct {
    int id;                     /* 0 = none */
    fmt_t fmt;
    int w, h, banks;
    int type, minw, minh, maxw, maxh;
    uint8_t *plane[3];          /* while mapped */
    int stride[3];
    int mapped;                 /* buffer number mapped, or -1 */
    int shown;                  /* last buffer displayed */
} ovl_t;

static int task_handle;

/* Mode selector: flags, width, height, Log2BPP, frame rate, then mode
   variable pairs: ModeFlags (0), NColour (3), MinScreenBanks (13). */
static int *selector(const fmt_t *f, int w, int h, int banks)
{
    static int s[12];
    s[0] = 1; s[1] = w; s[2] = h; s[3] = f->log2bpp; s[4] = -1;
    s[5] = 0;  s[6] = f->flags;
    s[7] = 3;  s[8] = f->ncolour;
    s[9] = 13; s[10] = banks;
    s[11] = -1;
    return s;
}

/* Vet (create = 0) or Create (create = 1). Fills in type and limits. */
static _kernel_oserror *ovl_vet_create(ovl_t *o, const fmt_t *f, int w, int h, int banks,
                                       int must_scale, int create)
{
    _kernel_swi_regs r;
    _kernel_oserror *e;
    memset(o, 0, sizeof *o);
    o->fmt = *f; o->w = w; o->h = h; o->banks = banks; o->mapped = -1; o->shown = -1;
    r.r[0] = (int) selector(f, w, h, banks);
    r.r[1] = (w << 16) | h;
    r.r[2] = must_scale ? 1 : 0;
    r.r[3] = task_handle;
    e = swi(ov_swi[create ? OV_CREATE : OV_VET].no, &r);
    if (e) return e;
    if (create) o->id = r.r[0];
    o->type = r.r[1] & 0xFF;
    o->minw = r.r[2]; o->minh = r.r[3]; o->maxw = r.r[4]; o->maxh = r.r[5];
    return NULL;
}

static _kernel_oserror *ovl_call2(int which, int id, int a)
{
    _kernel_swi_regs r;
    r.r[0] = id; r.r[1] = a;
    return swi(ov_swi[which].no, &r);
}

static _kernel_oserror *ovl_map(ovl_t *o, int buf)
{
    _kernel_swi_regs r;
    _kernel_oserror *e;
    const int *a;
    int i;
    r.r[0] = o->id; r.r[1] = buf;
    if ((e = swi(ov_swi[OV_MAP].no, &r)) != NULL) return e;
    a = (const int *) r.r[0];
    for (i = 0; i < o->fmt.planes; i++) {
        o->plane[i] = (uint8_t *) a[2 * i];
        o->stride[i] = a[2 * i + 1];
    }
    o->mapped = buf;
    return NULL;
}

static _kernel_oserror *ovl_unmap(ovl_t *o)
{
    _kernel_oserror *e = NULL;
    if (o->mapped >= 0) e = ovl_call2(OV_UNMAP, o->id, o->mapped);
    o->mapped = -1;
    return e;
}

static _kernel_oserror *ovl_display(ovl_t *o, int buf)
{
    o->shown = buf;
    return ovl_call2(OV_DISPLAY, o->id, buf);
}

static ovl_t *attached;         /* the overlay shown in the window */

static void ovl_destroy(ovl_t *o)
{
    if (attached == o) attached = NULL;
    if (!o->id) return;
    ovl_unmap(o);
    ovl_call2(OV_DISPLAY, o->id, -1);
    ovl_call2(OV_DESTROY, o->id, 0);
    o->id = 0;
}

static const char *type_name(int t)
{
    return t == 0 ? "Z-Order" : t == 1 ? "Basic" : "type?";
}

/* ------------------------------------------------------------------ */
/* The window                                                           */

static int win;                 /* window handle */
static int vis[4], scx, scy;    /* visible area (screen OS units), scroll */
static int behind;              /* the window it's behind (-1 = at the front) */
static int open_requests, open_requests_front;   /* Open_Window_Requests; ones asking for the front */
static int xeig, yeig;
static int fit_scale = 1;       /* scale the overlay to the area (else 1:1) */
static int crop_px;             /* T8: pixels cropped off the left edge */
static int quit_request, mode_changes, iconised;
static int keyq[16], keyq_n;
static char title[64];

/* The overlay area in pixels: the visible area above the panel. */
static int area_w(void) { return (vis[2] - vis[0]) >> xeig; }
static int area_h(void)
{
    int h = (vis[3] - vis[1] - PANEL_H) >> yeig;
    return h < 16 ? 16 : h;
}

/* Scale and place the attached overlay in the overlay area. */
static void place_overlay(void)
{
    _kernel_swi_regs r;
    _kernel_oserror *e;
    ovl_t *o = attached;
    int w, h, aw, ah;
    if (!o || !o->id) return;
    aw = area_w(); ah = area_h();
    if (fit_scale) {             /* the largest size that fits, same shape */
        w = aw; h = (int) ((long long) aw * o->h / o->w);
        if (h > ah) { h = ah; w = (int) ((long long) ah * o->w / o->h); }
    } else { w = o->w; h = o->h; }
    if (o->maxw && w > o->maxw) w = o->maxw;
    if (o->maxh && h > o->maxh) h = o->maxh;
    if (w < o->minw) w = o->minw;
    if (h < o->minh) h = o->minh;
    r.r[0] = o->id; r.r[1] = w; r.r[2] = h; r.r[3] = (o->w << 16) | o->h;
    e = swi(ov_swi[OV_SCALE].no, &r);
    if (e) say("SetScale %dx%d: %s", w, h, err_text(e));
    else note("SetScale asked %dx%d, got %dx%d (area %dx%d px)", w, h, r.r[1], r.r[2], aw, ah);
    /* Window coordinates: work area OS units, top left of the overlay at
       the top left of the visible area (minus any crop); clipped to the
       overlay area. */
    r.r[0] = o->id;
    r.r[1] = scx - (crop_px << xeig);
    r.r[2] = scy;
    r.r[3] = scx;
    r.r[4] = scy - (area_h() << yeig);
    r.r[5] = scx + (aw << xeig);
    r.r[6] = scy;
    if ((e = swi(ov_swi[OV_POSITION].no, &r)) != NULL) say("SetPosition: %s", err_text(e));
}

static void attach(ovl_t *o)
{
    _kernel_oserror *e;
    attached = o;
    if ((e = ovl_call2(OV_WINDOW, o->id, win)) != NULL) say("SetWindow: %s", err_text(e));
    place_overlay();
}

static void detach(void)
{
    attached = NULL;
}

static void read_state(void)
{
    int b[9];
    _kernel_swi_regs r;
    b[0] = win; r.r[1] = (int) b;
    swi(Wimp_GetWindowState, &r);
    vis[0] = b[1]; vis[1] = b[2]; vis[2] = b[3]; vis[3] = b[4];
    scx = b[5]; scy = b[6]; behind = b[7];
}

static void force_redraw(int all)
{
    _kernel_swi_regs r;
    r.r[0] = win;
    r.r[1] = scx; r.r[3] = scx + (vis[2] - vis[0]);
    r.r[4] = scy - (all ? 0 : (vis[3] - vis[1] - PANEL_H));
    r.r[2] = scy - (vis[3] - vis[1]);
    swi(Wimp_ForceRedraw, &r);
}

static void set_caret(void)
{
    _kernel_swi_regs r;
    r.r[0] = win; r.r[1] = -1; r.r[2] = 0; r.r[3] = 0; r.r[4] = 1 << 25; r.r[5] = -1;
    swi(Wimp_SetCaretPosition, &r);
}

/* Open the window with an overlay area of w x h pixels (plus the panel),
   at the top left of the screen when full is set, else near it. */
static void open_window(int w, int h)
{
    _kernel_swi_regs r;
    int b[8], sw = (mode_var(11) + 1) << xeig, sh = (mode_var(12) + 1) << yeig;
    int ow = w << xeig, oh = (h << yeig) + PANEL_H;
    if (ow > sw) ow = sw;
    if (oh > sh - 44) oh = sh - 44;
    b[0] = win; b[1] = 64; b[4] = sh - 44 - 64; b[3] = b[1] + ow; b[2] = b[4] - oh;
    if (b[2] < 0) { b[4] -= b[2]; b[2] = 0; if (b[4] > sh - 44) b[4] = sh - 44; }
    if (ow >= sw - 64) { b[1] = 0; b[3] = ow; }
    b[5] = 0; b[6] = 0; b[7] = -1;
    r.r[1] = (int) b;
    swi(Wimp_OpenWindow, &r);
    read_state();
    place_overlay();
    set_caret();
    force_redraw(1);
}

static int create_window(void)
{
    int b[23];
    _kernel_swi_regs r;
    _kernel_oserror *e;
    memset(b, 0, sizeof b);
    b[0] = 64; b[1] = 400; b[2] = 64 + 1280; b[3] = 400 + 720;
    b[6] = -1;
    b[7] = (int) 0xAF000002u;             /* back, close, title, toggle, size; moveable */
    b[8] = 7 | (2 << 8) | (7 << 16) | (0xFF << 24);  /* work area not filled by the Wimp */
    b[9] = 3 | (1 << 8) | (12 << 16);
    b[10] = 0; b[11] = -8192; b[12] = 8192; b[13] = 0;
    b[14] = 0x07000119;                   /* indirected text title */
    b[15] = 3 << 12;                      /* clicks reported (for the caret) */
    b[16] = 1;
    b[18] = (int) title; b[19] = -1; b[20] = sizeof title;
    r.r[1] = (int) b;
    if ((e = swi(Wimp_CreateWindow, &r)) != NULL) return 0;
    win = r.r[0];
    return 1;
}

static void draw_panel(const int *rb)
{
    _kernel_swi_regs r;
    int ox = rb[1] - rb[5], oy = rb[4] - rb[6];      /* screen position of work area 0,0 */
    int top = scy - (vis[3] - vis[1] - PANEL_H);     /* panel top, work area */
    int i;
    /* the overlay area when no overlay is attached: black */
    if (!attached) {
        r.r[0] = 7; swi(Wimp_SetColour, &r);
        r.r[0] = 4; r.r[1] = ox + scx; r.r[2] = oy + top; swi(OS_Plot, &r);
        r.r[0] = 101; r.r[1] = ox + scx + (vis[2] - vis[0]); r.r[2] = oy + scy; swi(OS_Plot, &r);
    }
    r.r[0] = 1; swi(Wimp_SetColour, &r);             /* light grey panel */
    r.r[0] = 4; r.r[1] = ox + scx; r.r[2] = oy + top - PANEL_H; swi(OS_Plot, &r);
    r.r[0] = 101; r.r[1] = ox + scx + (vis[2] - vis[0]); r.r[2] = oy + top; swi(OS_Plot, &r);
    r.r[0] = 7; swi(Wimp_SetColour, &r);
    _kernel_oswrch(5);
    for (i = 0; i < PANEL_LINES; i++) {
        if (!panel[i][0]) continue;
        r.r[0] = 4; r.r[1] = ox + scx + 8; r.r[2] = oy + top - 8 - i * LINE_H; swi(OS_Plot, &r);
        r.r[0] = (int) panel[i]; swi(OS_Write0, &r);
    }
    _kernel_oswrch(4);
}

static void redraw(int *block)
{
    _kernel_swi_regs r;
    int more;
    r.r[1] = (int) block;
    swi(Wimp_RedrawWindow, &r);
    more = r.r[0];
    while (more) {
        if (attached && attached->id) {
            r.r[0] = attached->id; r.r[1] = (int) block;
            swi(ov_swi[OV_REDRAW].no, &r);
        }
        draw_panel(block);
        r.r[1] = (int) block;
        swi(Wimp_GetRectangle, &r);
        more = r.r[0];
    }
}

/* Handlers the tests can set: a resize, and a mode change. */
static void (*on_open)(void);
static void (*on_mode_change)(void);

/* One Wimp_Poll (null events on). Returns the reason code. */
static int poll_once(int idle_cs)
{
    int block[64], reason;
    _kernel_swi_regs r;
    if (panel_dirty && win) { panel_dirty = 0; force_redraw(0); }
    if (idle_cs > 0) {
        _kernel_swi(OS_ReadMonotonicTime, &r, &r);
        r.r[2] = r.r[0] + idle_cs;
        r.r[0] = 0; r.r[1] = (int) block;
        swi(Wimp_PollIdle, &r);
    } else {
        r.r[0] = 0; r.r[1] = (int) block;
        swi(Wimp_Poll, &r);
    }
    polls++;
    reason = r.r[0];
    switch (reason) {
    case 1: redraw(block); break;
    case 2:
        open_requests++;
        if (block[7] == -1) open_requests_front++;
        r.r[1] = (int) block; swi(Wimp_OpenWindow, &r);
        read_state();
        note("window at %d,%d to %d,%d (OS units)%s", vis[0], vis[1], vis[2], vis[3],
             vis[0] < 0 || vis[1] < 0 ? " partly off-screen" : "");
        place_overlay();
        if (on_open) on_open();
        break;
    case 3: quit_request = 1; break;
    case 6: set_caret(); break;
    case 8:
        if (keyq_n < 16) keyq[keyq_n++] = block[6];
        break;
    case 17: case 18:
        if (block[4] == 0) quit_request = 1;                    /* Message_Quit */
        else if (block[4] == 0x400C1) {                         /* Message_ModeChange */
            mode_changes++;
            xeig = mode_var(4); yeig = mode_var(5);
            say("Message_ModeChange received (%d so far)", mode_changes);
            if (on_mode_change) on_mode_change();
        } else if (block[4] == 0x400CA) {                       /* Message_Iconize */
            iconised++;
            say("Message_Iconize received");
        }
        break;
    }
    return reason;
}

/* Keep the desktop going for ms milliseconds. */
static void pump(int ms)
{
    double end = now_ms() + ms;
    while (!quit_request && now_ms() < end) poll_once(0);
}

/* Wait for one of the keys in keys (lower case letters and digits; any
   key if keys is NULL). Returns the key, or -1 if the task is quitting. */
static int wait_key(const char *keys)
{
    while (!quit_request) {
        while (keyq_n > 0) {
            int k = keyq[0], i;
            for (i = 1; i < keyq_n; i++) keyq[i - 1] = keyq[i];
            keyq_n--;
            if (k >= 'A' && k <= 'Z') k += 32;
            if (!keys || (k < 256 && k && strchr(keys, k))) return k;
            if (k == 27) return 'q';
        }
        poll_once(10);
    }
    return -1;
}

/* A key if one is waiting, else 0. */
static int key_waiting(void)
{
    if (keyq_n > 0) {
        int k = keyq[0], i;
        for (i = 1; i < keyq_n; i++) keyq[i - 1] = keyq[i];
        keyq_n--;
        return k >= 'A' && k <= 'Z' ? k + 32 : k;
    }
    return 0;
}

static int yes_no(const char *question)
{
    int k;
    say("%s  Press Y or N.", question);
    k = wait_key("yn");
    note("  answer: %s", k == 'y' ? "yes" : k == 'n' ? "no" : "(none)");
    return k == 'y';
}

/* ------------------------------------------------------------------ */
/* Drawing into buffers                                                 */

static uint32_t rgb_word(const fmt_t *f, int r, int g, int b)
{
    if (f->flags & 0x4000) return (uint32_t) (r << 16 | g << 8 | b);   /* TRGB */
    return (uint32_t) (b << 16 | g << 8 | r);                            /* TBGR */
}

static void fill_rect32(ovl_t *o, int x0, int y0, int x1, int y1, uint32_t v)
{
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > o->w) x1 = o->w;
    if (y1 > o->h) y1 = o->h;
    for (y = y0; y < y1; y++) {
        uint32_t *p = (uint32_t *) (o->plane[0] + (size_t) y * o->stride[0]);
        for (x = x0; x < x1; x++) p[x] = v;
    }
}

/* YCbCr of an RGB colour for a YUV format's matrix and range. */
static void to_ycc(const fmt_t *f, int r, int g, int b, int *y, int *cb, int *cr)
{
    double kr = (f->flags & YCC_709) ? 0.2126 : 0.299;
    double kb = (f->flags & YCC_709) ? 0.0722 : 0.114;
    double R = r / 255.0, G = g / 255.0, B = b / 255.0;
    double Y = kr * R + (1 - kr - kb) * G + kb * B;
    double Cb = (B - Y) / (2 * (1 - kb)), Cr = (R - Y) / (2 * (1 - kr));
    double vy, vb, vr;
    if (f->flags & YCC_VID) { vy = 16 + 219 * Y; vb = 128 + 224 * Cb; vr = 128 + 224 * Cr; }
    else { vy = 255 * Y; vb = 128 + 255 * Cb; vr = 128 + 255 * Cr; }
#define CLAMP8(v) ((v) < 0 ? 0 : (v) > 255 ? 255 : (int) ((v) + 0.5))
    *y = CLAMP8(vy); *cb = CLAMP8(vb); *cr = CLAMP8(vr);
#undef CLAMP8
}

/* Write an RGB picture (w x h, 0x00RRGGBB words) into the mapped buffer,
   converting to the overlay's format. */
static void put_picture(ovl_t *o, const uint32_t *pic)
{
    const fmt_t *f = &o->fmt;
    int x, y;
    if (f->log2bpp == 5) {
        for (y = 0; y < o->h; y++) {
            uint32_t *d = (uint32_t *) (o->plane[0] + (size_t) y * o->stride[0]);
            const uint32_t *s = pic + (size_t) y * o->w;
            for (x = 0; x < o->w; x++)
                d[x] = rgb_word(f, s[x] >> 16 & 255, s[x] >> 8 & 255, s[x] & 255);
        }
        return;
    }
    if (f->log2bpp == 4) {
        for (y = 0; y < o->h; y++) {
            uint16_t *d = (uint16_t *) (o->plane[0] + (size_t) y * o->stride[0]);
            const uint32_t *s = pic + (size_t) y * o->w;
            for (x = 0; x < o->w; x++) {
                int r = s[x] >> 19 & 31, g = s[x] >> 10 & 63, b = s[x] >> 3 & 31;
                d[x] = (uint16_t) ((f->flags & 0x4000) ? (r << 11 | g << 5 | b) : (b << 11 | g << 5 | r));
            }
        }
        return;
    }
    /* YUV: luma for every pixel, chroma averaged over each cw x ch block */
    for (y = 0; y < o->h; y++) {
        uint8_t *d = o->plane[0] + (size_t) y * o->stride[0];
        const uint32_t *s = pic + (size_t) y * o->w;
        for (x = 0; x < o->w; x++) {
            int Y, cb, cr;
            to_ycc(f, s[x] >> 16 & 255, s[x] >> 8 & 255, s[x] & 255, &Y, &cb, &cr);
            d[x] = (uint8_t) Y;
        }
    }
    for (y = 0; y < o->h / f->ch; y++) {
        for (x = 0; x < o->w / f->cw; x++) {
            int sr = 0, sg = 0, sb = 0, n = 0, i, j, Y, cb, cr;
            for (j = 0; j < f->ch; j++)
                for (i = 0; i < f->cw; i++) {
                    uint32_t p = pic[(size_t) (y * f->ch + j) * o->w + x * f->cw + i];
                    sr += p >> 16 & 255; sg += p >> 8 & 255; sb += p & 255; n++;
                }
            to_ycc(f, sr / n, sg / n, sb / n, &Y, &cb, &cr);
            if (f->planes == 3) {
                o->plane[1][(size_t) y * o->stride[1] + x] = (uint8_t) cb;
                o->plane[2][(size_t) y * o->stride[2] + x] = (uint8_t) cr;
            } else {
                o->plane[1][(size_t) y * o->stride[1] + 2 * x] = (uint8_t) cb;
                o->plane[1][(size_t) y * o->stride[1] + 2 * x + 1] = (uint8_t) cr;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Start-up                                                             */

static int module_check(void)
{
    _kernel_swi_regs r;
    _kernel_oserror *e;
    int i;
    r.r[0] = 18; r.r[1] = (int) "VideoOverlay";
    if ((e = swi(OS_Module, &r)) != NULL) {
        say("VideoOverlay isn't loaded (%s).", e->errmess);
        say("It should be in !System (the Obey files RMLoad it from there).");
        return 0;
    } else {
        const char *base = (const char *) r.r[3];
        const char *help = base + *(const int *) (base + 0x14);
        char h[80];
        for (i = 0; i < 79 && help[i] >= 9; i++) h[i] = help[i] == 9 ? ' ' : help[i];
        h[i] = 0;
        say("Module: %s", h);
    }
    for (i = 0; i < OV_COUNT; i++) {
        r.r[1] = (int) ov_swi[i].name;
        e = swi(OS_SWINumberFromString, &r);
        if (e) note("  %s: not found by name (%s); using &%X", ov_swi[i].name, e->errmess, ov_swi[i].no);
        else {
            if ((r.r[0] & ~0x20000) != ov_swi[i].no)
                say("  %s is &%X, not &%X as documented", ov_swi[i].name, r.r[0] & ~0x20000, ov_swi[i].no);
            ov_swi[i].no = r.r[0] & ~0x20000;
        }
    }
    note("SWIs: Create &%X ... RedrawWindow &%X", ov_swi[OV_CREATE].no, ov_swi[OV_REDRAW].no);
    return 1;
}

static void describe_mode(void)
{
    int flags = mode_var(0), w = mode_var(11) + 1, h = mode_var(12) + 1, l2 = mode_var(9);
    int fam = flags >> 12 & 15;
    const char *order = fam == 0 ? "TBGR" : fam == 4 ? "TRGB" : fam == 8 ? "ABGR" : fam == 12 ? "ARGB" : "other";
    say("Screen: %dx%d, %d bpp, ModeFlags &%X (%s%s), eig %d,%d", w, h, 1 << l2, flags, order,
        (fam == 8 || fam == 12) ? ": alpha desktop" : "", mode_var(4), mode_var(5));
}

/* ------------------------------------------------------------------ */
/* T1: what the machine offers                                          */

static void t_vet(void)
{
    static const int sizes[][2] = { {320, 240}, {640, 360}, {1280, 720}, {1920, 1080}, {2560, 1440} };
    int f, s, banks, sc, ok = 0, tried = 0, vet_agrees = 0;
    /* VideoOverlay 0.02 with BCMVideo (Pi, 2026-09-28) fails every Vet
       with "GraphicsV call failed", and "accepts" BGR565 with nonsense
       limits, while Create works: so each row is a real Create (then
       Destroy), and Vet's answer is shown beside it for comparison. */
    say("T1: creating (and destroying) %d formats x 5 sizes x 1/3 banks x scaling;", NFORMATS);
    say("Vet's answer is logged beside each, for comparison.");
    note("format          size       banks scale  result (Create)                          Vet");
    for (f = 0; f < NFORMATS && !quit_request; f++) {
        for (s = 0; s < 5; s++)
            for (banks = 1; banks <= 3; banks += 2)
                for (sc = 0; sc <= 1; sc++) {
                    ovl_t o, v;
                    _kernel_oserror *ve = ovl_vet_create(&v, &formats[f], sizes[s][0], sizes[s][1],
                                                         banks, sc, 0);
                    char vet[80];
                    _kernel_oserror *e;
                    snprintf(vet, sizeof vet, "%s", ve ? ve->errmess : "yes");
                    e = ovl_vet_create(&o, &formats[f], sizes[s][0], sizes[s][1], banks, sc, 1);
                    tried++;
                    if (e) note("%-15s %4dx%-4d  %d     %-5s  no: %-36s %s", formats[f].name, sizes[s][0],
                                sizes[s][1], banks, sc ? "must" : "-", err_text(e), vet);
                    else {
                        char res[80];
                        ok++;
                        snprintf(res, sizeof res, "%s, scale %dx%d to %dx%d", type_name(o.type),
                                 o.minw, o.minh, o.maxw, o.maxh);
                        note("%-15s %4dx%-4d  %d     %-5s  yes: %-35s %s", formats[f].name, sizes[s][0],
                             sizes[s][1], banks, sc ? "must" : "-", res, vet);
                        ovl_destroy(&o);
                    }
                    if ((ve == NULL) == (e == NULL)) vet_agrees++;
                }
        pump(20);
    }
    /* a summary per format for the panel */
    for (f = 0; f < NFORMATS; f++) {
        ovl_t o;
        _kernel_oserror *e = ovl_vet_create(&o, &formats[f], 1280, 720, 3, 1, 1);
        if (e) say("%-15s 1280x720 x3: no (%s)", formats[f].name, e->errmess);
        else {
            say("%-15s 1280x720 x3: %s, %dx%d..%dx%d", formats[f].name, type_name(o.type),
                o.minw, o.minh, o.maxw, o.maxh);
            ovl_destroy(&o);
        }
    }
    say("T1 done: %d of %d created; Vet agreed with Create %d times.", ok, tried, vet_agrees);
}

/* ------------------------------------------------------------------ */
/* T2: memory speed                                                     */

typedef struct { uint8_t *base; int row_bytes, stride, rows; } region_t;

static volatile uint32_t sink;

static double bw_op(region_t *g, int op, const uint8_t *src)
{
    double t0 = hr_seconds();
    int y;
    size_t i, words = (size_t) g->row_bytes / 4;
    for (y = 0; y < g->rows; y++) {
        uint32_t *p = (uint32_t *) (g->base + (size_t) y * g->stride);
        switch (op) {
        case 0: memset(p, y & 255, g->row_bytes); break;
        case 1: for (i = 0; i < words; i++) p[i] = (uint32_t) i; break;
        case 2: memcpy(p, src + (size_t) y * g->row_bytes, g->row_bytes); break;
        case 3: { uint32_t s = 0; for (i = 0; i < words; i++) s += p[i]; sink += s; break; }
        case 4: for (i = 0; i < words; i++) p[i] += 1; break;
        }
    }
    return hr_seconds() - t0;
}

static const char *op_names[] = { "memset", "word writes", "memcpy in", "read (sum)", "read-modify-write" };

static void bw_report(const char *what, region_t *g, const uint8_t *src)
{
    int op, rep;
    double mb = (double) g->row_bytes * g->rows / (1024.0 * 1024.0);
    for (op = 0; op < 5; op++) {
        double best = 1e9, sum = 0;
        for (rep = 0; rep < 20; rep++) {
            double t = bw_op(g, op, src);
            sum += t;
            if (t < best) best = t;
        }
        say("%-8s %-18s %7.0f MB/s best, %7.0f average", what, op_names[op], mb / best, mb * 20 / sum);
    }
}

static void t_bw(void)
{
    ovl_t o;
    _kernel_oserror *e;
    region_t g;
    uint8_t *src, *mem;
    int i, rep;
    const int W = 1920, H = 1080;
    say("T2: memory speed, %dx%d 32bpp (%.1f MB). About a minute.", W, H, W * H * 4 / 1048576.0);
    src = malloc((size_t) W * H * 4);
    mem = malloc((size_t) W * H * 4);
    if (!src || !mem) { say("Not enough memory: give it a bigger WimpSlot."); return; }
    for (i = 0; i < W * H; i++) ((uint32_t *) src)[i] = (uint32_t) i * 2654435761u;
    pump(200);

    if ((e = ovl_vet_create(&o, &fmt_tbgr, W, H, 3, 0, 1)) != NULL)
        say("Create TBGR32 %dx%d x3: %s", W, H, err_text(e));
    else if ((e = ovl_map(&o, 0)) != NULL) {
        say("MapBuffer: %s", err_text(e));
        ovl_destroy(&o);
    } else {
        say("Overlay buffer 0 at &%08X, stride %d (%s)", (unsigned) (uintptr_t) o.plane[0],
            o.stride[0], type_name(o.type));
        g.base = o.plane[0]; g.row_bytes = W * 4; g.stride = o.stride[0]; g.rows = H;
        bw_report("overlay", &g, src);
        ovl_unmap(&o);
        ovl_destroy(&o);
    }
    pump(100);
    g.base = mem; g.row_bytes = W * 4; g.stride = W * 4; g.rows = H;
    bw_report("malloc", &g, src);
    pump(100);
    {   /* screen memory: as much of 1920x1080 as the screen has */
        int sw = mode_var(11) + 1, sh = mode_var(12) + 1, l2 = mode_var(9);
        if (l2 != 5) say("screen: not a 32bpp mode, skipped");
        else {
            g.base = (uint8_t *) vdu_var(148);
            g.stride = vdu_var(6);
            g.row_bytes = (sw < W ? sw : W) * 4;
            g.rows = sh < H ? sh : H;
            bw_report("screen", &g, src);
            {
                _kernel_swi_regs r;
                r.r[0] = -1; r.r[1] = 0; r.r[2] = 0; r.r[3] = 0x7FFF; r.r[4] = 0x7FFF;
                swi(Wimp_ForceRedraw, &r);      /* we scribbled on the screen */
            }
        }
    }
    pump(100);
    /* YV12: three planes, as a video player would copy them */
    {
        const fmt_t *yv = &formats[9];          /* YV12 709 video */
        if ((e = ovl_vet_create(&o, yv, W, H, 3, 0, 1)) != NULL)
            say("Create YV12 %dx%d x3: %s", W, H, err_text(e));
        else if ((e = ovl_map(&o, 0)) != NULL) {
            say("MapBuffer (YV12): %s", err_text(e));
            ovl_destroy(&o);
        } else {
            static const int pw[3] = { 1920, 960, 960 }, ph[3] = { 1080, 540, 540 };
            double best = 1e9, sum = 0, mb = (1920.0 * 1080 * 1.5) / 1048576.0;
            for (i = 0; i < 3; i++)
                say("YV12 plane %d at &%08X, stride %d, %s", i, (unsigned) (uintptr_t) o.plane[i],
                    o.stride[i], ((uintptr_t) o.plane[i] & 15) ? "NOT 16-byte aligned" : "16-byte aligned");
            for (rep = 0; rep < 20; rep++) {
                double t0 = hr_seconds(), t;
                const uint8_t *s = src;
                for (i = 0; i < 3; i++) {
                    int y;
                    for (y = 0; y < ph[i]; y++) memcpy(o.plane[i] + (size_t) y * o.stride[i], s + (size_t) y * pw[i], pw[i]);
                    s += (size_t) pw[i] * ph[i];
                }
                t = hr_seconds() - t0;
                sum += t;
                if (t < best) best = t;
            }
            say("YV12 1920x1080 copy in: %.2f ms best (%.0f MB/s), %.2f ms average",
                best * 1000, mb / best, sum / 20 * 1000);
            ovl_unmap(&o);
            ovl_destroy(&o);
        }
    }
    free(src); free(mem);
    say("T2 done (timer: %s).", hr_source());
}

/* ------------------------------------------------------------------ */
/* T3: does a frame written through the cache show?                     */

static void cache_step(ovl_t *o, int step, int sync, int unmap_first)
{
    static const int colours[4][3] = { {200, 0, 0}, {0, 170, 0}, {0, 60, 220}, {220, 200, 0} };
    static const char *cname[4] = { "red", "green", "blue", "yellow" };
    static const char *sname[3] = { "no cache operation", "OS_SynchroniseCodeAreas on the buffer",
                                    "OS_SynchroniseCodeAreas (whole cache)" };
    _kernel_oserror *e;
    _kernel_swi_regs r;
    int bars = step + 1, i, bw;
    const int *c = colours[step];
    if ((e = ovl_map(o, 0)) != NULL) { say("MapBuffer: %s", err_text(e)); return; }
    fill_rect32(o, 0, 0, o->w, o->h, rgb_word(&o->fmt, c[0], c[1], c[2]));
    bw = o->w / (2 * bars + 1);
    if (bw < 1) bw = 1;
    for (i = 0; i < bars; i++)
        fill_rect32(o, (2 * i + 1) * bw, o->h / 4, (2 * i + 2) * bw, o->h * 3 / 4, rgb_word(&o->fmt, 255, 255, 255));
    if (sync == 1) {
        r.r[0] = 1; r.r[1] = (int) o->plane[0];
        r.r[2] = (int) (o->plane[0] + (size_t) o->stride[0] * o->h - 1);
        swi(OS_SynchroniseCodeAreas, &r);
    } else if (sync == 2) {
        r.r[0] = 0;
        swi(OS_SynchroniseCodeAreas, &r);
    }
    if (unmap_first) ovl_unmap(o);
    ovl_display(o, 0);
    say("%dx%d, step %d: %s; %s.", o->w, o->h, step + 1, sname[sync],
        unmap_first ? "unmapped, then displayed" : "displayed while still mapped");
    say("You should see %s with %d white bar%s in the middle.", cname[step], bars, bars > 1 ? "s" : "");
    yes_no("Is that what you see?");
    ovl_unmap(o);
}

static void t_cache(void)
{
    static const int sizes[2][2] = { {64, 64}, {1920, 1080} };
    int s;
    say("T3: does a frame written through a cached mapping show?");
    open_window(640, 360);
    for (s = 0; s < 2 && !quit_request; s++) {
        ovl_t o;
        _kernel_oserror *e = ovl_vet_create(&o, &fmt_tbgr, sizes[s][0], sizes[s][1], 1, 1, 1);
        if (e) { say("Create %dx%d: %s", sizes[s][0], sizes[s][1], err_text(e)); continue; }
        say("Overlay %dx%d (%s), scaled to the window.", o.w, o.h, type_name(o.type));
        attach(&o);
        pump(300);
        cache_step(&o, 0, 0, 0);
        if (!quit_request) cache_step(&o, 1, 0, 1);
        if (!quit_request) cache_step(&o, 2, 1, 0);
        if (!quit_request) cache_step(&o, 3, 2, 0);
        detach();
        ovl_destroy(&o);
        force_redraw(1);
    }
    say("T3 done.");
}

/* ------------------------------------------------------------------ */
/* T4: can buffers stay mapped across Wimp_Poll?                        */

static int address_mapped(const void *p, size_t len)
{
    _kernel_swi_regs r;
    int carry = 1;
    r.r[0] = (int) p; r.r[1] = (int) ((const uint8_t *) p + len);
    if (_kernel_swi_c(OS_ValidateAddress, &r, &r, &carry)) return 0;
    return !carry;
}

static void t_map(void)
{
    ovl_t o;
    _kernel_oserror *e;
    uint8_t *addr[3];
    int stride[3], b, sec, invalid = 0, i;
    double start;
    say("T4: map all 3 buffers once, then keep using them for 30 seconds");
    say("while other programs run: open Filer windows and drag things around.");
    open_window(640, 360);
    if ((e = ovl_vet_create(&o, &fmt_tbgr, 640, 360, 3, 1, 1)) != NULL) {
        say("Create: %s", err_text(e)); return;
    }
    attach(&o);
    for (b = 0; b < 3; b++) {
        /* map each buffer and leave it mapped (plane pointers kept here) */
        if ((e = ovl_map(&o, b)) != NULL) { say("MapBuffer %d: %s", b, err_text(e)); ovl_destroy(&o); return; }
        addr[b] = o.plane[0]; stride[b] = o.stride[0];
        say("buffer %d at &%08X, stride %d", b, (unsigned) (uintptr_t) addr[b], stride[b]);
        o.mapped = -1;           /* not unmapped: that's the test */
    }
    start = now_ms();
    for (sec = 0; sec < 30 && !quit_request; sec++) {
        b = sec % 3;
        if (!address_mapped(addr[b], (size_t) stride[b] * o.h)) {
            invalid++;
            say("second %d: buffer %d's address is no longer mapped", sec, b);
        } else {
            o.plane[0] = addr[b]; o.stride[0] = stride[b];
            fill_rect32(&o, 0, 0, o.w, o.h, rgb_word(&o.fmt, 30, 30, 90 + 5 * sec));
            fill_rect32(&o, 0, o.h / 3, (sec + 1) * o.w / 30, o.h * 2 / 3, rgb_word(&o.fmt, 255, 200, 0));
            if ((e = ovl_display(&o, b)) != NULL) say("DisplayBuffer: %s", err_text(e));
        }
        if (sec % 5 == 0) say("second %d of 30", sec);
        while (!quit_request && now_ms() < start + (sec + 1) * 1000.0) poll_once(0);
    }
    say("The orange bar should have grown by one step a second, to the full width.");
    yes_no("Did it, with no garbage?");
    /* do fresh mappings give the same addresses? */
    for (b = 0; b < 3; b++) {
        if ((e = ovl_map(&o, b)) != NULL) { say("MapBuffer again %d: %s", b, err_text(e)); continue; }
        say("buffer %d mapped again: &%08X (%s)", b, (unsigned) (uintptr_t) o.plane[0],
            o.plane[0] == addr[b] ? "same address" : "DIFFERENT address");
        ovl_call2(OV_UNMAP, o.id, b);
        e = ovl_call2(OV_UNMAP, o.id, b);       /* the first mapping too */
        if (e) note("  second unmap of %d: %s", b, err_text(e));
    }
    o.mapped = -1;
    say("Addresses invalid during the run: %d times.", invalid);
    {   /* cost of mapping and unmapping */
        double t0 = hr_seconds(), t;
        for (i = 0; i < 1000; i++) {
            if ((e = ovl_call2(OV_MAP, o.id, 0)) != NULL) break;
            ovl_call2(OV_UNMAP, o.id, 0);
        }
        t = hr_seconds() - t0;
        if (e) say("Map/Unmap loop: %s", err_text(e));
        else say("MapBuffer + UnmapBuffer: %.1f us per pair (1000 pairs)", t * 1e6 / 1000);
    }
    detach();
    ovl_destroy(&o);
    say("T4 done.");
}

/* ------------------------------------------------------------------ */
/* T5 and T7: a white bar sweeping across black                         */

typedef struct {
    ovl_t o;
    int bar[3];                 /* where each buffer's bar is (-1 = buffer not cleared) */
    int x, next;
    long frames;
    double disp_min, disp_max, disp_sum, map_sum;
    int errors, failed;         /* failed: a buffer couldn't be mapped or shown */
} sweep_t;

#define BAR_W  32
#define BAR_STEP 8

static _kernel_oserror *sweep_create(sweep_t *s, int w, int h, int banks)
{
    _kernel_oserror *e;
    int b;
    memset(s, 0, sizeof *s);
    s->bar[0] = s->bar[1] = s->bar[2] = -1;
    s->disp_min = 1e9;
    if ((e = ovl_vet_create(&s->o, &fmt_tbgr, w, h, banks, 1, 1)) != NULL) return e;
    /* map and clear every buffer now, so a buffer that can't be had (the
       GPU out of memory: with Geminus loaded, the third 1920x1080 buffer
       failed) shows up here rather than in the middle of a run */
    for (b = 0; b < banks; b++) {
        if ((e = ovl_map(&s->o, b)) != NULL) {
            say("Buffer %d of %d can't be mapped: %s", b + 1, banks, err_text(e));
            ovl_destroy(&s->o);
            return e;
        }
        fill_rect32(&s->o, 0, 0, s->o.w, s->o.h, rgb_word(&s->o.fmt, 0, 0, 0));
        s->bar[b] = -2;                 /* cleared, no bar yet */
        ovl_unmap(&s->o);
    }
    return NULL;
}

/* 3 buffers, else 2, else 1 (the GPU may not have room for three). */
static _kernel_oserror *sweep_create_most(sweep_t *s, int w, int h)
{
    _kernel_oserror *e = NULL;
    int banks;
    for (banks = 3; banks >= 1; banks--) {
        if ((e = sweep_create(s, w, h, banks)) == NULL) {
            if (banks < 3) say("Using %d buffer%s.", banks, banks > 1 ? "s" : "");
            return NULL;
        }
    }
    return e;
}

/* Draw the next frame and show it. paced: wait for vsync first. */
static void sweep_frame(sweep_t *s, int paced)
{
    ovl_t *o = &s->o;
    _kernel_oserror *e;
    double t0, t;
    int b = s->next;
    uint32_t black = rgb_word(&o->fmt, 0, 0, 0), white = rgb_word(&o->fmt, 255, 255, 255);
    t0 = hr_seconds();
    e = ovl_map(o, b);
    s->map_sum += hr_seconds() - t0;
    if (e) { if (s->errors++ < 5) say("MapBuffer %d: %s", b, err_text(e)); s->failed = 1; return; }
    if (s->bar[b] == -1) fill_rect32(o, 0, 0, o->w, o->h, black);
    else if (s->bar[b] >= 0) fill_rect32(o, s->bar[b], 0, s->bar[b] + BAR_W, o->h, black);
    fill_rect32(o, s->x, 0, s->x + BAR_W, o->h, white);
    s->bar[b] = s->x;
    ovl_unmap(o);
    if (paced) _kernel_osbyte(19, 0, 0);
    t0 = hr_seconds();
    e = ovl_display(o, b);
    t = hr_seconds() - t0;
    if (e) { if (s->errors++ < 5) say("DisplayBuffer %d: %s", b, err_text(e)); s->failed = 1; return; }
    if (t < s->disp_min) s->disp_min = t;
    if (t > s->disp_max) s->disp_max = t;
    s->disp_sum += t;
    s->frames++;
    s->x += BAR_STEP;
    if (s->x + BAR_W > o->w) s->x = 0;
    s->next = (b + 1) % o->banks;
}

static void t_tear(void)
{
    static const int runs[4][2] = { {3, 1}, {3, 0}, {2, 1}, {2, 0} };   /* banks, paced */
    int sw = mode_var(11) + 1, sh = mode_var(12) + 1, run;
    say("T5: a 1920x1080 overlay filling the screen; a white bar sweeps across.");
    say("Watch the bar for tearing (a break in it). 4 runs of 10 seconds.");
    open_window(sw, sh);
    for (run = 0; run < 4 && !quit_request; run++) {
        sweep_t s;
        _kernel_oserror *e = sweep_create(&s, 1920, 1080, runs[run][0]);
        double t0, t;
        int k;
        if (e) { say("Create x%d: %s", runs[run][0], err_text(e)); continue; }
        attach(&s.o);
        say("Run %d: %d buffers, %s.", run + 1, runs[run][0],
            runs[run][1] ? "waiting for vsync (OS_Byte 19) before each switch" : "flat out");
        t0 = now_ms();
        while (!quit_request && now_ms() - t0 < 10000 && !s.failed) {
            sweep_frame(&s, runs[run][1]);
            poll_once(0);
        }
        if (s.failed) {
            say("  Run stopped after %ld frames: a buffer failed.", s.frames);
            detach();
            ovl_destroy(&s.o);
            continue;
        }
        t = (now_ms() - t0) / 1000.0;
        if (s.frames)
            say("  %ld frames, %.1f fps; DisplayBuffer %.0f/%.0f/%.0f us (min/avg/max); map %.0f us",
                s.frames, s.frames / t, s.disp_min * 1e6, s.disp_sum / s.frames * 1e6,
                s.disp_max * 1e6, s.map_sum / s.frames * 1e6);
        say("Tearing? 0 = none, 1 = a line that stays put, 2 = a line that moves, 3 = other");
        k = wait_key("0123");
        note("  run %d tearing: %c", run + 1, k > 0 ? k : '?');
        detach();
        ovl_destroy(&s.o);
        force_redraw(1);
    }
    say("T5 done.");
}

/* ------------------------------------------------------------------ */
/* T6: scaling                                                          */

static uint32_t *card;
static int card_w, card_h;

/* A test card: grid, circles, 1-pixel lines, colour patches. */
static void make_card(int w, int h)
{
    int x, y;
    free(card);
    card = malloc((size_t) w * h * 4);
    card_w = w; card_h = h;
    if (!card) return;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t v = 0x303030;
            int dx = x - w / 2, dy = y - h / 2;
            long d2 = (long) dx * dx + (long) dy * dy, r = h * 2 / 5, r2 = h / 5;
            if (x % 40 == 0 || y % 40 == 0) v = 0xC0C0C0;
            if (d2 >= (r - 1) * (r - 1) && d2 <= r * r) v = 0xFFFFFF;
            if (d2 >= (r2 - 1) * (r2 - 1) && d2 <= r2 * r2) v = 0xFFFF00;
            if (y >= 8 && y < 48) {                   /* 1-pixel lines, 2 and 3 apart */
                if (x >= 8 && x < 88 && x % 2 == 0) v = 0xFFFFFF;
                if (x >= 96 && x < 176 && x % 3 == 0) v = 0xFFFFFF;
                if (x >= 184 && x < 264 && y % 2 == 0) v = 0xFFFFFF;
            }
            if (y >= h - 48 && y < h - 8) {           /* colour patches */
                static const uint32_t pc[6] = { 0xFF0000, 0x00FF00, 0x0000FF, 0x00FFFF, 0xFF00FF, 0xFFFF00 };
                int i = (x - 8) / 48;
                if (x >= 8 && i < 6 && (x - 8) % 48 < 40) v = pc[i];
            }
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) v = 0xFF0000;   /* red edge */
            card[(size_t) y * w + x] = v;
        }
}

static ovl_t scale_ovl;

static void scale_reopen(const fmt_t *f)
{
    _kernel_oserror *e;
    ovl_t *o = &scale_ovl;
    int b;
    detach();
    ovl_destroy(o);
    if ((e = ovl_vet_create(o, f, card_w, card_h, 1, 1, 1)) != NULL) {
        say("Create %s %dx%d: %s", f->name, card_w, card_h, err_text(e));
        return;
    }
    for (b = 0; b < 1; b++) {
        if ((e = ovl_map(o, b)) != NULL) { say("MapBuffer: %s", err_text(e)); return; }
        put_picture(o, card);
        ovl_unmap(o);
    }
    ovl_display(o, 0);
    say("%s %dx%d, %s, scales %dx%d to %dx%d", f->name, o->w, o->h, type_name(o->type),
        o->minw, o->minh, o->maxw, o->maxh);
    attach(o);
    force_redraw(1);
}

static void scale_opened(void)
{
    note("  area now %dx%d px", area_w(), area_h());
}

static void t_scale(void)
{
    static const int fmt_index[4] = { 0, 9, 13, 17 };   /* TBGR32, YV12, YV16, NV12 (709 video) */
    int fi = 0, k;
    say("T6: a 640x360 test card scaled to the window.");
    say("Keys: 1 2 3 = that many times the size, M = smallest allowed,");
    say("F = next format (TBGR32, YV12, YV16, NV12), Q = done. Also try");
    say("resizing the window with its size icon. Look at the grid, circles,");
    say("the 1-pixel lines at the top left and the red edge; note what you see.");
    make_card(640, 360);
    if (!card) { say("No memory for the test card."); return; }
    open_window(640, 360);
    on_open = scale_opened;
    scale_reopen(&formats[fmt_index[fi]]);
    while ((k = wait_key("123mfq")) > 0 && k != 'q') {
        if (k >= '1' && k <= '3') {
            say("Scale %c: window %dx%d", k, 640 * (k - '0'), 360 * (k - '0'));
            open_window(640 * (k - '0'), 360 * (k - '0'));
        } else if (k == 'm') {
            int w = scale_ovl.minw > 16 ? scale_ovl.minw : 16, h = scale_ovl.minh > 16 ? scale_ovl.minh : 16;
            say("Smallest: %dx%d", w, h);
            open_window(w, h);
        } else if (k == 'f') {
            fi = (fi + 1) % 4;
            scale_reopen(&formats[fmt_index[fi]]);
        }
        say("Area %dx%d px. Next key?", area_w(), area_h());
    }
    on_open = NULL;
    detach();
    ovl_destroy(&scale_ovl);
    say("T6 done.");
}

/* ------------------------------------------------------------------ */
/* T7: living in the desktop                                            */

static sweep_t desk;
static int desk_lost;

static void desk_mode_change(void)
{
    _kernel_oserror *e;
    ovl_t o;
    int old = desk.o.id;
    e = ovl_call2(OV_DISPLAY, old, 0);
    say("After the mode change, DisplayBuffer with the old ID: %s", err_text(e));
    /* On the Pi (VideoOverlay 0.02) the old ID still worked after a mode
       change, and without destroying it the third change ran out of GPU
       memory: so see whether its buffers are still there, then destroy
       it before making a new one. */
    e = ovl_call2(OV_MAP, old, 0);
    say("  MapBuffer with the old ID: %s", err_text(e));
    if (!e) ovl_call2(OV_UNMAP, old, 0);
    e = ovl_call2(OV_DESTROY, old, 0);
    say("  Destroy with the old ID: %s", err_text(e));
    e = ovl_vet_create(&o, &fmt_tbgr, 1920, 1080, 3, 1, 0);
    if (e) say("Vet in the new mode: %s", err_text(e));
    else say("Vet in the new mode: yes, %s", type_name(o.type));
    detach();
    desk.o.id = 0;
    e = sweep_create_most(&desk, 1920, 1080);
    if (e) { say("Create in the new mode: %s", err_text(e)); desk_lost = 1; return; }
    say("Created again (%s).", type_name(desk.o.type));
    desk_lost = 0;
    attach(&desk.o);
}

static void t_desk(void)
{
    _kernel_oserror *e;
    int k = 0;
    say("T7: the sweeping bar in a normal window. While it runs, please:");
    say(" 1 open a menu over it (Menu on the icon bar), 2 drag another");
    say("   window across it, 3 move this window partly off the screen,");
    say(" 4 iconise it (Shift+close) and back, 5 change mode (*WimpMode or");
    say("   Display manager) and back. Note what you see. Q = done.");
    say(" H hides the overlay (and shows it again). F freezes it: still shown,");
    say("   but no new frames (no DisplayBuffer). Can a window cover it then?");
    open_window(640, 360);
    describe_mode();
    if ((e = sweep_create_most(&desk, 1920, 1080)) != NULL) { say("Create: %s", err_text(e)); return; }
    say("Overlay: %s", type_name(desk.o.type));
    attach(&desk.o);
    on_mode_change = desk_mode_change;
    {
    int hidden = 0, frozen = 0, last_behind, last_opens = open_requests, raised = 0, n = 0;
    read_state();
    last_behind = behind;
    while (!quit_request) {
        /* Who raises this window? Look at its place in the window stack
           every few polls: a change with no Open_Window_Request between
           is someone else calling Wimp_OpenWindow on it. */
        if (++n % 4 == 0) {
            read_state();
            if (behind != last_behind) {
                if (open_requests == last_opens) {
                    raised++;
                    if (raised <= 10)
                        say("Moved in the stack (behind &%X -> &%X) with no Open_Window_Request%s",
                            last_behind, behind, hidden ? " (overlay hidden)" : "");
                }
                last_behind = behind;
            }
            last_opens = open_requests;
        }
        if (k == 'h') {
            hidden = !hidden;
            if (hidden && desk.o.id) ovl_call2(OV_DISPLAY, desk.o.id, -1);
            say(hidden ? "Overlay hidden (DisplayBuffer -1)." : "Overlay shown again.");
        }
        if (k == 'f') {
            frozen = !frozen;
            say(frozen ? "Frozen: the overlay stays shown, no more DisplayBuffer calls." : "Running again.");
        }
        if (!hidden && !frozen && !desk_lost && desk.o.id && !desk.failed) sweep_frame(&desk, 1);
        else if (desk.failed && !desk_lost) {
            say("A buffer failed: the overlay is stopped (see above).");
            desk_lost = 1;
        }
        poll_once(0);
        if ((k = key_waiting()) == 'q' || k == 27) break;
    }
    say("Stack moves with no Open_Window_Request: %d; Open_Window_Requests: %d, %d of them asking for the front.",
        raised, open_requests, open_requests_front);
    }
    on_mode_change = NULL;
    say("%ld frames; %d errors; %d mode changes; %d iconise messages.", desk.frames, desk.errors,
        mode_changes, iconised);
    detach();
    ovl_destroy(&desk.o);
    say("T7 done. Please also repeat it on an alpha desktop (LARGB mode).");
}

/* ------------------------------------------------------------------ */
/* T8: YV12 for video                                                   */

static const uint32_t bars[8] = { 0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00,
                                  0xFF00FF, 0xFF0000, 0x0000FF, 0x000000 };

/* Colour bars (top two thirds), a grey ramp, a 1-pixel luma checker
   (bottom left) and a red/blue chroma checker (bottom right). */
static void make_yuv_card(int w, int h)
{
    int x, y;
    free(card);
    card = malloc((size_t) w * h * 4);
    card_w = w; card_h = h;
    if (!card) return;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t v;
            if (y < h * 2 / 3) v = bars[x * 8 / w];
            else if (y < h * 3 / 4) { int g = x * 255 / (w - 1); v = (uint32_t) (g << 16 | g << 8 | g); }
            else if (x < w / 2) v = ((x + y) & 1) ? 0xFFFFFF : 0x000000;
            else v = (((x / 2) + (y / 2)) & 1) ? 0xFF0000 : 0x0000FF;
            card[(size_t) y * w + x] = v;
        }
}

static void t_yuv(void)
{
    static const int variants[4] = { 9, 7, 8, 6 };      /* 709 video, 601 video, 709 full, 601 full */
    ovl_t o;
    _kernel_oserror *e;
    int vi = 0, k;
    memset(&o, 0, sizeof o);
    say("T8: YV12 1280x720. Top: bars white, yellow, cyan, green, magenta,");
    say("red, blue, black (left to right). Then a grey ramp, a 1-pixel");
    say("black/white checker (left) and a red/blue chroma checker (right).");
    say("Keys: V = next colour space, C = crop 64 px off the left edge (and");
    say("back), T = time a frame copy, Q = done. Is grey grey? Red/blue right?");
    make_yuv_card(1280, 720);
    if (!card) { say("No memory for the picture."); return; }
    open_window(640, 360);
    k = 'v'; vi = -1;
    do {
        if (k == 'v') {
            const fmt_t *f;
            vi = (vi + 1) % 4;
            f = &formats[variants[vi]];
            detach();
            ovl_destroy(&o);
            if ((e = ovl_vet_create(&o, f, 1280, 720, 1, 1, 1)) != NULL) {
                say("Create %s: %s", f->name, err_text(e));
                continue;
            }
            if ((e = ovl_map(&o, 0)) != NULL) { say("MapBuffer: %s", err_text(e)); continue; }
            put_picture(&o, card);
            ovl_unmap(&o);
            ovl_display(&o, 0);
            attach(&o);
            force_redraw(1);
            say("Showing %s (%s). Does it look right?", f->name, type_name(o.type));
        } else if (k == 'c') {
            crop_px = crop_px ? 0 : 64;
            place_overlay();
            force_redraw(1);
            say(crop_px ? "Left 64 px cropped: do the colours still line up?" : "Crop off.");
        } else if (k == 't' && o.id) {
            /* what a player pays per frame: map, copy three planes, unmap */
            uint8_t *src = malloc((size_t) 1280 * 720 * 3 / 2);
            double best = 1e9, sum = 0;
            int rep, i, y;
            if (!src) { say("No memory."); continue; }
            memset(src, 128, (size_t) 1280 * 720 * 3 / 2);
            for (rep = 0; rep < 100; rep++) {
                static const int pw[3] = { 1280, 640, 640 }, ph[3] = { 720, 360, 360 };
                const uint8_t *s = src;
                double t0 = hr_seconds(), t;
                if ((e = ovl_map(&o, 0)) != NULL) { say("MapBuffer: %s", err_text(e)); break; }
                for (i = 0; i < 3; i++) {
                    for (y = 0; y < ph[i]; y++) memcpy(o.plane[i] + (size_t) y * o.stride[i], s + (size_t) y * pw[i], pw[i]);
                    s += (size_t) pw[i] * ph[i];
                }
                ovl_unmap(&o);
                t = hr_seconds() - t0;
                sum += t;
                if (t < best) best = t;
            }
            free(src);
            say("1280x720 YV12 map + copy + unmap: %.2f ms best, %.2f ms average", best * 1000, sum / 100 * 1000);
            /* put the picture back */
            if (!ovl_map(&o, 0)) { put_picture(&o, card); ovl_unmap(&o); ovl_display(&o, 0); }
        }
    } while ((k = wait_key("vctq")) > 0 && k != 'q');
    crop_px = 0;
    detach();
    ovl_destroy(&o);
    say("T8 done.");
}

/* ------------------------------------------------------------------ */

static const struct { const char *name, *what; void (*fn)(void); } tests[] = {
    { "vet",   "T1 formats and sizes",       t_vet },
    { "bw",    "T2 memory speed",            t_bw },
    { "cache", "T3 cached writes",           t_cache },
    { "map",   "T4 mapping across polls",    t_map },
    { "tear",  "T5 switching and tearing",   t_tear },
    { "scale", "T6 scaling",                 t_scale },
    { "desk",  "T7 living in the desktop",   t_desk },
    { "yuv",   "T8 YV12 colours and copies", t_yuv },
};

static void cleanup(void)
{
    if (attached) ovl_destroy(attached);
    ovl_destroy(&desk.o);
    ovl_destroy(&scale_ovl);
}

int main(int argc, char **argv)
{
    static const int messages[] = { 0x400C1, 0x400CA, 0 };
    _kernel_swi_regs r;
    _kernel_oserror *e;
    const char *v;
    int t, n = (int) (sizeof tests / sizeof tests[0]);
    for (t = 0; t < n; t++)
        if (argc > 1 && strcmp(argv[1], tests[t].name) == 0) break;
    if (t == n) {
        fprintf(stderr, "usage: ovltest vet|bw|cache|map|tear|scale|desk|yuv\n");
        return 1;
    }
    if ((v = getenv("OVLTEST_VIRTUAL_MS")) != NULL) virtual_ms = atoi(v);
    logfile = fopen("ovlresults", "a");
    snprintf(title, sizeof title, "ovltest: %s", tests[t].what);
    r.r[0] = 380; r.r[1] = 0x4B534154; r.r[2] = (int) title; r.r[3] = (int) messages;
    if ((e = swi(Wimp_Initialise, &r)) != NULL) {
        printf("%s\nStart ovltest from its Obey files (double-click them), or with *WimpTask.\n",
               e->errmess);
        return 1;
    }
    task_handle = r.r[1];
    xeig = mode_var(4); yeig = mode_var(5);
    atexit(cleanup);
    note("\n==== ovltest %s (%s) ====", tests[t].name, tests[t].what);
    if (!create_window()) { note("Wimp_CreateWindow failed"); return 1; }
    open_window(640, 200);
    describe_mode();
    if (module_check()) {
        pump(100);
        tests[t].fn();
    }
    say("Finished: results are in ovlresults. Close the window to quit.");
    while (!quit_request) poll_once(20);
    cleanup();
    if (logfile) fclose(logfile);
    r.r[0] = task_handle; r.r[1] = 0x4B534154;
    swi(Wimp_CloseDown, &r);
    return 0;
}
