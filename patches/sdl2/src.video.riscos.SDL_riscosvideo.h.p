diff --git src/video/riscos/SDL_riscosvideo.h src/video/riscos/SDL_riscosvideo.h
--- src/video/riscos/SDL_riscosvideo.h
+++ src/video/riscos/SDL_riscosvideo.h
@@ -24,6 +24,7 @@
 #define SDL_riscosvideo_h_
 
 #include "../SDL_sysvideo.h"
+#include "SDL_riscoswimp.h"
 
 #define RISCOS_MAX_KEYS_PRESSED 6
 
@@ -31,8 +32,85 @@ typedef struct SDL_VideoData
 {
     int last_mouse_buttons;
     Uint8 key_pressed[RISCOS_MAX_KEYS_PRESSED];
+    Uint8 repeat_key;           /* 2026: the key that auto-repeats (the last one
+                                   pressed and still held), or 255 */
+    Uint32 repeat_due;          /* 2026: when it next repeats (SDL_GetTicks) */
+
+    /* 2026: windowed (Wimp) mode. Only one SDL window at a time can be a
+       desktop window (wimp_sdl_window); any other window is created full
+       screen, as the original driver did. Games only need one. */
+    int wimp_task;              /* task handle, or 0 if not a Wimp task */
+    int wimp_window;            /* Wimp window handle, or 0 in full screen (see RISCOS_IsWindowed) */
+    int wimp_open_x, wimp_open_y; /* where it opens (top left, OS units) when shown */
+    SDL_Window *wimp_sdl_window;
+    SDL_bool full_window;       /* 2026: wimp_window is a borderless screen-sized
+                                   "full window" (full screen that multitasks) */
+    SDL_bool pointer_in;        /* pointer is over our window */
+    SDL_bool has_caret;         /* we have the input focus */
+    SDL_bool cursor_hidden;     /* SDL asked for the pointer to be hidden */
+    int buttons_inside;         /* buttons pressed while over our window */
+    int pending_clicks;         /* Mouse_Click buttons not yet reported (short clicks) */
+    int pending_click_x, pending_click_y;   /* where the last of them happened (OS units) */
+    int xeig, yeig;             /* cached eigen factors of the current mode */
+    int iconbar_icon;           /* icon bar icon handle, or -1 */
+    int wscale_x, wscale_y;     /* screen pixels per SDL pixel in a desktop window */
+    char window_title[128];     /* title bar text (the Wimp reads it from here) */
+    int wheel_x, wheel_y;       /* last OS_Pointer 2 wheel position */
+    SDL_bool wheel_valid;       /* wheel_x/y have been read at least once */
+    int gl_swap_interval;       /* 2026: OpenGL swap interval (0 or 1) */
+    Uint32 gl_next_frame;       /* 2026: when the next paced GL frame is due (ms) */
+    SDL_threadID main_thread;   /* 2026: only this thread may call the Wimp */
+    volatile int *wakeup_pollword; /* 2026: Wimp pollword in the RMA (SDL_SendWakeupEvent) */
 } SDL_VideoData;
 
+/* 2026: SDL_TRUE when the program is running in a desktop window, SDL_FALSE
+   when it has the whole screen (single-tasking) or no window yet. */
+SDL_FORCE_INLINE SDL_bool
+RISCOS_IsWindowed(const SDL_VideoData *vdata)
+{
+    return (vdata->wimp_window != 0) ? SDL_TRUE : SDL_FALSE;
+}
+
+/* 2026: scale of a desktop window in screen pixels per SDL pixel: "1" never
+   scales, "2" to "4" always scale by that much; unset scales by 2 in high
+   resolution (EX0 EY0) modes. Can be set with SDL_SetHint or as the system
+   variable SDL_RISCOS_WINDOW_SCALE; SDL$WindowScale is still read if the
+   hint isn't set. */
+#define SDL_HINT_RISCOS_WINDOW_SCALE "SDL_RISCOS_WINDOW_SCALE"
+
+/* 2026: how full screen is done. A "full window" is a borderless Wimp
+   window the size of the screen, as RDPClient's full window mode: the
+   program stays a multitasking Wimp task (other tasks, TaskWindows
+   included, keep running, the icon bar pops up, windows can come in
+   front; a click brings the game back). The other kind owns the screen
+   and stops the desktop until it returns to a window (faster, single
+   tasking). Unset: SDL_WINDOW_FULLSCREEN_DESKTOP gives a full window and
+   SDL_WINDOW_FULLSCREEN (with a mode change) the single tasking kind.
+   "1": both give a full window (after Wimp_SetMode for
+   SDL_WINDOW_FULLSCREEN). "0": both single tasking, as before 2026-09-30.
+   Also read as the system variable of the same name. */
+#define SDL_HINT_RISCOS_FULLSCREEN_WINDOW "SDL_RISCOS_FULLSCREEN_WINDOW"
+
+/* 2026: OpenGL windows (riscos-mesa builds); each of these selects the
+   EGL path (SDL_riscosopengl.c). SDL_RISCOS_GL_RENDER_SIZE
+   "WxH": GL renders at WxH and the picture is stretched to fill the window
+   (or the screen); the program sees a WxH window (size, events, mouse).
+   SDL_RISCOS_GL_OVERLAY "1"/"0": ask for / refuse a hardware overlay
+   (EGL_RISCOS_overlay); unset, EGL$Overlay decides. Both can also be set as
+   system variables of those names, and are read when a window is made. */
+#define SDL_HINT_RISCOS_GL_RENDER_SIZE "SDL_RISCOS_GL_RENDER_SIZE"
+#define SDL_HINT_RISCOS_GL_OVERLAY "SDL_RISCOS_GL_OVERLAY"
+/* 2026: "1": GL windows use the EGL path even without the two above (the
+   render size or "1" for the overlay also select it; otherwise GL renders
+   into the window's sprite, as before) */
+#define SDL_HINT_RISCOS_GL_EGL "SDL_RISCOS_GL_EGL"
+
+extern void RISCOS_ApplyPointerVisibility(_THIS);
+extern void RISCOS_WimpPlotWindow(_THIS, SDL_Window *window, RISCOS_Redraw *redraw, int more);
+extern int RISCOS_WimpReadEig(int var);
+extern void RISCOS_UpdateEigs(_THIS);
+extern void RISCOS_ChooseWindowScale(_THIS, SDL_Window *window);
+
 #endif /* SDL_riscosvideo_h_ */
 
 /* vi: set ts=4 sw=4 expandtab: */
