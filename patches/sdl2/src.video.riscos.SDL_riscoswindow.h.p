diff --git src/video/riscos/SDL_riscoswindow.h src/video/riscos/SDL_riscoswindow.h
--- src/video/riscos/SDL_riscoswindow.h
+++ src/video/riscos/SDL_riscoswindow.h
@@ -30,10 +30,56 @@ typedef struct
     SDL_Window *window;
     sprite_area *fb_area;
     sprite_header *fb_sprite;
+    int fb_direct;              /* 2026: framebuffer is the screen itself */
+    /* 2026: OpenGL (SDL_riscosopengl.c). gl_active: a GL window. The
+       sprite path: fb_area/fb_sprite is the GL render target (gl_w, gl_h,
+       gl_sprite_mode). The EGL path (gl_egl): an EGL window surface. */
+    int gl_active;
+    int gl_w, gl_h;
+    void *gl_sprite_mode;
+    int gl_egl;                 /* shown by EGL (it stretches it full screen) */
+    void *egl_surface;          /* EGLSurface, or NULL */
+    void *egl_config;           /* the EGLConfig it was made with */
+    int egl_handle;             /* its native window: the Wimp window, or -1 (the screen) */
+    int egl_rw, egl_rh;         /* the render size it was given */
+    int gl_pending;             /* a finished frame waits for a vsync (overlay) */
+    /* 2026: render size (SDL_HINT_RISCOS_GL_RENDER_SIZE). When render_w is
+       set, window->w/h is the render size (what the program sees) and
+       disp_w/h is the size the program asked for: the desktop window's
+       size, before any wscale. */
+    int render_w, render_h;
+    int disp_w, disp_h;
 } SDL_WindowData;
 
+/* 2026: the size a window is shown at in SDL pixels (before wscale): the
+   size the program asked for when a render size is in use, else its own. */
+SDL_FORCE_INLINE int
+RISCOS_ShownW(SDL_Window *window)
+{
+    const SDL_WindowData *d = (const SDL_WindowData *) window->driverdata;
+    return (d && d->render_w && d->disp_w > 0) ? d->disp_w : window->w;
+}
+SDL_FORCE_INLINE int
+RISCOS_ShownH(SDL_Window *window)
+{
+    const SDL_WindowData *d = (const SDL_WindowData *) window->driverdata;
+    return (d && d->render_w && d->disp_h > 0) ? d->disp_h : window->h;
+}
+/* 2026: SDL_video.c asks whether a window keeps its render size full
+   screen (instead of taking the screen mode's size). */
+extern SDL_bool RISCOS_KeepsRenderSize(SDL_Window *window);
+
 extern int RISCOS_CreateWindow(_THIS, SDL_Window * window);
 extern void RISCOS_DestroyWindow(_THIS, SDL_Window * window);
+extern void RISCOS_SetWindowSize(_THIS, SDL_Window * window);
+extern void RISCOS_SetWindowFullscreen(_THIS, SDL_Window * window, SDL_VideoDisplay * display, SDL_bool fullscreen);
+extern void RISCOS_SetWindowTitle(_THIS, SDL_Window * window);
+extern void RISCOS_ShowWindow(_THIS, SDL_Window * window);
+extern void RISCOS_HideWindow(_THIS, SDL_Window * window);
+extern int RISCOS_WimpStart(_THIS);
+extern void RISCOS_WimpQuit(_THIS);
+/* 2026: the program's name, from its application directory (see window.c) */
+extern const char *RISCOS_AppName(void);
 extern SDL_bool RISCOS_GetWindowWMInfo(_THIS, SDL_Window * window,
                                     struct SDL_SysWMinfo *info);
 
