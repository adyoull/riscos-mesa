diff --git src/video/riscos/SDL_riscosmouse.c src/video/riscos/SDL_riscosmouse.c
index 072f8a7..1980952 100644
--- src/video/riscos/SDL_riscosmouse.c
+++ src/video/riscos/SDL_riscosmouse.c
@@ -23,6 +23,8 @@
 #if SDL_VIDEO_DRIVER_RISCOS
 
 #include "../../events/SDL_mouse_c.h"
+#include "../SDL_sysvideo.h"
+#include "SDL_riscosvideo.h"
 
 #include <kernel.h>
 
@@ -49,15 +51,28 @@ RISCOS_FreeCursor(SDL_Cursor * cursor)
     SDL_free(cursor);
 }
 
+/* 2026: in windowed mode only hide the pointer while it is over our window. */
+void
+RISCOS_ApplyPointerVisibility(_THIS)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    SDL_bool hide = vdata->cursor_hidden;
+
+    if (vdata->wimp_window != 0 && !vdata->pointer_in)
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
