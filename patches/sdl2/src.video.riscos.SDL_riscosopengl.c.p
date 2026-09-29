diff --git src/video/riscos/SDL_riscosopengl.c src/video/riscos/SDL_riscosopengl.c
new file mode 100644
--- /dev/null
+++ src/video/riscos/SDL_riscosopengl.c
@@ -0,0 +1,808 @@
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
+ * 2026: OpenGL for the RISC OS video driver: Mesa's OSMesa, statically
+ * linked (GL_LoadLibrary is a no-op). Two ways to show a GL window:
+ *
+ * 1. The sprite path (the default, as in riscos-mesa 20.3.5-9 and before).
+ *    A GL window's render target IS its framebuffer sprite (fb_area/
+ *    fb_sprite in SDL_WindowData, marked gl_active). OSMesa renders straight
+ *    into the sprite image, so everything that already knows how to show
+ *    that sprite - the Wimp redraw loop (RISCOS_WimpPlotWindow),
+ *    Wimp_UpdateWindow in a desktop window, and the full screen plot - shows
+ *    GL output too. In a 16M colour mode the sprite uses the screen's own
+ *    mode, so a plot needs no conversion: XBGR modes render as OSMESA_RGBA,
+ *    XRGB modes as OSMESA_BGRA (fixed when the context is made); in other
+ *    modes a type 6 (XBGR) sprite that SpriteExtend converts.
+ *
+ * 2. The EGL path (2026-09-29, opt-in): the window is a riscos-mesa EGL
+ *    window surface, on the desktop window or on the screen (-1) full
+ *    screen, and gets what EGL has. Chosen when a context is made if the
+ *    program (or the user, as system variables) sets any of:
+ *      SDL_RISCOS_GL_RENDER_SIZE "WxH": render at WxH, stretched over the
+ *        window or the screen; the program sees a WxH window (see
+ *        SDL_riscoswindow.c);
+ *      SDL_RISCOS_GL_OVERLAY "1": a hardware overlay (EGL_RISCOS_overlay),
+ *        with EGL's fallbacks to the sprite plot;
+ *      SDL_RISCOS_GL_EGL "1": the EGL path on its own.
+ *    Redraws go to eglRedrawWindowRISCOS. A swap never blocks the desktop
+ *    for a vsync: when EGL would wait (overlay) the frame is held and shown
+ *    from the event loop (RISCOS_GL_Idle) once one has passed, unless a
+ *    newer frame replaces it.
+ * Both paths are in every GL build, so programs link -lEGL too.
+ */
+
+#if SDL_VIDEO_DRIVER_RISCOS && SDL_VIDEO_OPENGL_OSMESA
+
+#include "SDL_timer.h"
+#include "SDL_hints.h"
+#include "../../SDL_hints_c.h"
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
+#define EGL_EGLEXT_PROTOTYPES 1
+#include <EGL/egl.h>
+#include <EGL/eglext.h>
+#include <EGL/eglext_riscos.h>
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
+#define OSMESA_ES1_PROFILE           0x1001  /* riscos-mesa's Mesa patch */
+#define OSMESA_ES2_PROFILE           0x1002
+
+/* 32bpp, 90x90 dpi, new format sprite: pixels are 0x00BBGGRR */
+#define GL_SPRITE_TYPE6 (1 | (90 << 1) | (90 << 14) | (6 << 27))
+
+typedef struct RISCOS_GLContext {
+    int egl;                    /* 1: the EGL path, 0: the sprite path */
+    /* the sprite path */
+    OSMesaContext osmesa;
+    int format;                 /* RO_GL_RGBA or OSMESA_BGRA */
+    /* the EGL path */
+    EGLContext eglctx;
+    EGLConfig config;
+    int es;                     /* 0: OpenGL; 1 or 2: OpenGL ES version */
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
+SPR_EnsureBuffer(SDL_Window *window, const RISCOS_GLContext *ctx)
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
+SPR_Bind(SDL_Window *window, const RISCOS_GLContext *ctx)
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
+
+static EGLDisplay gl_dpy = EGL_NO_DISPLAY;
+
+static int
+RISCOS_GL_Display(void)
+{
+    if (gl_dpy != EGL_NO_DISPLAY)
+        return 0;
+    gl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
+    if (gl_dpy == EGL_NO_DISPLAY || !eglInitialize(gl_dpy, NULL, NULL)) {
+        gl_dpy = EGL_NO_DISPLAY;
+        return SDL_SetError("eglInitialize failed (0x%x)", (unsigned) eglGetError());
+    }
+    return 0;
+}
+
+/* The EGL visual (colour order) that suits the current screen mode. */
+static EGLint
+RISCOS_GL_Visual(SDL_Window *window)
+{
+    SDL_DisplayMode mode;
+    if (SDL_GetCurrentDisplayMode(SDL_GetWindowDisplayIndex(window), &mode) == 0 &&
+        mode.format == SDL_PIXELFORMAT_XRGB8888)
+        return EGL_RISCOS_VISUAL_TRGB;
+    return EGL_RISCOS_VISUAL_TBGR;
+}
+
+/* Where the window's surface goes: its desktop window, or the screen. */
+static int
+RISCOS_GL_Target(_THIS, SDL_Window *window)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    if (RISCOS_IsWindowed(vdata) && vdata->wimp_sdl_window == window)
+        return vdata->wimp_window;
+    return -1;
+}
+
+/* "WxH" (or "W*H"), each 1..4096. */
+static int
+RISCOS_GL_ParseSize(const char *s, int *w, int *h)
+{
+    char *end;
+    long a, b;
+    if (!s || !*s)
+        return 0;
+    a = SDL_strtol(s, &end, 10);
+    if (end == s || (*end != 'x' && *end != 'X' && *end != '*'))
+        return 0;
+    s = end + 1;
+    b = SDL_strtol(s, &end, 10);
+    if (end == s || *end != 0 || a < 1 || b < 1 || a > 4096 || b > 4096)
+        return 0;
+    *w = (int) a;
+    *h = (int) b;
+    return 1;
+}
+
+void
+RISCOS_GL_ApplyRenderSize(SDL_Window *window)
+{
+    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
+    int w, h;
+    if (!data || !(window->flags & SDL_WINDOW_OPENGL) ||
+        !RISCOS_GL_ParseSize(SDL_GetHint(SDL_HINT_RISCOS_GL_RENDER_SIZE), &w, &h))
+        return;
+    data->render_w = w;
+    data->render_h = h;
+    data->disp_w = window->w;           /* what the program asked for */
+    data->disp_h = window->h;
+    window->w = w;                      /* what it sees */
+    window->h = h;
+}
+
+/* Make sure the window has an EGL surface on the right target, made with
+   ctx's config, rendering at the SDL window's size. *made is set when a new
+   surface was made (it has to be made current; its first frame is empty). */
+static int
+RISCOS_GL_EnsureSurface(_THIS, SDL_Window *window, const RISCOS_GLContext *ctx, int *made)
+{
+    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
+    int handle = RISCOS_GL_Target(_this, window);
+    const char *ovl;
+    EGLint attrs[8];
+    int n = 0;
+    EGLSurface surf;
+
+    *made = 0;
+    if (data->egl_surface && data->egl_handle == handle && data->egl_config == ctx->config) {
+        if (data->egl_rw != window->w || data->egl_rh != window->h) {
+            /* resized: EGL takes the new size at the next swap */
+            eglSurfaceAttrib(gl_dpy, data->egl_surface, EGL_RENDER_WIDTH_RISCOS, window->w);
+            eglSurfaceAttrib(gl_dpy, data->egl_surface, EGL_RENDER_HEIGHT_RISCOS, window->h);
+            data->egl_rw = window->w;
+            data->egl_rh = window->h;
+        }
+        return 0;
+    }
+    RISCOS_GL_DestroySurface(window);
+
+    attrs[n++] = EGL_RENDER_WIDTH_RISCOS;  attrs[n++] = window->w;
+    attrs[n++] = EGL_RENDER_HEIGHT_RISCOS; attrs[n++] = window->h;
+    ovl = SDL_GetHint(SDL_HINT_RISCOS_GL_OVERLAY);
+    if (handle >= 0 && ovl && *ovl) {
+        attrs[n++] = EGL_OVERLAY_RISCOS;
+        attrs[n++] = SDL_GetStringBoolean(ovl, SDL_FALSE) ? EGL_TRUE : EGL_FALSE;
+    }
+    attrs[n] = EGL_NONE;
+    surf = eglCreateWindowSurface(gl_dpy, ctx->config, (EGLNativeWindowType) handle, attrs);
+    if (surf == EGL_NO_SURFACE)
+        return SDL_SetError("eglCreateWindowSurface failed (0x%x)", (unsigned) eglGetError());
+    data->egl_surface = surf;
+    data->egl_config = ctx->config;
+    data->egl_handle = handle;
+    data->egl_rw = window->w;
+    data->egl_rh = window->h;
+    data->gl_active = 1;
+    data->gl_egl = 1;
+    data->gl_pending = 0;
+    *made = 1;
+    return 0;
+}
+
+static int
+RISCOS_GL_Bind(SDL_Window *window, const RISCOS_GLContext *ctx)
+{
+    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
+    eglBindAPI(ctx->es ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
+    if (!eglMakeCurrent(gl_dpy, data->egl_surface, data->egl_surface, ctx->eglctx))
+        return SDL_SetError("eglMakeCurrent failed (0x%x)", (unsigned) eglGetError());
+    /* SDL paces frames itself (RISCOS_GL_Pace). EGL's interval only matters
+       through an overlay, where 1 keeps to one buffer switch per vsync. */
+    eglSwapInterval(gl_dpy, data->egl_handle >= 0 ? 1 : 0);
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
+    void *p;
+    (void) _this;
+    p = (void *) OSMesaGetProcAddress(proc);
+    if (!p)
+        p = (void *) eglGetProcAddress(proc);     /* EGL's own extensions */
+    return p;
+}
+
+void
+RISCOS_GL_UnloadLibrary(_THIS)
+{
+    (void) _this;
+}
+
+static int EGL_MakeCurrent(_THIS, SDL_Window *window, RISCOS_GLContext *ctx);
+static int SPR_MakeCurrent(_THIS, SDL_Window *window, RISCOS_GLContext *ctx);
+
+static SDL_GLContext
+EGL_CreateContext(_THIS, SDL_Window *window)
+{
+    RISCOS_GLContext *ctx, *share = NULL;
+    EGLint cattrs[4], sattrs[12];
+    EGLConfig configs[32];
+    EGLint n = 0, i, v, visual;
+    int es = 0;
+
+    if (_this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_ES) {
+        if (_this->gl_config.major_version == 1 ||
+            (_this->gl_config.major_version == 2 && _this->gl_config.minor_version == 0)) {
+            es = _this->gl_config.major_version;
+        } else {
+            SDL_SetError("OpenGL ES %d.%d isn't available (OSMesa provides ES 1.1 and 2.0)",
+                         _this->gl_config.major_version, _this->gl_config.minor_version);
+            return NULL;
+        }
+    } else if (_this->gl_config.major_version > 2 ||
+               _this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_CORE) {
+        SDL_SetError("OpenGL %d.%d%s isn't available (this Mesa provides 2.1, ES 1.1 and ES 2.0)",
+                     _this->gl_config.major_version, _this->gl_config.minor_version,
+                     _this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_CORE ? " core" : "");
+        return NULL;
+    }
+    if (RISCOS_GL_Display() < 0)
+        return NULL;
+    if (_this->gl_config.share_with_current_context)
+        share = (RISCOS_GLContext *) SDL_GL_GetCurrentContext();
+
+    ctx = (RISCOS_GLContext *) SDL_calloc(1, sizeof(*ctx));
+    if (!ctx) {
+        SDL_OutOfMemory();
+        return NULL;
+    }
+    ctx->egl = 1;
+    ctx->es = es;
+
+    /* A config with at least the depth and stencil asked for, in the
+       screen's colour order. (No accumulation buffers with EGL.) */
+    i = 0;
+    sattrs[i++] = EGL_RENDERABLE_TYPE;
+    sattrs[i++] = es == 1 ? EGL_OPENGL_ES_BIT : es == 2 ? EGL_OPENGL_ES2_BIT : EGL_OPENGL_BIT;
+    sattrs[i++] = EGL_SURFACE_TYPE;   sattrs[i++] = EGL_WINDOW_BIT;
+    sattrs[i++] = EGL_DEPTH_SIZE;     sattrs[i++] = _this->gl_config.depth_size;
+    sattrs[i++] = EGL_STENCIL_SIZE;   sattrs[i++] = _this->gl_config.stencil_size;
+    sattrs[i] = EGL_NONE;
+    if (!eglChooseConfig(gl_dpy, sattrs, configs, 32, &n) || n < 1) {
+        SDL_free(ctx);
+        SDL_SetError("No EGL config with depth %d and stencil %d",
+                     _this->gl_config.depth_size, _this->gl_config.stencil_size);
+        return NULL;
+    }
+    visual = RISCOS_GL_Visual(window);
+    ctx->config = configs[0];
+    for (i = 0; i < n; i++) {
+        if (eglGetConfigAttrib(gl_dpy, configs[i], EGL_NATIVE_VISUAL_ID, &v) && v == visual) {
+            ctx->config = configs[i];
+            break;
+        }
+    }
+
+    eglBindAPI(es ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
+    i = 0;
+    if (es) {
+        cattrs[i++] = EGL_CONTEXT_CLIENT_VERSION;
+        cattrs[i++] = es;
+    }
+    cattrs[i] = EGL_NONE;
+    ctx->eglctx = eglCreateContext(gl_dpy, ctx->config,
+                                (share && share->egl && share->es == es) ? share->eglctx : EGL_NO_CONTEXT, cattrs);
+    if (ctx->eglctx == EGL_NO_CONTEXT) {
+        SDL_free(ctx);
+        SDL_SetError("eglCreateContext failed (0x%x)", (unsigned) eglGetError());
+        return NULL;
+    }
+
+    if (EGL_MakeCurrent(_this, window, ctx) < 0) {
+        eglDestroyContext(gl_dpy, ctx->eglctx);
+        SDL_free(ctx);
+        return NULL;
+    }
+    return (SDL_GLContext) ctx;
+}
+
+static int
+EGL_MakeCurrent(_THIS, SDL_Window *window, RISCOS_GLContext *ctx)
+{
+    int made;
+
+    if (RISCOS_GL_EnsureSurface(_this, window, ctx, &made) < 0)
+        return -1;
+    return RISCOS_GL_Bind(window, ctx);
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
+    if (!RISCOS_IsWindowed(vdata) || vdata->wimp_sdl_window != window) {
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
+static int
+EGL_SwapWindow(_THIS, SDL_Window *window, RISCOS_GLContext *ctx)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
+    int made, tries;
+
+    /* Full screen <-> window since the last frame: the surface moves to the
+       new target, and the frame drawn into the old one is gone; the next
+       frame fills the new one. (A resize is handled by EGL: it shows this
+       frame, then renders the next at the new size.) */
+    if (RISCOS_GL_EnsureSurface(_this, window, ctx, &made) < 0)
+        return -1;
+    if (made)
+        return RISCOS_GL_Bind(window, ctx);
+
+    if (vdata->gl_swap_interval > 0) {
+        RISCOS_GL_Pace(_this, window);
+    }
+
+    /* Through an overlay EGL waits for a vsync before switching buffers when
+       none has passed since the last switch. Don't let that stop the
+       desktop: with vsync on, wait for it cooperatively (a centisecond at a
+       time); otherwise, or if it still hasn't come, hold the frame and show
+       it from the event loop (RISCOS_GL_Idle), unless a newer one comes
+       first. */
+    if (eglSwapWouldWaitRISCOS(gl_dpy, data->egl_surface)) {
+        for (tries = 0; vdata->gl_swap_interval > 0 && tries < 3; tries++) {
+            if (!RISCOS_WimpDelay(_this, 10))
+                break;
+            if (!eglSwapWouldWaitRISCOS(gl_dpy, data->egl_surface))
+                break;
+        }
+        if (eglSwapWouldWaitRISCOS(gl_dpy, data->egl_surface)) {
+            data->gl_pending = 1;
+            return 0;
+        }
+    }
+    data->gl_pending = 0;
+    if (!eglSwapBuffers(gl_dpy, data->egl_surface))
+        return SDL_SetError("eglSwapBuffers failed (0x%x)", (unsigned) eglGetError());
+    return 0;
+}
+
+/* ---- the sprite path ---- */
+
+static SDL_GLContext
+SPR_CreateContext(_THIS, SDL_Window *window)
+{
+    RISCOS_GLContext *ctx, *share = NULL;
+    void *sprite_mode;
+    int attribs[20];
+    int n = 0;
+
+    /* OpenGL ES: classic swrast provides ES 1.1 and ES 2.0 */
+    if (_this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_ES &&
+        _this->gl_config.major_version != 1 &&
+        !(_this->gl_config.major_version == 2 && _this->gl_config.minor_version == 0)) {
+        SDL_SetError("OpenGL ES %d.%d isn't available (OSMesa provides ES 1.1 and 2.0)",
+                     _this->gl_config.major_version, _this->gl_config.minor_version);
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
+    if (_this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_ES)
+        attribs[n++] = (_this->gl_config.major_version == 1) ? OSMESA_ES1_PROFILE : OSMESA_ES2_PROFILE;
+    else
+        attribs[n++] = (_this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_CORE)
+                       ? OSMESA_CORE_PROFILE : OSMESA_COMPAT_PROFILE;
+    /* SDL defaults to 2.1 compat; only ask for a version when the app did. */
+    if (_this->gl_config.profile_mask != SDL_GL_CONTEXT_PROFILE_ES &&
+        (_this->gl_config.major_version > 2 ||
+         _this->gl_config.profile_mask == SDL_GL_CONTEXT_PROFILE_CORE)) {
+        attribs[n++] = OSMESA_CONTEXT_MAJOR_VERSION; attribs[n++] = _this->gl_config.major_version;
+        attribs[n++] = OSMESA_CONTEXT_MINOR_VERSION; attribs[n++] = _this->gl_config.minor_version;
+    }
+    attribs[n++] = 0;
+
+    ctx->osmesa = OSMesaCreateContextAttribs(attribs, (share && !share->egl) ? share->osmesa : NULL);
+    if (!ctx->osmesa) {
+        SDL_free(ctx);
+        SDL_SetError("OSMesaCreateContextAttribs failed (requested GL %d.%d; this Mesa provides 2.1, ES 1.1 and ES 2.0)",
+                     _this->gl_config.major_version, _this->gl_config.minor_version);
+        return NULL;
+    }
+
+    if (SPR_MakeCurrent(_this, window, ctx) < 0) {
+        OSMesaDestroyContext(ctx->osmesa);
+        SDL_free(ctx);
+        return NULL;
+    }
+    return (SDL_GLContext) ctx;
+}
+
+static int
+SPR_MakeCurrent(_THIS, SDL_Window *window, RISCOS_GLContext *ctx)
+{
+    (void) _this;
+    if (SPR_EnsureBuffer(window, ctx) < 0) {
+        return -1;
+    }
+    if (SPR_Bind(window, ctx) < 0) {
+        return -1;
+    }
+    if (!gl_finish) {
+        gl_finish = (void (*)(void)) OSMesaGetProcAddress("glFinish");
+    }
+    return 0;
+}
+
+static int
+SPR_SwapWindow(_THIS, SDL_Window *window, RISCOS_GLContext *ctx)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    int changed;
+
+    if (gl_finish) {
+        gl_finish();
+    }
+
+    /* Window resized, or the screen mode changed (e.g. full screen <->
+       window) since the last frame: rebind a fitting buffer. The new buffer
+       is empty, so skip showing it; the next frame fills it. */
+    changed = SPR_EnsureBuffer(window, ctx);
+    if (changed != 0) {
+        return (changed < 0) ? -1 : SPR_Bind(window, ctx);
+    }
+
+    if (vdata->gl_swap_interval > 0) {
+        RISCOS_GL_Pace(_this, window);
+    }
+    return RISCOS_UpdateWindowFramebuffer(_this, window, NULL, 0);
+}
+
+/* ---- which path, and the entry points ---- */
+
+static SDL_bool
+RISCOS_GL_WantEGL(SDL_Window *window)
+{
+    const SDL_WindowData *d = (const SDL_WindowData *) window->driverdata;
+    if (d && d->render_w)
+        return SDL_TRUE;
+    if (SDL_GetStringBoolean(SDL_GetHint(SDL_HINT_RISCOS_GL_OVERLAY), SDL_FALSE))
+        return SDL_TRUE;
+    return SDL_GetStringBoolean(SDL_GetHint(SDL_HINT_RISCOS_GL_EGL), SDL_FALSE);
+}
+
+SDL_GLContext
+RISCOS_GL_CreateContext(_THIS, SDL_Window *window)
+{
+    if (RISCOS_GL_WantEGL(window))
+        return EGL_CreateContext(_this, window);
+    return SPR_CreateContext(_this, window);
+}
+
+int
+RISCOS_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext context)
+{
+    RISCOS_GLContext *ctx = (RISCOS_GLContext *) context;
+
+    if (!window || !ctx) {
+        if (gl_dpy != EGL_NO_DISPLAY)
+            eglMakeCurrent(gl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
+        return 0;                       /* OSMesa has no "release" */
+    }
+    return ctx->egl ? EGL_MakeCurrent(_this, window, ctx) : SPR_MakeCurrent(_this, window, ctx);
+}
+
+int
+RISCOS_GL_SwapWindow(_THIS, SDL_Window *window)
+{
+    RISCOS_GLContext *ctx = (RISCOS_GLContext *) SDL_GL_GetCurrentContext();
+    if (!ctx) {
+        return SDL_SetError("No current GL context");
+    }
+    return ctx->egl ? EGL_SwapWindow(_this, window, ctx) : SPR_SwapWindow(_this, window, ctx);
+}
+
+int
+RISCOS_GL_Idle(_THIS, SDL_bool run)
+{
+    SDL_VideoData *vdata = (SDL_VideoData *) _this->driverdata;
+    SDL_Window *window = vdata->wimp_sdl_window;
+    SDL_WindowData *data;
+    EGLint state = 0;
+
+    if (!RISCOS_IsWindowed(vdata) || !window || gl_dpy == EGL_NO_DISPLAY)
+        return 0;
+    data = (SDL_WindowData *) window->driverdata;
+    if (!data || !data->egl_surface || data->egl_handle != vdata->wimp_window)
+        return 0;
+    if (run && data->gl_pending) {
+        /* a held frame: show it once a vsync has passed (only while its
+           surface is still the one being drawn to) */
+        if (eglGetCurrentSurface(EGL_DRAW) != data->egl_surface) {
+            data->gl_pending = 0;
+        } else if (!eglSwapWouldWaitRISCOS(gl_dpy, data->egl_surface)) {
+            data->gl_pending = 0;
+            eglSwapBuffers(gl_dpy, data->egl_surface);
+        }
+    }
+    eglQuerySurface(gl_dpy, data->egl_surface, EGL_OVERLAY_RISCOS, &state);
+    if (run && state != 0)
+        eglCheckOverlaysRISCOS(gl_dpy);     /* covered, uncovered, paused */
+    if (data->gl_pending)
+        return 1;
+    return state != 0 ? 10 : 0;
+}
+
+int
+RISCOS_GL_Redraw(_THIS, SDL_Window *window, void *redraw_block)
+{
+    SDL_WindowData *data = window ? (SDL_WindowData *) window->driverdata : NULL;
+    (void) _this;
+    if (!data || !data->egl_surface || data->egl_handle < 0 || gl_dpy == EGL_NO_DISPLAY)
+        return 0;
+    return eglRedrawWindowRISCOS(gl_dpy, (int *) redraw_block) ? 1 : 0;
+}
+
+void
+RISCOS_GL_DeleteContext(_THIS, SDL_GLContext context)
+{
+    RISCOS_GLContext *ctx = (RISCOS_GLContext *) context;
+    (void) _this;
+    if (ctx && ctx->egl) {
+        if (eglGetCurrentContext() == ctx->eglctx)
+            eglMakeCurrent(gl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
+        eglDestroyContext(gl_dpy, ctx->eglctx);
+        SDL_free(ctx);
+    } else if (ctx) {
+        OSMesaDestroyContext(ctx->osmesa);
+        SDL_free(ctx);
+    }
+}
+
+/* The EGL path's surface (and any overlay): before its desktop window goes. */
+void
+RISCOS_GL_DestroySurface(SDL_Window *window)
+{
+    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
+    if (data && data->egl_surface) {
+        /* Any overlay goes now (the desktop window may be about to go),
+           and a current surface is released first, keeping the context
+           current without one (EGL_KHR_surfaceless_context) until the next
+           frame binds the new surface. */
+        eglSurfaceAttrib(gl_dpy, data->egl_surface, EGL_OVERLAY_RISCOS, EGL_FALSE);
+        if (eglGetCurrentSurface(EGL_DRAW) == data->egl_surface) {
+            EGLContext c = eglGetCurrentContext();
+            if (!eglMakeCurrent(gl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, c))
+                eglMakeCurrent(gl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
+        }
+        eglDestroySurface(gl_dpy, data->egl_surface);
+        data->egl_surface = NULL;
+        data->egl_config = NULL;
+        data->egl_handle = 0;
+        data->gl_pending = 0;
+    }
+}
+
+void
+RISCOS_GL_DestroyWindowBuffer(SDL_Window *window)
+{
+    SDL_WindowData *data = (SDL_WindowData *) window->driverdata;
+    RISCOS_GL_DestroySurface(window);
+    if (data && data->gl_active && data->fb_area) {   /* the sprite path's sprite */
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
