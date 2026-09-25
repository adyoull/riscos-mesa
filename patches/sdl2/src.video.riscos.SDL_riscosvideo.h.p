diff --git src/video/riscos/SDL_riscosvideo.h src/video/riscos/SDL_riscosvideo.h
index db6c86e..14a5a76 100644
--- src/video/riscos/SDL_riscosvideo.h
+++ src/video/riscos/SDL_riscosvideo.h
@@ -31,8 +31,25 @@ typedef struct SDL_VideoData
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
+    int xeig, yeig;             /* cached eigen factors of the current mode */
+    int iconbar_icon;           /* icon bar icon handle, or -1 */
+    int gl_swap_interval;       /* 2026: OpenGL swap interval (0 or 1) */
 } SDL_VideoData;
 
+extern void RISCOS_ApplyPointerVisibility(_THIS);
+extern void RISCOS_WimpPlotWindow(_THIS, SDL_Window *window, int *block, int more);
+extern int RISCOS_WimpReadEig(int var);
+extern void RISCOS_UpdateEigs(_THIS);
+
 #endif /* SDL_riscosvideo_h_ */
 
 /* vi: set ts=4 sw=4 expandtab: */
