/*
 * sdlfbtest - checks SDL's software framebuffer shown through riscos-mesa's
 * EGL (SDL_RISCOS_FB_RENDER_SIZE, SDL_RISCOS_FB_OVERLAY, new in 20.3.5-17),
 * the way a 2D game uses it: SDL_GetWindowSurface, then each frame only
 * the parts that changed go to SDL_UpdateWindowSurfaceRects.
 *
 * The picture is a still grid with small squares moving over it. Each frame
 * a square is drawn at its new place and the grid put back where it was, so
 * every update is a few small rectangles, far apart. Whatever goes wrong
 * shows:
 *   - coloured slivers left on the grid behind the squares: the stretched
 *     plot missed the edge of an update;
 *   - squares that flicker, jump back or leave ghosts (most likely just
 *     after a menu or another window has been over the picture): an
 *     overlay buffer wasn't brought up to date;
 *   - a grid that isn't even, or a picture that isn't the window's size:
 *     the stretch.
 * The title shows the frame rate, the time spent in the update call and
 * how the picture is shown (overlay, overlay hidden, plotted, or SDL's
 * ordinary framebuffer); the summary gives the share of frames each way.
 * A summary is written when it quits (printing while it runs would pop up
 * a command window over the desktop).
 *
 * Usage: sdlfbtest [width height] [-S WxH] [-V|-N] [-f] [-a] [-n squares]
 *                  [-r fps] [-t seconds] [-o file]
 *   width height  the window (default 1024x768)
 *   -S  the program's picture is WxH, stretched to the window
 *       (SDL_RISCOS_FB_RENDER_SIZE)
 *   -V  ask for a hardware overlay (SDL_RISCOS_FB_OVERLAY=1); -N refuse one.
 *       Without -S or -V: SDL's ordinary framebuffer, for comparison
 *   -f  start full screen (the desktop's size: a "full window")
 *   -a  update the whole picture every frame, to compare with the
 *       rectangles (key A switches)
 *   -n  how many squares (default 8)
 *   -r  frames a second (default: the screen's refresh rate, as a game
 *       would). -r 0 runs flat out, to see the most updates the path can
 *       take: the squares then move thousands of times a second, faster
 *       than the screen refreshes, and a plotted picture shows them
 *       sheared (each screen row catches them at a later place), which
 *       isn't a fault
 *   -t  quit by itself after this many seconds
 *   -o  write the summary to file as well
 * Keys: F full screen and back, A whole/rectangles, P pause (the squares
 * stop: they should be square), Escape quits.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL.h"
#include "hrtime.h"
#ifdef __riscos__
#define EGL_EGLEXT_PROTOTYPES 1
#include <EGL/egl.h>
#include <EGL/eglext_riscos.h>
#endif

#define MAX_SQUARES 64
#define SQUARE 16                       /* side, in the program's pixels */
#define GRID 32                         /* grid spacing */

typedef struct { int x, y, dx, dy; Uint32 colour; } square;

typedef struct {
    SDL_Surface *surf;
    Uint32 bg, line, edge;
    square sq[MAX_SQUARES];
    int n;
} scene;

/* ---- drawing ---- */

/* The still picture at x, y: a grid of 1-pixel lines on a dark background */
static Uint32 grid_pixel(const scene *sc, int x, int y)
{
    return (x % GRID == 0 || y % GRID == 0) ? sc->line : sc->bg;
}

static Uint32 *row(const scene *sc, int y)
{
    return (Uint32 *) ((Uint8 *) sc->surf->pixels + (size_t) y * sc->surf->pitch);
}

/* Put the grid back in r (clipped to the picture) */
static void draw_grid(const scene *sc, const SDL_Rect *r)
{
    int x, y;
    for (y = r->y; y < r->y + r->h; y++) {
        Uint32 *p = row(sc, y);
        for (x = r->x; x < r->x + r->w; x++)
            p[x] = grid_pixel(sc, x, y);
    }
}

/* A square: its colour with a white edge, so a sliver left behind stands out */
static void draw_square(const scene *sc, const square *q)
{
    int x, y;
    for (y = 0; y < SQUARE; y++) {
        Uint32 *p = row(sc, q->y + y) + q->x;
        for (x = 0; x < SQUARE; x++)
            p[x] = (x == 0 || y == 0 || x == SQUARE - 1 || y == SQUARE - 1) ? sc->edge : q->colour;
    }
}

static SDL_Rect square_rect(const square *q)
{
    SDL_Rect r;
    r.x = q->x; r.y = q->y; r.w = SQUARE; r.h = SQUARE;
    return r;
}

/* ---- the scene ---- */

static void scene_start(scene *sc, SDL_Surface *surf, int n)
{
    static const Uint8 rgb[8][3] = {
        {255, 64, 64}, {64, 255, 64}, {64, 96, 255}, {255, 255, 64},
        {255, 64, 255}, {64, 255, 255}, {255, 160, 32}, {160, 96, 255}
    };
    SDL_Rect all;
    int i;
    sc->surf = surf;
    sc->bg = SDL_MapRGB(surf->format, 24, 24, 40);
    sc->line = SDL_MapRGB(surf->format, 90, 90, 120);
    sc->edge = SDL_MapRGB(surf->format, 255, 255, 255);
    sc->n = n;
    for (i = 0; i < n; i++) {
        square *q = &sc->sq[i];
        q->x = (37 * i + 11) % (surf->w - SQUARE);
        q->y = (53 * i + 7) % (surf->h - SQUARE);
        q->dx = 1 + i % 3;
        q->dy = 1 + (i / 3) % 3;
        if (i & 1) q->dx = -q->dx;
        if (i & 2) q->dy = -q->dy;
        q->colour = SDL_MapRGB(surf->format, rgb[i % 8][0], rgb[i % 8][1], rgb[i % 8][2]);
    }
    all.x = 0; all.y = 0; all.w = surf->w; all.h = surf->h;
    draw_grid(sc, &all);
    for (i = 0; i < n; i++)
        draw_square(sc, &sc->sq[i]);
}

/* Move the squares one step. rects gets, for each square, the area its old
   and new places cover (where the grid was put back and where it now is). */
static int scene_step(scene *sc, SDL_Rect *rects)
{
    int i, w = sc->surf->w, h = sc->surf->h;
    for (i = 0; i < sc->n; i++) {
        square *q = &sc->sq[i];
        SDL_Rect old = square_rect(q), now;
        draw_grid(sc, &old);
        if (q->x + q->dx < 0 || q->x + q->dx > w - SQUARE) q->dx = -q->dx;
        if (q->y + q->dy < 0 || q->y + q->dy > h - SQUARE) q->dy = -q->dy;
        q->x += q->dx;
        q->y += q->dy;
        now = square_rect(q);
        SDL_UnionRect(&old, &now, &rects[i]);
    }
    for (i = 0; i < sc->n; i++)         /* squares over the grid, after all of it */
        draw_square(sc, &sc->sq[i]);
    return sc->n;
}

/* Is the picture in memory exactly the grid with the squares where they
   are now? (So anything wrong on the screen is in showing it.) Returns the
   number of wrong pixels. */
static long scene_check(const scene *sc)
{
    long bad = 0;
    int x, y, i;
    for (y = 0; y < sc->surf->h; y++) {
        const Uint32 *p = row(sc, y);
        for (x = 0; x < sc->surf->w; x++) {
            Uint32 want = grid_pixel(sc, x, y);
            for (i = sc->n - 1; i >= 0; i--) {      /* the last square drawn is on top */
                const square *q = &sc->sq[i];
                int sx = x - q->x, sy = y - q->y;
                if (sx >= 0 && sy >= 0 && sx < SQUARE && sy < SQUARE) {
                    want = (sx == 0 || sy == 0 || sx == SQUARE - 1 || sy == SQUARE - 1) ? sc->edge : q->colour;
                    break;
                }
            }
            if (p[x] != want)
                bad++;
        }
    }
    return bad;
}

/* ---- how the picture is shown ---- */

enum { SHOWN_PLOTTED, SHOWN_OVERLAY, SHOWN_HIDDEN, SHOWN_ORDINARY, SHOWN_WAYS };
static const char *const shown_name[SHOWN_WAYS] = {
    "plotted", "overlay", "overlay hidden", "ordinary framebuffer"
};

/* SDL publishes the EGL surface of a framebuffer shown through EGL as the
   window's data "SDL_RISCOS_FB_EGLSurface"; EGL_OVERLAY_RISCOS says how
   it's shown: 0 plotted, 1 overlay, 2 overlay hidden. */
static int shown_way(SDL_Window *win)
{
#ifdef __riscos__
    EGLSurface s = SDL_GetWindowData(win, "SDL_RISCOS_FB_EGLSurface");
    EGLint v = 0;
    if (s && eglQuerySurface(eglGetDisplay(EGL_DEFAULT_DISPLAY), s, EGL_OVERLAY_RISCOS, &v))
        return v == 1 ? SHOWN_OVERLAY : v == 2 ? SHOWN_HIDDEN : SHOWN_PLOTTED;
#else
    (void) win;
#endif
    return SHOWN_ORDINARY;
}

/* ---- running it ---- */

/* The screen's refresh rate, or 60 if SDL doesn't know it */
static int refresh_rate(SDL_Window *win)
{
    SDL_DisplayMode m;
    if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(win), &m) == 0 &&
        m.refresh_rate >= 24 && m.refresh_rate <= 240)
        return m.refresh_rate;
    return 60;
}

/* Wait until time t (hr_seconds). SDL_Delay lets other programs run but
   only keeps to about 10 ms on RISC OS, so it sleeps until just before t
   and the rest is a short busy wait. Returns 0 after a long sleep, so the
   caller can deal with events first and come back. */
static int wait_until(double t)
{
    double left = t - hr_seconds();
    if (left > 0.012) {
        SDL_Delay((Uint32) ((left - 0.011) * 1000.0));
        return 0;
    }
    while (hr_seconds() < t)
        ;
    return 1;
}

typedef struct {
    int frames;
    double update_s;                    /* time in SDL_UpdateWindowSurface(Rects) */
    double start;
    int shown[SHOWN_WAYS];              /* frames shown each way (asked after the update) */
    char egl_status[128];               /* EGL$OverlayStatus while the window was open */
} stats;

static void report(FILE *f, const char *how, int win_w, int win_h, int pic_w, int pic_h,
                   int whole, int fps_cap, const stats *st, double now, long bad)
{
    double t = now - st->start;
    int i;
    fprintf(f, "sdlfbtest: %s\n", how);
    fprintf(f, "  window %dx%d, picture %dx%d, %s updates\n", win_w, win_h, pic_w, pic_h,
            whole ? "whole-picture" : "rectangle");
    if (fps_cap > 0)
        fprintf(f, "  paced to %d frames a second\n", fps_cap);
    else
        fprintf(f, "  flat out (-r 0)\n");
    fprintf(f, "  %d frames in %.1f s: %.1f fps, %.3f ms a frame in the update call\n",
            st->frames, t, t > 0 ? st->frames / t : 0.0,
            st->frames ? 1000.0 * st->update_s / st->frames : 0.0);
    fprintf(f, "  shown:");
    for (i = 0; i < SHOWN_WAYS; i++)
        if (st->shown[i])
            fprintf(f, " %s %.0f%%", shown_name[i], st->frames ? 100.0 * st->shown[i] / st->frames : 0.0);
    fprintf(f, "\n");
    if (st->egl_status[0] && st->shown[SHOWN_ORDINARY] < st->frames)
        fprintf(f, "  EGL$OverlayStatus: %s\n", st->egl_status);
    fprintf(f, "  timer: %s\n", hr_source());
    if (bad)
        fprintf(f, "  FAIL: %ld pixels of the program's own picture are wrong (a fault in sdlfbtest)\n", bad);
    else
        fprintf(f, "  the program's own picture is right, so anything wrong on the screen is in showing it\n");
}

int main(int argc, char **argv)
{
    int w = 1024, h = 768, full = 0, whole = 0, n = 8, seconds = 0, fps_cap = -1, i;
    int paused = 0, way = SHOWN_ORDINARY;
    double next_frame = 0;
    const char *size = NULL, *ovl = NULL, *out = NULL;
    char how[128], title[160];
    SDL_Window *win;
    SDL_Surface *surf;
    SDL_Rect rects[MAX_SQUARES];
    scene sc;
    stats st, shown;
    double last_title;
    int running = 1;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-S") && i + 1 < argc) size = argv[++i];
        else if (!strcmp(argv[i], "-V")) ovl = "1";
        else if (!strcmp(argv[i], "-N")) ovl = "0";
        else if (!strcmp(argv[i], "-f")) full = 1;
        else if (!strcmp(argv[i], "-a")) whole = 1;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) fps_cap = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (i + 1 < argc && atoi(argv[i]) > 0) { w = atoi(argv[i]); h = atoi(argv[++i]); }
    }
    if (n < 1) n = 1;
    if (n > MAX_SQUARES) n = MAX_SQUARES;
    if (size) SDL_SetHint("SDL_RISCOS_FB_RENDER_SIZE", size);
    if (ovl) SDL_SetHint("SDL_RISCOS_FB_OVERLAY", ovl);
    snprintf(how, sizeof how, "%s%s%s%s",
             size ? "render size " : "", size ? size : "",
             ovl ? (*ovl == '1' ? " overlay asked for" : " overlay refused") : "",
             !size && !ovl ? "SDL's ordinary framebuffer" : "");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    win = SDL_CreateWindow("sdlfbtest", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           w, h, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!win || !(surf = SDL_GetWindowSurface(win)) || surf->format->BytesPerPixel != 4 ||
        surf->w < SQUARE * 2 || surf->h < SQUARE * 2) {
        fprintf(stderr, "no 32-bit window surface: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    if (fps_cap < 0)
        fps_cap = refresh_rate(win);
    scene_start(&sc, surf, n);
    SDL_UpdateWindowSurface(win);

    memset(&st, 0, sizeof st);
    st.start = hr_seconds();
    shown = st;
    last_title = st.start;
    while (running) {
        SDL_Event ev;
        double t0, now;
        int nr;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            else if (ev.type == SDL_KEYDOWN) {
                SDL_Keycode k = ev.key.keysym.sym;
                if (k == SDLK_ESCAPE) running = 0;
                else if (k == SDLK_a) whole = !whole;
                else if (k == SDLK_p) paused = !paused;
                else if (k == SDLK_f) {
                    full = !full;
                    SDL_SetWindowFullscreen(win, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                }
            } else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                /* a new surface (full screen and back, say): start the picture again */
                if ((surf = SDL_GetWindowSurface(win)) == NULL) running = 0;
                else { scene_start(&sc, surf, n); SDL_UpdateWindowSurface(win); }
            }
        }
        if (!running)
            break;
        if (paused) {
            SDL_SetWindowTitle(win, "sdlfbtest - paused (P goes on): the squares should be square");
            SDL_Delay(20);
            last_title = hr_seconds() - 1.0;        /* a fresh title when it goes on */
            continue;
        }
        if (fps_cap > 0) {                          /* fps_cap frames a second */
            if (!wait_until(next_frame))
                continue;                           /* events first */
            now = hr_seconds();
            /* the next one a frame on (from now if it fell well behind) */
            next_frame = (next_frame > 0 && now - next_frame < 0.1 ? next_frame : now) + 1.0 / fps_cap;
        }
        nr = scene_step(&sc, rects);
        t0 = hr_seconds();
        if (whole) SDL_UpdateWindowSurface(win);
        else SDL_UpdateWindowSurfaceRects(win, rects, nr);
        now = hr_seconds();
        st.update_s += now - t0;
        st.frames++;
        way = shown_way(win);
        st.shown[way]++;
        if (now - last_title >= 1.0) {
            double t = now - last_title;
            int f = st.frames - shown.frames;
            snprintf(title, sizeof title, "sdlfbtest %dx%d - %.1f fps, update %.2f ms - %s - %s",
                     surf->w, surf->h, f / t, f ? 1000.0 * (st.update_s - shown.update_s) / f : 0.0,
                     whole ? "whole" : "rectangles", shown_name[way]);
            SDL_SetWindowTitle(win, title);
            shown = st;
            last_title = now;
        }
        if (seconds > 0 && now - st.start >= seconds)
            running = 0;
    }
    {
        double now = hr_seconds();
        int pw = surf->w, ph = surf->h;     /* the surface goes with the window */
        long bad = scene_check(&sc);
        /* EGL (from 20.3.5-17i) says how the last surface that could have had
           an overlay was shown: read it while this one is still there */
        const char *why = getenv("EGL$OverlayStatus");
        snprintf(st.egl_status, sizeof st.egl_status, "%s", why ? why : "");
        SDL_DestroyWindow(win);
        SDL_Quit();
        report(stdout, how, w, h, pw, ph, whole, fps_cap, &st, now, bad);
        if (out) {
            FILE *f = fopen(out, "w");
            if (f) { report(f, how, w, h, pw, ph, whole, fps_cap, &st, now, bad); fclose(f); }
        }
    }
    return 0;
}
