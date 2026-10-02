diff --git src/video/riscos/SDL_riscosmodes.h src/video/riscos/SDL_riscosmodes.h
--- src/video/riscos/SDL_riscosmodes.h
+++ src/video/riscos/SDL_riscosmodes.h
@@ -24,6 +24,9 @@
 #define SDL_riscosmodes_h_
 
 extern int RISCOS_InitModes(_THIS);
+/* 2026: the desktop's mode changed (Message_ModeChange): SDL's desktop
+   display mode follows it, unless SDL itself has set a mode */
+extern void RISCOS_DesktopModeChanged(_THIS);
 extern void RISCOS_GetDisplayModes(_THIS, SDL_VideoDisplay * display);
 extern int RISCOS_SetDisplayMode(_THIS, SDL_VideoDisplay * display,
                                  SDL_DisplayMode * mode);
