diff --git src/video/riscos/SDL_riscosopengl.c src/video/riscos/SDL_riscosopengl.c
new file mode 100644
index 0000000..80eaba9
--- /dev/null
+++ src/video/riscos/SDL_riscosopengl.c
@@ -0,0 +1,382 @@
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
+ * 2026: software OpenGL for the RISC OS video driver, using Mesa's OSMesa
+ * (statically linked; GL_LoadLibrary is a no-op).
+ *
+ * A GL window's render target IS its framebuffer sprite (fb_area/fb_sprite
+ * in SDL_WindowData, marked gl_active). OSMesa renders straight into the
+ * sprite image, so everything that already knows how to show that sprite -
+ * the Wimp redraw loop (RISCOS_WimpPlotWindow), Wimp_UpdateWindow in a
+ * desktop window, and the full screen plot - shows GL output too, including
+ * redraws after another window has covered ours.
+ *
+ * Pixel format: in a 16M colour mode the sprite uses the screen's own mode
+ * (as the framebuffer path does), so a plot needs no conversion. XBGR modes
+ * (0x00BBGGRR, R,G,B,X in memory) render as OSMESA_RGBA, XRGB modes as
+ * OSMESA_BGRA. The format is fixed when the context is created. In any other
+ * mode we fall back to a type 6 (XBGR) sprite and let SpriteExtend convert.
+ */
+
+#if SDL_VIDEO_DRIVER_RISCOS && SDL_VIDEO_OPENGL_OSMESA
+
+#include "SDL_timer.h"
+#include "../SDL_sysvideo.h"
+#include "SDL_riscosvideo.h"
+#include "SDL_riscoswindow.h"
+#include "SDL_riscosframebuffer_c.h"
+#include "SDL_riscosevents_c.h"
+#include "SDL_riscosopengl.h"
+
+#include <kernel.h>
+#include <swis.h>
+
+/* OSMesa declarations (values from Mesa's GL/osmesa.h), declared here so
+   SDL's own GL headers never clash with Mesa's. */
+typedef struct osmesa_context *OSMesaContext;
+typedef void (*OSMESAproc)(void);
+extern OSMesaContext OSMesaCreateContextAttribs(const int *attribList, OSMesaContext sharelist);
+extern void OSMesaDestroyContext(OSMesaContext ctx);
+extern unsigned char OSMesaMakeCurrent(OSMesaContext ctx, void *buffer, unsigned int type, int width, int height);
+extern void OSMesaPixelStore(int pname, int value);
+extern OSMESAproc OSMesaGetProcAddress(const char *funcName);
+
+#define RO_GL_RGBA                   0x1908
+#define RO_GL_UNSIGNED_BYTE          0x1401
+#define OSMESA_BGRA                  0x1
+#define OSMESA_ROW_LENGTH            0x10
+#define OSMESA_Y_UP                  0x11
+#define OSMESA_FORMAT                0x22
+#define OSMESA_DEPTH_BITS            0x30
+#define OSMESA_STENCIL_BITS          0x31
+#define OSMESA_ACCUM_BITS            0x32
+#define OSMESA_PROFILE               0x33
+#define OSMESA_CORE_PROFILE          0x34
+#define OSMESA_COMPAT_PROFILE        0x35
+#define OSMESA_CONTEXT_MAJOR_VERSION 0x36
+#define OSMESA_CONTEXT_MINOR_VERSION 0x37
+
+/* 32bpp, 90x90 dpi, new format sprite: pixels are 0x00BBGGRR */
+#define GL_SPRITE_TYPE6 (1 | (90 << 1) | (90 << 14) | (6 << 27))
+
+typedef struct RISCOS_GLContext {
+    OSMesaContext osmesa;
+    int format;                 /* RO_GL_RGBA or OSMESA_BGRA */
+} RISCOS_GLContext;
+
+static void (*gl_finish)(void) = NULL;
+
+/* Which OSMesa format and sprite mode suit the current screen mode. */
+static void
+RISCOS_GL_ScreenFormat(SDL_Window *window, int *format, void **sprite_mode)
+{
+    SDL_DisplayMode mode;
+
+    *format = RO_GL_RGBA;
+    *sprite_mode = (void *) GL_SPRITE_TYPE6;
+    if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &mode) == 0) {
+        if (mode.format == SDL_PIXELFORMAT_XBGR8888) {
+            *sprite_mode = mode.driverdata;
+        } else if (mode.format == SDL_PIXELFORMAT_XRGB8888) {
+            *format = OSMESA_BGRA;
+            *sprite_mode = mode.driverdata;
+        }
+    }
+}
+
+/* Make sure the window's GL sprite matches the window size and the
+   context's pixel format. Returns 1 if a new sprite was made, 0 if the old
+   one still fits, -1 on error. */
+static int
+RISCOS_GL_EnsureBuffer(SDL_Window *window, const RISCOS_GLContext *ctx)
+{
+    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
+    _kernel_swi_regs regs;
+    _kernel_oserror *error;
+    void *sprite_mode;
+    int screen_format, size;
+
+    RISCOS_GL_ScreenFormat(window, &screen_format, &sprite_mode);
+    if (screen_format != ctx->format) {
+        /* The mode changed to one with the other RGB order since the context
+           was made; a type 6 sprite is right for RGBA, and for BGRA the
+           colours will be swapped until the next context. */
+        sprite_mode = (void *) GL_SPRITE_TYPE6;
+    }
+
+    if (data->gl_active && data->fb_area &&
+        data->gl_w == window->w && data->gl_h == window->h &&
+        data->gl_sprite_mode == sprite_mode) {
+        return 0;
+    }
+
+    RISCOS_GL_DestroyWindowBuffer(window);
+
+    size = sizeof(sprite_area) + sizeof(sprite_header) + window->w * 4 * window->h;
+    data->fb_area = (sprite_area *) SDL_malloc(size);
+    if (!data->fb_area) {
+        return SDL_OutOfMemory();
+    }
+    data->fb_area->size  = size;
+    data->fb_area->count = 0;
+    data->fb_area->start = 16;
+    data->fb_area->end   = 16;
+
+    regs.r[0] = 256 + 15;               /* create sprite */
+    regs.r[1] = (int) data->fb_area;
+    regs.r[2] = (int) "gl";
+    regs.r[3] = 0;                      /* no palette */
+    regs.r[4] = window->w;
+    regs.r[5] = window->h;
+    regs.r[6] = (int) sprite_mode;
+    error = _kernel_swi(OS_SpriteOp, &regs, &regs);
+    if (error != NULL) {
+        SDL_free(data->fb_area);
+        data->fb_area = NULL;
+        return SDL_SetError("Unable to create GL sprite: %s (%i)", error->errmess, error->errnum);
+    }
+
+    data->fb_sprite = (sprite_header *) (((Uint8 *) data->fb_area) + data->fb_area->start);
+    data->fb_direct = 0;
+    data->gl_active = 1;
+    data->gl_w = window->w;
+    data->gl_h = window->h;
+    data->gl_sprite_mode = sprite_mode;
+    return 1;
+}
+
+static int
+RISCOS_GL_Bind(SDL_Window *window, const RISCOS_GLContext *ctx)
+{
+    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
+
+    if (!OSMesaMakeCurrent(ctx->osmesa,
+                           ((Uint8 *) data->fb_sprite) + data->fb_sprite->image_offset,
+                           RO_GL_UNSIGNED_BYTE, data->gl_w, data->gl_h)) {
+        return SDL_SetError("OSMesaMakeCurrent failed");
+    }
+    OSMesaPixelStore(OSMESA_Y_UP, 0);   /* sprite rows run top down */
+    OSMesaPixelStore(OSMESA_ROW_LENGTH, data->gl_w);
+    return 0;
+}
+
+int
+RISCOS_GL_LoadLibrary(_THIS, const char *path)
+{
+    (void) path;
+    SDL_strlcpy(_this->gl_config.driver_path, "OSMesa", sizeof(_this->gl_config.driver_path));
+    return 0;
+}
+
+void *
+RISCOS_GL_GetProcAddress(_THIS, const char *proc)
+{
+    (void) _this;
+    return (void *) OSMesaGetProcAddress(proc);
+}
+
+void
+RISCOS_GL_UnloadLibrary(_THIS)
+{
+    (void) _this;
+}
+
+SDL_GLContext
+RISCOS_GL_CreateContext(_THIS, SDL_Window *window)
+{
+    RISCOS_GLContext *ctx, *share = NULL;
+    void *sprite_mode;
+    int attribs[20];
+    int n = 0;
+
+    if (_this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_ES) {
+        SDL_SetError("OpenGL ES contexts are not supported by the OSMesa backend");
+        return NULL;
+    }
+    if (_this->gl_config.share_with_current_context) {
+        share = (RISCOS_GLContext *) SDL_GL_GetCurrentContext();
+    }
+
+    ctx = (RISCOS_GLContext *) SDL_calloc(1, sizeof(*ctx));
+    if (!ctx) {
+        SDL_OutOfMemory();
+        return NULL;
+    }
+    RISCOS_GL_ScreenFormat(window, &ctx->format, &sprite_mode);
+
+    attribs[n++] = OSMESA_FORMAT;       attribs[n++] = ctx->format;
+    attribs[n++] = OSMESA_DEPTH_BITS;   attribs[n++] = _this->gl_config.depth_size;
+    attribs[n++] = OSMESA_STENCIL_BITS; attribs[n++] = _this->gl_config.stencil_size;
+    attribs[n++] = OSMESA_ACCUM_BITS;   attribs[n++] = _this->gl_config.accum_red_size ? 16 : 0;
+    attribs[n++] = OSMESA_PROFILE;
+    attribs[n++] = (_this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_CORE)
+                   ? OSMESA_CORE_PROFILE : OSMESA_COMPAT_PROFILE;
+    /* SDL defaults to 2.1 compat; only ask for a version when the app did. */
+    if (_this->gl_config.major_version > 2 ||
+        _this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_CORE) {
+        attribs[n++] = OSMESA_CONTEXT_MAJOR_VERSION; attribs[n++] = _this->gl_config.major_version;
+        attribs[n++] = OSMESA_CONTEXT_MINOR_VERSION; attribs[n++] = _this->gl_config.minor_version;
+    }
+    attribs[n++] = 0;
+
+    ctx->osmesa = OSMesaCreateContextAttribs(attribs, share ? share->osmesa : NULL);
+    if (!ctx->osmesa) {
+        SDL_free(ctx);
+        SDL_SetError("OSMesaCreateContextAttribs failed (requested GL %d.%d; this Mesa provides 2.1)",
+                     _this->gl_config.major_version, _this->gl_config.minor_version);
+        return NULL;
+    }
+
+    if (RISCOS_GL_MakeCurrent(_this, window, (SDL_GLContext) ctx) < 0) {
+        OSMesaDestroyContext(ctx->osmesa);
+        SDL_free(ctx);
+        return NULL;
+    }
+    return (SDL_GLContext) ctx;
+}
+
+int
+RISCOS_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context)
+{
+    RISCOS_GLContext *ctx = (RISCOS_GLContext *) context;
+    (void) _this;
+
+    if (!window || !ctx) {
+        return 0;                       /* OSMesa has no "release" */
+    }
+    if (RISCOS_GL_EnsureBuffer(window, ctx) < 0) {
+        return -1;
+    }
+    if (RISCOS_GL_Bind(window, ctx) < 0) {
+        return -1;
+    }
+    if (!gl_finish) {
+        gl_finish = (void (*)(void)) OSMesaGetProcAddress("glFinish");
+    }
+    return 0;
+}
+
+int
+RISCOS_GL_SetSwapInterval(_THIS, int interval)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    vdata->gl_swap_interval = (interval < 0) ? 1 : interval;   /* no adaptive vsync */
+    return 0;
+}
+
+int
+RISCOS_GL_GetSwapInterval(_THIS)
+{
+    return ((SDL_VideoData *) _this->driverdata)->gl_swap_interval;
+}
+
+/* Swap interval 1 or more. Full screen we own the machine (single tasking),
+   so wait for the real vertical sync with OS_Byte 19. In a desktop window
+   OS_Byte 19 would stop every task for up to a frame, so instead pace to the
+   display's frame rate and spend the wait in Wimp_PollIdle (RISCOS_WimpDelay):
+   the other tasks run while we wait. */
+static void
+RISCOS_GL_Pace(_THIS, SDL_Window *window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    SDL_DisplayMode mode;
+    Uint32 period, now;
+
+    if (vdata->wimp_window == 0 || vdata->wimp_sdl_window != window) {
+        _kernel_osbyte(19, 0, 0);
+        return;
+    }
+    period = 1000 / 60;
+    if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &mode) == 0 &&
+        mode.refresh_rate >= 24 && mode.refresh_rate <= 240) {
+        period = 1000 / mode.refresh_rate;
+    }
+    period *= (Uint32) vdata->gl_swap_interval;
+
+    now = SDL_GetTicks();
+    if (vdata->gl_next_frame == 0 || SDL_TICKS_PASSED(now, vdata->gl_next_frame + period)) {
+        vdata->gl_next_frame = now;             /* first frame, or fell behind: resync */
+    } else if (!SDL_TICKS_PASSED(now, vdata->gl_next_frame)) {
+        if (!RISCOS_WimpDelay(_this, vdata->gl_next_frame - now)) {
+            SDL_Delay(vdata->gl_next_frame - now);
+        }
+    }
+    vdata->gl_next_frame += period;
+}
+
+int
+RISCOS_GL_SwapWindow(_THIS, SDL_Window *window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    RISCOS_GLContext *ctx = (RISCOS_GLContext *) SDL_GL_GetCurrentContext();
+    int changed;
+
+    if (gl_finish) {
+        gl_finish();
+    }
+    if (!ctx) {
+        return SDL_SetError("No current GL context");
+    }
+
+    /* Window resized, or the screen mode changed (e.g. full screen <->
+       window) since the last frame: rebind a fitting buffer. The new buffer
+       is empty, so skip showing it; the next frame fills it. */
+    changed = RISCOS_GL_EnsureBuffer(window, ctx);
+    if (changed != 0) {
+        return (changed < 0) ? -1 : RISCOS_GL_Bind(window, ctx);
+    }
+
+    if (vdata->gl_swap_interval > 0) {
+        RISCOS_GL_Pace(_this, window);
+    }
+    return RISCOS_UpdateWindowFramebuffer(_this, window, NULL, 0);
+}
+
+void
+RISCOS_GL_DeleteContext(_THIS, SDL_GLContext context)
+{
+    RISCOS_GLContext *ctx = (RISCOS_GLContext *) context;
+    (void) _this;
+    if (ctx) {
+        OSMesaDestroyContext(ctx->osmesa);
+        SDL_free(ctx);
+    }
+}
+
+void
+RISCOS_GL_DestroyWindowBuffer(SDL_Window *window)
+{
+    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
+    if (data && data->gl_active) {
+        SDL_free(data->fb_area);
+        data->fb_area = NULL;
+        data->fb_sprite = NULL;
+        data->gl_active = 0;
+        data->gl_w = data->gl_h = 0;
+        data->gl_sprite_mode = NULL;
+    }
+}
+
+#endif /* SDL_VIDEO_DRIVER_RISCOS && SDL_VIDEO_OPENGL_OSMESA */
+
+/* vi: set ts=4 sw=4 expandtab: */
