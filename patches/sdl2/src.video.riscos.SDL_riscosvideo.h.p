diff --git src/video/riscos/SDL_riscosvideo.h src/video/riscos/SDL_riscosvideo.h
index db6c86e..78b3057 100644
--- src/video/riscos/SDL_riscosvideo.h
+++ src/video/riscos/SDL_riscosvideo.h
@@ -31,8 +31,31 @@ typedef struct SDL_VideoData
 {
     int last_mouse_buttons;
     Uint8 key_pressed[RISCOS_MAX_KEYS_PRESSED];
+
+    /* 2026: windowed (Wimp) mode */
+    int wimp_task;              /* task handle, or 0 if not a Wimp task */
+    int wimp_window;            /* window handle of the windowed SDL window */
+    SDL_Window *wimp_sdl_window;
+    SDL_bool pointer_in;        /* pointer is over our window */
+    SDL_bool has_caret;         /* we have the input focus */
+    SDL_bool cursor_hidden;     /* SDL asked for the pointer to be hidden */
+    int buttons_inside;         /* buttons pressed while over our window */
+    int pending_clicks;         /* Mouse_Click buttons not yet reported (short clicks) */
+    int xeig, yeig;             /* cached eigen factors of the current mode */
+    int iconbar_icon;           /* icon bar icon handle, or -1 */
+    int wscale_x, wscale_y;     /* screen pixels per SDL pixel in a desktop window */
+    int gl_swap_interval;       /* 2026: OpenGL swap interval (0 or 1) */
+    Uint32 gl_next_frame;       /* 2026: when the next paced GL frame is due (ms) */
+    SDL_threadID main_thread;   /* 2026: only this thread may call the Wimp */
+    volatile int *wakeup_pollword; /* 2026: Wimp pollword in the RMA (SDL_SendWakeupEvent) */
 } SDL_VideoData;
 
+extern void RISCOS_ApplyPointerVisibility(_THIS);
+extern void RISCOS_WimpPlotWindow(_THIS, SDL_Window *window, int *block, int more);
+extern int RISCOS_WimpReadEig(int var);
+extern void RISCOS_UpdateEigs(_THIS);
+extern void RISCOS_ChooseWindowScale(_THIS, SDL_Window *window);
+
 #endif /* SDL_riscosvideo_h_ */
 
 /* vi: set ts=4 sw=4 expandtab: */
