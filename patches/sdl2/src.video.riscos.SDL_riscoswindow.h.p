diff --git src/video/riscos/SDL_riscoswindow.h src/video/riscos/SDL_riscoswindow.h
index d713b7a..cc322c9 100644
--- src/video/riscos/SDL_riscoswindow.h
+++ src/video/riscos/SDL_riscoswindow.h
@@ -30,10 +30,23 @@ typedef struct
     SDL_Window *window;
     sprite_area *fb_area;
     sprite_header *fb_sprite;
+    int fb_direct;              /* 2026: framebuffer is the screen itself */
+    /* 2026: OpenGL (OSMesa). When gl_active, fb_area/fb_sprite is the GL
+       render target, owned by SDL_riscosopengl.c. */
+    int gl_active;
+    int gl_w, gl_h;
+    void *gl_sprite_mode;
 } SDL_WindowData;
 
 extern int RISCOS_CreateWindow(_THIS, SDL_Window * window);
 extern void RISCOS_DestroyWindow(_THIS, SDL_Window * window);
+extern void RISCOS_SetWindowSize(_THIS, SDL_Window * window);
+extern void RISCOS_SetWindowFullscreen(_THIS, SDL_Window * window, SDL_VideoDisplay * display, SDL_bool fullscreen);
+extern void RISCOS_SetWindowTitle(_THIS, SDL_Window * window);
+extern int RISCOS_WimpStart(_THIS);
+extern void RISCOS_WimpQuit(_THIS);
+/* 2026: the program's name, from its application directory (see window.c) */
+extern const char *RISCOS_AppName(void);
 extern SDL_bool RISCOS_GetWindowWMInfo(_THIS, SDL_Window * window,
                                     struct SDL_SysWMinfo *info);
 
