diff --git src/audio/riscos/SDL_riscosaudio.c src/audio/riscos/SDL_riscosaudio.c
new file mode 100644
index 0000000..2bfcd5b
--- /dev/null
+++ src/audio/riscos/SDL_riscosaudio.c
@@ -0,0 +1,263 @@
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
+/*
+ * 2026: RISC OS audio output through SharedSoundBuffer / StreamManager.
+ *
+ * SharedSoundBuffer is the RISC OS 5 module that plays a stream of 16-bit
+ * stereo samples through SharedSound, mixing with any other programs that
+ * are making sound, and resampling to the hardware rate. StreamManager
+ * holds the queued data in its own memory, so nothing of ours has to stay
+ * paged in: this is plain user-mode code with no interrupt handlers.
+ * (It's the same interface RDPClient uses.)
+ *
+ * The modules (SharedSound 1.07+, StreamManager 0.03+, SharedSoundBuffer
+ * 0.07+) come with RISC OS 5. If they aren't loaded, this driver isn't
+ * available and SDL falls back to the next one (dsp, via UnixLib's
+ * DigitalRenderer /dev/dsp emulation).
+ */
+
+#if SDL_AUDIO_DRIVER_RISCOS
+
+#include "SDL_timer.h"
+#include "SDL_audio.h"
+#include "../SDL_audio_c.h"
+#include "SDL_riscosaudio.h"
+
+#include <kernel.h>
+#include <swis.h>
+#include <errno.h>
+
+#define XSharedSoundBuffer_OpenStream         (0x20000 | 0x55FC0)
+#define XSharedSoundBuffer_CloseStream        (0x20000 | 0x55FC1)
+#define XSharedSoundBuffer_Volume             (0x20000 | 0x55FC4)
+#define XSharedSoundBuffer_SampleRate         (0x20000 | 0x55FC5)
+#define XSharedSoundBuffer_Pause              (0x20000 | 0x55FC9)
+#define XSharedSoundBuffer_ReturnStreamHandle (0x20000 | 0x55FCE)
+#define XStreamManager_AddBlock               (0x20000 | 0x57282)
+#define XStreamManager_SetBuffer              (0x20000 | 0x57287)
+#define XStreamManager_BufferStats            (0x20000 | 0x57288)
+
+extern char *program_invocation_short_name;
+
+static SDL_bool
+RISCOS_SWIExists(const char *name)
+{
+    _kernel_swi_regs regs;
+    regs.r[1] = (int)name;
+    return _kernel_swi(OS_SWINumberFromString, &regs, &regs) == NULL;
+}
+
+/* Bytes queued in StreamManager that haven't been played yet. */
+static int
+RISCOS_Queued(_THIS)
+{
+    _kernel_swi_regs regs;
+    regs.r[0] = this->hidden->sm_stream;
+    if (_kernel_swi(XStreamManager_BufferStats, &regs, &regs) != NULL)
+        return -1;
+    return regs.r[0] - regs.r[1];     /* added - played */
+}
+
+static void
+RISCOS_SetPaused(_THIS, SDL_bool pause)
+{
+    _kernel_swi_regs regs;
+    regs.r[0] = this->hidden->ssb_handle;
+    regs.r[1] = pause ? 0 : 1;
+    _kernel_swi(XSharedSoundBuffer_Pause, &regs, &regs);
+}
+
+static void
+RISCOS_CloseDevice(_THIS)
+{
+    if (this->hidden->ssb_handle) {
+        _kernel_swi_regs regs;
+        regs.r[0] = this->hidden->ssb_handle;
+        _kernel_swi(XSharedSoundBuffer_CloseStream, &regs, &regs);
+    }
+    SDL_free(this->hidden->mixbuf);
+    SDL_free(this->hidden);
+}
+
+static int
+RISCOS_OpenDevice(_THIS, const char *devname)
+{
+    struct SDL_PrivateAudioData *h;
+    _kernel_swi_regs regs;
+    _kernel_oserror *err;
+    const char *name = program_invocation_short_name;
+
+    (void)devname;
+    if (this->iscapture) {
+        return SDL_SetError("RISC OS audio: no recording");
+    }
+
+    h = (struct SDL_PrivateAudioData *) SDL_calloc(1, sizeof(*h));
+    if (h == NULL) {
+        return SDL_OutOfMemory();
+    }
+    this->hidden = h;
+
+    /* SharedSoundBuffer takes 16-bit signed little endian stereo at any
+       rate; SDL converts everything else. */
+    this->spec.format = AUDIO_S16LSB;
+    this->spec.channels = 2;
+    if (this->spec.freq < 4000 || this->spec.freq > 96000) {
+        this->spec.freq = 44100;
+    }
+    SDL_CalculateAudioSpec(&this->spec);
+
+    h->mixlen = this->spec.size;
+    h->mixbuf = (Uint8 *) SDL_malloc(h->mixlen);
+    if (h->mixbuf == NULL) {
+        return SDL_OutOfMemory();
+    }
+    SDL_memset(h->mixbuf, this->spec.silence, h->mixlen);
+
+    h->delay_ms = (this->spec.samples * 1000) / this->spec.freq;
+    if (h->delay_ms < 1) {
+        h->delay_ms = 1;
+    }
+    /* Keep about three buffers (at least 60 ms) queued: enough to ride out
+       the desktop being busy for a moment, without noticeable lag. */
+    h->target = h->mixlen * 3;
+    {
+        Uint32 min = (Uint32)this->spec.freq * 4 * 60 / 1000;
+        if (h->target < min) {
+            h->target = min;
+        }
+    }
+
+    regs.r[0] = 2;                      /* R2 holds the usual block size */
+    regs.r[1] = (int)((name && *name) ? name : "SDL");
+    regs.r[2] = (int)h->mixlen;
+    err = _kernel_swi(XSharedSoundBuffer_OpenStream, &regs, &regs);
+    if (err != NULL) {
+        return SDL_SetError("SharedSoundBuffer_OpenStream: %s", err->errmess);
+    }
+    h->ssb_handle = regs.r[0];
+
+    regs.r[0] = h->ssb_handle;
+    err = _kernel_swi(XSharedSoundBuffer_ReturnStreamHandle, &regs, &regs);
+    if (err != NULL) {
+        return SDL_SetError("SharedSoundBuffer_ReturnStreamHandle: %s", err->errmess);
+    }
+    h->sm_stream = regs.r[0];
+
+    /* Room for well over the target, so AddBlock never has to refuse. */
+    regs.r[0] = h->sm_stream;
+    regs.r[1] = (int)(h->target * 4 + h->mixlen * 4);
+    _kernel_swi(XStreamManager_SetBuffer, &regs, &regs);
+
+    regs.r[0] = h->ssb_handle;
+    regs.r[1] = this->spec.freq * 1024;    /* Hz * 1024 */
+    _kernel_swi(XSharedSoundBuffer_SampleRate, &regs, &regs);
+
+    regs.r[0] = h->ssb_handle;
+    regs.r[1] = (int)0xFFFFFFFF;           /* full volume, left and right */
+    _kernel_swi(XSharedSoundBuffer_Volume, &regs, &regs);
+
+    /* Stay paused until a little data is queued, so it doesn't start by
+       running dry. */
+    RISCOS_SetPaused(this, SDL_TRUE);
+    h->started = SDL_FALSE;
+    return 0;
+}
+
+static Uint8 *
+RISCOS_GetDeviceBuf(_THIS)
+{
+    return this->hidden->mixbuf;
+}
+
+static void
+RISCOS_PlayDevice(_THIS)
+{
+    struct SDL_PrivateAudioData *h = this->hidden;
+    _kernel_swi_regs regs;
+    int tries;
+
+    /* StreamManager copies the block, so mixbuf can be reused at once. */
+    for (tries = 0; tries < 50; tries++) {
+        regs.r[0] = h->sm_stream;
+        regs.r[1] = (int)h->mixbuf;
+        regs.r[2] = (int)h->mixlen;
+        if (_kernel_swi(XStreamManager_AddBlock, &regs, &regs) == NULL)
+            break;
+        SDL_Delay(h->delay_ms);            /* buffer full: let some play */
+    }
+
+    if (!h->started && RISCOS_Queued(this) >= (int)(h->mixlen * 2)) {
+        RISCOS_SetPaused(this, SDL_FALSE);
+        h->started = SDL_TRUE;
+    }
+}
+
+/* Sleep until the queue has drained to the target level. */
+static void
+RISCOS_WaitDevice(_THIS)
+{
+    struct SDL_PrivateAudioData *h = this->hidden;
+    int queued, n;
+
+    if (!h->started)
+        return;                           /* fill up before playing */
+    for (n = 0; n < 200; n++) {
+        queued = RISCOS_Queued(this);
+        if (queued < 0 || queued <= (int)h->target)
+            return;
+        /* Sleep for about half of what's over the target. */
+        {
+            Uint32 over = (Uint32)queued - h->target;
+            Uint32 ms = (over * 1000) / ((Uint32)this->spec.freq * 4) / 2;
+            SDL_Delay(ms < 1 ? 1 : ms > 20 ? 20 : ms);
+        }
+    }
+}
+
+static SDL_bool
+RISCOS_Init(SDL_AudioDriverImpl * impl)
+{
+    if (!RISCOS_SWIExists("SharedSoundBuffer_OpenStream") ||
+        !RISCOS_SWIExists("StreamManager_AddBlock")) {
+        SDL_SetError("RISC OS audio: SharedSoundBuffer/StreamManager not loaded");
+        return SDL_FALSE;                 /* try the next driver */
+    }
+
+    impl->OpenDevice = RISCOS_OpenDevice;
+    impl->PlayDevice = RISCOS_PlayDevice;
+    impl->WaitDevice = RISCOS_WaitDevice;
+    impl->GetDeviceBuf = RISCOS_GetDeviceBuf;
+    impl->CloseDevice = RISCOS_CloseDevice;
+    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
+
+    return SDL_TRUE;
+}
+
+AudioBootStrap RISCOSAUDIO_bootstrap = {
+    "riscos", "RISC OS SharedSoundBuffer", RISCOS_Init, SDL_FALSE
+};
+
+#endif /* SDL_AUDIO_DRIVER_RISCOS */
+
+/* vi: set ts=4 sw=4 expandtab: */
