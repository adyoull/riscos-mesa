/*
 * egl/parts/buffers.c - the buffers GL renders into: sprites, screen banks, pbuffer memory, pixmaps.
 * Part of egl_riscos.c: it is compiled by being included from there, as
 * one unit with the other parts (so their helpers stay private to the
 * library), not on its own. MIT licence (see LICENSE).
 */

/* ------------------------------------------------------------------ */
/* Buffers                                                             */

static int banks_in_use;

static void restore_banks(void)
{
    _kernel_osbyte(113, 1, 0);          /* display and draw bank 1 again */
    _kernel_osbyte(112, 1, 0);
}

static void restore_banks_atexit(void)
{
    if (banks_in_use)
        restore_banks();
}

static void free_buffers(egl_surface *surf)
{
    if (surf->banks) {
        restore_banks();
        surf->banks = 0;
        banks_in_use--;
    }
    free(surf->area);
    surf->area = NULL;
    surf->sprite = NULL;
    free(surf->mem);
    surf->mem = NULL;
    surf->pixels = NULL;
    surf->direct = 0;
}

static void *vdu_bank_start(int bank)
{
    static const int vars[] = { 148, -1 };
    int val = 0;
    _kernel_swi_regs r;
    _kernel_osbyte(112, bank, 0);
    r.r[0] = (int) vars;
    r.r[1] = (int) &val;
    _kernel_swi(OS_ReadVduVariables, &r, &r);
    return (void *) val;
}

/* Hardware double (or triple) buffering: render into a screen bank that
   isn't being shown and switch the display to it on swap (OS_Byte 113),
   so a frame is never seen half drawn and nothing is copied. Grows screen
   memory if it can. Returns the number of banks (2 or 3), or 0. */
static int setup_banks(egl_surface *surf, const screen_info *s)
{
    static const int vars[] = { 7, -1 };    /* ScreenSize */
    static int atexit_done;
    _kernel_swi_regs r;
    int screen_size = 0, have, n, i;

    r.r[0] = (int) vars;
    r.r[1] = (int) &screen_size;
    if (_kernel_swi(OS_ReadVduVariables, &r, &r) != NULL || screen_size <= 0)
        return 0;
    r.r[0] = 2;                             /* screen memory dynamic area */
    if (_kernel_swi(OS_ReadDynamicArea, &r, &r) != NULL)
        return 0;
    have = r.r[1];
    for (n = surf->want_banks; n >= 2; n--) {
        if (have < n * screen_size) {
            r.r[0] = 2;
            r.r[1] = n * screen_size - have;
            _kernel_swi(OS_ChangeDynamicArea, &r, &r);
            r.r[0] = 2;
            if (_kernel_swi(OS_ReadDynamicArea, &r, &r) != NULL)
                return 0;
            have = r.r[1];
        }
        if (have >= n * screen_size)
            break;
    }
    if (n < 2)
        return 0;
    for (i = 1; i <= n; i++)
        surf->bank_addr[i] = vdu_bank_start(i);
    _kernel_osbyte(112, 1, 0);
    _kernel_osbyte(113, 1, 0);
    if (surf->bank_addr[1] != s->start || surf->bank_addr[2] == surf->bank_addr[1])
        return 0;
    if (!atexit_done) {
        atexit(restore_banks_atexit);   /* never leave the desktop on bank 2 */
        atexit_done = 1;
    }
    return n;
}

/* Make a window surface's buffer fit the window and the screen mode.
   Returns 1 if it changed, 0 if not, -1 on failure (EGL error set). */
static int update_window_buffer(egl_surface *surf, const screen_info *s)
{
    _kernel_swi_regs r;
    int w, h, size, mode, sprite_h;

    if (!wanted_size(surf, s, &w, &h))
        return fail(EGL_BAD_NATIVE_WINDOW), -1;

    if (surf->handle == -1 && !surf->rw && surf->render_buffer == EGL_BACK_BUFFER && !surf->no_banks &&
        surf->want_banks >= 2 &&
        screen_layout(s) == surf->cfg->layout && s->start != NULL &&
        (s->line_length & 3) == 0) {
        if (surf->banks && surf->bank_addr[1] == s->start && surf->w == w &&
            surf->h == h && surf->stride == s->line_length / 4)
            return 0;
        free_buffers(surf);
        surf->banks = setup_banks(surf, s);
        if (surf->banks) {
            banks_in_use++;
            surf->draw_bank = 2;
            surf->pixels = surf->bank_addr[2];
            surf->w = w;
            surf->h = h;
            surf->stride = s->line_length / 4;
            surf->swap_behavior = EGL_BUFFER_DESTROYED;
            surf->swaps = 0;
            return 1;
        }
        surf->no_banks = 1;             /* not enough screen memory: plot a sprite */
    }

    if (surf->handle == -1 && !surf->rw && surf->render_buffer == EGL_SINGLE_BUFFER &&
        screen_layout(s) == surf->cfg->layout && s->start != NULL &&
        (s->line_length & 3) == 0) {
        if (surf->direct && surf->pixels == s->start && surf->w == w &&
            surf->h == h && surf->stride == s->line_length / 4)
            return 0;
        free_buffers(surf);
        surf->direct = 1;
        surf->pixels = s->start;
        surf->w = w;
        surf->h = h;
        surf->stride = s->line_length / 4;
        surf->swaps = 0;
        return 1;
    }

    mode = sprite_mode_for(surf->cfg->layout, s);
    if (surf->sprite && surf->w == w && surf->h == h && surf->sprite_mode == mode &&
        surf->sprite_eig == (s->xeig << 4 | s->yeig))
        return 0;

    free_buffers(surf);
    sprite_h = h;
    if ((long) w * 4 * h < MIN_SPRITE_BYTES)
        sprite_h = (MIN_SPRITE_BYTES + w * 4 - 1) / (w * 4);
    size = 16 + 44 + w * 4 * sprite_h;
    surf->area = (int *) malloc(size);
    if (!surf->area)
        return fail(EGL_BAD_ALLOC), -1;
    surf->area[0] = size;
    surf->area[1] = 0;
    surf->area[2] = 16;
    surf->area[3] = 16;
    r.r[0] = 256 + 15;          /* create sprite */
    r.r[1] = (int) surf->area;
    r.r[2] = (int) "egl";
    r.r[3] = 0;                 /* no palette */
    r.r[4] = w;
    r.r[5] = sprite_h;
    r.r[6] = mode;
    if (_kernel_swi(OS_SpriteOp, &r, &r) != NULL) {
        free(surf->area);
        surf->area = NULL;
        return fail(EGL_BAD_ALLOC), -1;
    }
    surf->sprite = (int *) ((char *) surf->area + surf->area[2]);
    surf->pixels = (char *) surf->sprite + surf->sprite[8];
    surf->w = w;
    surf->h = h;
    surf->sprite_h = sprite_h;
    surf->stride = w;
    surf->sprite_mode = mode;
    surf->sprite_eig = s->xeig << 4 | s->yeig;
    surf->swaps = 0;
    return 1;
}

/* Check a native pixmap (a sprite header) and fill in its format. */
static int pixmap_info(void *pixmap, int *w, int *h, int *layout, void **pixels)
{
    const int *spr = (const int *) pixmap;
    int log2bpp, flags;

    if (!spr)
        return 0;
    if (!read_mode_variable(spr[10], 9, &log2bpp) || log2bpp != 5)
        return 0;
    if (!read_mode_variable(spr[10], 0, &flags))
        flags = 0;
    if (spr[6] != 0 || spr[7] != 31 || spr[4] < 0 || spr[5] < 0)
        return 0;
    *w = spr[4] + 1;
    *h = spr[5] + 1;
    *layout = (flags & MODEFLAG_TRGB) ? LAYOUT_TRGB : LAYOUT_TBGR;
    *pixels = (char *) pixmap + spr[8];
    return 1;
}

