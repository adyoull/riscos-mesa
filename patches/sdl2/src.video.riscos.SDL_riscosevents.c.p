diff --git src/video/riscos/SDL_riscosevents.c src/video/riscos/SDL_riscosevents.c
--- src/video/riscos/SDL_riscosevents.c
+++ src/video/riscos/SDL_riscosevents.c
@@ -23,15 +23,107 @@
 #if SDL_VIDEO_DRIVER_RISCOS
 
 #include "../../events/SDL_events_c.h"
+#include "../../events/SDL_windowevents_c.h"
 
+#include "SDL.h"
 #include "SDL_log.h"
+#include "SDL_timer.h"
 #include "SDL_riscosvideo.h"
 #include "SDL_riscosevents_c.h"
+#include "SDL_riscoswindow.h"
+#include "SDL_riscosopengl.h"
 #include "scancodes_riscos.h"
 
 #include <kernel.h>
+#include <stdlib.h>
+#include <time.h>
 #include <swis.h>
 
+/* 2026: quitting from the desktop.
+
+   Message_PreQuit (8) is sent to a task that the user quits from the Task
+   Manager, and broadcast to all tasks when the desktop is being shut down.
+   A task that doesn't object is then sent Message_Quit (0), and must exit.
+   We always object (by acknowledging the PreQuit), and post SDL_QUIT, so
+   the program can quit its own way: ask "are you sure?", save, clean up.
+   Bit 0 of the flag word at +20 is set when only this task is being quit;
+   it is clear (or the word is missing) for a desktop shutdown, and then,
+   once the program has quit, the shutdown has to be restarted: a
+   Ctrl-Shift-F12 key press through Wimp_ProcessKey
+   (RISCOS_RestartShutdown). If the program doesn't quit (the user said no),
+   the shutdown stays cancelled. SDL can't tell us which answer was given,
+   so the shutdown is only restarted if the program quits within
+   RISCOS_SHUTDOWN_ANSWER_MS of the PreQuit, and not if the user quits it
+   another way in the meantime (icon bar menu, close icon): after "no",
+   quitting the program later must not shut the desktop down.
+
+   Message_Quit means the task must go. We post SDL_APP_TERMINATING and
+   SDL_QUIT, and give the program the chance to act on them; if it takes
+   them and asks for more events instead of quitting, the driver quits
+   for it (SDL_Quit and exit). */
+#define RISCOS_SHUTDOWN_ANSWER_MS 30000
+
+static struct
+{
+    SDL_bool shutdown_pending;  /* we objected to a desktop shutdown */
+    Uint32 shutdown_ticks;      /* when (SDL_GetTicks) */
+    SDL_bool quit_received;     /* Message_Quit arrived: the task must exit */
+} riscos_quit;
+
+/* Restart a desktop shutdown that we objected to, if the program is now
+   quitting in answer to it. Called before Wimp_CloseDown, and at exit for
+   programs that exit without SDL_Quit. */
+void
+RISCOS_RestartShutdown(void)
+{
+    if (riscos_quit.shutdown_pending &&
+        SDL_GetTicks() - riscos_quit.shutdown_ticks < RISCOS_SHUTDOWN_ANSWER_MS) {
+        _kernel_swi_regs regs;
+        regs.r[0] = 0x1FC;      /* Ctrl-Shift-F12 */
+        _kernel_swi(Wimp_ProcessKey, &regs, &regs);
+    }
+    riscos_quit.shutdown_pending = SDL_FALSE;
+}
+
+/* The user asked to quit just the program (icon bar menu, close icon): a
+   desktop shutdown they said no to earlier must not restart. */
+static void
+RISCOS_ForgetShutdown(void)
+{
+    riscos_quit.shutdown_pending = SDL_FALSE;
+}
+
+static void
+RISCOS_WimpMessage(_THIS, RISCOS_Message *message)
+{
+    _kernel_swi_regs regs;
+
+    switch (message->action) {
+    case 8:  /* Message_PreQuit */
+        riscos_quit.shutdown_pending =
+            (message->size < 24 || (message->data[0] & 1) == 0) ? SDL_TRUE : SDL_FALSE;
+        riscos_quit.shutdown_ticks = SDL_GetTicks();
+        message->your_ref = message->my_ref;    /* object: acknowledge it */
+        regs.r[0] = 19;                          /* User_Message_Acknowledge */
+        regs.r[1] = (int)message;
+        regs.r[2] = message->sender;
+        _kernel_swi(Wimp_SendMessage, &regs, &regs);
+        SDL_SendQuit();
+        break;
+    case 0x400C1: /* Message_ModeChange: the desktop's screen mode changed */
+        RISCOS_UpdateEigs(_this);                /* OS units per pixel may have too */
+        break;
+    case 0:  /* Message_Quit */
+        riscos_quit.shutdown_pending = SDL_FALSE;
+        riscos_quit.quit_received = SDL_TRUE;
+        SDL_SendAppEvent(SDL_APP_TERMINATING);
+        SDL_SendQuit();
+        break;
+    default:
+        break;
+    }
+}
+
 static SDL_Scancode
 SDL_RISCOS_translate_keycode(int keycode)
 {
@@ -50,6 +142,44 @@ SDL_RISCOS_translate_keycode(int keycode)
     return scancode;
 }
 
+/* 2026: text input.  Key up/down events come from scanning the keyboard,
+   but typed characters come from the keyboard buffer (full screen) or the
+   Wimp's Key_Pressed events (windowed).  RISC OS characters are Latin-1. */
+static void
+RISCOS_SendTextChar(int c)
+{
+    char text[3];
+
+    if (c < 32 || c == 127 || (c >= 128 && c < 160) || c > 255)
+        return;                     /* control, delete and special keys */
+    if (c < 128) {
+        text[0] = (char)c;
+        text[1] = 0;
+    } else {
+        text[0] = (char)(0xC0 | (c >> 6));
+        text[1] = (char)(0x80 | (c & 0x3F));
+        text[2] = 0;
+    }
+    SDL_SendKeyboardText(text);
+}
+
+static void
+RISCOS_DrainKeyboardBuffer(void)
+{
+    _kernel_swi_regs regs;
+    int n;
+
+    /* OS_Byte 145: get a character from buffer 0 (keyboard); C set = empty. */
+    for (n = 0; n < 32; n++) {
+        int flags;
+        regs.r[0] = 145;
+        regs.r[1] = 0;
+        if (_kernel_swi_c(OS_Byte, &regs, &regs, &flags) != NULL || flags)
+            break;
+        RISCOS_SendTextChar(regs.r[2] & 0xFF);
+    }
+}
+
 void
 RISCOS_PollKeyboard(_THIS)
 {
@@ -57,6 +187,17 @@ RISCOS_PollKeyboard(_THIS)
     Uint8 key = 2;
     int i;
 
+    /* 2026: in windowed mode only read the keyboard while we have the focus. */
+    if (RISCOS_IsWindowed(driverdata) && !driverdata->has_caret) {
+        for (i = 0; i < RISCOS_MAX_KEYS_PRESSED; i++) {
+            if (driverdata->key_pressed[i] != 255) {
+                SDL_SendKeyboardKey(SDL_RELEASED, SDL_RISCOS_translate_keycode(driverdata->key_pressed[i]));
+                driverdata->key_pressed[i] = 255;
+            }
+        }
+        return;
+    }
+
     /* Check for key releases */
     for (i = 0; i < RISCOS_MAX_KEYS_PRESSED; i++) {
         if (driverdata->key_pressed[i] != 255) {
@@ -67,6 +208,10 @@ RISCOS_PollKeyboard(_THIS)
         }
     }
 
+    /* Typed characters (full screen; the Wimp delivers them when windowed). */
+    if (!RISCOS_IsWindowed(driverdata))
+        RISCOS_DrainKeyboardBuffer();
+
     /* Check for key presses */
     while (key < 0xff) {
         key = _kernel_osbyte(121, key + 1, 0) & 0xff;
@@ -111,36 +256,169 @@ static const Uint8 mouse_button_map[] = {
     SDL_BUTTON_X2 + 3
 };
 
-void
-RISCOS_PollMouse(_THIS)
+/* 2026: mouse handling for a Wimp window. */
+static void
+RISCOS_PollMouseWindowed(_THIS)
+{
+    SDL_VideoData *driverdata = (SDL_VideoData *)_this->driverdata;
+    SDL_Mouse *mouse = SDL_GetMouse();
+    SDL_Window *window = driverdata->wimp_sdl_window;
+    int xeig = driverdata->xeig, yeig = driverdata->yeig;
+    RISCOS_WindowState state;
+    RISCOS_Pointer ptr;
+    int i, x, y, buttons;
+    SDL_bool inside;
+    _kernel_swi_regs regs;
+
+    regs.r[1] = (int)&ptr;
+    if (_kernel_swi(Wimp_GetPointerInfo, &regs, &regs) != NULL)
+        return;
+    state.open.window = driverdata->wimp_window;
+    regs.r[1] = (int)&state;
+    if (_kernel_swi(Wimp_GetWindowState, &regs, &regs) != NULL)
+        return;
+
+    buttons = ptr.buttons & 7;
+    inside = (ptr.window == driverdata->wimp_window) ? SDL_TRUE : SDL_FALSE;
+    /* 2026: a click that was pressed and released between two polls (easy
+       when a frame takes 100 ms or more, as with software OpenGL) was never
+       seen. The Wimp's Mouse_Click event records it: report the press now
+       and the release on the next poll. */
+    if (driverdata->pending_clicks != 0) {
+        int missed = driverdata->pending_clicks & ~driverdata->last_mouse_buttons;
+        driverdata->pending_clicks = 0;
+        if (missed != 0) {
+            buttons |= missed;
+            inside = SDL_TRUE;
+            /* report the press where it happened (the pointer may have
+               moved on since); the next poll moves it back */
+            ptr.x = driverdata->pending_click_x;
+            ptr.y = driverdata->pending_click_y;
+        }
+    }
+    /* Keep reporting while a button pressed inside the window is held (drags). */
+    if (!inside && driverdata->buttons_inside != 0 && buttons != 0)
+        inside = SDL_TRUE;
+
+    x = (ptr.x - (state.open.visible.x0 - state.open.scroll_x)) >> xeig;
+    y = ((state.open.visible.y1 - state.open.scroll_y) - ptr.y) >> yeig;
+    /* screen pixels -> SDL pixels: the window may be scaled (wscale, and a
+       GL render size stretched over it; see RISCOS_ShownW) */
+    {
+        int ww = RISCOS_ShownW(window) * (driverdata->wscale_x > 0 ? driverdata->wscale_x : 1);
+        int wh = RISCOS_ShownH(window) * (driverdata->wscale_y > 0 ? driverdata->wscale_y : 1);
+        if (ww != window->w) x = (int)(((long long)x * window->w) / ww);
+        if (wh != window->h) y = (int)(((long long)y * window->h) / wh);
+    }
+    if (x < 0) x = 0;
+    if (y < 0) y = 0;
+    if (x >= window->w) x = window->w - 1;
+    if (y >= window->h) y = window->h - 1;
+
+    if (inside != driverdata->pointer_in) {
+        driverdata->pointer_in = inside;
+        SDL_SetMouseFocus(inside ? window : NULL);
+        RISCOS_ApplyPointerVisibility(_this);
+    }
+    if (!inside) {
+        driverdata->buttons_inside = 0;
+        if (driverdata->last_mouse_buttons != 0) {
+            for (i = 0; i < SDL_arraysize(mouse_button_map); i++)
+                SDL_SendMouseButton(window, mouse->mouseID, SDL_RELEASED, mouse_button_map[i]);
+            driverdata->last_mouse_buttons = 0;
+        }
+        return;
+    }
+
+    if (mouse->x != x || mouse->y != y) {
+        SDL_SendMouseMotion(window, mouse->mouseID, 0, x, y);
+    }
+
+    if (driverdata->last_mouse_buttons != buttons) {
+        /* A click in the window claims the input focus. */
+        if ((buttons & ~driverdata->last_mouse_buttons) != 0 && !driverdata->has_caret) {
+            regs.r[0] = driverdata->wimp_window;
+            regs.r[1] = -1;
+            regs.r[2] = 0;
+            regs.r[3] = 0;
+            regs.r[4] = 1 << 25;
+            regs.r[5] = -1;
+            _kernel_swi(Wimp_SetCaretPosition, &regs, &regs);
+        }
+        for (i = 0; i < SDL_arraysize(mouse_button_map); i++) {
+            SDL_SendMouseButton(window, mouse->mouseID, (buttons & (1 << i)) ? SDL_PRESSED : SDL_RELEASED, mouse_button_map[i]);
+        }
+        driverdata->last_mouse_buttons = buttons;
+        driverdata->buttons_inside = buttons;
+    }
+}
+
+static void
+RISCOS_PollMouseFullscreen(_THIS)
 {
+    /* 2026: always report against our (full screen) window rather than
+       mouse->focus, so that once the pointer has touched a screen edge and
+       SDL has dropped the focus, it gets it back; convert OS units with the
+       real eigen factors instead of assuming 2 OS units per pixel. */
     SDL_VideoData *driverdata = (SDL_VideoData *)_this->driverdata;
     SDL_Mouse *mouse = SDL_GetMouse();
+    SDL_Window *window = _this->windows ? _this->windows : mouse->focus;
     SDL_Rect rect;
     _kernel_swi_regs regs;
-    int i, x, y, buttons;
+    int i, x, y, buttons, xeig = 1, yeig = 1;
 
     if (SDL_GetDisplayBounds(0, &rect) < 0) {
         return;
     }
 
+    xeig = driverdata->xeig;
+    yeig = driverdata->yeig;
+
     _kernel_swi(OS_Mouse, &regs, &regs);
-    x = (regs.r[0] >> 1);
-    y = rect.h - (regs.r[1] >> 1);
+    x = (regs.r[0] >> xeig);
+    y = rect.h - 1 - (regs.r[1] >> yeig);
     buttons = regs.r[2];
+    if (x < 0) x = 0;
+    if (y < 0) y = 0;
+    if (x >= rect.w) x = rect.w - 1;
+    if (y >= rect.h) y = rect.h - 1;
+    {
+        /* A GL window is stretched over the whole screen (EGL renders at
+           the window's size): screen pixels -> the window's */
+        SDL_WindowData *wd = window ? (SDL_WindowData *) window->driverdata : NULL;
+        if (wd && wd->gl_egl && rect.w > 0 && rect.h > 0 &&
+            (window->w != rect.w || window->h != rect.h)) {
+            x = (int)(((long long)x * window->w) / rect.w);
+            y = (int)(((long long)y * window->h) / rect.h);
+        }
+    }
+
+    if (window && mouse->focus != window) {
+        SDL_SetMouseFocus(window);
+    }
 
     if (mouse->x != x || mouse->y != y) {
-        SDL_SendMouseMotion(mouse->focus, mouse->mouseID, 0, x, y);
+        SDL_SendMouseMotion(window, mouse->mouseID, 0, x, y);
     }
 
     if (driverdata->last_mouse_buttons != buttons) {
         for (i = 0; i < SDL_arraysize(mouse_button_map); i++) {
-            SDL_SendMouseButton(mouse->focus, mouse->mouseID, (buttons & (1 << i)) ? SDL_PRESSED : SDL_RELEASED, mouse_button_map[i]);
+            SDL_SendMouseButton(window, mouse->mouseID, (buttons & (1 << i)) ? SDL_PRESSED : SDL_RELEASED, mouse_button_map[i]);
         }
         driverdata->last_mouse_buttons = buttons;
     }
 }
 
+void
+RISCOS_PollMouse(_THIS)
+{
+    if (RISCOS_IsWindowed((SDL_VideoData *)_this->driverdata)) {
+        RISCOS_PollMouseWindowed(_this);
+    } else {
+        RISCOS_PollMouseFullscreen(_this);
+    }
+}
+
 int
 RISCOS_InitEvents(_THIS)
 {
@@ -165,10 +443,338 @@ RISCOS_InitEvents(_THIS)
     return 0;
 }
 
+/* Icon bar menu: just "Quit". A Wimp menu block (header + one item). It
+   stays static: the Wimp reads it for as long as the menu is open. */
+static int riscos_iconbar_menu[7 + 6] = {
+    0, 0, 0,                       /* title, filled in below */
+    0x00070207,                    /* title fg 7, bg 2, work fg 7, bg 0 */
+    160, 44, 0,                    /* width, height, gap */
+    0x80, -1, 0x07000021, 0, 0, 0  /* last item: "Quit" */
+};
+
+static void
+RISCOS_IconbarMenu(int x)
+{
+    _kernel_swi_regs regs;
+    /* Title: the program's name (indirected, so it may be longer than 11
+       characters; flag bit 8 of the first item says so). */
+    static char title[64];
+    SDL_strlcpy(title, RISCOS_AppName(), sizeof(title));
+    riscos_iconbar_menu[0] = (int)title;
+    riscos_iconbar_menu[1] = -1;
+    riscos_iconbar_menu[2] = (int)SDL_strlen(title) + 1;
+    riscos_iconbar_menu[4] = SDL_max(160, 16 * (int)SDL_strlen(title) + 32);
+    riscos_iconbar_menu[7] = 0x80 | 0x100;      /* last item, title indirected */
+    SDL_strlcpy((char *)&riscos_iconbar_menu[10], "Quit", 12);
+    regs.r[1] = (int)riscos_iconbar_menu;
+    regs.r[2] = x - 64;
+    regs.r[3] = 96 + 44;
+    _kernel_swi(Wimp_CreateMenu, &regs, &regs);
+}
+
+/* 2026: handle one Wimp event. Returns 0 for a null event, 1 otherwise. */
+static int
+RISCOS_WimpHandleEvent(_THIS, int reason, RISCOS_PollBlock *event)
+{
+    SDL_VideoData *driverdata = (SDL_VideoData *)_this->driverdata;
+    _kernel_swi_regs regs;
+
+    switch (reason) {
+    case 0:  /* Null */
+        return 0;
+    case 13: /* Pollword_NonZero: SDL_SendWakeupEvent from another thread */
+        if (driverdata->wakeup_pollword)
+            *driverdata->wakeup_pollword = 0;
+        break;
+    case 1:  /* Redraw_Window_Request */
+        if (event->window == driverdata->wimp_window && driverdata->wimp_sdl_window) {
+#if SDL_VIDEO_OPENGL_OSMESA
+            /* a GL window: EGL's redraw (its frame, or its overlay) */
+            if (RISCOS_GL_Redraw(_this, driverdata->wimp_sdl_window, &event->redraw))
+                break;
+#endif
+            regs.r[1] = (int)&event->redraw;
+            if (_kernel_swi(Wimp_RedrawWindow, &regs, &regs) == NULL)
+                RISCOS_WimpPlotWindow(_this, driverdata->wimp_sdl_window, &event->redraw, regs.r[0]);
+        }
+        break;
+    case 2:  /* Open_Window_Request */
+        regs.r[1] = (int)&event->open;
+        _kernel_swi(Wimp_OpenWindow, &regs, &regs);
+        break;
+    case 6:  /* Mouse_Click: the window's buttons are polled, but a short click
+                can come and go between two polls, so remember it */
+        if (event->click.window == driverdata->wimp_window && RISCOS_IsWindowed(driverdata)) {
+            driverdata->pending_clicks |= event->click.buttons & 7;
+            driverdata->pending_click_x = event->click.x;
+            driverdata->pending_click_y = event->click.y;
+        } else if (event->click.window == -2 && event->click.icon == driverdata->iconbar_icon) {
+            if (event->click.buttons & 2) {
+                RISCOS_IconbarMenu(event->click.x);
+            } else if (RISCOS_IsWindowed(driverdata)) {
+                /* Select/Adjust: bring the game window to the front. */
+                RISCOS_WindowState state;
+                state.open.window = driverdata->wimp_window;
+                regs.r[1] = (int)&state;
+                if (_kernel_swi(Wimp_GetWindowState, &regs, &regs) == NULL) {
+                    state.open.behind = -1;
+                    regs.r[1] = (int)&state;
+                    _kernel_swi(Wimp_OpenWindow, &regs, &regs);
+                }
+            }
+        }
+        break;
+    case 9:  /* Menu_Selection: the icon bar menu's Quit quits the program */
+        if (event->menu[0] == 0) {
+            RISCOS_ForgetShutdown();
+            SDL_SendQuit();
+        }
+        break;
+    case 3:  /* Close_Window_Request: SDL sends SDL_QUIT when the last
+                window is closed (unless SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE
+                is 0); a program can also handle the close itself */
+        if (event->window == driverdata->wimp_window && driverdata->wimp_sdl_window) {
+            RISCOS_ForgetShutdown();
+            SDL_SendWindowEvent(driverdata->wimp_sdl_window, SDL_WINDOWEVENT_CLOSE, 0, 0);
+        }
+        break;
+    case 8:  /* Key_Pressed: key up/down are scanned directly; typed characters become text */
+        if (event->key.caret.window == driverdata->wimp_window)
+            RISCOS_SendTextChar(event->key.code);
+        if (event->key.code >= 0x180 && event->key.code <= 0x1FF && event->key.code != 0x18B) {
+            /* function keys etc. that we might not want: hand F12 and friends to the Wimp */
+            if (event->key.code == 0x1CC || event->key.code == 0x1DC || event->key.code == 0x1EC || event->key.code == 0x1FC) {
+                regs.r[0] = event->key.code;
+                _kernel_swi(Wimp_ProcessKey, &regs, &regs);
+            }
+        }
+        break;
+    case 11: /* Lose_Caret */
+        if (event->caret.window == driverdata->wimp_window)
+            driverdata->has_caret = SDL_FALSE;
+        break;
+    case 12: /* Gain_Caret */
+        if (event->caret.window == driverdata->wimp_window)
+            driverdata->has_caret = SDL_TRUE;
+        break;
+    case 17: /* User_Message */
+    case 18: /* User_Message_Recorded */
+        RISCOS_WimpMessage(_this, &event->message);
+        break;
+    default:
+        break;
+    }
+    return 1;
+}
+
+/* 2026: Wimp event handling for windowed mode.
+   wait == SDL_FALSE: handle what is pending and return at the first null
+   event (the normal once-per-PumpEvents poll).
+   wait == SDL_TRUE: yield to other tasks with Wimp_PollIdle until the
+   monotonic time until_cs, handling any of our events that arrive in the
+   meantime (redraws, clicks, keys, messages). This is how the program waits
+   cooperatively: see RISCOS_WimpDelay. */
+static void
+RISCOS_PollWimpUntil(_THIS, SDL_bool wait, unsigned int until_cs)
+{
+    SDL_VideoData *driverdata = (SDL_VideoData *)_this->driverdata;
+    RISCOS_PollBlock event;
+    int n;
+    _kernel_swi_regs regs;
+
+    for (n = 0; wait || n < 32; n++) {
+        regs.r[0] = (1 << 4) | (1 << 5);  /* pointer leaving/entering are polled directly */
+        regs.r[1] = (int)&event;
+        regs.r[2] = (int)until_cs;
+        regs.r[3] = 0;
+        if (_kernel_swi(wait ? Wimp_PollIdle : Wimp_Poll, &regs, &regs) != NULL)
+            return;
+        if (RISCOS_WimpHandleEvent(_this, regs.r[0], &event) == 0)
+            return;                         /* null event: done (or time is up) */
+        if (wait && !RISCOS_IsWindowed(driverdata))
+            wait = SDL_FALSE;               /* went full screen while waiting */
+    }
+}
+
+/* 2026: the scroll wheel. RISC OS 5 doesn't deliver it to us as Wimp
+   Scroll_Request events, so read it directly: OS_Pointer 2 returns the
+   accumulated position of the "alternate positioning device" (the wheel),
+   R0 = X, R1 = Y, +ve Y = wheel pushed away (scroll up). This works both
+   in a window and in full screen. */
+static void
+RISCOS_PollWheel(_THIS)
+{
+    SDL_VideoData *driverdata = (SDL_VideoData *) _this->driverdata;
+    _kernel_swi_regs regs;
+    SDL_Window *window;
+    int dx, dy;
+
+    regs.r[0] = 2;
+    if (_kernel_swi(OS_Pointer, &regs, &regs) != NULL)
+        return;                         /* no wheel support in this OS */
+
+    if (!driverdata->wheel_valid) {     /* first read: just take a baseline */
+        driverdata->wheel_x = regs.r[0];
+        driverdata->wheel_y = regs.r[1];
+        driverdata->wheel_valid = SDL_TRUE;
+        return;
+    }
+    dx = regs.r[0] - driverdata->wheel_x;
+    dy = regs.r[1] - driverdata->wheel_y;
+    driverdata->wheel_x = regs.r[0];
+    driverdata->wheel_y = regs.r[1];
+    if (dx == 0 && dy == 0)
+        return;
+    /* Ignore a counter wrap or anything implausible. */
+    if (dx > 64 || dx < -64 || dy > 64 || dy < -64)
+        return;
+
+    /* Only while the pointer is over our window (always, in full screen). */
+    if (RISCOS_IsWindowed(driverdata) && !driverdata->pointer_in)
+        return;
+    window = SDL_GetMouseFocus();
+    if (window == NULL)
+        return;
+    SDL_SendMouseWheel(window, 0, (float)dx, (float)dy, SDL_MOUSEWHEEL_NORMAL);
+}
+
+static void
+RISCOS_PollWimp(_THIS)
+{
+    RISCOS_PollWimpUntil(_this, SDL_FALSE, 0);
+}
+
+static unsigned int
+RISCOS_MonotonicCs(void)
+{
+    _kernel_swi_regs regs;
+    _kernel_swi(OS_ReadMonotonicTime, &regs, &regs);
+    return (unsigned int)regs.r[0];
+}
+
+/* 2026: wait cooperatively. While we're a Wimp task with a desktop window,
+   waiting must not stop the desktop, so yield to the other tasks with
+   Wimp_PollIdle (still handling our own events) for the delay rounded to
+   the nearest centisecond, the resolution of RISC OS's cooperative timing.
+   Only delays under 5 ms are busy-waited, after one Wimp_Poll, so the desktop
+   is never held for more than 4 ms. Only the main thread may talk to the
+   Wimp. Returns SDL_FALSE (caller waits the ordinary way) when running full
+   screen (single tasking by design) or not in the desktop. */
+SDL_bool
+RISCOS_WimpDelay(_THIS, Uint32 ms)
+{
+    SDL_VideoData *driverdata = (SDL_VideoData *)_this->driverdata;
+    Uint32 cs = (ms + 5) / 10;
+
+    if (driverdata->wimp_task == 0 || !RISCOS_IsWindowed(driverdata) ||
+        SDL_ThreadID() != driverdata->main_thread)
+        return SDL_FALSE;
+
+    if (cs > 0) {
+        RISCOS_PollWimpUntil(_this, SDL_TRUE, RISCOS_MonotonicCs() + cs);
+    } else {
+        struct timespec ts;
+        RISCOS_PollWimp(_this);               /* give the desktop a turn */
+        ts.tv_sec = 0;
+        ts.tv_nsec = (long)ms * 1000000;
+        if (ms > 0)
+            nanosleep(&ts, NULL);
+    }
+    return SDL_TRUE;
+}
+
+/* 2026: SDL_WaitEvent / SDL_WaitEventTimeout. Block in the Wimp until
+   something happens, using no CPU while idle:
+   - any Wimp event for us wakes us (and is handled here);
+   - SDL_SendWakeupEvent (another thread) sets the RMA pollword;
+   - the pointer entering our window wakes us. While it is over the window
+     we also wake every 2 cs, because the Wimp has no pointer-motion events
+     and SDL needs to sample the mouse to report motion.
+   Returns 1 if woken (SDL then pumps and checks its queue), 0 on timeout, or
+   -1 (SDL falls back to polling) when full screen or not the main thread. */
+int
+RISCOS_WaitEventTimeout(_THIS, int timeout)
+{
+    SDL_VideoData *driverdata = (SDL_VideoData *)_this->driverdata;
+    unsigned int now, until = 0, deadline = 0;
+    RISCOS_PollBlock event;
+    _kernel_swi_regs regs;
+
+    if (driverdata->wimp_task == 0 || !RISCOS_IsWindowed(driverdata) ||
+        driverdata->wakeup_pollword == NULL || SDL_ThreadID() != driverdata->main_thread)
+        return -1;
+
+    now = RISCOS_MonotonicCs();
+    regs.r[0] = (1 << 4) | (1 << 22);     /* no Pointer_Leaving; scan the pollword */
+    if (timeout >= 0)
+        deadline = now + ((unsigned int)timeout + 9) / 10;
+#if SDL_VIDEO_OPENGL_OSMESA
+    {
+        /* a GL frame held for a vsync, or an overlay to keep right while
+           nothing is swapped: wake up for RISCOS_GL_Idle (in PumpEvents) */
+        int gl_cs = RISCOS_GL_Idle(_this, SDL_FALSE);
+        if (gl_cs > 0 && (!driverdata->pointer_in || gl_cs < 2)) {
+            until = now + (unsigned int)gl_cs;
+            if (timeout >= 0 && (int)(deadline - until) < 0)
+                until = deadline;
+            goto poll;
+        }
+    }
+#endif
+    if (driverdata->pointer_in) {
+        until = now + 2;
+        if (timeout >= 0 && (int)(deadline - until) < 0)
+            until = deadline;
+    } else if (timeout >= 0) {
+        until = deadline;
+    } else {
+        regs.r[0] |= 1;                   /* no null events: sleep until an event */
+    }
+#if SDL_VIDEO_OPENGL_OSMESA
+poll:
+#endif
+    regs.r[1] = (int)&event;
+    regs.r[2] = (int)until;
+    regs.r[3] = (int)driverdata->wakeup_pollword;
+    if (_kernel_swi(Wimp_PollIdle, &regs, &regs) != NULL)
+        return -1;
+
+    if (RISCOS_WimpHandleEvent(_this, regs.r[0], &event) == 0) {
+        /* Null event: our timeout, or a 2 cs wake to sample the mouse. */
+        if (timeout >= 0 && (int)(RISCOS_MonotonicCs() - deadline) >= 0)
+            return 0;
+    }
+    return 1;
+}
+
+void
+RISCOS_SendWakeupEvent(_THIS, SDL_Window *window)
+{
+    SDL_VideoData *driverdata = (SDL_VideoData *)_this->driverdata;
+    (void)window;
+    if (driverdata->wakeup_pollword)
+        *driverdata->wakeup_pollword = 1;
+}
+
 void
 RISCOS_PumpEvents(_THIS)
 {
+    /* Message_Quit: the program has taken SDL_APP_TERMINATING and SDL_QUIT
+       and carried on instead of quitting, but the task must go. */
+    if (riscos_quit.quit_received &&
+        !SDL_HasEvent(SDL_QUIT) && !SDL_HasEvent(SDL_APP_TERMINATING)) {
+        SDL_Quit();
+        exit(0);
+    }
+    /* Only multitask while we have a desktop window; full screen owns the machine. */
+    if (RISCOS_IsWindowed((SDL_VideoData *)_this->driverdata)) {
+        RISCOS_PollWimp(_this);
+#if SDL_VIDEO_OPENGL_OSMESA
+        RISCOS_GL_Idle(_this, SDL_TRUE);
+#endif
+    }
     RISCOS_PollMouse(_this);
+    RISCOS_PollWheel(_this);
     RISCOS_PollKeyboard(_this);
 }
 
