diff --git src/video/riscos/SDL_riscosvideo.c src/video/riscos/SDL_riscosvideo.c
index 4763011..574bda2 100644
--- src/video/riscos/SDL_riscosvideo.c
+++ src/video/riscos/SDL_riscosvideo.c
@@ -34,9 +34,23 @@
 #include "SDL_riscosmouse.h"
 #include "SDL_riscosmodes.h"
 #include "SDL_riscoswindow.h"
+#include "SDL_riscosopengl.h"
+
+#include <kernel.h>
+#include <swis.h>
 
 #define RISCOSVID_DRIVER_NAME "riscos"
 
+/* 2026: cooperative SDL_Delay (see SDL_systimer.c and RISCOS_WimpDelay) */
+extern int (*SDL_RISCOS_DelayHook)(Uint32 ms);
+
+static int
+RISCOS_DelayHook(Uint32 ms)
+{
+    SDL_VideoDevice *device = SDL_GetVideoDevice();
+    return (device && RISCOS_WimpDelay(device, ms)) ? 1 : 0;
+}
+
 /* Initialization/Query functions */
 static int RISCOS_VideoInit(_THIS);
 static void RISCOS_VideoQuit(_THIS);
@@ -77,18 +91,36 @@ RISCOS_CreateDevice(void)
     device->VideoInit = RISCOS_VideoInit;
     device->VideoQuit = RISCOS_VideoQuit;
     device->PumpEvents = RISCOS_PumpEvents;
+    device->WaitEventTimeout = RISCOS_WaitEventTimeout;   /* 2026: idle at 0% CPU */
+    device->SendWakeupEvent = RISCOS_SendWakeupEvent;
 
     device->GetDisplayModes = RISCOS_GetDisplayModes;
     device->SetDisplayMode = RISCOS_SetDisplayMode;
 
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
@@ -113,6 +145,34 @@ RISCOS_VideoInit(_THIS)
     if (RISCOS_InitModes(_this) < 0) {
         return -1;
     }
+    RISCOS_UpdateEigs(_this);
+
+    ((SDL_VideoData *) _this->driverdata)->main_thread = SDL_ThreadID();
+
+    /* The Wimp reads the pollword while other tasks are paged in, so it
+       must live in the RMA, not in application space. */
+    {
+        _kernel_swi_regs regs;
+        regs.r[0] = 6;                    /* OS_Module 6: claim RMA */
+        regs.r[3] = 4;
+        if (_kernel_swi(OS_Module, &regs, &regs) == NULL) {
+            volatile int *pw = (volatile int *) regs.r[2];
+            *pw = 0;
+            ((SDL_VideoData *) _this->driverdata)->wakeup_pollword = pw;
+        }
+    }
+    SDL_RISCOS_DelayHook = RISCOS_DelayHook;
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
@@ -121,7 +181,16 @@ RISCOS_VideoInit(_THIS)
 static void
 RISCOS_VideoQuit(_THIS)
 {
+    SDL_RISCOS_DelayHook = NULL;
     RISCOS_QuitEvents(_this);
+    RISCOS_WimpQuit(_this);
+    if (((SDL_VideoData *) _this->driverdata)->wakeup_pollword) {
+        _kernel_swi_regs regs;
+        regs.r[0] = 7;                    /* OS_Module 7: free RMA */
+        regs.r[2] = (int) ((SDL_VideoData *) _this->driverdata)->wakeup_pollword;
+        _kernel_swi(OS_Module, &regs, &regs);
+        ((SDL_VideoData *) _this->driverdata)->wakeup_pollword = NULL;
+    }
 }
 
 #endif /* SDL_VIDEO_DRIVER_RISCOS */
