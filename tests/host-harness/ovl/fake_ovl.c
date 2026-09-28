/*
 * fake_ovl.c - a fake VideoOverlay module (and the few other SWIs ovltest
 * uses) on top of the EGL harness's fake RISC OS, for running ovltest on
 * a Linux host. It behaves like the ROOL wiki's description and checks
 * that the program keeps to it:
 *   - the mode selector has ModeFlags (0), NColour (3) and MinScreenBanks
 *     (13), never variable 12; YUV formats are FOURCCs with the YCbCr
 *     family in ModeFlags;
 *   - every ID used is live; buffer numbers are below the bank count;
 *   - UnmapBuffer only for mapped buffers; nothing mapped when Wimp_Poll
 *     returns (unless FAKE_OVL_ALLOW_MAPPED is set: T4 keeps them mapped);
 *   - every overlay is destroyed by the end (fake_ovl_report).
 * Like the Pi: sizes up to 2048x2048, RGB 32bpp (all four orders), RGB565
 * (not BGR565), YV12, YV16, NV12; plane strides are wider than a row, to
 * catch code that ignores them. A mode change (User_Message 0x400C1 in
 * the script) destroys every overlay, as the driver does.
 * FAKE_OVL_VET_BROKEN=1: every Vet fails, as on the Pi (VideoOverlay 0.02).
 * FAKE_OVL_GPU_BYTES=<n>: MapBuffer fails once the buffers would need more
 * (the Pi with Geminus loaded couldn't map a third 1920x1080 buffer).
 * FAKE_OVL_MODE_KEEPS=1: a mode change leaves the overlays alive (as the
 * Pi did), so the program has to destroy the old one itself.
 * FAKE_OVL_STALE_OK=1: a call with an unknown ID fails quietly (T7 tries
 * the old ID after a mode change on purpose).
 * FAKE_OVL_BARS=1: at each DisplayBuffer of a YUV overlay, the eight
 * colour bars T8 draws are converted back to RGB and checked.
 * Problems are printed as "FAKE-ERROR ..." on stderr; run.sh looks for
 * them.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel.h>
#include <swis.h>
#include "fake_riscos.h"

#define MAXOVL 8
#define BASE   0x59CC0

typedef struct {
    int id, w, h, banks, log2bpp, flags, ncolour, planes, cw, ch, type;
    uint8_t *mem[3];                  /* per bank */
    int stride[3], plane_h[3];
    int mapped[3], map_calls, displays, redraws, scales, positions, window;
    int words[3][6];                  /* MapBuffer's array, per bank */
} fovl_t;

static fovl_t ovls[MAXOVL];
static int next_id = 0x101, errors, polls_mapped;
int fake_ovl_creates, fake_ovl_destroys, fake_ovl_vets, fake_ovl_bars_checked;
int fake_ovl_displays, fake_ovl_redraws, fake_ovl_scales, fake_ovl_positions, fake_ovl_maps;

static _kernel_oserror err_block;
static _kernel_oserror *ferr(int num, const char *m)
{
    err_block.errnum = num;
    snprintf(err_block.errmess, sizeof err_block.errmess, "%s", m);
    return &err_block;
}

static void protocol(const char *m, int a)
{
    errors++;
    if (errors < 20) fprintf(stderr, "FAKE-ERROR %s (%d)\n", m, a);
}

static fovl_t *find(int id)
{
    int i;
    for (i = 0; i < MAXOVL; i++)
        if (ovls[i].id && ovls[i].id == id) return &ovls[i];
    return NULL;
}

#define FOURCC(a, b, c, d) ((a) | (b) << 8 | (c) << 16 | (d) << 24)

/* Parse and check a mode selector; fill in o's format. */
static _kernel_oserror *parse(const int *s, int banks_default, fovl_t *o)
{
    const int *p;
    int have0 = 0, have3 = 0, have13 = 0;
    if (s[0] != 1) { protocol("selector flags word isn't 1", s[0]); return ferr(1, "Bad selector"); }
    memset(o, 0, sizeof *o);
    o->w = s[1]; o->h = s[2]; o->log2bpp = s[3]; o->banks = banks_default; o->ncolour = -2;
    for (p = s + 5; *p != -1; p += 2) {
        switch (p[0]) {
        case 0: o->flags = p[1]; have0 = 1; break;
        case 3: o->ncolour = p[1]; have3 = 1; break;
        case 13: o->banks = p[1]; have13 = 1; break;
        case 12: protocol("mode variable 12 used for banks (it's 13)", p[1]); break;
        default: protocol("unexpected mode variable", p[0]); break;
        }
    }
    if (!have0 || !have3) protocol("ModeFlags or NColour missing from the selector", have0 | have3 << 1);
    if (!have13) protocol("MinScreenBanks (13) missing", 0);
    o->planes = 1; o->cw = o->ch = 1;
    if (o->log2bpp == 5) {
        if (o->ncolour != -1) protocol("32bpp NColour isn't -1", o->ncolour);
        if (o->flags & ~0xF000) return ferr(2, "Format not supported");
    } else if (o->log2bpp == 4) {
        if (o->ncolour != 65535 || o->flags != 0x4080) return ferr(2, "Format not supported");
    } else if (o->log2bpp == 7) {
        if ((o->flags & 0x3000) != 0x2000) protocol("YUV format without the YCbCr family in ModeFlags", o->flags);
        if (o->ncolour == FOURCC('Y','V','1','2')) { o->planes = 3; o->cw = 2; o->ch = 2; }
        else if (o->ncolour == FOURCC('Y','V','1','6')) { o->planes = 3; o->cw = 2; o->ch = 1; }
        else if (o->ncolour == FOURCC('N','V','1','2')) { o->planes = 2; o->cw = 2; o->ch = 2; }
        else return ferr(2, "Format not supported");
    } else return ferr(2, "Format not supported");
    if (o->w < 16 || o->h < 16 || o->w > 2048 || o->h > 2048) return ferr(3, "Overlay size not supported");
    if (o->banks < 1 || o->banks > 3) return ferr(4, "Too many buffers");
    return NULL;
}

static void limits(const fovl_t *o, _kernel_swi_regs *r)
{
    r->r[1] = o->type;
    r->r[2] = o->w / 8 < 16 ? 16 : o->w / 8;
    r->r[3] = o->h / 8 < 16 ? 16 : o->h / 8;
    r->r[4] = 2048; r->r[5] = 2048;
}

static void plane_geometry(fovl_t *o)
{
    int bpp = o->log2bpp == 5 ? 4 : o->log2bpp == 4 ? 2 : 1;
    o->stride[0] = o->w * bpp + 64;
    o->plane_h[0] = o->h;
    if (o->planes == 3) {
        o->stride[1] = o->stride[2] = o->w / o->cw + 32;
        o->plane_h[1] = o->plane_h[2] = o->h / o->ch;
    } else if (o->planes == 2) {
        o->stride[1] = o->w + 32;
        o->plane_h[1] = o->h / o->ch;
    }
}

static size_t bank_bytes(const fovl_t *o)
{
    size_t n = 0;
    int i;
    for (i = 0; i < o->planes; i++) n += (size_t) o->stride[i] * o->plane_h[i];
    return n;
}

static void destroy(fovl_t *o)
{
    int b;
    for (b = 0; b < 3; b++) free(o->mem[b]);
    memset(o, 0, sizeof *o);
    fake_ovl_destroys++;
}

/* T8's bars: the top two thirds, eight across; check their centres. */
static void check_bars(fovl_t *o, int b)
{
    static const int rgb[8][3] = { {255,255,255}, {255,255,0}, {0,255,255}, {0,255,0},
                                   {255,0,255}, {255,0,0}, {0,0,255}, {0,0,0} };
    int i, bad = 0, y = o->h * 2 / 3 - 4;     /* low in the bars: a wrong stride misses them */
    double kr = (o->flags & 0x8000) ? 0.2126 : 0.299, kb = (o->flags & 0x8000) ? 0.0722 : 0.114;
    for (i = 0; i < 8; i++) {
        int x = o->w * (2 * i + 1) / 16, Y, Cb, Cr;
        uint8_t *m = o->mem[b];
        double yy, cb, cr, R, G, B;
        Y = m[(size_t) y * o->stride[0] + x];
        m += (size_t) o->stride[0] * o->plane_h[0];
        if (o->planes == 3) {
            Cb = m[(size_t) (y / o->ch) * o->stride[1] + x / o->cw];
            m += (size_t) o->stride[1] * o->plane_h[1];
            Cr = m[(size_t) (y / o->ch) * o->stride[2] + x / o->cw];
        } else {
            Cb = m[(size_t) (y / o->ch) * o->stride[1] + (x / o->cw) * 2];
            Cr = m[(size_t) (y / o->ch) * o->stride[1] + (x / o->cw) * 2 + 1];
        }
        if (o->flags & 0x4000) { yy = (Y - 16) / 219.0; cb = (Cb - 128) / 224.0; cr = (Cr - 128) / 224.0; }
        else { yy = Y / 255.0; cb = (Cb - 128) / 255.0; cr = (Cr - 128) / 255.0; }
        R = yy + 2 * (1 - kr) * cr;
        B = yy + 2 * (1 - kb) * cb;
        G = (yy - kr * R - kb * B) / (1 - kr - kb);
        if (abs((int) (R * 255 + 0.5) - rgb[i][0]) > 6 || abs((int) (G * 255 + 0.5) - rgb[i][1]) > 6 ||
            abs((int) (B * 255 + 0.5) - rgb[i][2]) > 6) {
            fprintf(stderr, "FAKE-ERROR bar %d reads back as %.0f,%.0f,%.0f (Y %d Cb %d Cr %d, flags &%X)\n",
                    i, R * 255, G * 255, B * 255, Y, Cb, Cr, o->flags);
            bad++;
        }
    }
    {   /* the 1-pixel black/white checker just below the ramp: a luma
           row found through the wrong stride lands in the ramp instead */
        int yc = o->h * 3 / 4 + 2, xc = o->w / 4;
        const uint8_t *row = o->mem[b] + (size_t) yc * o->stride[0];
        if (abs(row[xc] - row[xc + 1]) < 150) {
            fprintf(stderr, "FAKE-ERROR the luma checker at row %d reads %d, %d\n", yc, row[xc], row[xc + 1]);
            bad++;
        }
    }
    if (bad) errors++;
    else { fake_ovl_bars_checked++; fprintf(stderr, "fake-ovl: bars read back correctly (flags &%X)\n", o->flags); }
}

static const char *const names[] = {
    "VideoOverlay_Create", "VideoOverlay_Destroy", "VideoOverlay_DisplayBuffer",
    "VideoOverlay_MapBuffer", "VideoOverlay_UnmapBuffer", "VideoOverlay_DiscardBuffer",
    "VideoOverlay_Vet", "VideoOverlay_SetScale", "VideoOverlay_SetWindow",
    "VideoOverlay_SetPosition", "VideoOverlay_RedrawWindow",
};

/* A module header whose help string ovltest reads. */
static struct { int words[5], help_offset; char help[40]; } module = {
    { 0, 0, 0, 0, 0 }, 24, "VideoOverlay\t0.12 (01 Jan 2026)"
};

static int hook(int no, _kernel_swi_regs *r, _kernel_oserror **e)
{
    fovl_t *o, tmp;
    int i;
    switch (no) {
    case OS_Write0: case OS_Plot: case Wimp_SetColour: case OS_CLI:
        return 1;
    case OS_SynchroniseCodeAreas:
        return 1;
    case OS_Module:
        if (r->r[0] != 18) return 0;
        if (getenv("FAKE_OVL_MISSING") || strcmp((const char *) (long) r->r[1], "VideoOverlay") != 0)
            *e = ferr(0x102, "Module 'VideoOverlay' not found");
        else r->r[3] = (int) (long) &module;
        return 1;
    case OS_SWINumberFromString:
        for (i = 0; i < 11; i++)
            if (strcmp((const char *) (long) r->r[1], names[i]) == 0) { r->r[0] = BASE + i; return 1; }
        *e = ferr(0x1E6, "SWI name not known");
        return 1;
    }
    if (no < BASE || no > BASE + 10) return 0;
    switch (no - BASE) {
    case 0: case 6:                              /* Create, Vet */
        if ((*e = parse((const int *) (long) r->r[0], 1, &tmp)) != NULL) return 1;
        if (r->r[3] != 0x4A00) protocol("Create/Vet without the task handle", r->r[3]);
        tmp.type = getenv("FAKE_OVL_TYPE") ? atoi(getenv("FAKE_OVL_TYPE")) : 1;
        limits(&tmp, r);
        if (no == BASE + 6) {
            fake_ovl_vets++;
            /* like VideoOverlay 0.02 + BCMVideo on the Pi: Vet always fails */
            if (getenv("FAKE_OVL_VET_BROKEN")) *e = ferr(0x820D03, "GraphicsV call failed");
            return 1;
        }
        for (i = 0; i < MAXOVL && ovls[i].id; i++) ;
        if (i == MAXOVL) { *e = ferr(5, "No free overlays"); return 1; }
        o = &ovls[i];
        *o = tmp;
        o->id = next_id++;
        plane_geometry(o);
        r->r[0] = o->id;
        fake_ovl_creates++;
        return 1;
    }
    if (!(o = find(r->r[0]))) {
        /* T7 uses the old ID after a mode change on purpose */
        if (!getenv("FAKE_OVL_STALE_OK")) protocol("VideoOverlay SWI with an unknown ID", r->r[0]);
        *e = ferr(6, "Bad overlay ID");
        return 1;
    }
    switch (no - BASE) {
    case 1: destroy(o); break;
    case 2:                                      /* DisplayBuffer */
        if (r->r[1] >= o->banks) { protocol("DisplayBuffer past the last bank", r->r[1]); *e = ferr(7, "Bad buffer"); break; }
        if (r->r[1] >= 0 && !o->mem[r->r[1]]) protocol("DisplayBuffer of a buffer never mapped", r->r[1]);
        o->displays++; fake_ovl_displays++;
        if (r->r[1] >= 0 && o->log2bpp == 7 && getenv("FAKE_OVL_BARS") && o->mem[r->r[1]])
            check_bars(o, r->r[1]);
        break;
    case 3: {                                    /* MapBuffer */
        int b = r->r[1], p;
        uint8_t *m;
        if (b < 0 || b >= o->banks) { *e = ferr(7, "Bad buffer"); break; }
        if (!o->mem[b]) {
            /* FAKE_OVL_GPU_BYTES: the GPU memory for buffers (with Geminus
               loaded, the Pi ran out at the third 1920x1080 buffer) */
            static size_t used;
            const char *lim = getenv("FAKE_OVL_GPU_BYTES");
            size_t i2, live = 0;
            for (i2 = 0; i2 < MAXOVL; i2++) {
                int b2;
                for (b2 = 0; b2 < 3; b2++) if (ovls[i2].id && ovls[i2].mem[b2]) live += bank_bytes(&ovls[i2]);
            }
            used = live;
            if (lim && used + bank_bytes(o) > (size_t) atol(lim)) { *e = ferr(0x820D03, "GraphicsV call failed"); break; }
            o->mem[b] = malloc(bank_bytes(o));
        }
        m = o->mem[b];
        for (p = 0; p < o->planes; p++) {
            o->words[b][2 * p] = (int) (long) m;
            o->words[b][2 * p + 1] = o->stride[p];
            m += (size_t) o->stride[p] * o->plane_h[p];
        }
        o->mapped[b]++;
        o->map_calls++; fake_ovl_maps++;
        r->r[0] = (int) (long) o->words[b];
        break;
    }
    case 4:                                      /* UnmapBuffer */
        if (r->r[1] < 0 || r->r[1] >= o->banks || o->mapped[r->r[1]] <= 0) {
            protocol("UnmapBuffer of a buffer that isn't mapped", r->r[1]);
            *e = ferr(8, "Buffer not mapped");
        } else o->mapped[r->r[1]]--;
        break;
    case 5:                                      /* DiscardBuffer */
        if (r->r[1] >= 0 && r->r[1] < o->banks) { free(o->mem[r->r[1]]); o->mem[r->r[1]] = NULL; o->mapped[r->r[1]] = 0; }
        break;
    case 7:                                      /* SetScale */
        o->scales++; fake_ovl_scales++;
        if (r->r[1] < 16) r->r[1] = 16;
        if (r->r[2] < 16) r->r[2] = 16;
        if (r->r[1] > 2048) r->r[1] = 2048;
        if (r->r[2] > 2048) r->r[2] = 2048;
        r->r[1] &= ~1;                           /* "hardware rounding" */
        break;
    case 8: o->window = r->r[1]; break;          /* SetWindow */
    case 9:                                      /* SetPosition */
        if (!o->window) protocol("SetPosition before SetWindow", 0);
        if (r->r[3] > r->r[5] || r->r[4] > r->r[6]) protocol("SetPosition clip rectangle upside down", 0);
        o->positions++; fake_ovl_positions++;
        break;
    case 10:                                     /* RedrawWindow */
        if (!o->window) protocol("RedrawWindow before SetWindow", 0);
        o->redraws++; fake_ovl_redraws++;
        break;
    }
    return 1;
}

static int allow_mapped;
static void (*chained)(int reason);

static void wimp_hook(int reason)
{
    int i, b;
    if (!allow_mapped)
        for (i = 0; i < MAXOVL; i++)
            for (b = 0; b < 3; b++)
                if (ovls[i].id && ovls[i].mapped[b] > 0) {
                    if (!polls_mapped++) protocol("Wimp_Poll returned while a buffer was mapped", b);
                }
    if (reason == 17 && getenv("FAKE_OVL_MODE_KEEPS")) {
        /* as VideoOverlay 0.02 on the Pi: old IDs still work afterwards */
        fprintf(stderr, "fake-ovl: mode change, overlays kept\n");
    } else if (reason == 17) {                   /* the script's mode change */
        for (i = 0; i < MAXOVL; i++)
            if (ovls[i].id) { destroy(&ovls[i]); fake_ovl_destroys--; }
        fprintf(stderr, "fake-ovl: mode change, every overlay destroyed\n");
    }
    if (chained) chained(reason);
}

void fake_ovl_init(void)
{
    fake_swi_hook = hook;
    allow_mapped = getenv("FAKE_OVL_ALLOW_MAPPED") != NULL;
    chained = fake_wimp_hook;
    fake_wimp_hook = wimp_hook;
}

void fake_ovl_report(void)
{
    int i, live = 0;
    for (i = 0; i < MAXOVL; i++)
        if (ovls[i].id) live++;
    if (live) protocol("overlays still alive at the end", live);
    fprintf(stderr, "fake-ovl: %d vets, %d creates, %d destroys, %d maps, %d displays, "
            "%d scales, %d positions, %d redraws, %d errors\n", fake_ovl_vets, fake_ovl_creates,
            fake_ovl_destroys, fake_ovl_maps, fake_ovl_displays, fake_ovl_scales,
            fake_ovl_positions, fake_ovl_redraws, errors);
}
