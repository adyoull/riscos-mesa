diff --git src/video/riscos/SDL_riscosmodes.c src/video/riscos/SDL_riscosmodes.c
index 9500b22..df73e42 100644
--- src/video/riscos/SDL_riscosmodes.c
+++ src/video/riscos/SDL_riscosmodes.c
@@ -293,9 +293,16 @@ RISCOS_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
     _kernel_oserror *error;
     int i;
 
-    regs.r[0] = 0;
-    regs.r[1] = (int)mode->driverdata;
-    error = _kernel_swi(OS_ScreenMode, &regs, &regs);
+    if (((SDL_VideoData *) _this->driverdata)->wimp_task != 0) {
+        /* Fix 13: never change mode behind the Wimp's back; Wimp_SetMode
+           broadcasts the change so the desktop redraws properly. */
+        regs.r[0] = (int)mode->driverdata;
+        error = _kernel_swi(Wimp_SetMode, &regs, &regs);
+    } else {
+        regs.r[0] = 0;
+        regs.r[1] = (int)mode->driverdata;
+        error = _kernel_swi(OS_ScreenMode, &regs, &regs);
+    }
     if (error != NULL) {
         return SDL_SetError("Unable to set the current screen mode: %s (%i)", error->errmess, error->errnum);
     }
@@ -308,6 +315,8 @@ RISCOS_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
     /* Update cursor visibility, since it may have been disabled by the mode change. */
     SDL_SetCursor(NULL);
 
+    RISCOS_UpdateEigs(_this);
+
     return 0;
 }
 
