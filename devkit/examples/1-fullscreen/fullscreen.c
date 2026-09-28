/*
 * Example 1: a spinning triangle over the whole screen.
 *
 * The smallest complete OpenGL program for RISC OS. It takes over the
 * screen, draws 300 frames (about five seconds on a 60 Hz monitor), then
 * gives the desktop back.
 *
 * What you learn here: the five EGL steps every program does, whatever it
 * draws. Think of a theatre: EGL is the stage crew and OpenGL the actor.
 * The crew find out what the theatre can do (the display and a "config"),
 * build a stage (a "surface": the picture you draw on) and give the actor
 * a dressing room to keep their things in (a "context": GL's colours,
 * matrices and textures). Then they put the actor on stage ("make
 * current"). After that you mostly talk to OpenGL, and call EGL once a
 * frame to raise the curtain on the finished picture (eglSwapBuffers).
 *
 * Build: make 1-fullscreen   (see ../README.md)
 *
 * Part of the riscos-mesa devkit. MIT licence: copy it, change it, use it
 * as the start of your own program.
 */
#include <stddef.h>               /* NULL */
#include <stdio.h>
#include <EGL/egl.h>
#include <EGL/eglext_riscos.h>   /* the RISC OS additions: EGL_RISCOS_SCREEN_WINDOW */
#include <GL/gl.h>
#include <kernel.h>
#include <swis.h>

/* Something went wrong: say what, and which EGL error it was. From the
 * desktop, RISC OS shows printed text in a window, so the user sees it. */
static int fail(const char *what)
{
    printf("%s failed (EGL error 0x%x)\n", what, eglGetError());
    return 1;
}

int main(void)
{
    /* What kind of drawing we want. EGL's default is OpenGL ES; this
     * example uses "desktop" OpenGL 2.1 (glBegin/glEnd and friends), so it
     * says so. A list of attributes always ends with EGL_NONE. */
    static const EGLint want[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_NONE
    };
    EGLDisplay display;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
    EGLint count, width, height;
    int frame;
    _kernel_swi_regs r;

    /* Step 1: the display. RISC OS has one screen, so this is always the
     * "default display". eglInitialize wakes the library up. */
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(display, NULL, NULL))
        return fail("eglInitialize");
    eglBindAPI(EGL_OPENGL_API);          /* desktop GL, not ES, from here on */

    /* Step 2: a config: a description of the pixels (colour, depth...).
     * We ask for one and take the first; the library lists the one that
     * matches the current screen mode first, so it's the fastest. */
    if (!eglChooseConfig(display, want, &config, 1, &count) || count < 1)
        return fail("eglChooseConfig");

    /* Step 3: the surface, where the picture goes. EGL_RISCOS_SCREEN_WINDOW
     * means "the whole screen" rather than a desktop window. */
    surface = eglCreateWindowSurface(display, config, EGL_RISCOS_SCREEN_WINDOW, NULL);
    if (surface == EGL_NO_SURFACE)
        return fail("eglCreateWindowSurface");

    /* Step 4: the context: GL's memory of colours, matrices, textures. */
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    if (context == EGL_NO_CONTEXT)
        return fail("eglCreateContext");

    /* Step 5: make it current: from now on every gl... call draws onto
     * this surface. */
    if (!eglMakeCurrent(display, surface, surface, context))
        return fail("eglMakeCurrent");

    /* Wait for the monitor's refresh before showing each frame (vsync).
     * Without it you can see the top of one frame and the bottom of the
     * next ("tearing"). 1 = every refresh. */
    eglSwapInterval(display, 1);

    /* The surface is as big as the screen mode. Tell GL to use all of it. */
    eglQuerySurface(display, surface, EGL_WIDTH, &width);
    eglQuerySurface(display, surface, EGL_HEIGHT, &height);
    glViewport(0, 0, width, height);

    /* Now draw. GL draws into a hidden picture; nothing appears on the
     * screen until eglSwapBuffers shows it all at once, so you never see a
     * half-drawn frame. */
    for (frame = 0; frame < 300; frame++) {
        glClearColor(0.1f, 0.1f, 0.3f, 1.0f);   /* dark blue background */
        glClear(GL_COLOR_BUFFER_BIT);

        glLoadIdentity();
        /* GL's picture runs from -1 to 1 across and up, whatever the
         * screen's shape. On a wide screen that stretches things sideways;
         * squash x by height/width to get the shape back. */
        glScalef((float) height / width, 1, 1);
        glRotatef(frame * 2.0f, 0, 0, 1);         /* turn 2 degrees a frame */
        glBegin(GL_TRIANGLES);                    /* one triangle, a colour per corner */
        glColor3f(1, 0, 0); glVertex2f(-0.5f, -0.4f);
        glColor3f(0, 1, 0); glVertex2f( 0.5f, -0.4f);
        glColor3f(0, 0, 1); glVertex2f( 0.0f,  0.6f);
        glEnd();

        eglSwapBuffers(display, surface);         /* wait for vsync, then show it */
    }

    /* Tidy up: take the actor off stage, then clear everything away. */
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);

    /* We drew straight over the desktop. Ask the Window Manager to repaint
     * the whole screen (window -1), so the user gets their desktop back. */
    r.r[0] = -1;
    r.r[1] = 0; r.r[2] = 0; r.r[3] = 16384; r.r[4] = 16384;   /* big enough for any mode */
    _kernel_swi(Wimp_ForceRedraw, &r, &r);
    return 0;
}
