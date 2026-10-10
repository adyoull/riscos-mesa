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
 *   - how to animate without slowing the rest of the machine down;
 *   - a menu (click Menu over the window) with two speed options for the
 *     Raspberry Pi: showing the window through the display's hardware
 *     overlay, and drawing at a smaller size that is stretched to fill
 *     the window. Both are one EGL call each.
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
#define EGL_EGLEXT_PROTOTYPES 1          /* declare the extension functions,
                                            such as eglRedrawWindowRISCOS */
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

#define SMALL_W 240                      /* "Draw at 240x180": half size */
#define SMALL_H 180

/* Wimp_Poll tells us what happened with a "reason code". These are the
 * ones this program cares about. */
enum { NULL_REASON = 0, REDRAW = 1, OPEN = 2, CLOSE = 3, MOUSE_CLICK = 6,
       MENU_SELECTION = 9, MESSAGE = 17, MESSAGE_RECORDED = 18 };

/* The window's title. The Wimp reads it from our memory ("indirected"),
 * so we can change it later to show what the program is doing. */
static char title[64] = "Spinning triangle";

/* ---- The menu -------------------------------------------------------
 *
 * A Wimp menu is a block of words: a header (title, colours, size), then
 * six words for each item. We build it once, and tick items by setting
 * bit 0 of their first word. */
enum { ITEM_OVERLAY, ITEM_SMALL, ITEMS };
static char item_text[ITEMS][24] = { "Hardware overlay", "Draw at 240x180" };
static int menu[7 + 6 * ITEMS];

static void make_menu(void)
{
    int i;
    memset(menu, 0, sizeof menu);
    strcpy((char *) menu, "Triangle");   /* title: up to 12 characters */
    ((char *) menu)[12] = 7;             /* title text black, */
    ((char *) menu)[13] = 2;             /* title bar grey,   */
    ((char *) menu)[14] = 7;             /* item text black,  */
    ((char *) menu)[15] = 0;             /* item background white */
    menu[4] = 18 * 16;                   /* width, OS units (18 characters) */
    menu[5] = 44;                        /* height of each item */
    menu[6] = 0;                         /* gap between items */
    for (i = 0; i < ITEMS; i++) {
        int *item = &menu[7 + 6 * i];
        item[0] = i == ITEMS - 1 ? 0x80 : 0;   /* bit 7: the last item */
        item[1] = -1;                          /* no submenu */
        item[2] = 0x07000121;                  /* text, filled, black,
                                                  indirected (0x100) */
        item[3] = (int) item_text[i];          /* the text, */
        item[4] = -1;                          /* no validation string, */
        item[5] = sizeof item_text[i];         /* and its buffer size */
    }
}

static void tick(int item, int on)
{
    if (on) menu[7 + 6 * item] |= 1;
    else    menu[7 + 6 * item] &= ~1;
}

/* Puts new text in the title bar. The Wimp only redraws the title when
 * asked: Wimp_ForceRedraw with "TASK" and 3 means "just the title bar". */
static void set_title(int window, const char *text)
{
    _kernel_swi_regs r;
    if (strcmp(title, text) == 0)
        return;                          /* no change: don't redraw */
    snprintf(title, sizeof title, "%s", text);
    r.r[0] = window;
    r.r[1] = 0x4B534154;                 /* "TASK" */
    r.r[2] = 3;
    _kernel_swi(Wimp_ForceRedraw, &r, &r);
}

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
 * resolution (EX0 EY0, "180 dpi") modes it's 1. We read which from the
 * mode. scale is the window scale EGL chose (see main): 2 on a high
 * resolution desktop, so the window is the size it would be in a normal
 * mode, each of our pixels shown as 2x2. */
static int open_window(int scale)
{
    int xeig = vdu_var(4), yeig = vdu_var(5);
    int w = (WIDTH * scale) << xeig, h = (HEIGHT * scale) << yeig;
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
    block[15] = 3 << 12;                   /* 15: tell us about mouse clicks
                                              (button type 3, "click") */
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
    static const int messages[] = { 0 };   /* no messages besides Message_Quit,
                                              which every task gets (0 ends the list) */
    EGLDisplay display;
    EGLConfig config;
    EGLSurface surface;
    EGLContext context;
    EGLint count, width, height;
    int task, window, frame = 0, running = 1, block[64], scale;
    int want_overlay = 0, small = 0;     /* the two menu options */
    int menu_x = 0, menu_y = 0;          /* where the menu was opened */
    EGLint shown;
    char text[64];
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

    /* 2. EGL, as in example 1, except that the surface is our window's
     * handle instead of the whole screen. */
    display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(display, NULL, NULL))
        return fail("eglInitialize");
    eglBindAPI(EGL_OPENGL_API);
    if (!eglChooseConfig(display, want, &config, 1, &count) || count < 1)
        return fail("eglChooseConfig");

    /* The window scale: 2 on a high resolution desktop if a window twice
     * the size fits on the screen, else 1 (the user can choose with
     * *Set EGL$WindowScale). We open the window that many times the size
     * and tell EGL the same scale: the surface stays WIDTH x HEIGHT. */
    scale = eglWindowScaleRISCOS(display, WIDTH, HEIGHT);
    window = open_window(scale);
    if (!window)
        return fail("Wimp_CreateWindow");

    /* Start with the hardware overlay off, since it's a menu option.
     * Saying so here (rather than leaving it out) also stops a user's
     * "*Set EGL$Overlay on" from turning it on behind the menu's back. */
    {
        EGLint surface_attrs[] = { EGL_OVERLAY_RISCOS, EGL_FALSE,
                                   EGL_WINDOW_SCALE_RISCOS, 0, EGL_NONE };
        surface_attrs[3] = scale;
        surface = eglCreateWindowSurface(display, config, window, surface_attrs);
    }
    if (surface == EGL_NO_SURFACE)
        return fail("eglCreateWindowSurface");
    context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    if (context == EGL_NO_CONTEXT)
        return fail("eglCreateContext");
    if (!eglMakeCurrent(display, surface, surface, context))
        return fail("eglMakeCurrent");
    make_menu();

    /* 3. The poll loop. Wimp_PollIdle is Wimp_Poll with an alarm clock:
     * "wake me with a null event at this time, or sooner if something
     * happens". Asking for a frame every 2 centiseconds (50 a second)
     * leaves the rest of the time to other programs. (In a window,
     * eglSwapBuffers doesn't wait for vsync: waiting would freeze every
     * other task, so the pacing is done here instead. Through a hardware
     * overlay it may wait, but never longer than one screen refresh.) */
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

            /* Show in the title how the frame reached the screen. Asking
             * for an overlay doesn't guarantee one: without the
             * VideoOverlay module, or while another window or menu is
             * over ours (our own menu included), EGL plots the frame as
             * usual, and the overlay only starts after a few frames in a
             * row. EGL_OVERLAY_RISCOS says what actually happened: 1
             * overlay, 2 overlay hidden, 0 plotted. */
            eglQuerySurface(display, surface, EGL_OVERLAY_RISCOS, &shown);
            snprintf(text, sizeof text, "Spinning triangle: %s%s",
                     shown == 1 ? "overlay" : shown == 2 ? "overlay hidden" : "plotted",
                     small ? ", 240x180" : "");
            set_title(window, text);
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

        case MOUSE_CLICK:
            /* block[2] says which button: 4 Select, 2 Menu, 1 Adjust.
             * On Menu, open our menu where the pointer is. */
            if (block[2] == 2) {
                menu_x = block[0] - 64;  /* the RISC OS custom: 64 OS */
                menu_y = block[1];       /* units left of the pointer */
                r.r[1] = (int) menu;
                r.r[2] = menu_x;
                r.r[3] = menu_y;
                _kernel_swi(Wimp_CreateMenu, &r, &r);
            }
            break;

        case MENU_SELECTION:
            /* block[0] is the item chosen (0 = the first). */
            if (block[0] == ITEM_OVERLAY) {
                /* Ask for a hardware overlay, or stop using one. On a
                 * Raspberry Pi with the VideoOverlay module loaded (see
                 * !Run), the display shows our frames itself instead of
                 * EGL plotting them: the plot's cost goes, and the picture
                 * doesn't tear. It's off unless a program asks, because
                 * the overlay sits on top of everything on the screen:
                 * EGL hides it whenever something overlaps our window. */
                want_overlay = !want_overlay;
                eglSurfaceAttrib(display, surface, EGL_OVERLAY_RISCOS,
                                 want_overlay ? EGL_TRUE : EGL_FALSE);
                tick(ITEM_OVERLAY, want_overlay);
            } else if (block[0] == ITEM_SMALL) {
                /* Draw at 240x180 whatever size the window is, and let EGL
                 * stretch each frame to fill it: a quarter of the pixels
                 * to draw. With an overlay the display does the
                 * stretching for free; without one the sprite plot does
                 * it. From the next frame, EGL_WIDTH and EGL_HEIGHT
                 * report 240x180, so draw() needs no change. (A program
                 * that uses the mouse must scale its position into the
                 * 240x180 picture itself: see the EGL guide.) Setting 0
                 * goes back to following the window's size. */
                small = !small;
                eglSurfaceAttrib(display, surface, EGL_RENDER_WIDTH_RISCOS, small ? SMALL_W : 0);
                if (small)
                    eglSurfaceAttrib(display, surface, EGL_RENDER_HEIGHT_RISCOS, SMALL_H);
                tick(ITEM_SMALL, small);
            }
            /* The menu closes after a choice, unless it was made with
             * Adjust: then the RISC OS convention is to reopen it. */
            r.r[1] = (int) block;
            _kernel_swi(Wimp_GetPointerInfo, &r, &r);
            if (block[2] & 1) {
                r.r[1] = (int) menu;
                r.r[2] = menu_x;
                r.r[3] = menu_y;
                _kernel_swi(Wimp_CreateMenu, &r, &r);
            }
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
