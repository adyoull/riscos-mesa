/*
 * sdlgltest - checks the SDL2 RISC OS OpenGL (OSMesa) path.
 * Opens an SDL_WINDOW_OPENGL window (a desktop window when run from the
 * desktop), spins a lit cube and shows the frame rate in the window title
 * (a summary is printed after it quits: printing while running would pop up
 * a command window over the desktop). Keys: F toggles full screen, Space toggles vsync,
 * scroll wheel zooms the cube, Escape or closing the window quits.
 *
 * Timing: the title and the quit summary split each frame into "render"
 * (drawing + glFinish, i.e. Mesa) and "present" (SDL_GL_SwapWindow: the
 * sprite plot through the Wimp or to the screen), using hrtime.c.
 *
 * Usage: sdlgltest [width height] [-f] [-t seconds] [-r fps] [-w]
 *   -f  start full screen      -t  quit by itself after this many seconds
 *   -r  cap the frame rate with SDL_Delay (tests cooperative SDL_Delay)
 *   -w  event driven: wait in SDL_WaitEvent and only redraw when something
 *       happens (wheel, keys, clicks, moving the pointer over the window);
 *       tests that an idle SDL program uses no CPU
 */
#include <stdio.h>
#include <stdlib.h>
#include "SDL.h"
#include "SDL_opengl.h"
#include "hrtime.h"

#ifndef VARIANT
#define VARIANT "unnamed build"
#endif

static void cube(void)
{
    static const float n[6][3] = {{0,0,1},{0,0,-1},{0,1,0},{0,-1,0},{1,0,0},{-1,0,0}};
    static const float c[6][3] = {{1,.3f,.3f},{.3f,1,.3f},{.3f,.3f,1},{1,1,.3f},{1,.3f,1},{.3f,1,1}};
    static const int f[6][4][3] = {
        {{-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}},
        {{-1,-1,-1},{-1, 1,-1},{ 1, 1,-1},{ 1,-1,-1}},
        {{-1, 1,-1},{-1, 1, 1},{ 1, 1, 1},{ 1, 1,-1}},
        {{-1,-1,-1},{ 1,-1,-1},{ 1,-1, 1},{-1,-1, 1}},
        {{ 1,-1,-1},{ 1, 1,-1},{ 1, 1, 1},{ 1,-1, 1}},
        {{-1,-1,-1},{-1,-1, 1},{-1, 1, 1},{-1, 1,-1}}};
    int i, j;
    glBegin(GL_QUADS);
    for (i = 0; i < 6; i++) {
        glColor3fv(c[i]);
        glNormal3fv(n[i]);
        for (j = 0; j < 4; j++)
            glVertex3f((float)f[i][j][0], (float)f[i][j][1], (float)f[i][j][2]);
    }
    glEnd();
}

int main(int argc, char **argv)
{
    static const float lpos[4] = {2, 3, 4, 0};
    int w = 640, h = 480, running = 1, frames = 0, vsync = 0, full = 0, i;
    char title[80], glinfo[160];
    Uint32 start, total_frames = 0;
    int cap = 0, waitmode = 0;
    double elapsed, limit = 0, t_start, t_frame, t_drawn, render_s = 0, present_s = 0,
           render_tot = 0, present_tot = 0;
    Uint32 last;
    float angle = 0, dist = 6.0f;
    SDL_Window *win;
    SDL_GLContext ctx;
    SDL_Event ev;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] == 'f') full = 1;
        else if (argv[i][0] == '-' && argv[i][1] == 't' && i + 1 < argc) limit = atof(argv[++i]);
        else if (argv[i][0] == '-' && argv[i][1] == 'r' && i + 1 < argc) cap = atoi(argv[++i]);
        else if (argv[i][0] == '-' && argv[i][1] == 'w') waitmode = 1;
        else if (i + 1 < argc) { w = atoi(argv[i]); h = atoi(argv[i + 1]); i++; }
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    win = SDL_CreateWindow("sdlgltest", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           w, h, SDL_WINDOW_OPENGL | (full ? SDL_WINDOW_FULLSCREEN : 0));
    if (!win) { printf("SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    ctx = SDL_GL_CreateContext(win);
    if (!ctx) { printf("SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }

    SDL_snprintf(glinfo, sizeof glinfo, "%s / %s / GLSL %s", glGetString(GL_RENDERER),
                 glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
    SDL_GL_SetSwapInterval(vsync);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glLightfv(GL_LIGHT0, GL_POSITION, lpos);

    last = start = SDL_GetTicks();
    t_start = hr_seconds();
    while (running) {
        int dw, dh;
        t_frame = hr_seconds();
        if (limit > 0 && t_frame - t_start >= limit) running = 0;
        if (waitmode && running) {
            /* sleep until something happens; the frame after it redraws */
            if (SDL_WaitEvent(&ev)) SDL_PushEvent(&ev);
            angle += 4.0f;
        }
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (ev.type == SDL_MOUSEWHEEL) {
                dist -= ev.wheel.y * 0.5f;          /* wheel away = closer */
                if (dist < 3.0f) dist = 3.0f;
                if (dist > 18.0f) dist = 18.0f;
            }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_f) {
                full = !full;
                SDL_SetWindowFullscreen(win, full ? SDL_WINDOW_FULLSCREEN : 0);
            }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_SPACE) {
                vsync = !vsync;
                SDL_GL_SetSwapInterval(vsync);
            }
        }
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-1.0 * dw / dh, 1.0 * dw / dh, -1, 1, 2, 20);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0, 0, -dist);
        glRotatef(angle, 1, 0, 0);
        glRotatef(angle * 0.7f, 0, 1, 0);
        if (!waitmode) angle += 2.0f;

        glClearColor(0.1f, 0.1f, 0.2f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        cube();
        glFinish();
        t_drawn = hr_seconds();
        SDL_GL_SwapWindow(win);
        render_s += t_drawn - t_frame;
        present_s += hr_seconds() - t_drawn;
        if (cap > 0) {
            /* hold to the cap using SDL_Delay (cooperative on RISC OS) */
            double due = t_frame + 1.0 / cap, now = hr_seconds();
            if (due > now) SDL_Delay((Uint32)((due - now) * 1000));
        }

        frames++;
        total_frames++;
        if (SDL_GetTicks() - last >= 1000) {
            SDL_snprintf(title, sizeof title, "sdlgltest %dx%d - %d fps - render %.2f ms - present %.2f ms%s%s",
                         dw, dh, frames, frames ? render_s * 1000 / frames : 0.0,
                         frames ? present_s * 1000 / frames : 0.0, vsync ? " (vsync)" : "",
                         waitmode ? " (wait)" : cap ? " (capped)" : "");
            render_tot += render_s; present_tot += present_s;
            render_s = present_s = 0;
            SDL_SetWindowTitle(win, title);
            frames = 0;
            last = SDL_GetTicks();
        }
    }

    render_tot += render_s; present_tot += present_s;   /* the last part-second */
    elapsed = hr_seconds() - t_start;   /* before SDL_Quit, which resets SDL's tick count */
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    printf("sdlgltest: %s\n%s\n%dx%d %s, timer: %s\n"
           "%u frames in %.1f s = %.1f fps average\n"
           "per frame: render %.2f ms, present %.2f ms\n", VARIANT, glinfo, w, h,
           full ? "full screen (at quit)" : "window", hr_source(), (unsigned)total_frames,
           elapsed, elapsed > 0 ? total_frames / elapsed : 0.0,
           total_frames ? render_tot * 1000 / total_frames : 0.0,
           total_frames ? present_tot * 1000 / total_frames : 0.0);
    return 0;
}
