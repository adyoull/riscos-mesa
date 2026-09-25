diff --git src/video/riscos/SDL_riscosevents.c src/video/riscos/SDL_riscosevents.c
index fcca470..cbbeb02 100644
--- src/video/riscos/SDL_riscosevents.c
+++ src/video/riscos/SDL_riscosevents.c
@@ -27,9 +27,11 @@
 #include "SDL_log.h"
 #include "SDL_riscosvideo.h"
 #include "SDL_riscosevents_c.h"
+#include "SDL_riscoswindow.h"
 #include "scancodes_riscos.h"
 
 #include <kernel.h>
+#include <time.h>
 #include <swis.h>
 
 static SDL_Scancode
@@ -50,6 +52,44 @@ SDL_RISCOS_translate_keycode(int keycode)
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
@@ -57,6 +97,17 @@ RISCOS_PollKeyboard(_THIS)
     Uint8 key = 2;
     int i;
 
+    /* 2026: in windowed mode only read the keyboard while we have the focus. */
+    if (driverdata->wimp_window != 0 && !driverdata->has_caret) {
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
@@ -67,6 +118,10 @@ RISCOS_PollKeyboard(_THIS)
         }
     }
 
+    /* Typed characters (full screen; the Wimp delivers them when windowed). */
+    if (driverdata->wimp_window == 0)
+        RISCOS_DrainKeyboardBuffer();
+
     /* Check for key presses */
     while (key < 0xff) {
         key = _kernel_osbyte(121, key + 1, 0) & 0xff;
@@ -111,36 +166,135 @@ static const Uint8 mouse_button_map[] = {
     SDL_BUTTON_X2 + 3
 };
 
-void
-RISCOS_PollMouse(_THIS)
+/* 2026: mouse handling for a Wimp window. */
+static void
+RISCOS_PollMouseWindowed(_THIS)
 {
     SDL_VideoData *driverdata = (SDL_VideoData *)_this->driverdata;
     SDL_Mouse *mouse = SDL_GetMouse();
+    SDL_Window *window = driverdata->wimp_sdl_window;
+    int xeig = driverdata->xeig, yeig = driverdata->yeig;
+    int state[9], ptr[5], i, x, y, buttons;
+    SDL_bool inside;
+    _kernel_swi_regs regs;
+
+    regs.r[1] = (int)ptr;
+    if (_kernel_swi(Wimp_GetPointerInfo, &regs, &regs) != NULL)
+        return;
+    state[0] = driverdata->wimp_window;
+    regs.r[1] = (int)state;
+    if (_kernel_swi(Wimp_GetWindowState, &regs, &regs) != NULL)
+        return;
+
+    buttons = ptr[2] & 7;
+    inside = (ptr[3] == driverdata->wimp_window) ? SDL_TRUE : SDL_FALSE;
+    /* Keep reporting while a button pressed inside the window is held (drags). */
+    if (!inside && driverdata->buttons_inside != 0 && buttons != 0)
+        inside = SDL_TRUE;
+
+    x = (ptr[0] - (state[1] - state[5])) >> xeig;
+    y = ((state[4] - state[6]) - ptr[1]) >> yeig;
+    if (driverdata->wscale_x > 1) x /= driverdata->wscale_x;   /* scaled window */
+    if (driverdata->wscale_y > 1) y /= driverdata->wscale_y;
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
+{
+    /* 2026: always report against our (full screen) window rather than
+       mouse->focus, so that once the pointer has touched a screen edge and
+       SDL has dropped the focus, it gets it back; convert OS units with the
+       real eigen factors instead of assuming 2 OS units per pixel. */
+    SDL_VideoData *driverdata = (SDL_VideoData *)_this->driverdata;
+    SDL_Mouse *mouse = SDL_GetMouse();
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
+    if (((SDL_VideoData *)_this->driverdata)->wimp_window != 0) {
+        RISCOS_PollMouseWindowed(_this);
+    } else {
+        RISCOS_PollMouseFullscreen(_this);
+    }
+}
+
 int
 RISCOS_InitEvents(_THIS)
 {
@@ -165,10 +319,298 @@ RISCOS_InitEvents(_THIS)
     return 0;
 }
 
+/* Icon bar menu: just "Quit". */
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
+RISCOS_WimpHandleEvent(_THIS, int reason, int *block)
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
+        if (block[0] == driverdata->wimp_window && driverdata->wimp_sdl_window) {
+            regs.r[1] = (int)block;
+            if (_kernel_swi(Wimp_RedrawWindow, &regs, &regs) == NULL)
+                RISCOS_WimpPlotWindow(_this, driverdata->wimp_sdl_window, block, regs.r[0]);
+        }
+        break;
+    case 2:  /* Open_Window_Request */
+        regs.r[1] = (int)block;
+        _kernel_swi(Wimp_OpenWindow, &regs, &regs);
+        break;
+    case 6:  /* Mouse_Click: only the icon bar icon matters, the window is polled */
+        if (block[3] == -2 && block[4] == driverdata->iconbar_icon) {
+            if (block[2] & 2) {
+                RISCOS_IconbarMenu(block[0]);
+            } else if (driverdata->wimp_window != 0) {
+                /* Select/Adjust: bring the game window to the front. */
+                int state[9];
+                state[0] = driverdata->wimp_window;
+                regs.r[1] = (int)state;
+                if (_kernel_swi(Wimp_GetWindowState, &regs, &regs) == NULL) {
+                    state[7] = -1;
+                    regs.r[1] = (int)state;
+                    _kernel_swi(Wimp_OpenWindow, &regs, &regs);
+                }
+            }
+        }
+        break;
+    case 9:  /* Menu_Selection */
+        if (block[0] == 0)
+            SDL_SendQuit();
+        break;
+    case 3:  /* Close_Window_Request */
+        if (block[0] == driverdata->wimp_window)
+            SDL_SendQuit();
+        break;
+    case 8:  /* Key_Pressed: key up/down are scanned directly; typed characters become text */
+        if (block[0] == driverdata->wimp_window)
+            RISCOS_SendTextChar(block[6]);
+        if (block[6] >= 0x180 && block[6] <= 0x1FF && block[6] != 0x18B) {
+            /* function keys etc. that we might not want: hand F12 and friends to the Wimp */
+            if (block[6] == 0x1CC || block[6] == 0x1DC || block[6] == 0x1EC || block[6] == 0x1FC) {
+                regs.r[0] = block[6];
+                _kernel_swi(Wimp_ProcessKey, &regs, &regs);
+            }
+        }
+        break;
+    case 11: /* Lose_Caret */
+        if (block[0] == driverdata->wimp_window)
+            driverdata->has_caret = SDL_FALSE;
+        break;
+    case 12: /* Gain_Caret */
+        if (block[0] == driverdata->wimp_window)
+            driverdata->has_caret = SDL_TRUE;
+        break;
+    case 17: /* User_Message */
+    case 18: /* User_Message_Recorded */
+        if (block[4] == 0) /* Message_Quit */
+            SDL_SendQuit();
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
+    int block[64], n;
+    _kernel_swi_regs regs;
+
+    for (n = 0; wait || n < 32; n++) {
+        regs.r[0] = (1 << 4) | (1 << 5);  /* pointer leaving/entering are polled directly */
+        regs.r[1] = (int)block;
+        regs.r[2] = (int)until_cs;
+        regs.r[3] = 0;
+        if (_kernel_swi(wait ? Wimp_PollIdle : Wimp_Poll, &regs, &regs) != NULL)
+            return;
+        if (RISCOS_WimpHandleEvent(_this, regs.r[0], block) == 0)
+            return;                         /* null event: done (or time is up) */
+        if (wait && driverdata->wimp_window == 0)
+            wait = SDL_FALSE;               /* went full screen while waiting */
+    }
+}
+
+/* 2026: the scroll wheel. RISC OS 5 doesn't deliver it to us as Wimp
+   Scroll_Request events, so read it directly: OS_Pointer 2 returns the
+   accumulated position of the "alternate positioning device" (the wheel),
+   R0 = X, R1 = Y, +ve Y = wheel pushed away (scroll up). This works both
+   in a window and in full screen. */
+static int riscos_wheel_x, riscos_wheel_y;
+static SDL_bool riscos_wheel_valid = SDL_FALSE;
+
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
+    if (!riscos_wheel_valid) {          /* first read: just take a baseline */
+        riscos_wheel_x = regs.r[0];
+        riscos_wheel_y = regs.r[1];
+        riscos_wheel_valid = SDL_TRUE;
+        return;
+    }
+    dx = regs.r[0] - riscos_wheel_x;
+    dy = regs.r[1] - riscos_wheel_y;
+    riscos_wheel_x = regs.r[0];
+    riscos_wheel_y = regs.r[1];
+    if (dx == 0 && dy == 0)
+        return;
+    /* Ignore a counter wrap or anything implausible. */
+    if (dx > 64 || dx < -64 || dy > 64 || dy < -64)
+        return;
+
+    /* Only while the pointer is over our window (always, in full screen). */
+    if (driverdata->wimp_window != 0 && !driverdata->pointer_in)
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
+    if (driverdata->wimp_task == 0 || driverdata->wimp_window == 0 ||
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
+    int block[64];
+    _kernel_swi_regs regs;
+
+    if (driverdata->wimp_task == 0 || driverdata->wimp_window == 0 ||
+        driverdata->wakeup_pollword == NULL || SDL_ThreadID() != driverdata->main_thread)
+        return -1;
+
+    now = RISCOS_MonotonicCs();
+    regs.r[0] = (1 << 4) | (1 << 22);     /* no Pointer_Leaving; scan the pollword */
+    if (timeout >= 0)
+        deadline = now + ((unsigned int)timeout + 9) / 10;
+    if (driverdata->pointer_in) {
+        until = now + 2;
+        if (timeout >= 0 && (int)(deadline - until) < 0)
+            until = deadline;
+    } else if (timeout >= 0) {
+        until = deadline;
+    } else {
+        regs.r[0] |= 1;                   /* no null events: sleep until an event */
+    }
+    regs.r[1] = (int)block;
+    regs.r[2] = (int)until;
+    regs.r[3] = (int)driverdata->wakeup_pollword;
+    if (_kernel_swi(Wimp_PollIdle, &regs, &regs) != NULL)
+        return -1;
+
+    if (RISCOS_WimpHandleEvent(_this, regs.r[0], block) == 0) {
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
+    /* Only multitask while we have a desktop window; full screen owns the machine. */
+    if (((SDL_VideoData *)_this->driverdata)->wimp_window != 0) {
+        RISCOS_PollWimp(_this);
+    }
     RISCOS_PollMouse(_this);
+    RISCOS_PollWheel(_this);
     RISCOS_PollKeyboard(_this);
 }
 
