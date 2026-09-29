/*
 * egl/parts/screen.c - the screen, Wimp windows and plotting frames (swap/present).
 * Part of egl_riscos.c: it is compiled by being included from there, as
 * one unit with the other parts (so their helpers stay private to the
 * library), not on its own. MIT licence (see LICENSE).
 */

#include "../egl_internal.h"

/* ------------------------------------------------------------------ */
/* Screen and Wimp                                                     */


/* One VDU variable (OS_ReadVduVariables), or 0 if it can't be read. */
static int vdu_variable(int var)
{
    int vars[2] = { var, -1 }, val = 0;
    _kernel_swi_regs r;
    r.r[0] = (int) vars;
    r.r[1] = (int) &val;
    if (_kernel_swi(OS_ReadVduVariables, &r, &r) != NULL)
        return 0;
    return val;
}

static void read_screen(screen_info *s)
{
    /* VDU variables: 0 ModeFlags, 4/5 X/YEigFactor, 9 Log2BPP,
       11/12 X/YWindLimit, 148 ScreenStart, 6 LineLength. */
    static const int vars[] = { 0, 4, 5, 9, 11, 12, 148, 6, -1 };
    int vals[8];
    _kernel_swi_regs r;

    memset(vals, 0, sizeof vals);
    vals[1] = vals[2] = 1;
    r.r[0] = (int) vars;
    r.r[1] = (int) vals;
    _kernel_swi(OS_ReadVduVariables, &r, &r);
    s->flags = vals[0];
    s->xeig = vals[1];
    s->yeig = vals[2];
    s->log2bpp = vals[3];
    s->width = vals[4] + 1;
    s->height = vals[5] + 1;
    s->start = (void *) vals[6];
    s->line_length = vals[7];
}

/* Colour order of the screen, or -1 if it isn't 32bpp. */
static int screen_layout(const screen_info *s)
{
    if (s->log2bpp != 5)
        return -1;
    return (s->flags & MODEFLAG_TRGB) ? LAYOUT_TRGB : LAYOUT_TBGR;
}

static int read_mode_variable(int mode, int var, int *value)
{
    _kernel_swi_regs r;
    int invalid = 0;
    r.r[0] = mode;
    r.r[1] = var;
    if (_kernel_swi_c(OS_ReadModeVariable, &r, &r, &invalid) != NULL || invalid)
        return 0;               /* C set: not a valid mode or variable */
    *value = r.r[2];
    return 1;
}

/* A sprite mode for a 32bpp sprite in the given colour order. TBGR is sprite
   type 6. TRGB uses the screen's own mode when that is 32bpp TRGB (what the
   SDL driver does), else a mode selector asking for TRGB. */
static int sprite_mode_for(int layout, const screen_info *s)
{
    static int trgb_selector[] = {
        1, 640, 480, 5, -1,     /* flags, x, y, log2bpp, frame rate */
        0, MODEFLAG_TRGB,       /* ModeFlags */
        3, -1,                  /* NColour: 16M */
        4, 1, 5, 1,             /* X/YEigFactor */
        -1
    };
    _kernel_swi_regs r;

    if (layout == LAYOUT_TBGR)
        return SPRITE_MODE_TYPE6_EIG(s->xeig, s->yeig);
    if (screen_layout(s) == LAYOUT_TRGB) {
        r.r[0] = 1;             /* return current mode specifier */
        if (_kernel_swi(OS_ScreenMode, &r, &r) == NULL)
            return r.r[1];
    }
    trgb_selector[10] = s->xeig;
    trgb_selector[12] = s->yeig;
    return (int) trgb_selector;
}


/* The vsync counter (OS_Byte 176: counts down, 8 bits here), and how many
   vsyncs have passed since it read `then` (a wrap after 255 reads as few,
   so waits stay short). */
static int vsync_counter(void)
{
    return _kernel_osbyte(OSBYTE_VSYNC_COUNT, 0, 255) & 0xFF;
}

static int vsyncs_since(int then)
{
    return (then - vsync_counter()) & 0xFF;
}

/* Wait for n vsyncs (the start of the next frame, n times). */
static void wait_vsyncs(int n)
{
    while (n-- > 0)
        _kernel_osbyte(OSBYTE_WAIT_VSYNC, 0, 0);
}

static int get_window_state(int handle, window_state *ws)
{
    _kernel_swi_regs r;
    ws->handle = handle;
    r.r[1] = (int) ws;
    return _kernel_swi(Wimp_GetWindowState, &r, &r) == NULL;
}

/* The size a window surface should have now. */
static int wanted_size(const egl_surface *surf, const screen_info *s, int *w, int *h)
{
    window_state ws;

    if (surf->rw && !surf->fixed && !surf->dmx) {
        *w = surf->rw;              /* a render size, scaled when shown */
        *h = surf->rh;
    } else if (surf->handle == HANDLE_SCREEN) {
        *w = s->width;
        *h = s->height;
    } else if (surf->fixed || surf->dmx) {
        *w = surf->w;
        *h = surf->h;
    } else {
        if (!get_window_state(surf->handle, &ws))
            return 0;
        *w = (ws.x1 - ws.x0) >> s->xeig;
        *h = (ws.y1 - ws.y0) >> s->yeig;
    }
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
    return 1;
}


/* In a Wimp_RedrawWindow / Wimp_UpdateWindow loop: the rectangle to draw
   now (words 7 to 10 of the block), and the next one (0 at the end). */
static os_rect redraw_clip(const int *block)
{
    os_rect c;
    c.x0 = block[7]; c.y0 = block[8]; c.x1 = block[9]; c.y1 = block[10];
    return c;
}

static int next_redraw_rect(int *block)
{
    _kernel_swi_regs r;
    r.r[1] = (int) block;
    if (_kernel_swi(Wimp_GetRectangle, &r, &r) != NULL)
        return 0;
    return r.r[0];
}

/* Is this a live window surface in the Wimp window `handle`? */
static int window_surface_in(const egl_surface *surf, int handle)
{
    return surf->kind == SURF_WINDOW && surf->handle == handle && !surf->destroy_pending;
}

#define MAX_PIECES 16

/* Set the graphics window (VDU 24 takes inclusive coordinates). */
static void set_graphics_window(const os_rect *c)
{
    int v[4], i;
    v[0] = c->x0; v[1] = c->y0; v[2] = c->x1 - 1; v[3] = c->y1 - 1;
    _kernel_oswrch(24);
    for (i = 0; i < 4; i++) {
        _kernel_oswrch(v[i] & 0xFF);
        _kernel_oswrch((v[i] >> 8) & 0xFF);
    }
}

/* A surface with a render size (EGL_RENDER_WIDTH/HEIGHT_RISCOS): shown
   scaled to fill its window's visible area, or the screen. */
static int is_scaled(const egl_surface *surf)
{
    return surf->rw && !surf->fixed && !surf->dmx;
}

/* The size, in screen pixels, a visible-area or full screen surface is
   shown at: its own size, or for a scaled one the window's or screen's. */
static void shown_size(const egl_surface *surf, const window_state *ws, const screen_info *s,
                       int *w, int *h)
{
    if (!is_scaled(surf)) {
        *w = surf->w;
        *h = surf->h;
        return;
    }
    if (surf->handle == HANDLE_SCREEN) {
        *w = s->width;
        *h = s->height;
    } else {
        *w = (ws->x1 - ws->x0) >> s->xeig;
        *h = (ws->y1 - ws->y0) >> s->yeig;
    }
    if (*w < 1) *w = 1;
    if (*h < 1) *h = 1;
}

/* The work area a visible-area surface covers, as box[0..3] = x0, y0,
   x1, y1 (OS units): from the top left of the visible area, the size it's
   shown at. */
static void shown_area(const egl_surface *surf, const window_state *ws, const screen_info *s,
                       int box[4])
{
    int dw, dh;
    shown_size(surf, ws, s, &dw, &dh);
    box[0] = ws->scroll_x;
    box[1] = ws->scroll_y - (dh << s->yeig);
    box[2] = ws->scroll_x + (dw << s->xeig);
    box[3] = ws->scroll_y;
}

/* Plot a window surface's sprite for one rectangle of a Wimp redraw or
   update loop (block = the Wimp_RedrawWindow/UpdateWindow block). */
static void plot_rectangle(const egl_surface *surf, const int *block, const screen_info *s,
                           const os_rect *clip)
{
    _kernel_swi_regs r;
    int x, top;
    os_rect vis;

    if (!surf->sprite)
        return;
    if (is_scaled(surf)) {
        /* stretched over the visible area (or the screen); the plain plot
           below when that happens to be the render size */
        riscos_dmx_placement pl;
        int base_x, top_os;
        memset(&pl, 0, sizeof pl);
        pl.visible = 1;
        if (surf->handle == HANDLE_SCREEN) {
            base_x = 0;
            top_os = s->height << s->yeig;
            pl.w = s->width;
            pl.h = s->height;
        } else {
            base_x = block[1];
            top_os = block[4];
            pl.w = (block[3] - block[1]) >> s->xeig;
            pl.h = (block[4] - block[2]) >> s->yeig;
        }
        if (pl.w != surf->w || pl.h != surf->h) {
            dmx_plot(surf, s, &pl, base_x, top_os, *clip);
            set_graphics_window(clip);
            return;
        }
    }
    if (surf->handle == HANDLE_SCREEN) {
        x = 0;
        top = s->height << s->yeig;                 /* top of the screen */
    } else if (surf->fixed) {
        x = block[1] - block[5] + surf->wa_x;       /* work area origin + offset */
        top = block[4] - block[6] + surf->wa_y;
    } else {
        x = block[1];                               /* visible area top left */
        top = block[4];
    }
    if (surf->sprite_h > surf->h) {
        /* padded sprite: show only the surface's own rows */
        vis.x0 = x > clip->x0 ? x : clip->x0;
        vis.x1 = x + (surf->w << s->xeig) < clip->x1 ? x + (surf->w << s->xeig) : clip->x1;
        vis.y1 = top < clip->y1 ? top : clip->y1;
        vis.y0 = top - (surf->h << s->yeig) > clip->y0 ? top - (surf->h << s->yeig) : clip->y0;
        if (vis.x0 >= vis.x1 || vis.y0 >= vis.y1)
            return;
        set_graphics_window(&vis);
    }
    r.r[0] = SPRITEOP_PUT_USER;  /* pixel for pixel */
    r.r[1] = (int) surf->area;
    r.r[2] = (int) surf->sprite;
    r.r[3] = x;
    r.r[4] = top - (surf->sprite_h << s->yeig);
    r.r[5] = 0;
    _kernel_swi(OS_SpriteOp, &r, &r);
    if (surf->sprite_h > surf->h)
        set_graphics_window(clip);
}

/* Where a work area surface sits on screen during a redraw/update loop. */
static os_rect fixed_rect(const egl_surface *surf, const int *block, const screen_info *s)
{
    os_rect r;
    r.x0 = block[1] - block[5] + surf->wa_x;
    r.y1 = block[4] - block[6] + surf->wa_y;
    r.x1 = r.x0 + (surf->w << s->xeig);
    r.y0 = r.y1 - (surf->h << s->yeig);
    return r;
}

/* Remove hole from the list of rectangles (each splits into up to 4). */
static int subtract_rect(os_rect *pieces, int n, const os_rect *hole)
{
    os_rect out[MAX_PIECES];
    int i, m = 0;
    for (i = 0; i < n; i++) {
        os_rect a = pieces[i];
        if (hole->x0 >= a.x1 || hole->x1 <= a.x0 || hole->y0 >= a.y1 || hole->y1 <= a.y0) {
            if (m < MAX_PIECES) out[m++] = a;
            continue;
        }
        if (hole->y1 < a.y1 && m < MAX_PIECES) {            /* band above */
            out[m].x0 = a.x0; out[m].x1 = a.x1; out[m].y0 = hole->y1; out[m].y1 = a.y1; m++;
        }
        if (hole->y0 > a.y0 && m < MAX_PIECES) {            /* band below */
            out[m].x0 = a.x0; out[m].x1 = a.x1; out[m].y0 = a.y0; out[m].y1 = hole->y0; m++;
        }
        {
            int y0 = hole->y0 > a.y0 ? hole->y0 : a.y0;
            int y1 = hole->y1 < a.y1 ? hole->y1 : a.y1;
            if (hole->x0 > a.x0 && m < MAX_PIECES) {        /* left of it */
                out[m].x0 = a.x0; out[m].x1 = hole->x0; out[m].y0 = y0; out[m].y1 = y1; m++;
            }
            if (hole->x1 < a.x1 && m < MAX_PIECES) {        /* right of it */
                out[m].x0 = hole->x1; out[m].x1 = a.x1; out[m].y0 = y0; out[m].y1 = y1; m++;
            }
        }
    }
    memcpy(pieces, out, m * sizeof out[0]);
    return m;
}

static void plot_loop(egl_display *d, int handle, egl_surface *only, int *block, int more)
{
    screen_info s;
    egl_surface *surf, *f;
    os_rect clip, pieces[MAX_PIECES], hole;
    int n, i, above;

    /* Visible area surfaces first, but never over the work area surfaces
       (plotting them twice would flash the wrong image there while the
       screen is being scanned out); then the work area surfaces. Showing a
       visible area surface (only != NULL) replots the work area ones too. */
    read_screen(&s);
    while (more) {
        clip = redraw_clip(block);
        for (surf = d->surfaces; surf; surf = surf->next) {
            if (!window_surface_in(surf, handle) || surf->fixed || (only && only != surf))
                continue;
            if (surf->ovl_shown) {
                ovl_redraw_rectangle(surf, block);  /* the overlay shows it */
                continue;
            }
            pieces[0] = clip;
            n = 1;
            for (f = d->surfaces; f; f = f->next) {
                if (window_surface_in(f, handle) && f->fixed && f->sprite) {
                    hole = fixed_rect(f, block, &s);
                    n = subtract_rect(pieces, n, &hole);
                }
            }
            if (n == 1 && pieces[0].x0 == clip.x0 && pieces[0].x1 == clip.x1 &&
                pieces[0].y0 == clip.y0 && pieces[0].y1 == clip.y1) {
                plot_rectangle(surf, block, &s, &clip);
            } else {
                for (i = 0; i < n; i++) {
                    set_graphics_window(&pieces[i]);
                    plot_rectangle(surf, block, &s, &pieces[i]);
                }
                set_graphics_window(&clip);
            }
        }
        /* Work area surfaces in stacking order. Showing one replots those
           above it too (a subwindow's own subwindows, say). */
        above = (only == NULL || !only->fixed);
        for (surf = d->surfaces; surf; surf = surf->next) {
            if (surf == only)
                above = 1;
            if (!window_surface_in(surf, handle) || !surf->fixed)
                continue;
            if (above)
                plot_rectangle(surf, block, &s, &clip);
        }
        more = next_redraw_rect(block);
    }
}

/* Clip damage rectangles (x, y, w, h in pixels from the bottom left, as
   eglSwapBuffersWithDamageKHR gives them) to the surface and turn them into
   OS unit offsets from its top left: out[i] = x0, y0, x1, y1 with y going up
   (so y values are <= 0). More than MAX_DAMAGE become their bounding box.
   Returns the number of rectangles, or -1 for "the whole surface". */
static int damage_rects(const egl_surface *surf, const EGLint *rects, int n,
                        const screen_info *s, os_rect *out)
{
    int i, m = 0, over = 0;
    os_rect box = { 0, 0, 0, 0 };

    if (!rects || n <= 0)
        return -1;
    for (i = 0; i < n; i++) {
        int x0 = rects[i * 4], y0 = rects[i * 4 + 1];
        int x1 = x0 + rects[i * 4 + 2], y1 = y0 + rects[i * 4 + 3];
        os_rect r;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > surf->w) x1 = surf->w;
        if (y1 > surf->h) y1 = surf->h;
        if (x0 >= x1 || y0 >= y1)
            continue;
        r.x0 = x0 << s->xeig;
        r.x1 = x1 << s->xeig;
        r.y0 = (y0 - surf->h) << s->yeig;
        r.y1 = (y1 - surf->h) << s->yeig;
        if (m == 0 && !over) {
            box = r;
        } else {
            if (r.x0 < box.x0) box.x0 = r.x0;
            if (r.y0 < box.y0) box.y0 = r.y0;
            if (r.x1 > box.x1) box.x1 = r.x1;
            if (r.y1 > box.y1) box.y1 = r.y1;
        }
        if (m < MAX_DAMAGE)
            out[m++] = r;
        else
            over = 1;
    }
    if (over) {
        out[0] = box;
        return 1;
    }
    return m;
}

/* DispmanX compatibility: plot the surface (its source rectangle) scaled to
   the element's destination rectangle. base_x/top: where the element's
   coordinate space starts (its top left, OS units on screen: the screen's
   top left, or a window's work area origin); clip: the part of the screen
   that may be drawn (OS units, x1/y1 exclusive). */
static void dmx_plot(const egl_surface *surf, const screen_info *s,
                     const riscos_dmx_placement *pl, int base_x, int top_os,
                     os_rect clip)
{
    _kernel_swi_regs r;
    int factors[4], sw, sh, sxe, sye;
    long long top, y;
    os_rect e;

    sw = pl->src_w > 0 ? pl->src_w : surf->w;
    sh = pl->src_h > 0 ? pl->src_h : surf->h;
    sxe = s->xeig;                      /* the sprite is at the screen's resolution */
    sye = s->yeig;
    factors[0] = pl->w << s->xeig;      /* x: multiply, then divide */
    factors[1] = pl->h << s->yeig;
    factors[2] = sw << sxe;
    factors[3] = sh << sye;

    e.x0 = base_x + (pl->x << s->xeig);
    e.x1 = base_x + ((pl->x + pl->w) << s->xeig);
    e.y1 = top_os - (pl->y << s->yeig);
    e.y0 = top_os - ((pl->y + pl->h) << s->yeig);
    if (clip.x0 < e.x0) clip.x0 = e.x0;
    if (clip.y0 < e.y0) clip.y0 = e.y0;
    if (clip.x1 > e.x1) clip.x1 = e.x1;
    if (clip.y1 > e.y1) clip.y1 = e.y1;
    if (clip.x0 >= clip.x1 || clip.y0 >= clip.y1)
        return;

    /* The source rectangle's top left lands on the destination's top left;
       the sprite's padding rows (see MIN_SPRITE_BYTES) fall outside. */
    top = (long long) e.y1 + ((long long) (pl->src_y << sye) * factors[1]) / factors[3];
    /* whole screen pixels, so the top row lands on the destination's top */
    y = top - (((((long long) (surf->sprite_h << sye) * factors[1]) / factors[3])
                >> s->yeig) << s->yeig);
    set_graphics_window(&clip);
    r.r[0] = SPRITEOP_PUT_SCALED;
    r.r[1] = (int) surf->area;
    r.r[2] = (int) surf->sprite;
    r.r[3] = (int) (e.x0 - ((long long) (pl->src_x << sxe) * factors[0]) / factors[2]);
    r.r[4] = (int) y;
    r.r[5] = 0;
    r.r[6] = (int) factors;
    r.r[7] = 0;
    _kernel_swi(OS_SpriteOp, &r, &r);
}

/* Plot a DispmanX surface shown in a window over each rectangle of a
   Wimp_RedrawWindow / Wimp_UpdateWindow loop. */
static void dmx_window_loop(const egl_surface *surf, const screen_info *s,
                            const riscos_dmx_placement *pl, int *block, int more)
{
    while (more) {
        os_rect clip = redraw_clip(block);
        dmx_plot(surf, s, pl, block[1] - block[5], block[4] - block[6], clip);
        set_graphics_window(&clip);
        more = next_redraw_rect(block);
    }
}

/* DispmanX compatibility: show a frame of an element. Full screen: after
   the vsync wait, plot it on the screen. Window mode (libbcm_host shows its
   display in a desktop window): update the window with no vsync wait (it
   would stop the desktop); swap() then lets libbcm_host poll the Wimp. */
static void present_dmx(egl_surface *surf, const screen_info *s)
{
    riscos_dmx_placement pl;

    memset(&pl, 0, sizeof pl);
    if (!__riscos_dispmanx_placement || !__riscos_dispmanx_placement(surf->dmx, &pl))
        return;                         /* element gone */
    if (pl.window) {
        if (pl.visible && surf->sprite && pl.w > 0 && pl.h > 0) {
            _kernel_swi_regs r;
            int block[11];
            block[0] = pl.window;
            block[1] = pl.x << s->xeig;
            block[2] = -((pl.y + pl.h) << s->yeig);
            block[3] = (pl.x + pl.w) << s->xeig;
            block[4] = -(pl.y << s->yeig);
            r.r[1] = (int) block;
            if (_kernel_swi(Wimp_UpdateWindow, &r, &r) == NULL)
                dmx_window_loop(surf, s, &pl, block, r.r[0]);
        }
        return;
    }
    wait_vsyncs(surf->swap_interval);
    if (!pl.visible || !surf->sprite || pl.w <= 0 || pl.h <= 0)
        return;
    {
        os_rect all;
        all.x0 = 0; all.y0 = 0;
        all.x1 = s->width << s->xeig; all.y1 = s->height << s->yeig;
        dmx_plot(surf, s, &pl, 0, all.y1, all);
        set_graphics_window(&all);
    }
}

/* Present a full screen surface: switch screen banks, or wait for the
   vsync and plot the frame (all of it, or the nd damaged rectangles;
   nd < 0 = all). */
static void present_fullscreen(egl_surface *surf, const screen_info *s, const os_rect *dmg, int nd)
{
    os_rect all;
    int top = s->height << s->yeig, i;

    if (surf->banks) {
        /* Wait only until swap_interval vsyncs have passed since the last
           switch: at most one switch per vsync (so no tearing with three
           banks), without blocking for the next vsync when one has
           already gone by (that cost a 60 fps video player most of a
           frame). */
        while (surf->bank_vsync >= 0 && vsyncs_since(surf->bank_vsync) < surf->swap_interval)
            wait_vsyncs(1);
        /* Show the finished bank, then draw into the oldest one. With
           three banks that one isn't on screen even if the display only
           switches at the next vsync. */
        _kernel_osbyte(OSBYTE_DISPLAY_BANK, surf->draw_bank, 0);
        surf->bank_vsync = vsync_counter();
        surf->draw_bank = surf->draw_bank % surf->banks + 1;
        surf->pixels = surf->bank_addr[surf->draw_bank];
        return;
    }
    /* one buffer: the wait times the plot to just after a vsync */
    wait_vsyncs(surf->swap_interval);
    if (surf->direct || !surf->sprite)
        return;
    all.x0 = 0; all.y0 = 0;
    all.x1 = s->width << s->xeig; all.y1 = top;
    if (nd < 0) {
        plot_rectangle(surf, NULL, s, &all);
        return;
    }
    for (i = 0; i < nd; i++) {
        os_rect c;
        c.x0 = dmg[i].x0; c.x1 = dmg[i].x1;
        c.y0 = top + dmg[i].y0; c.y1 = top + dmg[i].y1;
        set_graphics_window(&c);
        plot_rectangle(surf, NULL, s, &c);
    }
    set_graphics_window(&all);
}

/* Present a window surface: through its hardware overlay if it has one
   showing, else update the part of the work area it covers (or each of
   the nd damaged parts; nd < 0 = all) and plot it there. */
static void present_window(egl_display *d, egl_surface *surf, const screen_info *s,
                           const os_rect *dmg, int nd)
{
    _kernel_swi_regs r;
    int block[11];
    int i, left, top, dw = surf->w, dh = surf->h;

    /* A visible-area surface shown through a hardware overlay: the frame
       is copied into it instead of being plotted (parts/overlay.c). If the
       overlay was showing and now isn't, plot the whole surface. */
    if (!surf->fixed) {
        int was_shown = surf->ovl_shown;
        if (ovl_update(d, surf, s, 1))
            return;
        if (was_shown)
            nd = -1;
    }

    if (surf->fixed) {
        left = surf->wa_x;
        top = surf->wa_y;
    } else {
        window_state ws;
        if (!get_window_state(surf->handle, &ws))
            return;
        left = ws.scroll_x;
        top = ws.scroll_y;
        shown_size(surf, &ws, s, &dw, &dh);
    }
    for (i = 0; i < (nd < 0 ? 1 : nd); i++) {
        block[0] = surf->handle;
        if (nd < 0) {
            block[1] = left;
            block[2] = top - (dh << s->yeig);
            block[3] = left + (dw << s->xeig);
            block[4] = top;
        } else {
            block[1] = left + dmg[i].x0;
            block[2] = top + dmg[i].y0;
            block[3] = left + dmg[i].x1;
            block[4] = top + dmg[i].y1;
        }
        r.r[1] = (int) block;
        if (_kernel_swi(Wimp_UpdateWindow, &r, &r) != NULL)
            return;
        plot_loop(d, surf->handle, surf, block, r.r[0]);
    }
}

/* Show a finished frame of a window surface: all of it, or only the damaged
   rectangles (rects/n as for eglSwapBuffersWithDamageKHR; NULL = all). */
static void present(egl_display *d, egl_surface *surf, const screen_info *s,
                    const EGLint *rects, int n)
{
    os_rect dmg[MAX_DAMAGE];
    int nd;

    if (surf->dmx) {
        present_dmx(surf, s);           /* always the whole surface */
        return;
    }
    nd = damage_rects(surf, rects, n, s, dmg);
    if (nd == 0 && !surf->banks)
        return;                         /* all rectangles empty: nothing changed */
    if (is_scaled(surf))
        nd = -1;                        /* scaled: always all of it */
    if (surf->handle == HANDLE_SCREEN)
        present_fullscreen(surf, s, dmg, nd);
    else
        present_window(d, surf, s, dmg, nd);
}
