diff --git src/video/riscos/SDL_riscosmodes.c src/video/riscos/SDL_riscosmodes.c
--- src/video/riscos/SDL_riscosmodes.c
+++ src/video/riscos/SDL_riscosmodes.c
@@ -230,6 +230,39 @@ RISCOS_InitModes(_THIS)
     return SDL_AddBasicVideoDisplay(&mode);
 }
 
+void
+RISCOS_DesktopModeChanged(_THIS)
+{
+    SDL_VideoDisplay *display;
+    SDL_DisplayMode mode;
+    _kernel_swi_regs regs;
+    int *current_mode;
+    void *old;
+    size_t size;
+
+    if (_this->num_displays < 1)
+        return;
+    display = &_this->displays[0];
+    /* SDL's own mode (full screen with a mode change) isn't the desktop's:
+       the desktop mode is what it goes back to */
+    if (display->current_mode.driverdata != display->desktop_mode.driverdata)
+        return;
+    regs.r[0] = 1;
+    if (_kernel_swi(OS_ScreenMode, &regs, &regs) != NULL)
+        return;
+    current_mode = (int *)regs.r[1];
+    if (!read_mode_block(current_mode, &mode, SDL_TRUE))
+        return;
+    size = measure_mode_block(current_mode);
+    mode.driverdata = copy_memory(current_mode, size, size);
+    if (!mode.driverdata)
+        return;
+    old = display->desktop_mode.driverdata;
+    display->desktop_mode = mode;
+    display->current_mode = mode;
+    SDL_free(old);
+}
+
 void
 RISCOS_GetDisplayModes(_THIS, SDL_VideoDisplay * display)
 {
@@ -293,9 +326,16 @@ RISCOS_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
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
@@ -308,6 +348,8 @@ RISCOS_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
     /* Update cursor visibility, since it may have been disabled by the mode change. */
     SDL_SetCursor(NULL);
 
+    RISCOS_UpdateEigs(_this);
+
     return 0;
 }
 
