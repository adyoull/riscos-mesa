diff --git src/video/riscos/SDL_riscosmodes.c src/video/riscos/SDL_riscosmodes.c
--- src/video/riscos/SDL_riscosmodes.c
+++ src/video/riscos/SDL_riscosmodes.c
@@ -1,6 +1,6 @@
 /*
   Simple DirectMedia Layer
-  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>
+  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>
 
   This software is provided 'as-is', without any express or implied
   warranty.  In no event will the authors be held liable for any damages
@@ -20,7 +20,7 @@
 */
 #include "../../SDL_internal.h"
 
-#ifdef SDL_VIDEO_DRIVER_RISCOS
+#if SDL_VIDEO_DRIVER_RISCOS
 
 #include "../SDL_sysvideo.h"
 #include "../../events/SDL_mouse_c.h"
@@ -31,8 +31,7 @@
 #include <kernel.h>
 #include <swis.h>
 
-enum
-{
+enum {
     MODE_FLAG_565 = 1 << 7,
 
     MODE_FLAG_COLOUR_SPACE = 0xF << 12,
@@ -43,8 +42,7 @@ enum
     MODE_FLAG_ARGB = MODE_FLAG_TRGB | MODE_FLAG_ABGR
 };
 
-static const struct
-{
+static const struct {
     SDL_PixelFormatEnum pixel_format;
     int modeflags, ncolour, log2bpp;
 } mode_to_pixelformat[] = {
@@ -52,32 +50,33 @@ static const struct
     /* { SDL_PIXELFORMAT_INDEX2LSB, 0, 3, 1 }, */
     /* { SDL_PIXELFORMAT_INDEX4LSB, 0, 15, 2 }, */
     /* { SDL_PIXELFORMAT_INDEX8,    MODE_FLAG_565, 255, 3 }, */
-    { SDL_PIXELFORMAT_XBGR1555, MODE_FLAG_TBGR, 65535, 4 },
-    { SDL_PIXELFORMAT_XRGB1555, MODE_FLAG_TRGB, 65535, 4 },
-    { SDL_PIXELFORMAT_ABGR1555, MODE_FLAG_ABGR, 65535, 4 },
-    { SDL_PIXELFORMAT_ARGB1555, MODE_FLAG_ARGB, 65535, 4 },
-    { SDL_PIXELFORMAT_XBGR4444, MODE_FLAG_TBGR, 4095, 4 },
-    { SDL_PIXELFORMAT_XRGB4444, MODE_FLAG_TRGB, 4095, 4 },
-    { SDL_PIXELFORMAT_ABGR4444, MODE_FLAG_ABGR, 4095, 4 },
-    { SDL_PIXELFORMAT_ARGB4444, MODE_FLAG_ARGB, 4095, 4 },
-    { SDL_PIXELFORMAT_BGR565, MODE_FLAG_TBGR | MODE_FLAG_565, 65535, 4 },
-    { SDL_PIXELFORMAT_RGB565, MODE_FLAG_TRGB | MODE_FLAG_565, 65535, 4 },
-    { SDL_PIXELFORMAT_BGR24, MODE_FLAG_TBGR, 16777215, 6 },
-    { SDL_PIXELFORMAT_RGB24, MODE_FLAG_TRGB, 16777215, 6 },
-    { SDL_PIXELFORMAT_XBGR8888, MODE_FLAG_TBGR, -1, 5 },
-    { SDL_PIXELFORMAT_XRGB8888, MODE_FLAG_TRGB, -1, 5 },
-    { SDL_PIXELFORMAT_ABGR8888, MODE_FLAG_ABGR, -1, 5 },
-    { SDL_PIXELFORMAT_ARGB8888, MODE_FLAG_ARGB, -1, 5 }
+    { SDL_PIXELFORMAT_XBGR1555,  MODE_FLAG_TBGR, 65535, 4 },
+    { SDL_PIXELFORMAT_XRGB1555,  MODE_FLAG_TRGB, 65535, 4 },
+    { SDL_PIXELFORMAT_ABGR1555,  MODE_FLAG_ABGR, 65535, 4 },
+    { SDL_PIXELFORMAT_ARGB1555,  MODE_FLAG_ARGB, 65535, 4 },
+    { SDL_PIXELFORMAT_XBGR4444,  MODE_FLAG_TBGR, 4095, 4 },
+    { SDL_PIXELFORMAT_XRGB4444,  MODE_FLAG_TRGB, 4095, 4 },
+    { SDL_PIXELFORMAT_ABGR4444,  MODE_FLAG_ABGR, 4095, 4 },
+    { SDL_PIXELFORMAT_ARGB4444,  MODE_FLAG_ARGB, 4095, 4 },
+    { SDL_PIXELFORMAT_BGR565,    MODE_FLAG_TBGR | MODE_FLAG_565, 65535, 4 },
+    { SDL_PIXELFORMAT_RGB565,    MODE_FLAG_TRGB | MODE_FLAG_565, 65535, 4 },
+    { SDL_PIXELFORMAT_BGR24,     MODE_FLAG_TBGR, 16777215, 6 },
+    { SDL_PIXELFORMAT_RGB24,     MODE_FLAG_TRGB, 16777215, 6 },
+    { SDL_PIXELFORMAT_XBGR8888,  MODE_FLAG_TBGR, -1, 5 },
+    { SDL_PIXELFORMAT_XRGB8888,  MODE_FLAG_TRGB, -1, 5 },
+    { SDL_PIXELFORMAT_ABGR8888,  MODE_FLAG_ABGR, -1, 5 },
+    { SDL_PIXELFORMAT_ARGB8888,  MODE_FLAG_ARGB, -1, 5 }
 };
 
-static SDL_PixelFormatEnum RISCOS_ModeToPixelFormat(int ncolour, int modeflags, int log2bpp)
+static SDL_PixelFormatEnum
+RISCOS_ModeToPixelFormat(int ncolour, int modeflags, int log2bpp)
 {
     int i;
 
     for (i = 0; i < SDL_arraysize(mode_to_pixelformat); i++) {
         if (log2bpp == mode_to_pixelformat[i].log2bpp &&
-            (ncolour == mode_to_pixelformat[i].ncolour || ncolour == 0) &&
-            (modeflags & (MODE_FLAG_565 | MODE_FLAG_COLOUR_SPACE)) == mode_to_pixelformat[i].modeflags) {
+           (ncolour == mode_to_pixelformat[i].ncolour || ncolour == 0) &&
+           (modeflags & (MODE_FLAG_565 | MODE_FLAG_COLOUR_SPACE)) == mode_to_pixelformat[i].modeflags) {
             return mode_to_pixelformat[i].pixel_format;
         }
     }
@@ -85,10 +84,11 @@ static SDL_PixelFormatEnum RISCOS_ModeToPixelFormat(int ncolour, int modeflags,
     return SDL_PIXELFORMAT_UNKNOWN;
 }
 
-static size_t measure_mode_block(const int *block)
+static size_t
+measure_mode_block(const int *block)
 {
     size_t blockSize = ((block[0] & 0xFF) == 3) ? 7 : 5;
-    while (block[blockSize] != -1) {
+    while(block[blockSize] != -1) {
         blockSize += 2;
     }
     blockSize++;
@@ -96,7 +96,8 @@ static size_t measure_mode_block(const int *block)
     return blockSize * 4;
 }
 
-static int read_mode_variable(int *block, int var)
+static int
+read_mode_variable(int *block, int var)
 {
     _kernel_swi_regs regs;
     regs.r[0] = (int)block;
@@ -105,7 +106,8 @@ static int read_mode_variable(int *block, int var)
     return regs.r[2];
 }
 
-static SDL_bool read_mode_block(int *block, SDL_DisplayMode *mode, SDL_bool extended)
+static SDL_bool
+read_mode_block(int *block, SDL_DisplayMode *mode, SDL_bool extended)
 {
     int xres, yres, ncolour, modeflags, log2bpp, rate;
 
@@ -143,7 +145,8 @@ static SDL_bool read_mode_block(int *block, SDL_DisplayMode *mode, SDL_bool exte
     return SDL_TRUE;
 }
 
-static void *convert_mode_block(const int *block)
+static void *
+convert_mode_block(const int *block)
 {
     int xres, yres, log2bpp, rate, ncolour = 0, modeflags = 0;
     size_t pos = 0;
@@ -188,7 +191,8 @@ static void *convert_mode_block(const int *block)
     return dst;
 }
 
-static void *copy_memory(const void *src, size_t size, size_t alloc)
+static void *
+copy_memory(const void *src, size_t size, size_t alloc)
 {
     void *dst = SDL_malloc(alloc);
     if (dst) {
@@ -197,7 +201,8 @@ static void *copy_memory(const void *src, size_t size, size_t alloc)
     return dst;
 }
 
-int RISCOS_InitModes(_THIS)
+int
+RISCOS_InitModes(_THIS)
 {
     SDL_DisplayMode mode;
     int *current_mode;
@@ -207,7 +212,7 @@ int RISCOS_InitModes(_THIS)
 
     regs.r[0] = 1;
     error = _kernel_swi(OS_ScreenMode, &regs, &regs);
-    if (error) {
+    if (error != NULL) {
         return SDL_SetError("Unable to retrieve the current screen mode: %s (%i)", error->errmess, error->errnum);
     }
 
@@ -225,7 +230,41 @@ int RISCOS_InitModes(_THIS)
     return SDL_AddBasicVideoDisplay(&mode);
 }
 
-void RISCOS_GetDisplayModes(_THIS, SDL_VideoDisplay *display)
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
+void
+RISCOS_GetDisplayModes(_THIS, SDL_VideoDisplay * display)
 {
     SDL_DisplayMode mode;
     _kernel_swi_regs regs;
@@ -237,7 +276,7 @@ void RISCOS_GetDisplayModes(_THIS, SDL_VideoDisplay *display)
     regs.r[6] = 0;
     regs.r[7] = 0;
     error = _kernel_swi(OS_ScreenMode, &regs, &regs);
-    if (error) {
+    if (error != NULL) {
         SDL_SetError("Unable to enumerate screen modes: %s (%i)", error->errmess, error->errnum);
         return;
     }
@@ -251,7 +290,7 @@ void RISCOS_GetDisplayModes(_THIS, SDL_VideoDisplay *display)
     regs.r[6] = (int)block;
     regs.r[7] = -regs.r[7];
     error = _kernel_swi(OS_ScreenMode, &regs, &regs);
-    if (error) {
+    if (error != NULL) {
         SDL_free(block);
         SDL_SetError("Unable to enumerate screen modes: %s (%i)", error->errmess, error->errnum);
         return;
@@ -262,9 +301,8 @@ void RISCOS_GetDisplayModes(_THIS, SDL_VideoDisplay *display)
             continue;
         }
 
-        if (mode.format == SDL_PIXELFORMAT_UNKNOWN) {
+        if (mode.format == SDL_PIXELFORMAT_UNKNOWN)
             continue;
-        }
 
         mode.driverdata = convert_mode_block(pos + 4);
         if (!mode.driverdata) {
@@ -280,17 +318,25 @@ void RISCOS_GetDisplayModes(_THIS, SDL_VideoDisplay *display)
     SDL_free(block);
 }
 
-int RISCOS_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
+int
+RISCOS_SetDisplayMode(_THIS, SDL_VideoDisplay * display, SDL_DisplayMode * mode)
 {
     const char disable_cursor[] = { 23, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
     _kernel_swi_regs regs;
     _kernel_oserror *error;
     int i;
 
-    regs.r[0] = 0;
-    regs.r[1] = (int)mode->driverdata;
-    error = _kernel_swi(OS_ScreenMode, &regs, &regs);
-    if (error) {
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
+    if (error != NULL) {
         return SDL_SetError("Unable to set the current screen mode: %s (%i)", error->errmess, error->errnum);
     }
 
@@ -302,6 +348,8 @@ int RISCOS_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mod
     /* Update cursor visibility, since it may have been disabled by the mode change. */
     SDL_SetCursor(NULL);
 
+    RISCOS_UpdateEigs(_this);
+
     return 0;
 }
 
