/*
 * egl/parts/overlay.c - window surfaces shown through a hardware overlay
 * (the VideoOverlay module). Part of egl_riscos.c: it is compiled by being
 * included from there, as one unit with the other parts, not on its own.
 * MIT licence (see LICENSE).
 *
 * What it does. GL still renders into the surface's sprite in ordinary
 * memory (overlay memory is uncached: reading it, as depth testing and
 * blending do, runs at 152 MB/s on a Pi 4). eglSwapBuffers then copies the
 * finished frame into an overlay buffer and shows that buffer, instead of
 * plotting the sprite into the window. That saves the plot (about 3 ms a
 * frame for a 640x480 window on a Pi 4) and switches buffers at vsync.
 *
 * When. Opt-in: only for a surface whose program asked for one
 * (EGL_OVERLAY_RISCOS = EGL_TRUE when creating it, or later with
 * eglSurfaceAttrib: a program's "hardware acceleration" option), or for
 * every program that hasn't refused (EGL_FALSE) when the user sets the
 * system variable EGL$Overlay to "on" ("yes", "1"). EGL$Overlay "off"
 * ("no", "0") turns them off for every program, whatever it asked for.
 * Opt-in because an overlay changes things a program may not expect: it
 * covers menus until the program swaps or calls eglCheckOverlaysRISCOS,
 * swaps can wait for a vsync, screen grabs don't see it, and it takes GPU
 * memory. Then only visible-area window surfaces (not work area surfaces,
 * full screen or DispmanX), once the program is animating (OVL_WARMUP swaps
 * in a row, each within OVL_GAP_CS of the last: a window redrawn now and
 * then gains nothing).
 * Every problem falls back to plotting the sprite, as without overlays:
 * VideoOverlay not loaded, Create refusing, a buffer that can't be mapped
 * (the GPU out of memory), any error while showing a frame. After a
 * failure the surface doesn't try again until its size or the screen mode
 * changes.
 *
 * What the Pi 4 taught us (riscos-mesa tests/ovltest.c, 2026-09-28,
 * VideoOverlay 0.02 with BCMVideo):
 *   - VideoOverlay_Vet fails for every format: probe with Create.
 *   - A buffer switch takes effect at the next vsync. Showing two new
 *     buffers within one frame, or writing into a buffer that was shown
 *     less than a vsync ago, tears. So before copying a frame in we wait
 *     for a vsync if none has happened since the last switch (unless the
 *     swap interval is 0). Three buffers when the GPU has room, else two.
 *   - The overlay is "Basic": it covers everything, windows and menus
 *     included, and VideoOverlay doesn't cut it back. So while any window
 *     or menu overlaps the surface (found by following the window stack up
 *     from ours with Wimp_GetWindowState), the overlay is hidden and the
 *     sprite is plotted instead; it comes back when nothing overlaps.
 *     This is checked at every swap, in eglRedrawWindowRISCOS, and in
 *     eglCheckOverlaysRISCOS, which a program that stops swapping (a
 *     paused video) calls on null events. Once it hasn't swapped for
 *     OVL_GAP_CS, those checks hide the overlay and plot the last frame,
 *     and leave it plotted (the Wimp then looks after menus and windows
 *     over it); the overlay is kept, and the next swap shows through it.
 *   - A mode change doesn't free the old overlay (its ID keeps working), so
 *     we destroy it ourselves and make a new one.
 *   - Buffers stay valid across Wimp_Poll, but we map a buffer only while
 *     copying into it (MapBuffer + UnmapBuffer cost about 27 us).
 */

#include "../egl_internal.h"

#define OVL_WARMUP   3          /* swaps in a row before an overlay is made */
#define OVL_GAP_CS   25         /* ... each at most this long after the last */
#define OVL_TYPE_BASIC 1        /* VideoOverlay_Create's type: on top of everything */
#ifndef OS_ReadMonotonicTime
#define OS_ReadMonotonicTime 0x42
#endif

/* VideoOverlay's SWIs, looked up by name (its SWI chunk isn't fixed) */
enum { OV_CREATE, OV_DESTROY, OV_DISPLAY, OV_MAP, OV_UNMAP,
       OV_SET_SCALE, OV_SET_WINDOW, OV_SET_POSITION, OV_REDRAW_WINDOW, OV_COUNT_ };
static struct { const char *name; int no; } ovl_swi[OV_COUNT_] = {
    { "VideoOverlay_Create", 0 },        { "VideoOverlay_Destroy", 0 },
    { "VideoOverlay_DisplayBuffer", 0 }, { "VideoOverlay_MapBuffer", 0 },
    { "VideoOverlay_UnmapBuffer", 0 },   { "VideoOverlay_SetScale", 0 },
    { "VideoOverlay_SetWindow", 0 },     { "VideoOverlay_SetPosition", 0 },
    { "VideoOverlay_RedrawWindow", 0 },
};
#define OVL_SWI(which) (ovl_swi[OV_##which].no)
static int ovl_module;          /* 0 unknown, 1 there, -1 missing */

/* Is VideoOverlay loaded? Looks the SWIs up by name (once it's found). */
static int ovl_available(void)
{
    _kernel_swi_regs r;
    int i;
    if (ovl_module > 0)
        return 1;
    /* not cached when missing: the program may load it later */
    for (i = 0; i < OV_COUNT_; i++) {
        r.r[1] = (int) ovl_swi[i].name;
        if (_kernel_swi(OS_SWINumberFromString, &r, &r) != NULL)
            return 0;
        ovl_swi[i].no = r.r[0] & ~SWI_X_BIT;
    }
    ovl_module = 1;
    return 1;
}

/* EGL$Overlay: "off" (or "no", "0") turns overlays off for every program;
   "on" (or "yes", "1") turns them on for every program that hasn't said
   EGL_FALSE. Returns -1 off, 1 on, 0 unset (the program decides). */
static int ovl_user_setting(void)
{
    const char *v = getenv("EGL$Overlay");
    if (!v)
        return 0;
    if (strcasecmp(v, "off") == 0 || strcasecmp(v, "no") == 0 || strcmp(v, "0") == 0)
        return -1;
    if (strcasecmp(v, "on") == 0 || strcasecmp(v, "yes") == 0 || strcmp(v, "1") == 0)
        return 1;
    return 0;
}

/* Does this surface want an overlay? Opt-in: the program asks with
   EGL_OVERLAY_RISCOS = EGL_TRUE, or the user sets EGL$Overlay on. */
static int ovl_wanted(const egl_surface *surf)
{
    int user = ovl_user_setting();
    if (user < 0 || surf->ovl_want == 0)
        return 0;
    return surf->ovl_want > 0 || user > 0;
}

static int ovl_task_handle(void)
{
    _kernel_swi_regs r;
    r.r[0] = 5;                                 /* Wimp_ReadSysInfo 5: current task */
    if (_kernel_swi(Wimp_ReadSysInfo, &r, &r) != NULL)
        return 0;
    return r.r[0];
}

static _kernel_oserror *ovl_call(int which, int id, int a)
{
    _kernel_swi_regs r;
    r.r[0] = id;
    r.r[1] = a;
    return _kernel_swi(which, &r, &r);
}

/* A number that changes when the screen mode does. */
static int ovl_mode_signature(const screen_info *s)
{
    return s->width ^ (s->height << 12) ^ (s->log2bpp << 24) ^ (s->flags << 7);
}

static void ovl_destroy(egl_surface *surf)
{
    if (surf->ovl_id) {
        if (surf->ovl_shown)
            ovl_call(OVL_SWI(DISPLAY), surf->ovl_id, -1);
        ovl_call(OVL_SWI(DESTROY), surf->ovl_id, 0);
    }
    surf->ovl_id = 0;
    surf->ovl_shown = 0;
    surf->ovl_last = -1;                /* a new overlay starts with nothing to show */
    if (surf->ovl_state == OVL_ON)
        surf->ovl_state = OVL_OFF;
}

/* Give up on the overlay for this surface until its size or the mode
   changes; the sprite is plotted meanwhile. */
static void ovl_fail(egl_surface *surf)
{
    ovl_destroy(surf);
    surf->ovl_state = OVL_FAILED;
}

/* Make an overlay the size of the surface, in its colour order, with 3
   buffers (else 2), and map each buffer once so a GPU without room for
   them shows up now. */
static int ovl_create(egl_surface *surf, const screen_info *s)
{
    int sel[12], banks, b, task = ovl_task_handle();
    _kernel_swi_regs r;
    if (!task)
        return 0;
    for (banks = 3; banks >= 2; banks--) {
        sel[0] = 1; sel[1] = surf->w; sel[2] = surf->h; sel[3] = 5; sel[4] = -1;
        sel[5] = 0;  sel[6] = surf->cfg->layout == LAYOUT_TRGB ? MODEFLAG_TRGB : 0;
        sel[7] = 3;  sel[8] = -1;
        sel[9] = 13; sel[10] = banks;
        sel[11] = -1;
        r.r[0] = (int) sel;
        r.r[1] = (surf->w << 16) | surf->h;
        r.r[2] = 0;
        r.r[3] = task;
        if (_kernel_swi(OVL_SWI(CREATE), &r, &r) != NULL)
            return 0;                   /* can't have one of this size or format */
        surf->ovl_id = r.r[0];
        surf->ovl_type = r.r[1] & 0xFF;
        for (b = 0; b < banks; b++) {
            if (ovl_call(OVL_SWI(MAP), surf->ovl_id, b) != NULL)
                break;
            ovl_call(OVL_SWI(UNMAP), surf->ovl_id, b);
        }
        if (b == banks)
            break;
        ovl_call(OVL_SWI(DESTROY), surf->ovl_id, 0);
        surf->ovl_id = 0;
    }
    if (!surf->ovl_id)
        return 0;
    if (ovl_call(OVL_SWI(SET_WINDOW), surf->ovl_id, surf->handle) != NULL) {
        ovl_call(OVL_SWI(DESTROY), surf->ovl_id, 0);
        surf->ovl_id = 0;
        return 0;
    }
    surf->ovl_banks = banks;
    surf->ovl_next = 0;
    surf->ovl_w = surf->w;
    surf->ovl_h = surf->h;
    surf->ovl_mode = ovl_mode_signature(s);
    surf->ovl_placed[0] = surf->ovl_placed[1] = 0x7FFFFFFF;
    surf->ovl_vsync = vsync_counter() - 1;     /* "a vsync has happened since" */
    surf->ovl_state = OVL_ON;
    return 1;
}

/* Scale the overlay to the surface's pixels and put it at the top left of
   the window's visible area (work area coordinates), clipped to it. */
static int ovl_place(egl_surface *surf, const window_state *ws, const screen_info *s)
{
    _kernel_swi_regs r;
    int dw, dh;
    shown_size(surf, ws, s, &dw, &dh);      /* > the surface: scaled by the overlay */
    if (surf->ovl_placed[0] == ws->scroll_x && surf->ovl_placed[1] == ws->scroll_y &&
        surf->ovl_placed[2] == dw && surf->ovl_placed[3] == dh)
        return 1;
    r.r[0] = surf->ovl_id;
    r.r[1] = dw;
    r.r[2] = dh;
    r.r[3] = (surf->w << 16) | surf->h;
    if (_kernel_swi(OVL_SWI(SET_SCALE), &r, &r) != NULL)
        return 0;
    r.r[0] = surf->ovl_id;
    r.r[1] = ws->scroll_x;
    r.r[2] = ws->scroll_y;
    r.r[3] = ws->scroll_x;
    r.r[4] = ws->scroll_y - (dh << s->yeig);
    r.r[5] = ws->scroll_x + (dw << s->xeig);
    r.r[6] = ws->scroll_y;
    if (_kernel_swi(OVL_SWI(SET_POSITION), &r, &r) != NULL)
        return 0;
    surf->ovl_placed[0] = ws->scroll_x;
    surf->ovl_placed[1] = ws->scroll_y;
    surf->ovl_placed[2] = dw;
    surf->ovl_placed[3] = dh;
    return 1;
}

/* Is the window closed, or is any window or menu above it in the stack
   over its visible area? Each window's "behind" word is the handle of the
   window just in front of it (-1 at the front). */
static int ovl_covered(const window_state *ws)
{
    window_state w;
    int h, n;
    if (!(ws->flags & WINDOW_FLAG_OPEN))
        return 1;                               /* not open (closed, iconised) */
    for (h = ws->behind, n = 0; h != -1 && n < 256; n++) {
        if (!get_window_state(h, &w))
            return 1;                           /* can't tell: be safe */
        if ((w.flags & WINDOW_FLAG_OPEN) && w.x0 < ws->x1 && w.x1 > ws->x0 &&
            w.y0 < ws->y1 && w.y1 > ws->y0)
            return 1;
        h = w.behind;
    }
    return 0;
}

/* Can this surface use an overlay at all? */
static int ovl_candidate(egl_display *d, egl_surface *surf)
{
    egl_surface *o;
    if (surf->kind != SURF_WINDOW || surf->handle < 0 || surf->fixed || surf->dmx ||
        !ovl_wanted(surf) || !surf->sprite || surf->destroy_pending)
        return 0;
    /* work area surfaces in the same window would be under the overlay */
    for (o = d->surfaces; o; o = o->next)
        if (o != surf && window_surface_in(o, surf->handle))
            return 0;
    return 1;
}

/* Update the window's surface area from the sprite (after the overlay has
   been hidden or given up). */
static void ovl_replot(egl_display *d, egl_surface *surf, const window_state *ws,
                      const screen_info *s)
{
    _kernel_swi_regs r;
    int block[11];
    block[0] = surf->handle;
    shown_area(surf, ws, s, &block[1]);
    r.r[1] = (int) block;
    if (_kernel_swi(Wimp_UpdateWindow, &r, &r) == NULL)
        plot_loop(d, surf->handle, surf, block, r.r[0]);
}

/* The overlay has just started showing the surface: have the window's
   area redrawn, so VideoOverlay_RedrawWindow can prepare it (a Z-Order
   overlay shows through transparent pixels there; a Basic overlay's
   software fallback draws there). */
static void ovl_just_shown(egl_surface *surf, const window_state *ws, const screen_info *s)
{
    _kernel_swi_regs r;
    int box[4];
    shown_area(surf, ws, s, box);
    r.r[0] = surf->handle;
    r.r[1] = box[0]; r.r[2] = box[1]; r.r[3] = box[2]; r.r[4] = box[3];
    _kernel_swi(Wimp_ForceRedraw, &r, &r);
}

/* Copy the finished frame into overlay buffer b (mapped just for the
   copy). Returns 0 if the buffer can't be mapped. */
static int ovl_copy_frame(egl_surface *surf, int b)
{
    _kernel_swi_regs r;
    const int *planes;
    uint8_t *dst;
    int stride, y;
    r.r[0] = surf->ovl_id;
    r.r[1] = b;
    if (_kernel_swi(OVL_SWI(MAP), &r, &r) != NULL)
        return 0;
    planes = (const int *) r.r[0];          /* plane 0: address, row stride */
    dst = (uint8_t *) planes[0];
    stride = planes[1];
    for (y = 0; y < surf->h; y++)
        memcpy(dst + (size_t) y * stride, (const uint8_t *) surf->pixels + (size_t) y * surf->stride * 4,
               (size_t) surf->w * 4);
    ovl_call(OVL_SWI(UNMAP), surf->ovl_id, b);
    return 1;
}

/* Show or hide the overlay to suit what covers the window. new_frame: a
   frame to copy in and show (eglSwapBuffers); otherwise just re-show the
   last one. Returns 1 if the overlay shows the surface, 0 if the caller
   has to plot the sprite. */
/* Can the surface (still) have an overlay? If it can't any more, destroy
   the overlay; after a mode or size change, start again. When the overlay
   goes while it was showing and there's no new frame to plot, the last one
   is plotted. */
static int ovl_still_usable(egl_display *d, egl_surface *surf, const window_state *ws,
                            const screen_info *s, int new_frame)
{
    int was_shown = surf->ovl_shown;
    if (!ovl_candidate(d, surf)) {
        if (surf->ovl_id) {
            ovl_destroy(surf);
            if (was_shown && !new_frame && surf->sprite)
                ovl_replot(d, surf, ws, s);
        }
        return 0;
    }
    /* a new mode or size: start again (the old ID outlives a mode change) */
    if (surf->ovl_state != OVL_OFF &&
        (surf->ovl_mode != ovl_mode_signature(s) || surf->ovl_w != surf->w || surf->ovl_h != surf->h)) {
        ovl_destroy(surf);
        surf->ovl_state = OVL_OFF;
        if (was_shown && !new_frame)
            ovl_replot(d, surf, ws, s);
    }
    return surf->ovl_state != OVL_FAILED;
}

/* Hide the overlay (keeping it), and plot the last frame if asked. */
static void ovl_hide(egl_display *d, egl_surface *surf, const window_state *ws,
                     const screen_info *s, int replot)
{
    if (!surf->ovl_shown)
        return;
    ovl_call(OVL_SWI(DISPLAY), surf->ovl_id, -1);
    surf->ovl_shown = 0;
    if (replot)
        ovl_replot(d, surf, ws, s);
}

/* Make the overlay. Returns 0 if there's none to be had (then, with
   VideoOverlay loaded, don't try again until the size or mode changes), or
   if it has to stay hidden for now. */
static int ovl_start(egl_surface *surf, const window_state *ws, const screen_info *s)
{
    if (!ovl_available() || !ovl_create(surf, s)) {
        if (ovl_module > 0) {
            surf->ovl_state = OVL_FAILED;
            surf->ovl_w = surf->w;
            surf->ovl_h = surf->h;
            surf->ovl_mode = ovl_mode_signature(s);
        }
        return 0;
    }
    return !(surf->ovl_type == OVL_TYPE_BASIC && ovl_covered(ws));
}

/* No new frame (a redraw or a check): show the last frame through the
   overlay again if it's hidden. */
static int ovl_reshow(egl_display *d, egl_surface *surf, const window_state *ws,
                      const screen_info *s)
{
    if (!surf->ovl_shown && surf->ovl_last >= 0) {
        if (ovl_call(OVL_SWI(DISPLAY), surf->ovl_id, surf->ovl_last) != NULL) {
            ovl_fail(surf);
            ovl_replot(d, surf, ws, s);
            return 0;
        }
        surf->ovl_shown = 1;
        ovl_just_shown(surf, ws, s);
    }
    return surf->ovl_shown;
}

/* A new frame: copy it into the next buffer and show that. */
static int ovl_show_frame(egl_surface *surf, const window_state *ws, const screen_info *s)
{
    int b = surf->ovl_next;
    /* A buffer switch happens at the next vsync: don't write into a buffer
       until one has passed since the last switch. */
    if (surf->swap_interval > 0 && vsync_counter() == surf->ovl_vsync)
        wait_vsyncs(1);
    if (!ovl_copy_frame(surf, b) || ovl_call(OVL_SWI(DISPLAY), surf->ovl_id, b) != NULL) {
        ovl_fail(surf);
        return 0;
    }
    surf->ovl_vsync = vsync_counter();
    surf->ovl_last = b;
    surf->ovl_next = (b + 1) % surf->ovl_banks;
    if (!surf->ovl_shown) {
        surf->ovl_shown = 1;
        ovl_just_shown(surf, ws, s);
    }
    return 1;
}

static int ovl_update(egl_display *d, egl_surface *surf, const screen_info *s, int new_frame)
{
    window_state ws;
    int covered, now;
    _kernel_swi_regs r;

    now = _kernel_swi(OS_ReadMonotonicTime, &r, &r) == NULL ? r.r[0] : 0;
    if (new_frame) {
        surf->ovl_run = surf->ovl_run > 0 && now - surf->ovl_swap_cs <= OVL_GAP_CS ? surf->ovl_run + 1 : 1;
        surf->ovl_swap_cs = now;
    }
    if (!get_window_state(surf->handle, &ws) || !ovl_still_usable(d, surf, &ws, s, new_frame))
        return 0;
    /* Stopped swapping (a paused video): back to the plotted sprite, which
       the Wimp keeps right under menus and windows with no help. The
       overlay and its buffers are kept, so the next swap shows through it
       again at once. Otherwise, hide a Basic overlay while something
       overlaps the window. */
    covered = !new_frame && now - surf->ovl_swap_cs > OVL_GAP_CS;
    if (!covered)
        covered = surf->ovl_type == OVL_TYPE_BASIC && ovl_covered(&ws);
    if (covered) {
        ovl_hide(d, surf, &ws, s, !new_frame);
        return 0;
    }
    if (surf->ovl_state == OVL_OFF) {
        if (!new_frame || surf->ovl_run < OVL_WARMUP)
            return 0;                           /* not animating (yet) */
        if (!ovl_start(surf, &ws, s))
            return 0;
    }
    if (!ovl_place(surf, &ws, s)) {
        ovl_fail(surf);
        return 0;
    }
    return new_frame ? ovl_show_frame(surf, &ws, s) : ovl_reshow(d, surf, &ws, s);
}

/* In a Wimp redraw loop: the overlay's own redraw for one rectangle. */
static void ovl_redraw_rectangle(egl_surface *surf, int *block)
{
    _kernel_swi_regs r;
    r.r[0] = surf->ovl_id;
    r.r[1] = (int) block;
    _kernel_swi(OVL_SWI(REDRAW_WINDOW), &r, &r);
}

/* Re-check every overlay-backed surface (stacking, mode). */
static void ovl_check_all(egl_display *d)
{
    egl_surface *surf;
    screen_info s;
    read_screen(&s);
    for (surf = d->surfaces; surf; surf = surf->next)
        if (surf->kind == SURF_WINDOW && (surf->ovl_id || surf->ovl_state == OVL_ON))
            ovl_update(d, surf, &s, 0);
}
