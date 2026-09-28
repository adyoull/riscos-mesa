/*
 * Example 2: a spinning triangle in a desktop window.
 *
 * This is how most RISC OS programs should use OpenGL: in a normal window
 * that you can move, resize and cover up, while every other program keeps
 * running.
 *
 * What you learn here:
 *   - how a program becomes a desktop "task" and opens a window;
 *   - the Wimp_Poll loop, the heart of every desktop program;
 *   - the two places EGL joins in: eglSwapBuffers after each frame, and
 *     eglRedrawWindowRISCOS when the desktop asks for a repaint;
 *   - how to animate without slowing the rest of the machine down.
 *
 * Why the loop matters: RISC OS multitasks "cooperatively". Only one
 * program runs at a time, and it keeps running until it calls Wimp_Poll,
 * which is the program saying "I'm done for now, let the others have a
 * go, and wake me when something happens". A program that never calls
 * Wimp_Poll freezes the whole desktop. So we draw one frame, then poll.
 *
 * Build: make 2-window   (see ../README.md)
 *
 * Part of the riscos-mesa devkit. MIT licence: copy it, change it, use it
 * as the start of your own program.
 */
#define EGL_EGLEXT_PROTOTYPES 1          /* declare eglRedrawWindowRISCOS */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext_riscos.h>
#include <GL/gl.h>
#include <kernel.h>
#include <swis.h>

#define WIDTH  480                       /* window size in pixels */
#define HEIGHT 360

/* Wimp_Poll tells us what happened with a "reason code". These are the
 * ones this program cares about. */
enum { NULL_REASON = 0, REDRAW = 1, OPEN = 2, CLOSE = 3, MESSAGE = 17, MESSAGE_RECORDED = 18 };

static char title[] = "Spinning triangle";

/* Something went wrong: say what (RISC OS shows printed text in a window). */
static int fail(const char *what)
{
    printf("%s failed (EGL error 0x%x)\n", what, eglGetError());
    return 1;
}

/* Reads a screen-mode value, e.g. how many OS units make a pixel. */
static int vdu_var(int var)
{
    int in[2] = { var, -1 }, out[1];
    _kernel_swi_regs r;
    r.r[0] = (int) in;
    r.r[1] = (int) out;
    _kernel_swi(OS_ReadVduVariables, &r, &r);
    return out[0];
}

/* Creates the window and puts it on the screen. The desktop measures in
 * "OS units", not pixels: on most modes a pixel is 2 OS units, on high
 * resolution (EX0 EY0) modes it's 1. We read which from the mode. */
static int open_window(void)
{
    int xeig = vdu_var(4), yeig = vdu_var(5);
    int w = WIDTH << xeig, h = HEIGHT << yeig;
    int x0 = 200, y0 = 300;              /* bottom left corner, OS units */
    int block[23], open[8];
    _kernel_swi_regs r;

    /* The window definition for Wimp_CreateWindow: 23 words, each with a
     * fixed meaning (the Programmer's Reference Manual lists them all). You
     * can copy this block as it is; the comments say which bits you might
     * want to change. */
    memset(block, 0, sizeof block);
    block[0] = x0;     block[1] = y0;      /* 0-3: where the window is on */
    block[2] = x0 + w; block[3] = y0 + h;  /* screen (left, bottom, right, top) */
    /* 4, 5: scroll offsets (0, 0) */
    block[6] = -1;                         /* 6: open it on top of the others */
    block[7] = (int) 0xAF000002u;          /* 7: flags: can be moved; has back,
                                              close, title bar, toggle-size and
                                              resize icons */
    block[8] = 7 | (2 << 8) | (7 << 16) | (4 << 24);
                                           /* 8: colours: title text black, title
                                              bar grey, work area text black and
                                              background mid grey */
    block[9] = 3 | (1 << 8) | (12 << 16);  /* 9: colours: scroll bars, and the
                                              title bar when the window has the
                                              input focus (cream) */
    block[10] = 0;    block[11] = -1024;   /* 10-13: the "work area": how big the */
    block[12] = 2048; block[13] = 0;       /* window's contents are. Big enough to
                                              let the user make it larger */
    block[14] = 0x07000119;                /* 14: the title is text that we keep
                                              in our own memory ("indirected") */
    /* 15: what clicks in the window do (0: nothing special) */
    block[16] = 1;                         /* 16: sprites come from the Wimp's pool */
    /* 17: minimum size (0: the Wimp decides) */
    block[18] = (int) title;               /* 18-20: the title text, no */
    block[19] = -1;                        /* validation string, and the */
    block[20] = sizeof title;              /* text's buffer size */
    /* 21: no icons in the window */
    r.r[1] = (int) block;
    if (_kernel_swi(Wimp_CreateWindow, &r, &r) != NULL)
        return 0;
    open[0] = r.r[0];                              /* the window's handle */
    memcpy(&open[1], block, 7 * sizeof(int));
    r.r[1] = (int) open;
    _kernel_swi(Wimp_OpenWindow, &r, &r);
    return open[0];
}

static void draw(int frame, int width, int height)
{
    glViewport(0, 0, width, height);               /* the window may have been resized */
    glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glLoadIdentity();
    glScalef((float) height / width, 1, 1);        /* keep the triangle's shape */
    glRotatef(frame * 2.0f, 0, 0, 1);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex2f(-0.5f, -0.4f);
    glColor3f(0, 1, 0); glVertex2f( 0.5f, -0.4f);
    glColor3f(0, 0, 1); glVertex2f( 0.0f,  0.6f);
    glEnd();
}

int main(void)
{
    static const EGLint want[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_NONE
    };
    static const int messages[] = { 0 };   /* we only want Message_Quit (number 0) */
    EGLDisplay display;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
    EGLint count, width, height;
    int task, window, frame = 0, running = 1, block[64];
    unsigned next_frame;
    _kernel_swi_regs r;
    _kernel_oserror *error;

    /* 1. Become a desktop task. "TASK" (0x4B534154) proves we know the
     * rules; 380 means "I understand RISC OS 3.8 behaviour and later". */
    r.r[0] = 380;
    r.r[1] = 0x4B534154;
    r.r[2] = (int) "Spinning triangle";
    r.r[3] = (int) messages;
    error = _kernel_swi(Wimp_Initialise, &r, &r);
    if (error != NULL) {
        /* Usually "Window Manager is currently in use": we were started
         * from a TaskWindow. Say so (in a TaskWindow, printing is fine). */
        printf("%s\n(Start this program by double-clicking its application.)\n",
               error->errmess);
        return 1;
    }
    task = r.r[1];

    window = open_window();
    if (!window)
        return fail("Wimp_CreateWindow");

    /* 2. EGL, as in example 1, except that the surface is our window's
     * handle instead of the whole screen. */
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(display, NULL, NULL))
        return fail("eglInitialize");
    eglBindAPI(EGL_OPENGL_API);
    if (!eglChooseConfig(display, want, &config, 1, &count) || count < 1)
        return fail("eglChooseConfig");
    surface = eglCreateWindowSurface(display, config, window, NULL);
    if (surface == EGL_NO_SURFACE)
        return fail("eglCreateWindowSurface");
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    if (context == EGL_NO_CONTEXT)
        return fail("eglCreateContext");
    if (!eglMakeCurrent(display, surface, surface, context))
        return fail("eglMakeCurrent");

    /* 3. The poll loop. Wimp_PollIdle is Wimp_Poll with an alarm clock:
     * "wake me with a null event at this time, or sooner if something
     * happens". Asking for a frame every 2 centiseconds (50 a second)
     * leaves the rest of the time to other programs. (In a window,
     * eglSwapBuffers never waits for vsync: waiting would freeze every
     * other task, so the pacing is done here instead.) */
    _kernel_swi(OS_ReadMonotonicTime, &r, &r);
    next_frame = r.r[0];
    while (running) {
        r.r[0] = 0;                    /* 0 = do send us null events */
        r.r[1] = (int) block;
        r.r[2] = next_frame;
        _kernel_swi(Wimp_PollIdle, &r, &r);

        switch (r.r[0]) {
        case NULL_REASON:
            /* Nothing else to do: time for a frame. The surface follows
             * the window's size, so ask how big it is each time. */
            eglQuerySurface(display, surface, EGL_WIDTH, &width);
            eglQuerySurface(display, surface, EGL_HEIGHT, &height);
            draw(frame++, width, height);
            eglSwapBuffers(display, surface);    /* puts the frame in the window */
            next_frame += 2;
            _kernel_swi(OS_ReadMonotonicTime, &r, &r);
            if ((int) (next_frame - r.r[0]) < 0)
                next_frame = r.r[0];             /* we fell behind: don't rush */
            break;

        case REDRAW:
            /* Part of the window needs repainting, e.g. another window
             * was dragged across it. The desktop doesn't remember what was
             * there, so we must paint it again. EGL keeps the last frame
             * and does the whole job for us. */
            eglRedrawWindowRISCOS(display, block);
            break;

        case OPEN:
            /* The user moved, resized or brought the window to the front.
             * The desktop asks first; we say yes by opening it there. */
            r.r[1] = (int) block;
            _kernel_swi(Wimp_OpenWindow, &r, &r);
            break;

        case CLOSE:
            running = 0;               /* the close icon was clicked */
            break;

        case MESSAGE:
        case MESSAGE_RECORDED:
            if (block[4] == 0)         /* Message_Quit: the desktop is shutting */
                running = 0;           /* down, or the Task Manager said Quit */
            break;
        }
    }

    /* 4. Tidy up, then leave the desktop politely. */
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    block[0] = window;
    r.r[1] = (int) block;
    _kernel_swi(Wimp_DeleteWindow, &r, &r);
    r.r[0] = task;
    r.r[1] = 0x4B534154;
    _kernel_swi(Wimp_CloseDown, &r, &r);
    return 0;
}
