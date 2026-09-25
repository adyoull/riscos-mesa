/*
 * riscos_wimpwin.h - the smallest Wimp task with one window, for porting
 * OpenGL and OpenGL ES examples that expect "open a window, run a loop,
 * get keys". The window handle is the native window for riscos-mesa's EGL
 * (eglCreateWindowSurface(dpy, cfg, w.handle, NULL)); EGL does all the
 * plotting, this code only runs the Wimp side.
 *
 * Part of riscos-mesa. MIT licence (see LICENCES.txt).
 */
#ifndef RISCOS_WIMPWIN_H
#define RISCOS_WIMPWIN_H

#include <EGL/egl.h>

/* RISC OS key codes (Wimp Key_Pressed), for the keys ports usually need.
   Shift adds 0x10 and Ctrl 0x20 to the function and cursor keys. */
#define RW_KEY_ESCAPE 0x1B
#define RW_KEY_F1     0x181   /* F1-F9 are 0x181-0x189 */
#define RW_KEY_F10    0x1CA
#define RW_KEY_F11    0x1CB
#define RW_KEY_F12    0x1CC
#define RW_KEY_LEFT   0x18C
#define RW_KEY_RIGHT  0x18D
#define RW_KEY_DOWN   0x18E
#define RW_KEY_UP     0x18F

typedef struct {
    int handle;             /* Wimp window handle: the EGL native window */
    int width, height;      /* size asked for, in pixels */
    char title[128];        /* indirected title text */
} rw_window;

/* What rw_poll returns. RW_OPEN: the window was opened, moved or resized
   (a program that only draws on demand should draw a frame: the surface
   takes a new size at the next eglSwapBuffers). */
enum { RW_NONE, RW_IDLE, RW_KEY, RW_CLOSE, RW_OPEN };

/* Start the Wimp task. Returns 0 outside the desktop. */
int rw_init(const char *task_name);

/* Create and open a window with a visible area of width x height pixels.
   x, y: top left in pixels from the screen's top left, or -1 to centre.
   The window has a title, close, back, toggle size and adjust size icons
   and no scroll bars: an EGL window surface follows its visible area. */
int rw_open(rw_window *w, const char *title, int x, int y, int width, int height);

/* Change the title bar text. */
void rw_set_title(rw_window *w, const char *title);

/* One Wimp_Poll. Redraw requests are passed to eglRedrawWindowRISCOS(dpy,
   ...) (dpy may be EGL_NO_DISPLAY before the surface exists); opening,
   clicks (to take the input focus) and unused keys are handled here.
     want_idle: 1 = return RW_IDLE when nothing else happens (animation);
                0 = sleep until an event arrives.
   Returns RW_IDLE, RW_KEY (*key = RISC OS key code), RW_CLOSE (close icon
   or the desktop quitting), RW_OPEN (see above), or RW_NONE (something
   handled here). */
int rw_poll(rw_window *w, EGLDisplay dpy, int want_idle, int *key);

/* Pass a key the program didn't use back to the Wimp (hot keys). rw_poll's
   caller does this for RW_KEY codes it ignores. */
void rw_pass_key(int key);

/* Close and delete the window. */
void rw_close(rw_window *w);

/* End the Wimp task. */
void rw_end(void);

/* Printing from a Wimp task opens a command window. If the variable var
   is set (a !Run file sets it, e.g. Set App$Output /|<App$Dir>/Output),
   send stdout and stderr to that file instead. Returns 1 if redirected. */
int rw_redirect_output(const char *var);

/* After rw_redirect_output: show the newest line written to the file since
   the last call in the title bar, as "base: line". Cheap when nothing new. */
void rw_show_output(rw_window *w, const char *base);

/* Monotonic time in milliseconds (centisecond resolution). */
unsigned rw_millis(void);

#endif
