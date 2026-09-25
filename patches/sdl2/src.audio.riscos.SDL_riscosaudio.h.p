diff --git src/audio/riscos/SDL_riscosaudio.h src/audio/riscos/SDL_riscosaudio.h
new file mode 100644
index 0000000..4a689a1
--- /dev/null
+++ src/audio/riscos/SDL_riscosaudio.h
@@ -0,0 +1,44 @@
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
+#ifndef SDL_riscosaudio_h_
+#define SDL_riscosaudio_h_
+
+#include "../SDL_sysaudio.h"
+
+/* Hidden "this" pointer for the audio functions */
+#define _THIS   SDL_AudioDevice *this
+
+struct SDL_PrivateAudioData
+{
+    int ssb_handle;             /* SharedSoundBuffer stream handle */
+    int sm_stream;              /* StreamManager stream behind it */
+    Uint8 *mixbuf;              /* one SDL buffer, handed to StreamManager */
+    Uint32 mixlen;
+    Uint32 target;              /* bytes to keep queued (the latency) */
+    Uint32 delay_ms;            /* how long one buffer plays for */
+    SDL_bool started;           /* playback has been unpaused */
+};
+
+#endif /* SDL_riscosaudio_h_ */
+
+/* vi: set ts=4 sw=4 expandtab: */
