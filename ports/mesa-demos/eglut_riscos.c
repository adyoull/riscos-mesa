/*
 * eglut_riscos.c - RISC OS back end for mesa-demos' eglut (the small GLUT
 * over EGL that the src/egl demos use). It replaces eglut_x11.c: build
 * eglut.c + this file + a demo, and the demo runs in a desktop window
 * through riscos-mesa's native EGL (the window handle is the native
 * window). Unchanged demos: eglgears, egltri, es1 gears/torus/tri/..,
 * es2gears, es2tri.
 *
 * Output: printing from a Wimp task opens a command window, so if the
 * variable EGLUT$Output is set (the !Run files set it), stdout and stderr
 * go to that file instead and the newest line (the demos' FPS reports)
 * is shown in the window's title bar.
 *
 * Keys: RISC OS key codes are turned into eglut's: characters go to the
 * keyboard callback (Escape quits, as in eglut), F1-F12 and the cursor
 * keys to the special callback.
 *
 * Part of riscos-mesa. MIT licence (see LICENCES.txt).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eglutint.h"
#include "riscos_wimpwin.h"

static rw_window rwin;
static char base_title[96];

static void finish(void)
{
    rw_close(&rwin);
    rw_end();
}

void _eglutNativeInitDisplay(void)
{
    rw_redirect_output("EGLUT$Output");
    if (!rw_init("eglut"))
        _eglutFatal("needs the desktop");
    atexit(finish);
    _eglut->native_dpy = EGL_DEFAULT_DISPLAY;
    _eglut->surface_type = EGL_WINDOW_BIT;
}

void _eglutNativeFiniDisplay(void)
{
    finish();
}

void _eglutNativeInitWindow(struct eglut_window *win, const char *title,
                            int x, int y, int w, int h)
{
    (void) x; (void) y;              /* eglut always asks for 0,0: centre it */
    snprintf(base_title, sizeof base_title, "%s", title);
    if (!rw_open(&rwin, title, -1, -1, w, h))
        _eglutFatal("failed to create a window");
    win->native.u.window = (EGLNativeWindowType) rwin.handle;
    win->native.width = w;
    win->native.height = h;
}

void _eglutNativeFiniWindow(struct eglut_window *win)
{
    (void) win;
    rw_close(&rwin);
}

static int special_key(int k)
{
    if (k >= RW_KEY_F1 && k <= RW_KEY_F1 + 8) return EGLUT_KEY_F1 + (k - RW_KEY_F1);
    switch (k) {
    case RW_KEY_F10:   return EGLUT_KEY_F10;
    case RW_KEY_F11:   return EGLUT_KEY_F11;
    case RW_KEY_F12:   return EGLUT_KEY_F12;
    case RW_KEY_LEFT:  return EGLUT_KEY_LEFT;
    case RW_KEY_RIGHT: return EGLUT_KEY_RIGHT;
    case RW_KEY_UP:    return EGLUT_KEY_UP;
    case RW_KEY_DOWN:  return EGLUT_KEY_DOWN;
    }
    return -1;
}

void _eglutNativeEventLoop(void)
{
    /* X11 draws the first frame on the first Expose event; here the Wimp's
       redraw requests are answered by EGL from the surface, so draw it
       once to begin with (programs without an idle callback need this). */
    _eglut->redisplay = 1;
    for (;;) {
        struct eglut_window *win = _eglut->current;
        int animate = _eglut->idle_cb != NULL || _eglut->redisplay;
        int key = 0, ev = rw_poll(&rwin, _eglut->dpy, animate, &key);

        switch (ev) {
        case RW_CLOSE:
            exit(0);                                  /* atexit closes the window */
        case RW_KEY:
            if (key < 0x100 && win->keyboard_cb) {
                win->keyboard_cb((unsigned char) key);
            } else {
                int s = special_key(key);
                if (s >= 0 && win->special_cb)
                    win->special_cb(s);
                else
                    rw_pass_key(key);                 /* hot keys: F12 etc. */
            }
            _eglut->redisplay = 1;
            break;
        case RW_OPEN:
            _eglut->redisplay = 1;                    /* maybe resized: draw at the new size */
            break;
        case RW_IDLE:
            if (_eglut->idle_cb)
                _eglut->idle_cb();                    /* usually posts a redisplay */
            else
                _eglut->redisplay = 1;
            break;
        default:
            break;
        }

        if (_eglut->redisplay && win) {
            EGLint w = 0, h = 0;
            _eglut->redisplay = 0;
            if (win->display_cb)
                win->display_cb();
            eglSwapBuffers(_eglut->dpy, win->surface);
            /* A window surface follows the visible area: the finished frame
               is shown, then the surface takes the new size for the next. */
            eglQuerySurface(_eglut->dpy, win->surface, EGL_WIDTH, &w);
            eglQuerySurface(_eglut->dpy, win->surface, EGL_HEIGHT, &h);
            if (w > 0 && h > 0 && (w != win->native.width || h != win->native.height)) {
                win->native.width = w;
                win->native.height = h;
                if (win->reshape_cb)
                    win->reshape_cb(w, h);
                _eglut->redisplay = 1;
            }
            rw_show_output(&rwin, base_title);
        }
    }
}
