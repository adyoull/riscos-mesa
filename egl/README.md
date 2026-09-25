# EGL for RISC OS (riscos-mesa)

`libEGL.a` is EGL 1.4 on top of Mesa's OSMesa. It lets a program set up
OpenGL through the standard Khronos window-system API instead of calling
OSMesa directly. The code that creates the context and surfaces is then the
same code a future hardware driver (or a shared module) would serve.

    #define EGL_EGLEXT_PROTOTYPES 1        /* to call extension functions directly */
    #include <EGL/egl.h>
    #include <EGL/eglext.h>
    #include <EGL/eglext_riscos.h>   /* RISC OS additions */
    #include <GL/gl.h>
    link: -lEGL -lOSMesa -lstdc++ -lz -lm      (compile with -fstack-clash-protection)

`tests/egltest.c` is a complete example: a Wimp task with a GL window, a
full screen program, and pbuffer and pixmap use.

## What it provides
| | |
|---|---|
| Version | EGL 1.4, with the extensions listed under [Extensions](#extensions) (sync objects, surfaceless contexts, buffer age, swap with damage, surface locking, debug callbacks, platform displays) and `EGL_RISCOS_wimp_window` |
| Client API | `EGL_OPENGL_API` only: OpenGL 2.1 compatibility profile, GLSL 1.20 (Mesa 20.3 classic swrast). A request for GL 3.x or core gives `EGL_BAD_MATCH`. `eglBindAPI(EGL_OPENGL_ES_API)` fails for now. |
| Configs | 8: RGBA 8888 with depth/stencil 0/0, 16/0, 24/0, 24/8, in each of the two RISC OS 32bpp colour orders. The configs matching the current screen mode have the lowest IDs. Caveat `EGL_NONE`, no multisampling. |
| Surfaces | window, pbuffer (up to 4096x4096), pixmap. All preserve their contents across swaps. |

**Remember the EGL default:** `eglChooseConfig` matches `EGL_RENDERABLE_TYPE`
= `EGL_OPENGL_ES_BIT` unless you say otherwise. Ask for `EGL_OPENGL_BIT`, or
you get no configs.

## Native types
| EGL type | RISC OS meaning |
|---|---|
| `EGLNativeDisplayType` (int) | `EGL_DEFAULT_DISPLAY` (0) = the screen. Nothing else is accepted. |
| `EGLNativeWindowType` (int) | A Wimp window handle, or `EGL_RISCOS_SCREEN_WINDOW` (-1) for the whole screen. |
| `EGLNativePixmapType` (void *) | A pointer to a 32bpp sprite (its header, in any sprite area). |

### Wimp windows
- A window surface covers the window's **visible area**. It stays put when
  the window scrolls, and it follows the window's size. After a resize, the
  next `eglSwapBuffers` shows the finished frame and then resizes the
  surface, so query `EGL_WIDTH`/`EGL_HEIGHT` (and set `glViewport`) each
  frame.
- To put a GL view **inside** a window that has other content, pass
  `EGL_WORK_AREA_X_RISCOS`, `EGL_WORK_AREA_Y_RISCOS` (the top left corner,
  in work area OS units) and `EGL_WORK_AREA_WIDTH_RISCOS`,
  `EGL_WORK_AREA_HEIGHT_RISCOS` (pixels). This gives a fixed-size surface
  that scrolls with the work area. One window can have several surfaces.
  Work area surfaces are drawn on top of a visible area surface.
- `eglSwapBuffers` shows the frame with a `Wimp_UpdateWindow` loop. It never
  calls `Wimp_Poll`: your task keeps its own event loop.
- **Redraws:** on `Redraw_Window_Request`, call
  `eglRedrawWindowRISCOS(dpy, poll_block)`. It runs the whole
  `Wimp_RedrawWindow` loop and plots every EGL surface in that window. It
  returns `EGL_FALSE` (without starting a redraw) if the window has no
  surfaces. If you also draw other things in the window, run the loop
  yourself and call `eglPlotSurfaceRISCOS(dpy, surface, block)` for each
  rectangle.
- Surface sprites smaller than 1 MB are padded with unused rows (never
  shown). On the Pi 4, something caches small sprites between plots, so
  a small surface otherwise kept showing its first image.
- Give the window a background colour. The Wimp then clears any part the GL
  image doesn't cover, for example just after a resize.
- **Swap interval** doesn't wait in a window, because waiting would stop
  every task. Pace frames with `Wimp_PollIdle`.

### Full screen (`EGL_RISCOS_SCREEN_WINDOW`)
- The surface is the size of the screen mode. The swap waits for vsync
  (`OS_Byte 19`) as many times as the swap interval says (default 1,
  0 = don't wait).
- **Sprite (default):** GL renders into a sprite that the swap plots at the
  top left of the screen.
- **Screen banks (experimental):** pass `EGL_SCREEN_BANKS_RISCOS` = 2 or 3
  when creating the surface. In a 32bpp mode with the config's colour
  order, GL then renders into a screen bank that isn't being shown, and the
  swap switches the display to it (`OS_Byte 113`), with no copy. On the
  Pi 4 this tears badly: the display switch isn't applied in step with the
  vsync. So it's off by default; the sprite method gives clean frames there.
  `eglQuerySurface(EGL_SCREEN_BANKS_RISCOS)` says how many banks are in use (0
  if screen memory was too small). Bank contents aren't preserved across
  swaps (`EGL_SWAP_BEHAVIOR` is `EGL_BUFFER_DESTROYED`); setting
  `EGL_BUFFER_PRESERVED` goes back to the sprite. The display returns to
  bank 1 when the surface goes, and at exit.
- **Single buffer:** with `EGL_RENDER_BUFFER` set to `EGL_SINGLE_BUFFER`, GL
  renders straight into the visible screen. Nothing is copied, but you see
  the frame as it's drawn (tearing). `eglQuerySurface(EGL_RENDER_BUFFER)`
  tells you which one you got.
- A mode change is picked up at the next swap.

### Pixmaps
A pixmap surface renders straight into the sprite's image, so a GL result is
an ordinary sprite you can plot, save or edit. The sprite must be 32bpp,
with its colour order matching the config: `EGL_MATCH_NATIVE_PIXMAP` in
`eglChooseConfig` picks one. It must not move or change size while the
surface exists. `eglCopyBuffers` copies any surface into a 32bpp sprite,
swapping red and blue if the orders differ.

### Colour order
32bpp RISC OS modes are either `0x00BBGGRR` (ModeFlags bit 14 clear, sprite
type 6) or `0x00RRGGBB` (bit 14 set). `EGL_NATIVE_VISUAL_ID` of a config is
that bit: `EGL_RISCOS_VISUAL_TBGR` (0) or `EGL_RISCOS_VISUAL_TRGB` (0x4000).
If a window surface's order matches the screen, the plot is a straight copy.
Otherwise SpriteExtend converts it. Screen modes below 16M colours work
through SpriteExtend too, but haven't been tested.

## Extensions

`eglQueryString(dpy, EGL_EXTENSIONS)` lists the display extensions below;
`eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS)` lists the client ones.
Every function is also returned by `eglGetProcAddress`. To call them
directly, `#define EGL_EGLEXT_PROTOTYPES 1` before including `EGL/egl.h`
and include `EGL/eglext.h`.

| Extension | What it does here |
| --- | --- |
| `EGL_KHR_create_context` | GL version, profile and flags when creating a context (2.1 compatibility is what you get) |
| `EGL_KHR_get_all_proc_addresses`, `EGL_KHR_client_get_all_proc_addresses` | `eglGetProcAddress` returns core functions as well |
| `EGL_KHR_surfaceless_context` | `eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)` works: for loading textures or rendering to framebuffer objects without a window. Giving just one of draw and read is `EGL_BAD_MATCH` |
| `EGL_KHR_fence_sync`, `EGL_KHR_reusable_sync`, `EGL_KHR_wait_sync` | Sync objects. GL runs on the CPU, in order, so a fence is signalled as soon as it's created (the library calls `glFinish`). With one thread nothing can signal a reusable sync while `eglClientWaitSyncKHR` waits, so an unsignalled one returns `EGL_TIMEOUT_EXPIRED_KHR` at once, whatever the timeout |
| `EGL_EXT_buffer_age` | `eglQuerySurface(..., EGL_BUFFER_AGE_EXT, ...)` on the current surface: 0 = contents unknown (first frame, or the surface was just resized or the mode changed), 1 = the buffer still holds the previous frame (window sprites, full screen sprite, direct rendering), N = the frame from N swaps ago (N screen banks). Pbuffers and pixmaps: 0 |
| `EGL_KHR_swap_buffers_with_damage`, `EGL_EXT_swap_buffers_with_damage` | `eglSwapBuffersWithDamageKHR(dpy, surf, rects, n)`, rectangles x, y, w, h in pixels from the bottom left. In a window only those parts are updated (one `Wimp_UpdateWindow` each; more than 16 become their bounding box); full screen only those parts are plotted after the vsync wait. Screen banks and direct rendering show the whole frame. `n` = 0 is a normal swap |
| `EGL_KHR_partial_update` | After querying the buffer age, `eglSetDamageRegionKHR` says which parts of the surface this frame will change; the next `eglSwapBuffers` then shows only those |
| `EGL_KHR_lock_surface`, `2`, `3` | `eglLockSurfaceKHR` gives direct access to a surface's pixels (a surface that isn't current): query `EGL_BITMAP_POINTER_KHR`, `EGL_BITMAP_PITCH_KHR` (bytes), origin (always `EGL_UPPER_LEFT_KHR`) and the pixel offsets (red at 0, blue at 16 for 0x00BBGGRR configs; the other way round for 0x00RRGGBB), `eglQuerySurface64KHR` for the pointer as an `EGLAttribKHR`. A locked surface can't be made current or swapped. After `eglUnlockSurfaceKHR`, `eglSwapBuffers` shows a window surface written this way even though no context is current to it. `EGL_MATCH_FORMAT_KHR`: 0x00RRGGBB configs are `EGL_FORMAT_RGBA_8888_EXACT_KHR` (B, G, R, A bytes), 0x00BBGGRR ones `EGL_FORMAT_RGBA_8888_KHR` |
| `EGL_KHR_context_flush_control` | `EGL_CONTEXT_RELEASE_BEHAVIOR_KHR` = `EGL_CONTEXT_RELEASE_BEHAVIOR_NONE_KHR` skips the flush when a context stops being current |
| `EGL_KHR_debug` | `eglDebugMessageControlKHR` sets a callback that gets every EGL error with the function name and object labels (`eglLabelObjectKHR`); `eglQueryDebugKHR` reads the settings. Errors and critical messages are on by default |
| `EGL_EXT_client_extensions`, `EGL_EXT_platform_base`, `EGL_RISCOS_platform_wimp` | `eglGetPlatformDisplayEXT(EGL_PLATFORM_RISCOS, NULL, NULL)`. For `eglCreatePlatformWindowSurfaceEXT` the native window is a *pointer to* an int holding the Wimp handle (or -1); for `eglCreatePlatformPixmapSurfaceEXT` it's the sprite pointer. `EGL_PLATFORM_RISCOS` is provisional |
| `EGL_RISCOS_wimp_window` | The RISC OS native types, work area surfaces, full screen options and redraw helpers (above) |

`egltest -w -D` (Obey file `egl-damage`) is a small example of buffer age
with swap with damage.

## Limits
- Not thread safe: make all EGL and GL calls from one thread.
- OSMesa can't un-bind a context. After releasing with `eglMakeCurrent(dpy,
  EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)`, don't make GL calls.
- The draw and read surfaces must be the same. No bind-to-texture, no
  OpenVG, no EGL 1.5 entry points (the KHR sync extensions are there).
- The enum values and function names of `EGL_RISCOS_wimp_window` are
  provisional: they aren't registered with Khronos.

## Design notes: toward a common RISC OS GL interface
Cameron Cawley's ROOL thread "Generic OpenGL interface" (July 2024) asked
how one application could target every GL implementation on RISC OS.
This library is one concrete answer to the parts that can be settled now:

1. **Native types.** The Khronos module ties them to DispmanX, which only
   exists on Pi 1-3. The definitions above are about RISC OS itself: Wimp
   window handles, the whole screen, sprites. Any back end can use them.
   A hardware driver would render into its own buffers and do the same
   Wimp_UpdateWindow/redraw work (or use an overlay) behind the same API.
2. **Wimp integration.** GL output lives in the window's own redraw cycle,
   so covering, dragging and scrolling work. No layer sits on top of the
   desktop, and nothing is left behind when a program crashes.
3. **EGL + desktop GL now, GLES next.** Classic swrast can also run GLES
   1.1/2.0 contexts. Adding them needs a small OSMesa patch plus
   `EGL_OPENGL_ES_API`, and would let GLES code written for the Pi 1-3
   Khronos module run on every machine.
4. **Linking.** Today it's static (`libEGL.a` + `libOSMesa.a`). The API
   boundary is the standard Khronos one, so the implementation can later
   move behind a relocatable module with a function table (like the Shared
   C Library's stubs, which avoid a SWI per GL call) or into SOManager
   shared libraries, without changing application source.
5. **Per-process state.** Each program has its own copy of the library, so
   GL state is per task by construction. A shared module version will need
   a context per client task.
