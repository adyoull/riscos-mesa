diff --git src/video/riscos/SDL_riscosmodes.h src/video/riscos/SDL_riscosmodes.h
--- src/video/riscos/SDL_riscosmodes.h
+++ src/video/riscos/SDL_riscosmodes.h
@@ -1,6 +1,6 @@
 /*
   Simple DirectMedia Layer
-  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>
+  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>
 
   This software is provided 'as-is', without any express or implied
   warranty.  In no event will the authors be held liable for any damages
@@ -24,9 +24,12 @@
 #define SDL_riscosmodes_h_
 
 extern int RISCOS_InitModes(_THIS);
-extern void RISCOS_GetDisplayModes(_THIS, SDL_VideoDisplay *display);
-extern int RISCOS_SetDisplayMode(_THIS, SDL_VideoDisplay *display,
-                                 SDL_DisplayMode *mode);
+/* 2026: the desktop's mode changed (Message_ModeChange): SDL's desktop
+   display mode follows it, unless SDL itself has set a mode */
+extern void RISCOS_DesktopModeChanged(_THIS);
+extern void RISCOS_GetDisplayModes(_THIS, SDL_VideoDisplay * display);
+extern int RISCOS_SetDisplayMode(_THIS, SDL_VideoDisplay * display,
+                                 SDL_DisplayMode * mode);
 
 #endif /* SDL_riscosmodes_h_ */
 
