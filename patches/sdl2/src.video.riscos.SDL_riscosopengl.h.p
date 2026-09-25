diff --git src/video/riscos/SDL_riscosopengl.h src/video/riscos/SDL_riscosopengl.h
new file mode 100644
index 0000000..d5e3207
--- /dev/null
+++ src/video/riscos/SDL_riscosopengl.h
@@ -0,0 +1,45 @@
+/*
+  Simple DirectMedia Layer
+  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>
+
+  This software is provided 'as-is', without any express or implied
+  warranty.  In no event will the authors be held liable for any damages
+  arising from the use of this software.
+
+  Permission is granted to anyone to use this software for any purpose,
+  including commercial applications, and to alter it and redistribute it
+  freely, subject to the following restrictions:
+
+  1. The origin of this software must not be misrepresented; you must not
+     claim that you wrote the original software. If you use this software
+     in a product, an acknowledgment in the product documentation would be
+     appreciated but is not required.
+  2. Altered source versions must be plainly marked as such, and must not be
+     misrepresented as being the original software.
+  3. This notice may not be removed or altered from any source distribution.
+*/
+#include "../../SDL_internal.h"
+
+#ifndef SDL_riscosopengl_h_
+#define SDL_riscosopengl_h_
+
+#if SDL_VIDEO_OPENGL_OSMESA
+
+#include "../SDL_sysvideo.h"
+
+extern int RISCOS_GL_LoadLibrary(_THIS, const char *path);
+extern void *RISCOS_GL_GetProcAddress(_THIS, const char *proc);
+extern void RISCOS_GL_UnloadLibrary(_THIS);
+extern SDL_GLContext RISCOS_GL_CreateContext(_THIS, SDL_Window *window);
+extern int RISCOS_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context);
+extern int RISCOS_GL_SetSwapInterval(_THIS, int interval);
+extern int RISCOS_GL_GetSwapInterval(_THIS);
+extern int RISCOS_GL_SwapWindow(_THIS, SDL_Window *window);
+extern void RISCOS_GL_DeleteContext(_THIS, SDL_GLContext context);
+extern void RISCOS_GL_DestroyWindowBuffer(SDL_Window *window);
+
+#endif /* SDL_VIDEO_OPENGL_OSMESA */
+
+#endif /* SDL_riscosopengl_h_ */
+
+/* vi: set ts=4 sw=4 expandtab: */
