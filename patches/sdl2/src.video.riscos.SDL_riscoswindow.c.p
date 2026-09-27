diff --git src/video/riscos/SDL_riscoswindow.c src/video/riscos/SDL_riscoswindow.c
index f47d33a..2136097 100644
--- src/video/riscos/SDL_riscoswindow.c
+++ src/video/riscos/SDL_riscoswindow.c
@@ -24,16 +24,340 @@
 
 #include "SDL_version.h"
 #include "SDL_syswm.h"
+#include "SDL_hints.h"
 #include "../SDL_sysvideo.h"
 #include "../../events/SDL_mouse_c.h"
 
 
 #include "SDL_riscosvideo.h"
 #include "SDL_riscoswindow.h"
+#include "SDL_riscosevents_c.h"
+#include "SDL_riscosopengl.h"
+
+/* 2026: windowed mode.  A window created without SDL_WINDOW_FULLSCREEN is
+   shown in a Wimp window on the desktop (the program becomes a Wimp task);
+   full screen windows keep the original behaviour of owning the screen.  */
+
+#include <kernel.h>
+#include <stdlib.h>
+#include <swis.h>
+
+/* Messages we want besides Message_Quit, which every task gets: Message_PreQuit
+   (see SDL_riscosevents.c). */
+static const int riscos_wimp_messages[] = { 8 /* Message_PreQuit */, 0 };
+
+/* 2026: the program's own name (task name, icon bar menu title) and icon
+   bar sprite, found from its application directory: a program run as
+   ...!TestGL2.!RunImage is "TestGL2" with the sprite "!TestGL2" if the
+   Wimp sprite pool has it (IconSprites / the Filer loads it), otherwise
+   the generic "application" sprite. SDL_HINT_APP_NAME overrides the name.
+   These are properties of the program, not of a window or video device,
+   so they're kept per process (static) rather than in SDL_VideoData.
+   (This replaces the old global SDL$IconSprite variable, which leaked from
+   one program to the next: OpenTTD's setting showed up in other programs.) */
+#include <unixlib/local.h>
+extern char *program_invocation_name, *program_invocation_short_name;
+static char riscos_app_name[64];
+static char riscos_app_sprite[13];
+
+static int
+RISCOS_WimpSpriteExists(const char *name)
+{
+    _kernel_swi_regs regs;
+    regs.r[0] = 40;                   /* read sprite information */
+    regs.r[2] = (int)name;
+    return _kernel_swi(Wimp_SpriteOp, &regs, &regs) == NULL;
+}
+
+static void
+RISCOS_FindAppIdentity(void)
+{
+    char ro[256], canon[256];
+    const char *hint = SDL_GetHint(SDL_HINT_APP_NAME);
+    const char *path = program_invocation_name;
+    char *leaf = NULL, *end;
+    _kernel_swi_regs regs;
+
+    if (riscos_app_name[0])
+        return;
+    riscos_app_sprite[0] = 0;
+    if (path && *path) {
+        /* argv[0] can be a Unix or a RISC OS path; make it a full RISC OS one. */
+        if (SDL_strchr(path, '/') && __riscosify_std(path, 0, ro, sizeof(ro), NULL))
+            path = ro;
+        regs.r[0] = 37;               /* OS_FSControl 37: canonicalise path */
+        regs.r[1] = (int)path; regs.r[2] = (int)canon;
+        regs.r[3] = 0; regs.r[4] = 0; regs.r[5] = sizeof(canon);
+        if (_kernel_swi(OS_FSControl, &regs, &regs) == NULL) {
+            end = SDL_strrchr(canon, '.');        /* ...!App.!RunImage */
+            if (end) {
+                *end = 0;
+                leaf = SDL_strrchr(canon, '.');
+                leaf = leaf ? leaf + 1 : canon;
+                if (*leaf != '!')
+                    leaf = NULL;
+            }
+        }
+    }
+    if (leaf && leaf[1]) {
+        SDL_strlcpy(riscos_app_sprite, leaf, sizeof(riscos_app_sprite));
+        SDL_strlcpy(riscos_app_name, leaf + 1, sizeof(riscos_app_name));
+    } else if (program_invocation_short_name && *program_invocation_short_name) {
+        SDL_strlcpy(riscos_app_name, program_invocation_short_name, sizeof(riscos_app_name));
+    }
+    if (hint && *hint)
+        SDL_strlcpy(riscos_app_name, hint, sizeof(riscos_app_name));
+    if (!riscos_app_name[0])
+        SDL_strlcpy(riscos_app_name, "SDL", sizeof(riscos_app_name));
+    if (!riscos_app_sprite[0] || !RISCOS_WimpSpriteExists(riscos_app_sprite))
+        SDL_strlcpy(riscos_app_sprite, "application", sizeof(riscos_app_sprite));
+}
+
+const char *
+RISCOS_AppName(void)
+{
+    RISCOS_FindAppIdentity();
+    return riscos_app_name;
+}
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
+   90dpi (EX1 EY1) mode would show it. SDL_HINT_RISCOS_WINDOW_SCALE (or
+   the older SDL$WindowScale variable) overrides this (1 = never scale,
+   2-4 = always scale by that much). Automatic scaling is skipped if the
+   scaled window wouldn't fit on the screen. */
+void
+RISCOS_ChooseWindowScale(_THIS, SDL_Window *window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    int xeig = RISCOS_WimpReadEig(4), yeig = RISCOS_WimpReadEig(5);
+    const char *env = SDL_GetHint(SDL_HINT_RISCOS_WINDOW_SCALE);
+    int sx, sy;
+
+    if (!env || !*env)
+        env = SDL_getenv("SDL$WindowScale");    /* older name, as set by !Run files */
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
+    RISCOS_FindAppIdentity();
+    regs.r[0] = 380;
+    regs.r[1] = 0x4B534154; /* "TASK" */
+    regs.r[2] = (int)riscos_app_name;      /* the name in the Task Manager */
+    regs.r[3] = (int)riscos_wimp_messages;
+    err = _kernel_swi(Wimp_Initialise, &regs, &regs);
+    if (err != NULL)
+        return SDL_SetError("Wimp_Initialise failed: %s", err->errmess);
+    vdata->wimp_task = regs.r[1];
+    {
+        /* A program may exit without SDL_Quit: restart a desktop shutdown
+           from there too (while we are still a Wimp task). */
+        static SDL_bool registered = SDL_FALSE;
+        if (!registered) {
+            atexit(RISCOS_RestartShutdown);
+            registered = SDL_TRUE;
+        }
+    }
+
+    /* Put the program's icon on the icon bar: its own sprite (!App, loaded
+       by the Filer or IconSprites in !Run) or the generic "application". */
+    vdata->iconbar_icon = -1;
+    {
+        const char *sprite = riscos_app_sprite;
+        if (sprite && *sprite) {
+            RISCOS_IconCreate icon;
+            SDL_memset(&icon, 0, sizeof(icon));
+            icon.window = -1;              /* right hand side of the icon bar */
+            icon.box.x0 = 0; icon.box.y0 = 0; icon.box.x1 = 68; icon.box.y1 = 68;
+            icon.flags = 0x0000301A;       /* sprite, centred, button type click */
+            RISCOS_IconSpriteName(&icon, sprite);
+            regs.r[0] = 0;
+            regs.r[1] = (int)&icon;
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
+    RISCOS_WindowOpen open;
+    _kernel_swi_regs regs;
+
+    open.window = handle;
+    open.visible.x0 = minx;
+    open.visible.y0 = maxy - h_os;
+    open.visible.x1 = minx + w_os;
+    open.visible.y1 = maxy;
+    open.scroll_x = 0;
+    open.scroll_y = 0;
+    open.behind = -1;          /* open on top */
+    regs.r[1] = (int)&open;
+    _kernel_swi(Wimp_OpenWindow, &regs, &regs);
+}
+
+/* Open the desktop window at its remembered position (wimp_open_x/y, the
+   top left corner) and take the input focus. */
+static void
+RISCOS_WimpShowWindow(_THIS, SDL_Window * window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    int xeig = RISCOS_WimpReadEig(4), yeig = RISCOS_WimpReadEig(5);
+    int w_os = (window->w * vdata->wscale_x) << xeig;
+    int h_os = (window->h * vdata->wscale_y) << yeig;
+    _kernel_swi_regs regs;
+
+    RISCOS_WimpOpenAt(vdata->wimp_window, vdata->wimp_open_x, vdata->wimp_open_y, w_os, h_os);
+
+    regs.r[0] = vdata->wimp_window;
+    regs.r[1] = -1;
+    regs.r[2] = 0;
+    regs.r[3] = 0;
+    regs.r[4] = 1 << 25;   /* invisible caret */
+    regs.r[5] = -1;
+    _kernel_swi(Wimp_SetCaretPosition, &regs, &regs);
+}
+
+static int
+RISCOS_WimpCreateWindow(_THIS, SDL_Window * window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    int xeig = RISCOS_WimpReadEig(4), yeig = RISCOS_WimpReadEig(5);
+    int w_os, h_os;
+    int scr_w = RISCOS_WimpScreenSize(11) << xeig, scr_h = RISCOS_WimpScreenSize(12) << yeig;
+    RISCOS_WindowDef def;
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
+        SDL_strlcpy(vdata->window_title, window->title, sizeof(vdata->window_title));
+    if (!vdata->window_title[0])
+        SDL_strlcpy(vdata->window_title, "SDL", sizeof(vdata->window_title));
+
+    SDL_memset(&def, 0, sizeof(def));
+    def.visible.x0 = 0; def.visible.y0 = 0;                   /* visible area (set when opened) */
+    def.visible.x1 = w_os; def.visible.y1 = h_os;
+    def.scroll_x = 0; def.scroll_y = 0;
+    def.behind = -1;
+    def.flags = 0x80000002 | 0x01000000 | 0x02000000 | 0x04000000;  /* new format, moveable, back, close, title */
+    def.title_fg = 7;
+    def.title_bg = 2;
+    def.work_fg = 7;
+    def.work_bg = 255;             /* transparent: we draw it all */
+    def.scroll_outer = 3;
+    def.scroll_inner = 1;
+    def.title_focus_bg = 12;
+    def.extra_flags = 0;
+    def.extent.x0 = 0; def.extent.y0 = -h_os;                 /* work area extent */
+    def.extent.x1 = w_os; def.extent.y1 = 0;
+    def.title_flags = 0x0700013D;  /* indirected text, centred, filled */
+    def.work_flags = 3 << 12;      /* work area button type: click */
+    def.sprite_area = 1;           /* the Wimp sprite area */
+    def.min_width = 0; def.min_height = 0;
+    def.title_data[0] = (int)vdata->window_title;
+    def.title_data[1] = -1;
+    def.title_data[2] = sizeof(vdata->window_title);
+    def.icon_count = 0;
+
+    regs.r[1] = (int)&def;
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
+    /* Centred on the screen, below the top edge. SDL creates windows hidden
+       and then shows them (RISCOS_ShowWindow) unless the program asked for
+       SDL_WINDOW_HIDDEN; coming back from full screen, show it now. */
+    minx = (scr_w - w_os) / 2;
+    if (minx < 0) minx = 0;
+    maxy = scr_h - (scr_h - h_os) / 2;
+    if (maxy > scr_h - 40) maxy = scr_h - 40;
+    vdata->wimp_open_x = minx;
+    vdata->wimp_open_y = maxy;
+    if (!(window->flags & SDL_WINDOW_HIDDEN) && !window->is_hiding)
+        RISCOS_WimpShowWindow(_this, window);
+
+    return 0;
+}
 
 int
 RISCOS_CreateWindow(_THIS, SDL_Window * window)
 {
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
     SDL_WindowData *driverdata;
 
     driverdata = (SDL_WindowData *) SDL_calloc(1, sizeof(*driverdata));
@@ -42,27 +366,212 @@ RISCOS_CreateWindow(_THIS, SDL_Window * window)
     }
     driverdata->window = window;
 
-    window->flags |= SDL_WINDOW_FULLSCREEN;
-
-    SDL_SetMouseFocus(window);
+    if ((window->flags & SDL_WINDOW_FULLSCREEN) || RISCOS_IsWindowed(vdata)) {
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
+    int handle;
+
+    if (!RISCOS_IsWindowed(vdata))
+        return;
+    handle = vdata->wimp_window;
+    regs.r[1] = (int)&handle;
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
+    } else if (!window->is_destroying && !RISCOS_IsWindowed(vdata)) {
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
+    int xeig, yeig, w_os, h_os;
+    RISCOS_WindowState state;
+    RISCOS_Box extent;
+    _kernel_swi_regs regs;
+
+    if (!RISCOS_IsWindowed(vdata) || vdata->wimp_sdl_window != window)
+        return;
+
+    xeig = RISCOS_WimpReadEig(4);
+    yeig = RISCOS_WimpReadEig(5);
+    RISCOS_ChooseWindowScale(_this, window);
+    w_os = (window->w * vdata->wscale_x) << xeig;
+    h_os = (window->h * vdata->wscale_y) << yeig;
+
+    extent.x0 = 0; extent.y0 = -h_os; extent.x1 = w_os; extent.y1 = 0;
+    regs.r[0] = vdata->wimp_window;
+    regs.r[1] = (int)&extent;
+    _kernel_swi(Wimp_SetExtent, &regs, &regs);
+
+    /* A hidden window opens at its remembered place when it is shown. */
+    if (window->flags & SDL_WINDOW_HIDDEN)
+        return;
+
+    /* Keep the top left corner where it is. */
+    state.open.window = vdata->wimp_window;
+    regs.r[1] = (int)&state;
+    if (_kernel_swi(Wimp_GetWindowState, &regs, &regs) == NULL) {
+        RISCOS_WimpOpenAt(vdata->wimp_window, state.open.visible.x0, state.open.visible.y1, w_os, h_os);
+    }
+}
+
+/* 2026: SDL_ShowWindow / SDL_HideWindow for a desktop window. A full screen
+   window is always shown. */
+void
+RISCOS_ShowWindow(_THIS, SDL_Window * window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+
+    if (!RISCOS_IsWindowed(vdata) || vdata->wimp_sdl_window != window)
+        return;
+    RISCOS_WimpShowWindow(_this, window);
+}
+
+void
+RISCOS_HideWindow(_THIS, SDL_Window * window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    RISCOS_WindowState state;
+    _kernel_swi_regs regs;
+    int handle;
+
+    if (!RISCOS_IsWindowed(vdata) || vdata->wimp_sdl_window != window)
+        return;
+
+    /* Remember where it was, to open it there again. */
+    state.open.window = vdata->wimp_window;
+    regs.r[1] = (int)&state;
+    if (_kernel_swi(Wimp_GetWindowState, &regs, &regs) == NULL) {
+        vdata->wimp_open_x = state.open.visible.x0;
+        vdata->wimp_open_y = state.open.visible.y1;
+    }
+    handle = vdata->wimp_window;
+    regs.r[1] = (int)&handle;
+    _kernel_swi(Wimp_CloseWindow, &regs, &regs);
+
+    if (vdata->pointer_in) {
+        vdata->pointer_in = SDL_FALSE;
+        SDL_SetMouseFocus(NULL);
+        RISCOS_ApplyPointerVisibility(_this);
+    }
+    vdata->has_caret = SDL_FALSE;
+}
+
+void
+RISCOS_SetWindowTitle(_THIS, SDL_Window * window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    _kernel_swi_regs regs;
+
+    if (window->title)
+        SDL_strlcpy(vdata->window_title, window->title, sizeof(vdata->window_title));
+    if (!RISCOS_IsWindowed(vdata) || vdata->wimp_sdl_window != window)
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
 
+    if (RISCOS_IsWindowed(vdata) && vdata->wimp_sdl_window == window) {
+        _kernel_swi_regs regs;
+        int handle = vdata->wimp_window;
+        regs.r[1] = (int)&handle;
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
+        RISCOS_RestartShutdown();   /* while we are still a Wimp task */
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
