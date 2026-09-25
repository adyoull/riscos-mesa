diff --git src/video/riscos/SDL_riscosvideo.c src/video/riscos/SDL_riscosvideo.c
index 4763011..bd9e666 100644
--- src/video/riscos/SDL_riscosvideo.c
+++ src/video/riscos/SDL_riscosvideo.c
@@ -34,6 +34,10 @@
 #include "SDL_riscosmouse.h"
 #include "SDL_riscosmodes.h"
 #include "SDL_riscoswindow.h"
+#include "SDL_riscosopengl.h"
+
+#include <kernel.h>
+#include <swis.h>
 
 #define RISCOSVID_DRIVER_NAME "riscos"
 
@@ -83,12 +87,28 @@ RISCOS_CreateDevice(void)
 
     device->CreateSDLWindow = RISCOS_CreateWindow;
     device->DestroyWindow = RISCOS_DestroyWindow;
+    device->SetWindowSize = RISCOS_SetWindowSize;
+    device->SetWindowFullscreen = RISCOS_SetWindowFullscreen;
+    device->SetWindowTitle = RISCOS_SetWindowTitle;
     device->GetWindowWMInfo = RISCOS_GetWindowWMInfo;
 
     device->CreateWindowFramebuffer = RISCOS_CreateWindowFramebuffer;
     device->UpdateWindowFramebuffer = RISCOS_UpdateWindowFramebuffer;
     device->DestroyWindowFramebuffer = RISCOS_DestroyWindowFramebuffer;
 
+#if SDL_VIDEO_OPENGL_OSMESA
+    /* 2026: software OpenGL via Mesa OSMesa */
+    device->GL_LoadLibrary = RISCOS_GL_LoadLibrary;
+    device->GL_GetProcAddress = RISCOS_GL_GetProcAddress;
+    device->GL_UnloadLibrary = RISCOS_GL_UnloadLibrary;
+    device->GL_CreateContext = RISCOS_GL_CreateContext;
+    device->GL_MakeCurrent = RISCOS_GL_MakeCurrent;
+    device->GL_SetSwapInterval = RISCOS_GL_SetSwapInterval;
+    device->GL_GetSwapInterval = RISCOS_GL_GetSwapInterval;
+    device->GL_SwapWindow = RISCOS_GL_SwapWindow;
+    device->GL_DeleteContext = RISCOS_GL_DeleteContext;
+#endif
+
     device->free = RISCOS_DeleteDevice;
 
     return device;
@@ -113,6 +133,18 @@ RISCOS_VideoInit(_THIS)
     if (RISCOS_InitModes(_this) < 0) {
         return -1;
     }
+    RISCOS_UpdateEigs(_this);
+
+    /* Fix 13: if the desktop is running (Wimp_ReadSysInfo 0 = number of
+       active tasks), become a Wimp task now and stay one until we quit,
+       whether we run in a window or full screen. */
+    {
+        _kernel_swi_regs regs;
+        regs.r[0] = 0;
+        if (_kernel_swi(Wimp_ReadSysInfo, &regs, &regs) == NULL && regs.r[0] != 0) {
+            RISCOS_WimpStart(_this);
+        }
+    }
 
     /* We're done! */
     return 0;
@@ -122,6 +154,7 @@ static void
 RISCOS_VideoQuit(_THIS)
 {
     RISCOS_QuitEvents(_this);
+    RISCOS_WimpQuit(_this);
 }
 
 #endif /* SDL_VIDEO_DRIVER_RISCOS */
