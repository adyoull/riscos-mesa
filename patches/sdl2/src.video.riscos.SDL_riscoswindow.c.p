diff --git src/video/riscos/SDL_riscoswindow.c src/video/riscos/SDL_riscoswindow.c
index f47d33a..ddb709d 100644
--- src/video/riscos/SDL_riscoswindow.c
+++ src/video/riscos/SDL_riscoswindow.c
@@ -30,10 +30,221 @@
 
 #include "SDL_riscosvideo.h"
 #include "SDL_riscoswindow.h"
+#include "SDL_riscosopengl.h"
+
+/* 2026: windowed mode.  A window created without SDL_WINDOW_FULLSCREEN is
+   shown in a Wimp window on the desktop (the program becomes a Wimp task);
+   full screen windows keep the original behaviour of owning the screen.  */
+
+#include <kernel.h>
+#include <swis.h>
+
+static char riscos_window_title[128] = "SDL";
+static int riscos_wimp_messages[] = { 0 };
+
+int
+RISCOS_WimpReadEig(int var)
+{
+    _kernel_swi_regs regs;
+    regs.r[0] = -1;
+    regs.r[1] = var;
+    if (_kernel_swi(OS_ReadModeVariable, &regs, &regs) != NULL)
+        return 1;
+    return regs.r[2];
+}
+
+void
+RISCOS_UpdateEigs(_THIS)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    vdata->xeig = RISCOS_WimpReadEig(4);
+    vdata->yeig = RISCOS_WimpReadEig(5);
+}
+
+static int
+RISCOS_WimpScreenSize(int var)
+{
+    _kernel_swi_regs regs;
+    regs.r[0] = -1;
+    regs.r[1] = var;               /* 11 = XWindLimit, 12 = YWindLimit */
+    if (_kernel_swi(OS_ReadModeVariable, &regs, &regs) != NULL)
+        return 640;
+    return regs.r[2] + 1;
+}
+
+/* 2026: high resolution desktops. In a mode with 1 OS unit per pixel
+   (EX0 EY0, "180dpi") a window the size the program asked for would look
+   half size, so each SDL pixel is shown as 2x2 screen pixels, as a normal
+   90dpi (EX1 EY1) mode would show it. SDL$WindowScale overrides this
+   (1 = never scale, 2-4 = always scale by that much). Automatic scaling is
+   skipped if the scaled window wouldn't fit on the screen. */
+void
+RISCOS_ChooseWindowScale(_THIS, SDL_Window *window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    int xeig = RISCOS_WimpReadEig(4), yeig = RISCOS_WimpReadEig(5);
+    const char *env = SDL_getenv("SDL$WindowScale");
+    int sx, sy;
+
+    if (env && *env >= '1' && *env <= '4' && env[1] == 0) {
+        sx = sy = *env - '0';
+    } else {
+        sx = (xeig < 1) ? 2 : 1;
+        sy = (yeig < 1) ? 2 : 1;
+        if ((sx > 1 || sy > 1) &&
+            (window->w * sx > RISCOS_WimpScreenSize(11) ||
+             window->h * sy > RISCOS_WimpScreenSize(12) - (80 >> yeig))) {
+            sx = sy = 1;
+        }
+    }
+    vdata->wscale_x = sx;
+    vdata->wscale_y = sy;
+}
+
+int
+RISCOS_WimpStart(_THIS)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    _kernel_swi_regs regs;
+    _kernel_oserror *err;
+
+    if (vdata->wimp_task != 0)
+        return 0;
+
+    regs.r[0] = 380;
+    regs.r[1] = 0x4B534154; /* "TASK" */
+    regs.r[2] = (int)riscos_window_title;
+    regs.r[3] = (int)riscos_wimp_messages;
+    err = _kernel_swi(Wimp_Initialise, &regs, &regs);
+    if (err != NULL)
+        return SDL_SetError("Wimp_Initialise failed: %s", err->errmess);
+    vdata->wimp_task = regs.r[1];
+
+    /* Put an icon on the icon bar if the application told us which sprite
+       to use (e.g. "Set SDL$IconSprite !myapp" in !Run, after IconSprites). */
+    vdata->iconbar_icon = -1;
+    {
+        const char *sprite = SDL_getenv("SDL$IconSprite");
+        if (sprite && *sprite) {
+            int block[9];
+            SDL_memset(block, 0, sizeof(block));
+            block[0] = -1;                 /* right hand side of the icon bar */
+            block[1] = 0; block[2] = 0; block[3] = 68; block[4] = 68;
+            block[5] = 0x0000301A;         /* sprite, centred, button type click */
+            SDL_strlcpy((char *)&block[6], sprite, 12);
+            regs.r[0] = 0;
+            regs.r[1] = (int)block;
+            if (_kernel_swi(Wimp_CreateIcon, &regs, &regs) == NULL)
+                vdata->iconbar_icon = regs.r[0];
+        }
+    }
+    return 0;
+}
+
+static void
+RISCOS_WimpOpenAt(int handle, int minx, int maxy, int w_os, int h_os)
+{
+    int block[8];
+    _kernel_swi_regs regs;
+
+    block[0] = handle;
+    block[1] = minx;
+    block[2] = maxy - h_os;
+    block[3] = minx + w_os;
+    block[4] = maxy;
+    block[5] = 0;
+    block[6] = 0;
+    block[7] = -1;             /* open on top */
+    regs.r[1] = (int)block;
+    _kernel_swi(Wimp_OpenWindow, &regs, &regs);
+}
+
+static int
+RISCOS_WimpCreateWindow(_THIS, SDL_Window * window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    int xeig = RISCOS_WimpReadEig(4), yeig = RISCOS_WimpReadEig(5);
+    int w_os, h_os;
+    int scr_w = RISCOS_WimpScreenSize(11) << xeig, scr_h = RISCOS_WimpScreenSize(12) << yeig;
+    int block[23];
+    unsigned char *b = (unsigned char *)block;
+    _kernel_swi_regs regs;
+    _kernel_oserror *err;
+    int minx, maxy;
+
+    if (RISCOS_WimpStart(_this) < 0)
+        return -1;
+    RISCOS_UpdateEigs(_this);
+    RISCOS_ChooseWindowScale(_this, window);
+    w_os = (window->w * vdata->wscale_x) << xeig;
+    h_os = (window->h * vdata->wscale_y) << yeig;
+
+    if (window->title)
+        SDL_strlcpy(riscos_window_title, window->title, sizeof(riscos_window_title));
+
+    SDL_memset(block, 0, sizeof(block));
+    block[0] = 0; block[1] = 0; block[2] = w_os; block[3] = h_os;  /* visible area (set when opened) */
+    block[4] = 0; block[5] = 0;                                    /* scroll offsets */
+    block[6] = -1;                                                 /* behind */
+    block[7] = 0x80000002 | 0x01000000 | 0x02000000 | 0x04000000;  /* new format, moveable, back, close, title */
+    b[32] = 7;    /* title foreground */
+    b[33] = 2;    /* title background */
+    b[34] = 7;    /* work area foreground */
+    b[35] = 255;  /* work area background: transparent, we draw it all */
+    b[36] = 3;    /* scroll bar outer */
+    b[37] = 1;    /* scroll bar inner */
+    b[38] = 12;   /* title background when focused */
+    b[39] = 0;
+    block[10] = 0; block[11] = -h_os; block[12] = w_os; block[13] = 0;  /* work area extent */
+    block[14] = 0x0700013D;        /* title bar icon flags: indirected text, centred, filled */
+    block[15] = 3 << 12;           /* work area button type: click */
+    block[16] = 1;                 /* sprite area: Wimp */
+    block[17] = 0;                 /* minimum size */
+    block[18] = (int)riscos_window_title;
+    block[19] = -1;
+    block[20] = sizeof(riscos_window_title);
+    block[21] = 0;                 /* no icons */
+
+    regs.r[1] = (int)block;
+    err = _kernel_swi(Wimp_CreateWindow, &regs, &regs);
+    if (err != NULL)
+        return SDL_SetError("Wimp_CreateWindow failed: %s", err->errmess);
+
+    vdata->wimp_window = regs.r[0];
+    vdata->wimp_sdl_window = window;
+    vdata->pointer_in = SDL_FALSE;
+    vdata->has_caret = SDL_FALSE;
+
+    /* Redraw the whole desktop, in case we were running full screen before. */
+    regs.r[0] = -1;
+    regs.r[1] = 0;
+    regs.r[2] = 0;
+    regs.r[3] = scr_w;
+    regs.r[4] = scr_h;
+    _kernel_swi(Wimp_ForceRedraw, &regs, &regs);
+
+    minx = (scr_w - w_os) / 2;
+    if (minx < 0) minx = 0;
+    maxy = scr_h - (scr_h - h_os) / 2;
+    if (maxy > scr_h - 40) maxy = scr_h - 40;
+    RISCOS_WimpOpenAt(vdata->wimp_window, minx, maxy, w_os, h_os);
+
+    /* Take the input focus straight away. */
+    regs.r[0] = vdata->wimp_window;
+    regs.r[1] = -1;
+    regs.r[2] = 0;
+    regs.r[3] = 0;
+    regs.r[4] = 1 << 25;   /* invisible caret */
+    regs.r[5] = -1;
+    _kernel_swi(Wimp_SetCaretPosition, &regs, &regs);
+
+    return 0;
+}
 
 int
 RISCOS_CreateWindow(_THIS, SDL_Window * window)
 {
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
     SDL_WindowData *driverdata;
 
     driverdata = (SDL_WindowData *) SDL_calloc(1, sizeof(*driverdata));
@@ -42,27 +253,164 @@ RISCOS_CreateWindow(_THIS, SDL_Window * window)
     }
     driverdata->window = window;
 
-    window->flags |= SDL_WINDOW_FULLSCREEN;
-
-    SDL_SetMouseFocus(window);
+    if ((window->flags & SDL_WINDOW_FULLSCREEN) || vdata->wimp_window != 0) {
+        /* Full screen: we own the whole screen. We stay a Wimp task (if we
+           are one) but stop calling Wimp_Poll, so the desktop is suspended
+           until we return to a window or quit (fix 13). */
+        window->flags |= SDL_WINDOW_FULLSCREEN;
+        SDL_SetMouseFocus(window);
+    } else {
+        if (RISCOS_WimpCreateWindow(_this, window) < 0) {
+            SDL_free(driverdata);
+            return -1;
+        }
+    }
 
     /* All done! */
     window->driverdata = driverdata;
     return 0;
 }
 
+/* 2026: SDL only applies SDL_WINDOW_FULLSCREEN after the window has been
+   created (and after it has switched the screen mode), so this is where a
+   desktop window turns into a single tasking full screen one and back. */
+static void
+RISCOS_WimpDeleteWindow(_THIS)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    _kernel_swi_regs regs;
+    int block[1];
+
+    if (vdata->wimp_window == 0)
+        return;
+    block[0] = vdata->wimp_window;
+    regs.r[1] = (int)block;
+    _kernel_swi(Wimp_DeleteWindow, &regs, &regs);
+    vdata->wimp_window = 0;
+    vdata->wimp_sdl_window = NULL;
+    vdata->pointer_in = SDL_FALSE;
+    vdata->has_caret = SDL_FALSE;
+}
+
+void
+RISCOS_SetWindowFullscreen(_THIS, SDL_Window * window, SDL_VideoDisplay * display, SDL_bool fullscreen)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    (void)display;
+
+    if (fullscreen) {
+        if (vdata->wimp_sdl_window == window) {
+            RISCOS_WimpDeleteWindow(_this);
+        }
+        /* Stay a Wimp task but stop polling (PumpEvents only calls Wimp_Poll
+           while a desktop window exists): the desktop is suspended until we
+           go back to a window or quit. Never Wimp_CloseDown here (fix 13). */
+        RISCOS_UpdateEigs(_this);
+        SDL_SetMouseFocus(window);
+        RISCOS_ApplyPointerVisibility(_this);
+    } else if (!window->is_destroying && vdata->wimp_window == 0) {
+        /* Back to a desktop window of the windowed size. */
+        if (window->windowed.w > 0 && window->windowed.h > 0) {
+            window->w = window->windowed.w;
+            window->h = window->windowed.h;
+        }
+        RISCOS_UpdateEigs(_this);
+        RISCOS_WimpCreateWindow(_this, window);
+        RISCOS_ApplyPointerVisibility(_this);
+    }
+}
+
+void
+RISCOS_SetWindowSize(_THIS, SDL_Window * window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    int xeig, yeig, w_os, h_os, state[9], extent[4];
+    _kernel_swi_regs regs;
+
+    if (vdata->wimp_window == 0 || vdata->wimp_sdl_window != window)
+        return;
+
+    xeig = RISCOS_WimpReadEig(4);
+    yeig = RISCOS_WimpReadEig(5);
+    RISCOS_ChooseWindowScale(_this, window);
+    w_os = (window->w * vdata->wscale_x) << xeig;
+    h_os = (window->h * vdata->wscale_y) << yeig;
+
+    extent[0] = 0; extent[1] = -h_os; extent[2] = w_os; extent[3] = 0;
+    regs.r[0] = vdata->wimp_window;
+    regs.r[1] = (int)extent;
+    _kernel_swi(Wimp_SetExtent, &regs, &regs);
+
+    /* Keep the top left corner where it is. */
+    state[0] = vdata->wimp_window;
+    regs.r[1] = (int)state;
+    if (_kernel_swi(Wimp_GetWindowState, &regs, &regs) == NULL) {
+        RISCOS_WimpOpenAt(vdata->wimp_window, state[1], state[4], w_os, h_os);
+    }
+}
+
+void
+RISCOS_SetWindowTitle(_THIS, SDL_Window * window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    _kernel_swi_regs regs;
+
+    if (window->title)
+        SDL_strlcpy(riscos_window_title, window->title, sizeof(riscos_window_title));
+    if (vdata->wimp_window == 0 || vdata->wimp_sdl_window != window)
+        return;
+
+    /* Redraw the title bar (RISC OS 3.8+ form of Wimp_ForceRedraw). */
+    regs.r[0] = vdata->wimp_window;
+    regs.r[1] = 0x4B534154;
+    regs.r[2] = 3;
+    _kernel_swi(Wimp_ForceRedraw, &regs, &regs);
+}
+
 void
 RISCOS_DestroyWindow(_THIS, SDL_Window * window)
 {
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
     SDL_WindowData *driverdata = (SDL_WindowData *) window->driverdata;
 
+    if (vdata->wimp_window != 0 && vdata->wimp_sdl_window == window) {
+        _kernel_swi_regs regs;
+        int block[1];
+        block[0] = vdata->wimp_window;
+        regs.r[1] = (int)block;
+        _kernel_swi(Wimp_DeleteWindow, &regs, &regs);
+        vdata->wimp_window = 0;
+        vdata->wimp_sdl_window = NULL;
+        vdata->pointer_in = SDL_FALSE;
+        vdata->has_caret = SDL_FALSE;
+        /* Make sure the pointer is visible again on the desktop. */
+        _kernel_osbyte(106, 1, 0);
+    }
+
     if (!driverdata)
         return;
 
+#if SDL_VIDEO_OPENGL_OSMESA
+    RISCOS_GL_DestroyWindowBuffer(window);
+#endif
     SDL_free(driverdata);
     window->driverdata = NULL;
 }
 
+void
+RISCOS_WimpQuit(_THIS)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    if (vdata->wimp_task != 0) {
+        _kernel_swi_regs regs;
+        regs.r[0] = vdata->wimp_task;
+        regs.r[1] = 0x4B534154;
+        _kernel_swi(Wimp_CloseDown, &regs, &regs);
+        vdata->wimp_task = 0;
+        vdata->iconbar_icon = -1;
+    }
+}
+
 SDL_bool
 RISCOS_GetWindowWMInfo(_THIS, SDL_Window * window, struct SDL_SysWMinfo *info)
 {
