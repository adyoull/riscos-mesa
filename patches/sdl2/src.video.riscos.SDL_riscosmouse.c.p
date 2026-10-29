diff --git src/video/riscos/SDL_riscosmouse.c src/video/riscos/SDL_riscosmouse.c
--- src/video/riscos/SDL_riscosmouse.c
+++ src/video/riscos/SDL_riscosmouse.c
@@ -23,6 +23,9 @@
 #if SDL_VIDEO_DRIVER_RISCOS
 
 #include "../../events/SDL_mouse_c.h"
+#include "../SDL_sysvideo.h"
+#include "SDL_riscosvideo.h"
+#include "SDL_riscosevents_c.h"
 
 #include <kernel.h>
 
@@ -49,20 +52,60 @@ RISCOS_FreeCursor(SDL_Cursor * cursor)
     SDL_free(cursor);
 }
 
+/* 2026: in windowed mode only hide the pointer while it is over our window. */
+void
+RISCOS_ApplyPointerVisibility(_THIS)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    SDL_bool hide = vdata->cursor_hidden;
+
+    if (RISCOS_IsWindowed(vdata) && !vdata->pointer_in)
+        hide = SDL_FALSE;
+    _kernel_osbyte(106, hide ? 0 : 1, 0);
+}
+
 static int
 RISCOS_ShowCursor(SDL_Cursor * cursor)
 {
-    if (cursor) {
-        /* Turn the mouse pointer on */
-        _kernel_osbyte(106, 1, 0);
+    SDL_VideoDevice *_this = SDL_GetVideoDevice();
+
+    if (_this && _this->driverdata) {
+        ((SDL_VideoData *) _this->driverdata)->cursor_hidden = cursor ? SDL_FALSE : SDL_TRUE;
+        RISCOS_ApplyPointerVisibility(_this);
     } else {
-        /* Turn the mouse pointer off */
-        _kernel_osbyte(106, 0, 0);
+        _kernel_osbyte(106, cursor ? 1 : 0, 0);
     }
 
     return 0;
 }
 
+/* 2026: SDL_WarpMouseInWindow: OS_Word 21,3 (see RISCOS_WarpPointer) */
+static void
+RISCOS_WarpMouse(SDL_Window * window, int x, int y)
+{
+    SDL_VideoDevice *_this = SDL_GetVideoDevice();
+    if (_this && _this->driverdata)
+        RISCOS_WarpPointer(_this, window, x, y);
+}
+
+/* 2026: relative mouse mode (SDL_SetRelativeMouseMode): the pointer is
+   held in the middle of the window and each poll reports how far it moved
+   (RISCOS_PollRelativeMotion in SDL_riscosevents.c). SDL hides the pointer
+   itself. */
+static int
+RISCOS_SetRelativeMouseMode(SDL_bool enabled)
+{
+    SDL_VideoDevice *_this = SDL_GetVideoDevice();
+    SDL_VideoData *vdata = _this ? (SDL_VideoData *) _this->driverdata : NULL;
+    if (!vdata)
+        return SDL_Unsupported();
+    vdata->relative_mouse = enabled;
+    vdata->rel_rest_x = vdata->rel_rest_y = 0;
+    if (enabled)
+        RISCOS_CentrePointer(_this, vdata->wimp_sdl_window ? vdata->wimp_sdl_window : _this->windows);
+    return 0;
+}
+
 int
 RISCOS_InitMouse(_THIS)
 {
@@ -72,9 +115,9 @@ RISCOS_InitMouse(_THIS)
     /* mouse->CreateSystemCursor = RISCOS_CreateSystemCursor; */
     mouse->ShowCursor = RISCOS_ShowCursor;
     mouse->FreeCursor = RISCOS_FreeCursor;
-    /* mouse->WarpMouse = RISCOS_WarpMouse; */
+    mouse->WarpMouse = RISCOS_WarpMouse;
     /* mouse->WarpMouseGlobal = RISCOS_WarpMouseGlobal; */
-    /* mouse->SetRelativeMouseMode = RISCOS_SetRelativeMouseMode; */
+    mouse->SetRelativeMouseMode = RISCOS_SetRelativeMouseMode;
     /* mouse->CaptureMouse = RISCOS_CaptureMouse; */
     /* mouse->GetGlobalMouseState = RISCOS_GetGlobalMouseState; */
 
