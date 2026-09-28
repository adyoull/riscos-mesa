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
 * When. Visible-area window surfaces only (not work area surfaces, full
 * screen or DispmanX), once the program is animating (OVL_WARMUP swaps in
 * a row, each within OVL_GAP_CS of the last: a window redrawn now and then
 * gains nothing, and an overlay left over a menu until the next swap would
 * be worse), unless the program or the user opts out:
 *   - EGL_OVERLAY_RISCOS = EGL_FALSE when creating the surface, or later
 *     with eglSurfaceAttrib (a program's "hardware acceleration" option);
 *   - the system variable EGL$Overlay set to "off" (or "no", "0"): no
 *     overlays for any program.
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
 *     paused video) calls on null events.
 *   - A mode change doesn't free the old overlay (its ID keeps working), so
 *     we destroy it ourselves and make a new one.
 *   - Buffers stay valid across Wimp_Poll, but we map a buffer only while
 *     copying into it (MapBuffer + UnmapBuffer cost about 27 us).
 */

#define OVL_OFF      0          /* no overlay (yet): the sprite is plotted */
#define OVL_ON       1          /* an overlay exists */
#define OVL_FAILED   2          /* gave up until the size or mode changes */
#define OVL_WARMUP   3          /* swaps in a row before an overlay is made */
#define OVL_GAP_CS   25         /* ... each at most this long after the last */
#define OS_ReadMonotonicTime 0x42

enum { OV_CREATE, OV_DESTROY, OV_DISPLAY, OV_MAP, OV_UNMAP, OV_COUNT_ };
static struct { const char *name; int no; } ovl_swi[] = {
    { "VideoOverlay_Create", 0 },  { "VideoOverlay_Destroy", 0 },
    { "VideoOverlay_DisplayBuffer", 0 }, { "VideoOverlay_MapBuffer", 0 },
    { "VideoOverlay_UnmapBuffer", 0 },
};
static int ovl_scale_swi, ovl_window_swi, ovl_position_swi, ovl_redraw_swi;
static int ovl_module;          /* 0 unknown, 1 there, -1 missing */

/* Is VideoOverlay loaded? Looks the SWIs up by name (once it's found). */
static int ovl_available(void)
{
    static const char *extra[] = { "VideoOverlay_SetScale", "VideoOverlay_SetWindow",
                                   "VideoOverlay_SetPosition", "VideoOverlay_RedrawWindow" };
    int *extra_no[] = { &ovl_scale_swi, &ovl_window_swi, &ovl_position_swi, &ovl_redraw_swi };
    _kernel_swi_regs r;
    int i;
    if (ovl_module > 0)
        return 1;
    /* not cached when missing: the program may load it later */
    for (i = 0; i < OV_COUNT_; i++) {
        r.r[1] = (int) ovl_swi[i].name;
        if (_kernel_swi(OS_SWINumberFromString, &r, &r) != NULL)
            return 0;
        ovl_swi[i].no = r.r[0] & ~0x20000;
    }
    for (i = 0; i < 4; i++) {
        r.r[1] = (int) extra[i];
        if (_kernel_swi(OS_SWINumberFromString, &r, &r) != NULL)
            return 0;
        *extra_no[i] = r.r[0] & ~0x20000;
    }
    ovl_module = 1;
    return 1;
}

/* EGL$Overlay: "off", "no" or "0" turns overlays off for every program. */
static int ovl_allowed_by_user(void)
{
    const char *v = getenv("EGL$Overlay");
    if (!v)
        return 1;
    return !(strcmp(v, "off") == 0 || strcmp(v, "Off") == 0 || strcmp(v, "OFF") == 0 ||
             strcmp(v, "no") == 0 || strcmp(v, "No") == 0 || strcmp(v, "0") == 0);
}

static int ovl_task_handle(void)
{
    _kernel_swi_regs r;
    r.r[0] = 5;                                 /* Wimp_ReadSysInfo 5: current task */
    if (_kernel_swi(Wimp_ReadSysInfo, &r, &r) != NULL)
        return 0;
    return r.r[0];
}

static int ovl_vsyncs(void)
{
    return _kernel_osbyte(176, 0, 255) & 0xFF;  /* the vsync counter */
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
            ovl_call(ovl_swi[OV_DISPLAY].no, surf->ovl_id, -1);
        ovl_call(ovl_swi[OV_DESTROY].no, surf->ovl_id, 0);
    }
    surf->ovl_id = 0;
    surf->ovl_shown = 0;
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
        if (_kernel_swi(ovl_swi[OV_CREATE].no, &r, &r) != NULL)
            return 0;                   /* can't have one of this size or format */
        surf->ovl_id = r.r[0];
        surf->ovl_type = r.r[1] & 0xFF;
        for (b = 0; b < banks; b++) {
            if (ovl_call(ovl_swi[OV_MAP].no, surf->ovl_id, b) != NULL)
                break;
            ovl_call(ovl_swi[OV_UNMAP].no, surf->ovl_id, b);
        }
        if (b == banks)
            break;
        ovl_call(ovl_swi[OV_DESTROY].no, surf->ovl_id, 0);
        surf->ovl_id = 0;
    }
    if (!surf->ovl_id)
        return 0;
    if (ovl_call(ovl_window_swi, surf->ovl_id, surf->handle) != NULL) {
        ovl_call(ovl_swi[OV_DESTROY].no, surf->ovl_id, 0);
        surf->ovl_id = 0;
        return 0;
    }
    surf->ovl_banks = banks;
    surf->ovl_next = 0;
    surf->ovl_w = surf->w;
    surf->ovl_h = surf->h;
    surf->ovl_mode = ovl_mode_signature(s);
    surf->ovl_placed[0] = surf->ovl_placed[1] = 0x7FFFFFFF;
    surf->ovl_vsync = ovl_vsyncs() - 1;     /* "a vsync has happened since" */
    surf->ovl_state = OVL_ON;
    return 1;
}

/* Scale the overlay to the surface's pixels and put it at the top left of
   the window's visible area (work area coordinates), clipped to it. */
static int ovl_place(egl_surface *surf, const window_state *ws, const screen_info *s)
{
    _kernel_swi_regs r;
    if (surf->ovl_placed[0] == ws->scroll_x && surf->ovl_placed[1] == ws->scroll_y &&
        surf->ovl_placed[2] == surf->w && surf->ovl_placed[3] == surf->h)
        return 1;
    r.r[0] = surf->ovl_id;
    r.r[1] = surf->w;
    r.r[2] = surf->h;
    r.r[3] = (surf->w << 16) | surf->h;
    if (_kernel_swi(ovl_scale_swi, &r, &r) != NULL)
        return 0;
    r.r[0] = surf->ovl_id;
    r.r[1] = ws->scroll_x;
    r.r[2] = ws->scroll_y;
    r.r[3] = ws->scroll_x;
    r.r[4] = ws->scroll_y - (surf->h << s->yeig);
    r.r[5] = ws->scroll_x + (surf->w << s->xeig);
    r.r[6] = ws->scroll_y;
    if (_kernel_swi(ovl_position_swi, &r, &r) != NULL)
        return 0;
    surf->ovl_placed[0] = ws->scroll_x;
    surf->ovl_placed[1] = ws->scroll_y;
    surf->ovl_placed[2] = surf->w;
    surf->ovl_placed[3] = surf->h;
    return 1;
}

/* Is the window closed, or is any window or menu above it in the stack
   over its visible area? Each window's "behind" word is the handle of the
   window just in front of it (-1 at the front). */
static int ovl_covered(const window_state *ws)
{
    window_state w;
    int h, n;
    if (!(ws->flags & (1 << 16)))
        return 1;                               /* not open (closed, iconised) */
    for (h = ws->behind, n = 0; h != -1 && n < 256; n++) {
        if (!get_window_state(h, &w))
            return 1;                           /* can't tell: be safe */
        if ((w.flags & (1 << 16)) && w.x0 < ws->x1 && w.x1 > ws->x0 &&
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
        !surf->ovl_want || !surf->sprite || surf->destroy_pending)
        return 0;
    /* work area surfaces in the same window would be under the overlay */
    for (o = d->surfaces; o; o = o->next)
        if (o != surf && o->kind == SURF_WINDOW && o->handle == surf->handle && !o->destroy_pending)
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
    block[1] = ws->scroll_x;
    block[2] = ws->scroll_y - (surf->h << s->yeig);
    block[3] = ws->scroll_x + (surf->w << s->xeig);
    block[4] = ws->scroll_y;
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
    r.r[0] = surf->handle;
    r.r[1] = ws->scroll_x;
    r.r[2] = ws->scroll_y - (surf->h << s->yeig);
    r.r[3] = ws->scroll_x + (surf->w << s->xeig);
    r.r[4] = ws->scroll_y;
    _kernel_swi(Wimp_ForceRedraw, &r, &r);
}

/* Show or hide the overlay to suit what covers the window. new_frame: a
   frame to copy in and show (eglSwapBuffers); otherwise just re-show the
   last one. Returns 1 if the overlay shows the surface, 0 if the caller
   has to plot the sprite. */
static int ovl_update(egl_display *d, egl_surface *surf, const screen_info *s, int new_frame)
{
    window_state ws;
    int b, y, covered;
    _kernel_oserror *e;

    if (new_frame) {
        _kernel_swi_regs r;
        int now = _kernel_swi(OS_ReadMonotonicTime, &r, &r) == NULL ? r.r[0] : 0;
        surf->ovl_run = surf->ovl_run > 0 && now - surf->ovl_swap_cs <= OVL_GAP_CS ? surf->ovl_run + 1 : 1;
        surf->ovl_swap_cs = now;
    }
    if (!get_window_state(surf->handle, &ws))
        return 0;
    if (!ovl_candidate(d, surf) || !ovl_allowed_by_user()) {
        if (surf->ovl_id) {
            int was_shown = surf->ovl_shown;
            ovl_destroy(surf);
            if (was_shown && !new_frame && surf->sprite)
                ovl_replot(d, surf, &ws, s);
        }
        return 0;
    }
    /* a new mode or size: start again (the old ID outlives a mode change) */
    if (surf->ovl_state != OVL_OFF &&
        (surf->ovl_mode != ovl_mode_signature(s) || surf->ovl_w != surf->w || surf->ovl_h != surf->h)) {
        int was_shown = surf->ovl_shown;
        ovl_destroy(surf);
        surf->ovl_state = OVL_OFF;
        if (was_shown && !new_frame)
            ovl_replot(d, surf, &ws, s);
    }
    if (surf->ovl_state == OVL_FAILED)
        return 0;
    covered = surf->ovl_type == 1 && ovl_covered(&ws);  /* Basic overlays only */
    if (covered) {
        if (surf->ovl_shown) {
            ovl_call(ovl_swi[OV_DISPLAY].no, surf->ovl_id, -1);
            surf->ovl_shown = 0;
            if (!new_frame)
                ovl_replot(d, surf, &ws, s);
        }
        return 0;
    }
    if (surf->ovl_state == OVL_OFF) {
        if (!new_frame || surf->ovl_run < OVL_WARMUP)
            return 0;                           /* not animating (yet) */
        if (!ovl_available() || !ovl_create(surf, s)) {
            if (new_frame && ovl_module > 0) {
                surf->ovl_state = OVL_FAILED;   /* VideoOverlay there, but it won't */
                surf->ovl_w = surf->w;          /* retry at a new size or mode */
                surf->ovl_h = surf->h;
                surf->ovl_mode = ovl_mode_signature(s);
            }
            return 0;
        }
        covered = surf->ovl_type == 1 && ovl_covered(&ws);
        if (covered)
            return 0;
    }
    if (!ovl_place(surf, &ws, s)) {
        ovl_fail(surf);
        return 0;
    }
    if (!new_frame) {
        if (!surf->ovl_shown && surf->ovl_last >= 0) {
            if (ovl_call(ovl_swi[OV_DISPLAY].no, surf->ovl_id, surf->ovl_last) != NULL) {
                ovl_fail(surf);
                ovl_replot(d, surf, &ws, s);
                return 0;
            }
            surf->ovl_shown = 1;
            ovl_just_shown(surf, &ws, s);
        }
        return surf->ovl_shown;
    }
    /* A buffer switch happens at the next vsync: don't write into a buffer
       until one has passed since the last switch. */
    if (surf->swap_interval > 0 && ovl_vsyncs() == surf->ovl_vsync)
        _kernel_osbyte(19, 0, 0);
    b = surf->ovl_next;
    {
        _kernel_swi_regs r;
        const int *planes;
        uint8_t *dst;
        int stride;
        r.r[0] = surf->ovl_id;
        r.r[1] = b;
        if ((e = _kernel_swi(ovl_swi[OV_MAP].no, &r, &r)) != NULL) {
            ovl_fail(surf);
            return 0;
        }
        planes = (const int *) r.r[0];
        dst = (uint8_t *) planes[0];
        stride = planes[1];
        for (y = 0; y < surf->h; y++)
            memcpy(dst + (size_t) y * stride, (const uint8_t *) surf->pixels + (size_t) y * surf->stride * 4,
                   (size_t) surf->w * 4);
        ovl_call(ovl_swi[OV_UNMAP].no, surf->ovl_id, b);
    }
    if (ovl_call(ovl_swi[OV_DISPLAY].no, surf->ovl_id, b) != NULL) {
        ovl_fail(surf);
        return 0;
    }
    surf->ovl_vsync = ovl_vsyncs();
    surf->ovl_last = b;
    surf->ovl_next = (b + 1) % surf->ovl_banks;
    if (!surf->ovl_shown) {
        surf->ovl_shown = 1;
        ovl_just_shown(surf, &ws, s);
    }
    return 1;
}

/* In a Wimp redraw loop: the overlay's own redraw for one rectangle. */
static void ovl_redraw_rectangle(egl_surface *surf, int *block)
{
    _kernel_swi_regs r;
    r.r[0] = surf->ovl_id;
    r.r[1] = (int) block;
    _kernel_swi(ovl_redraw_swi, &r, &r);
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
