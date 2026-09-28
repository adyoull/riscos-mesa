/*
 * Example 4: OpenGL through SDL2.
 *
 * SDL is a library that hides the operating system: the same program
 * builds on Windows, Linux, macOS and RISC OS. Thousands of games and
 * tools use it, so it's the easiest way to port one. On RISC OS, SDL does
 * the Wimp work of example 2 for you (task, window, poll loop, redraws)
 * inside SDL_PollEvent.
 *
 * What you learn here: an SDL program with a GL window you can resize,
 * that quits on Escape, the close icon or a desktop shutdown.
 *
 * When to use SDL: porting existing SDL code, or writing something that
 * should also build elsewhere. When not to: a RISC OS-only program that
 * wants menus, dialogue boxes or several windows is better off with the
 * Wimp directly (example 2) or a RISC OS toolkit, plus EGL.
 *
 * Build: make 4-sdl2   (see ../README.md)
 *
 * Part of the riscos-mesa devkit. MIT licence: copy it, change it, use it
 * as the start of your own program.
 */
#include <stdio.h>
#include <SDL.h>
#include <GL/gl.h>

int main(int argc, char **argv)
{
    SDL_Window *window;
    SDL_GLContext context;
    int running = 1, frame = 0, width = 480, height = 360;

    (void) argc; (void) argv;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    /* Say what kind of GL we want before making the window. */
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);   /* not needed for a flat
                                                     triangle, but 3D scenes
                                                     want one (see example 5) */

    window = SDL_CreateWindow("SDL2 + OpenGL", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    context = SDL_GL_CreateContext(window);   /* SDL does the EGL-like setup */

    while (running) {
        SDL_Event event;

        /* Handle everything that has happened since the last frame. On
         * RISC OS this is also where SDL calls Wimp_Poll, so other
         * programs get their turn: always call it once a frame. */
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                running = 0;              /* close icon or desktop shutdown */
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)
                running = 0;
            else if (event.type == SDL_WINDOWEVENT &&
                     event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                width = event.window.data1;
                height = event.window.data2;
            }
        }

        glViewport(0, 0, width, height);
        glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glLoadIdentity();
        glScalef((float) height / width, 1, 1);
        glRotatef(frame++ * 2.0f, 0, 0, 1);
        glBegin(GL_TRIANGLES);
        glColor3f(1, 0, 0); glVertex2f(-0.5f, -0.4f);
        glColor3f(0, 1, 0); glVertex2f( 0.5f, -0.4f);
        glColor3f(0, 0, 1); glVertex2f( 0.0f,  0.6f);
        glEnd();

        SDL_GL_SwapWindow(window);        /* show the frame */
        SDL_Delay(20);                    /* about 50 frames a second; leaves
                                             time for other programs */
    }

    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
