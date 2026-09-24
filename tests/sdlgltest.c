/*
 * sdlgltest - checks the SDL2 RISC OS OpenGL (OSMesa) path.
 * Opens an SDL_WINDOW_OPENGL window, spins a lit cube, prints FPS once a
 * second. Escape or closing the window quits. Space toggles vsync.
 *
 * Usage: sdlgltest [width height]
 */
#include <stdio.h>
#include <stdlib.h>
#include "SDL.h"
#include "SDL_opengl.h"

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
    int w = 640, h = 480, running = 1, frames = 0, vsync = 0;
    Uint32 last;
    float angle = 0;
    SDL_Window *win;
    SDL_GLContext ctx;
    SDL_Event ev;

    if (argc >= 3) { w = atoi(argv[1]); h = atoi(argv[2]); }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    win = SDL_CreateWindow("sdlgltest", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           w, h, SDL_WINDOW_OPENGL);
    if (!win) { printf("SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    ctx = SDL_GL_CreateContext(win);
    if (!ctx) { printf("SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }

    printf("GL_RENDERER : %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION  : %s\n", glGetString(GL_VERSION));
    printf("GLSL        : %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    SDL_GL_SetSwapInterval(vsync);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glLightfv(GL_LIGHT0, GL_POSITION, lpos);

    last = SDL_GetTicks();
    while (running) {
        int dw, dh;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_SPACE) {
                vsync = !vsync;
                SDL_GL_SetSwapInterval(vsync);
                printf("vsync %s\n", vsync ? "on" : "off");
            }
        }
        SDL_GL_GetDrawableSize(win, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glFrustum(-1.0 * dw / dh, 1.0 * dw / dh, -1, 1, 2, 20);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(0, 0, -6);
        glRotatef(angle, 1, 0, 0);
        glRotatef(angle * 0.7f, 0, 1, 0);
        angle += 2.0f;

        glClearColor(0.1f, 0.1f, 0.2f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        cube();
        SDL_GL_SwapWindow(win);

        frames++;
        if (SDL_GetTicks() - last >= 1000) {
            printf("%d fps\n", frames);
            frames = 0;
            last = SDL_GetTicks();
        }
    }

    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
