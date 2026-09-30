/* sdlblitbench: how fast SDL 2.26 draws 2D graphics in software, for
 * comparing builds (such as SDL with and without its ARM SIMD and NEON
 * blitters). No window: everything is drawn into a 1024x768 surface in
 * the RISC OS window surface's format (XBGR8888), as a 2D game's frame
 * would be before SDL_UpdateWindowSurface.
 *
 * Usage: sdlblitbench [seconds_per_scene] [-o file]
 *   seconds_per_scene: default 3
 *   -o file: also write the results to file
 *
 * Scenes (ms per frame; lower is faster):
 *   tiles        96x48 diamond map tiles covering the frame: opaque, with
 *                clear corners and a 2-pixel soft edge (Freeciv-style)
 *   units        300 round 64x64 sprites: opaque middle, soft edge
 *   glass        16 256x256 sprites that are see-through all over (alpha
 *                96 to 160), such as shadows, fog or menus
 *   fill         SDL_FillRect of the whole frame
 *   tiles-alpha  the tiles onto a frame with its own alpha (ABGR8888),
 *                which the ARM blitters leave to SDL's C code: it should
 *                be the same in both builds
 *   copy         opaque 96x48 tiles with no blending (a plain copy), also
 *                the same in both builds
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "hrtime.h"

#define W 1024
#define H 768

static FILE *out;
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    if (out) { va_start(ap, fmt); vfprintf(out, fmt, ap); va_end(ap); fflush(out); }
}

static Uint32 seed = 1;
static Uint32 rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

static Uint32 abgr(Uint32 r, Uint32 g, Uint32 b, Uint32 a)
{
    return a << 24 | b << 16 | g << 8 | r;
}

/* a diamond tile: alpha 255 inside, 0 outside, a soft edge 2 pixels wide */
static SDL_Surface *make_tile(void)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 96, 48, 32, SDL_PIXELFORMAT_ABGR8888);
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 96; x++) {
            /* distance inside the diamond's edge, in pixels */
            double dx = x + 0.5 - 48, dy = y + 0.5 - 24;
            double d = (1.0 - (dx < 0 ? -dx : dx) / 48 - (dy < 0 ? -dy : dy) / 24) * 21.5;
            Uint32 a = d <= 0 ? 0 : d >= 2 ? 255 : (Uint32)(d * 127.5);
            ((Uint32 *)((Uint8 *)s->pixels + y * s->pitch))[x] =
                abgr(60 + rnd() % 40, 110 + rnd() % 60, 40 + rnd() % 30, a);
        }
    return s;
}

/* a round sprite: opaque middle, soft edge 3 pixels wide, clear outside */
static SDL_Surface *make_unit(void)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_ABGR8888);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            double dx = x + 0.5 - 32, dy = y + 0.5 - 32, r2 = dx * dx + dy * dy;
            double d = 28 - SDL_sqrt(r2);
            Uint32 a = d <= 0 ? 0 : d >= 3 ? 255 : (Uint32)(d * 85);
            ((Uint32 *)((Uint8 *)s->pixels + y * s->pitch))[x] =
                abgr(200, 40 + x * 2, 30 + y * 2, a);
        }
    return s;
}

/* see-through all over */
static SDL_Surface *make_glass(void)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, 256, 256, 32, SDL_PIXELFORMAT_ABGR8888);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            ((Uint32 *)((Uint8 *)s->pixels + y * s->pitch))[x] =
                abgr(x, y, 128, 96 + (x + y) % 65);
    return s;
}

static void draw_tiles(SDL_Surface *tile, SDL_Surface *dst)
{
    for (int row = -1; row < H / 24 + 1; row++)
        for (int col = -1; col < W / 96 + 1; col++) {
            SDL_Rect r = { col * 96 + (row & 1) * 48, row * 24, 96, 48 };
            SDL_BlitSurface(tile, NULL, dst, &r);
        }
}

static SDL_Surface *tile, *unit, *glass, *opaque;
static SDL_Surface *frame, *frame_alpha;

static void scene_tiles(void) { draw_tiles(tile, frame); }
static void scene_units(void)
{
    seed = 7;
    for (int i = 0; i < 300; i++) {
        SDL_Rect r = { (int)(rnd() % (W - 64)), (int)(rnd() % (H - 64)), 64, 64 };
        SDL_BlitSurface(unit, NULL, frame, &r);
    }
}
static void scene_glass(void)
{
    for (int i = 0; i < 16; i++) {
        SDL_Rect r = { (i % 4) * 250, (i / 4) * 170, 256, 256 };
        SDL_BlitSurface(glass, NULL, frame, &r);
    }
}
static void scene_fill(void) { SDL_FillRect(frame, NULL, 0x00336699); }
static void scene_tiles_alpha(void) { draw_tiles(tile, frame_alpha); }
static void scene_copy(void) { draw_tiles(opaque, frame); }

static const struct { const char *name; void (*draw)(void); } scenes[] = {
    { "tiles", scene_tiles },
    { "units", scene_units },
    { "glass", scene_glass },
    { "fill", scene_fill },
    { "tiles-alpha", scene_tiles_alpha },
    { "copy", scene_copy },
};

int main(int argc, char **argv)
{
    double secs = 3;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out = fopen(argv[++i], "w");
            if (!out) { printf("can't write %s\n", argv[i]); return 1; }
        } else {
            secs = atof(argv[i]);
            if (secs <= 0) secs = 3;
        }
    }

    tile = make_tile();
    unit = make_unit();
    glass = make_glass();
    opaque = SDL_CreateRGBSurfaceWithFormat(0, 96, 48, 32, SDL_PIXELFORMAT_XBGR8888);
    SDL_FillRect(opaque, NULL, 0x00408020);
    frame = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_XBGR8888);
    frame_alpha = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!tile || !unit || !glass || !opaque || !frame || !frame_alpha) {
        printf("out of memory: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetSurfaceBlendMode(tile, SDL_BLENDMODE_BLEND);
    SDL_SetSurfaceBlendMode(unit, SDL_BLENDMODE_BLEND);
    SDL_SetSurfaceBlendMode(glass, SDL_BLENDMODE_BLEND);
    SDL_FillRect(frame, NULL, 0x00202020);
    SDL_FillRect(frame_alpha, NULL, 0x80202020);

    say("sdlblitbench: SDL %d.%d.%d, %dx%d XBGR8888 frame, %.0f s a scene\n",
        SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL, W, H, secs);
    say("CPU: NEON %s, ARM SIMD %s; timer: %s\n", SDL_HasNEON() ? "yes" : "no",
        SDL_HasARMSIMD() ? "yes" : "no", hr_source());
    say("%-12s %10s %8s\n", "scene", "ms/frame", "frames");
    for (unsigned s = 0; s < sizeof scenes / sizeof scenes[0]; s++) {
        scenes[s].draw();                     /* warm up (blit set up, caches) */
        int frames = 0;
        double t0 = hr_seconds(), t;
        do {
            scenes[s].draw();
            frames++;
            t = hr_seconds() - t0;
        } while (t < secs);
        say("%-12s %10.2f %8d\n", scenes[s].name, t * 1000 / frames, frames);
    }
    if (out) fclose(out);
    return 0;
}
