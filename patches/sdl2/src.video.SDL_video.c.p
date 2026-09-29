diff --git src/video/SDL_video.c src/video/SDL_video.c
index 0804c9c..ad78b2e 100644
--- src/video/SDL_video.c
+++ src/video/SDL_video.c
@@ -24,6 +24,10 @@
 
 #include "SDL.h"
 #include "SDL_video.h"
+#if SDL_VIDEO_DRIVER_RISCOS
+/* riscos-mesa: see SDL_UpdateFullscreenMode */
+extern SDL_bool RISCOS_KeepsRenderSize(SDL_Window *window);
+#endif
 #include "SDL_sysvideo.h"
 #include "SDL_blit.h"
 #include "SDL_pixels_c.h"
@@ -1481,6 +1485,15 @@ SDL_UpdateFullscreenMode(SDL_Window * window, SDL_bool fullscreen)
                 if (_this->SetWindowFullscreen) {
                     _this->SetWindowFullscreen(_this, other, display, SDL_TRUE);
                 }
+#if SDL_VIDEO_DRIVER_RISCOS
+                /* riscos-mesa: a GL window with a render size
+                   (SDL_RISCOS_GL_RENDER_SIZE) keeps that size full screen,
+                   stretched to the screen; the driver has set it. */
+                if (RISCOS_KeepsRenderSize(other)) {
+                    fullscreen_mode.w = other->w;
+                    fullscreen_mode.h = other->h;
+                }
+#endif
                 display->fullscreen_window = other;
 
                 /* Generate a mode change event here */
