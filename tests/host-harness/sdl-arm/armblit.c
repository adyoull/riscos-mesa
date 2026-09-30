/* armblit.c: checks SDL 2.26's ARM SIMD and NEON blitters through SDL's
 * own API, on an emulated ARM CPU (qemu-arm), against SDL's C code.
 *
 * SDL is built for ARM Linux from the patched tree (patches/sdl2), with
 * --enable-arm-simd --enable-arm-neon as build/build-sdl2.sh does for
 * RISC OS. The blitters and SDL_blit_A.c's choice between them are the
 * same code on both; only the CPU check differs (RISC OS asks the OS,
 * Linux reads the auxiliary vector, which qemu fills in for the CPU it
 * emulates). See run.sh.
 *
 * What's checked, for each CPU:
 *  1. Per-pixel alpha, ABGR8888 onto XBGR8888 (a sprite onto the RISC OS
 *     window surface): the ARM routine is used when the CPU has one, and
 *     each colour is within 1 of SDL's C formula. The ARM routines leave
 *     the destination's unused top byte alone where the C code writes an
 *     alpha into it, which tells the two apart.
 *  2. Per-pixel alpha, ABGR8888 onto ABGR8888 (a destination with
 *     alpha): the C code is used whatever the CPU (the riscos-mesa guard
 *     in SDL_blit_A.c), so the result is exactly the C formula, alpha
 *     included.
 *  3. ARGB8888 onto RGB565 with alpha: within 1 of the exact blend.
 *  4. SDL_FillRect at 8, 16 and 32 bpp: exact.
 *  5. XBGR8888 to XRGB8888 and RGB444 to XRGB8888 conversions: exact.
 * Every check uses odd sizes and positions, and checks that nothing
 * outside the rectangle changed.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static Uint32 seed = 12345;
static Uint32 rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

/* a sprite pixel: mostly opaque or clear, some soft edges */
static Uint32 sprite_alpha(void)
{
    Uint32 r = rnd() % 10;
    if (r < 4) return 0;
    if (r < 7) return 255;
    return 1 + rnd() % 254;
}

static void fill_random(SDL_Surface *s)
{
    for (int y = 0; y < s->h; y++) {
        Uint8 *row = (Uint8 *)s->pixels + y * s->pitch;
        for (int x = 0; x < s->pitch; x++) row[x] = rnd();
    }
}

static Uint32 *px32(SDL_Surface *s, int x, int y)
{
    return (Uint32 *)((Uint8 *)s->pixels + y * s->pitch) + x;
}

/* a copy of a surface's pixels, before a blit */
typedef struct { Uint8 *pixels; int pitch; } Copy;
static Copy snapshot(SDL_Surface *s)
{
    Copy c = { malloc(s->h * s->pitch), s->pitch };
    memcpy(c.pixels, s->pixels, s->h * s->pitch);
    return c;
}
static Uint32 cpx32(Copy c, int x, int y) { return ((Uint32 *)(c.pixels + y * c.pitch))[x]; }
static Uint16 cpx16(Copy c, int x, int y) { return ((Uint16 *)(c.pixels + y * c.pitch))[x]; }

/* SDL's C formula (BlitRGBtoRGBPixelAlpha), for 8888 with alpha at the top */
static Uint32 c_blend(Uint32 s, Uint32 d)
{
    Uint32 alpha = s >> 24, dalpha, s1, d1;
    if (!alpha) return d;
    if (alpha == 255) return s;
    dalpha = d >> 24;
    s1 = s & 0xff00ff; d1 = d & 0xff00ff;
    d1 = (d1 + ((s1 - d1) * alpha >> 8)) & 0xff00ff;
    s &= 0xff00; d &= 0xff00;
    d = (d + ((s - d) * alpha >> 8)) & 0xff00;
    dalpha = alpha + (dalpha * (alpha ^ 0xFF) >> 8);
    return d1 | d | (dalpha << 24);
}

/* SDL's C formula for ARGB8888 onto RGB565 (BlitARGBto565PixelAlpha) */
static Uint16 c_blend565(Uint32 s, Uint16 dp)
{
    unsigned alpha = s >> 27;
    Uint32 d = dp;
    if (!alpha) return dp;
    if (alpha == 31) return (Uint16)((s >> 8 & 0xf800) + (s >> 5 & 0x7e0) + (s >> 3 & 0x1f));
    s = ((s & 0xfc00) << 11) + (s >> 8 & 0xf800) + (s >> 3 & 0x1f);
    d = (d | d << 16) & 0x07e0f81f;
    d += (s - d) * alpha >> 5;
    d &= 0x07e0f81f;
    return (Uint16)(d | d >> 16);
}

static int absdiff(int a, int b) { return a > b ? a - b : b - a; }
static double fdiff(double a, double b) { return a > b ? a - b : b - a; }

/* the exact blend of one colour, in the destination's units */
static double exact(int sv, int dv, int a, int m)
{
    return (sv * m / 255.0) * a / 255.0 + dv * (1 - a / 255.0);
}

/* 1 and 2 */
static void test_alpha8888(Uint32 dst_format, const char *name, int expect_arm)
{
    int maxdiff = 0, arm_used = 1, c_used = 1;
    double err = 0, err_c = 0;
    for (int iter = 0; iter < 40; iter++) {
        int sw = 1 + rnd() % 67, sh = 1 + rnd() % 9;
        SDL_Surface *src = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_ABGR8888);
        SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, 80, 12, 32, dst_format);
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++)
                *px32(src, x, y) = (rnd() & 0xffffff) | sprite_alpha() << 24;
        fill_random(dst);
        Copy before = snapshot(dst);
        SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
        SDL_Rect r = { (int)(rnd() % (80 - sw + 1)), (int)(rnd() % (12 - sh + 1)), sw, sh };
        SDL_BlitSurface(src, NULL, dst, &r);
        for (int y = 0; y < dst->h; y++)
            for (int x = 0; x < dst->w; x++) {
                Uint32 d0 = cpx32(before, x, y), d1 = *px32(dst, x, y);
                int in = x >= r.x && x < r.x + sw && y >= r.y && y < r.y + sh;
                if (!in) {
                    CHECK(d0 == d1, "%s: pixel %d,%d outside the blit changed", name, x, y);
                    continue;
                }
                Uint32 s = *px32(src, x - r.x, y - r.y), want = c_blend(s, d0);
                for (int sh8 = 0; sh8 < 24; sh8 += 8) {
                    int dd = absdiff(want >> sh8 & 255, d1 >> sh8 & 255);
                    if (dd > maxdiff) maxdiff = dd;
                    double e = exact(s >> sh8 & 255, d0 >> sh8 & 255, s >> 24, 255);
                    if (fdiff(d1 >> sh8 & 255, e) > err) err = fdiff(d1 >> sh8 & 255, e);
                    if (fdiff(want >> sh8 & 255, e) > err_c) err_c = fdiff(want >> sh8 & 255, e);
                }
                Uint32 a = s >> 24;
                if (a && a != 255) {
                    if ((d1 >> 24) != (d0 >> 24)) arm_used = 0;
                    if ((d1 >> 24) != (want >> 24)) c_used = 0;
                }
            }
        SDL_FreeSurface(src); SDL_FreeSurface(dst); free(before.pixels);
    }
    if (expect_arm) {
        CHECK(arm_used, "%s: expected the ARM routine, got the C code", name);
        CHECK(err <= 1.0, "%s: a colour %.2f away from the exact blend", name, err);
    } else {
        CHECK(c_used, "%s: expected the C code (alpha blended as C does)", name);
        CHECK(maxdiff == 0, "%s: a colour %d away from the C formula", name, maxdiff);
    }
    printf("  %-34s %-11s within %.2f of exact (C: %.2f), %d of C\n", name,
           arm_used && !c_used ? "ARM routine" : c_used ? "C code" : "?", err, err_c, maxdiff);
}

/* 3 */
static void test_565(int expect_arm)
{
    int maxdiff = 0, arm_differs = 0;
    double err = 0, err_c = 0;
    for (int iter = 0; iter < 40; iter++) {
        int sw = 1 + rnd() % 67, sh = 1 + rnd() % 9;
        SDL_Surface *src = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_ARGB8888);
        SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, 80, 12, 16, SDL_PIXELFORMAT_RGB565);
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++)
                *px32(src, x, y) = (rnd() & 0xffffff) | sprite_alpha() << 24;
        fill_random(dst);
        Copy before = snapshot(dst);
        SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_BLEND);
        SDL_Rect r = { (int)(rnd() % (80 - sw + 1)), (int)(rnd() % (12 - sh + 1)), sw, sh };
        SDL_BlitSurface(src, NULL, dst, &r);
        for (int y = 0; y < dst->h; y++)
            for (int x = 0; x < dst->w; x++) {
                Uint16 d0 = cpx16(before, x, y);
                Uint16 d1 = ((Uint16 *)((Uint8 *)dst->pixels + y * dst->pitch))[x];
                int in = x >= r.x && x < r.x + sw && y >= r.y && y < r.y + sh;
                if (!in) {
                    CHECK(d0 == d1, "565: pixel %d,%d outside the blit changed", x, y);
                    continue;
                }
                Uint32 s = *px32(src, x - r.x, y - r.y);
                Uint16 c = c_blend565(s, d0);
                if (c != d1) arm_differs = 1;
                int bits[3] = { 5, 6, 5 }, dsh[3] = { 11, 5, 0 }, ssh[3] = { 16, 8, 0 };
                for (int k = 0; k < 3; k++) {
                    int m = (1 << bits[k]) - 1;
                    double e = exact(s >> ssh[k] & 255, d0 >> dsh[k] & m, s >> 24, m);
                    int got = d1 >> dsh[k] & m, cv = c >> dsh[k] & m;
                    if (fdiff(got, e) > err) err = fdiff(got, e);
                    if (fdiff(cv, e) > err_c) err_c = fdiff(cv, e);
                    if (absdiff(got, cv) > maxdiff) maxdiff = absdiff(got, cv);
                }
            }
        SDL_FreeSurface(src); SDL_FreeSurface(dst); free(before.pixels);
    }
    if (expect_arm) CHECK(arm_differs, "565: expected the ARM routine, got the C code");
    CHECK(err <= err_c + 1e-9 || err <= 1.0, "565: a colour %.2f away from the exact blend (C: %.2f)", err, err_c);
    printf("  %-34s %-11s within %.2f of exact (C: %.2f), %d of C\n", "ARGB8888 onto RGB565",
           arm_differs ? "ARM routine" : "C code", err, err_c, maxdiff);
}

/* 4 */
static void test_fill(int bpp, Uint32 format)
{
    for (int iter = 0; iter < 40; iter++) {
        SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, 77, 11, bpp, format);
        fill_random(dst);
        Copy before = snapshot(dst);
        int w = 1 + rnd() % 70, h = 1 + rnd() % 10;
        SDL_Rect r = { (int)(rnd() % (77 - w + 1)), (int)(rnd() % (11 - h + 1)), w, h };
        Uint32 colour = rnd() & (bpp == 32 ? 0xffffffff : bpp == 16 ? 0xffff : 0xff);
        SDL_FillRect(dst, &r, colour);
        int bytes = bpp / 8, bad = 0;
        for (int y = 0; y < dst->h; y++)
            for (int x = 0; x < dst->w; x++) {
                Uint8 *p0 = before.pixels + y * before.pitch + x * bytes;
                Uint8 *p1 = (Uint8 *)dst->pixels + y * dst->pitch + x * bytes;
                int in = x >= r.x && x < r.x + w && y >= r.y && y < r.y + h;
                Uint32 v = 0;
                memcpy(&v, p1, bytes);
                if (in ? v != colour : memcmp(p0, p1, bytes) != 0) bad++;
            }
        CHECK(!bad, "FillRect %d bpp: %d wrong pixels (%dx%d at %d,%d)", bpp, bad, w, h, r.x, r.y);
        SDL_FreeSurface(dst); free(before.pixels);
    }
    printf("  FillRect %-2d bpp                     exact\n", bpp);
}

/* 5 */
static void test_convert(Uint32 sformat, int sbpp, const char *name)
{
    int bad = 0;
    for (int iter = 0; iter < 40; iter++) {
        int sw = 1 + rnd() % 67, sh = 1 + rnd() % 9;
        SDL_Surface *src = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, sbpp, sformat);
        SDL_Surface *dst = SDL_CreateRGBSurfaceWithFormat(0, 80, 12, 32, SDL_PIXELFORMAT_XRGB8888);
        fill_random(src);
        fill_random(dst);
        Copy before = snapshot(dst);
        SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
        SDL_Rect r = { (int)(rnd() % (80 - sw + 1)), (int)(rnd() % (12 - sh + 1)), sw, sh };
        SDL_BlitSurface(src, NULL, dst, &r);
        for (int y = 0; y < dst->h; y++)
            for (int x = 0; x < dst->w; x++) {
                Uint32 d0 = cpx32(before, x, y), d1 = *px32(dst, x, y);
                int in = x >= r.x && x < r.x + sw && y >= r.y && y < r.y + sh;
                if (!in) { if (d0 != d1) bad++; continue; }
                Uint32 want;
                if (sbpp == 32) {
                    Uint32 s = *px32(src, x - r.x, y - r.y);
                    want = (s & 0xff) << 16 | (s & 0xff00) | (s >> 16 & 0xff);
                } else {
                    Uint16 s = ((Uint16 *)((Uint8 *)src->pixels + (y - r.y) * src->pitch))[x - r.x];
                    want = (s >> 8 & 15) * 17 << 16 | (s >> 4 & 15) * 17 << 8 | (s & 15) * 17;
                }
                if ((d1 & 0xffffff) != want) bad++;
            }
        SDL_FreeSurface(src); SDL_FreeSurface(dst); free(before.pixels);
    }
    CHECK(!bad, "%s: %d wrong pixels", name, bad);
    printf("  %-34s exact\n", name);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int neon = SDL_HasNEON(), simd = SDL_HasARMSIMD();
    printf("CPU: NEON %s, ARM SIMD %s\n", neon ? "yes" : "no", simd ? "yes" : "no");
    test_alpha8888(SDL_PIXELFORMAT_XBGR8888, "ABGR8888 onto XBGR8888 (window)", neon || simd);
    test_alpha8888(SDL_PIXELFORMAT_ABGR8888, "ABGR8888 onto ABGR8888 (alpha)", 0);
    test_565(neon || simd);
    test_fill(8, SDL_PIXELFORMAT_INDEX8);
    test_fill(16, SDL_PIXELFORMAT_RGB565);
    test_fill(32, SDL_PIXELFORMAT_XBGR8888);
    test_convert(SDL_PIXELFORMAT_XBGR8888, 32, "XBGR8888 to XRGB8888");
    test_convert(SDL_PIXELFORMAT_RGB444, 16, "RGB444 to XRGB8888");
    printf("%d checks, %d failures\n", checks, fails);
    return fails != 0;
}
