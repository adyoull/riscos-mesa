# EGL for RISC OS (riscos-mesa)

`libEGL.a` is EGL 1.4 on top of Mesa's OSMesa. It lets a program set up
OpenGL through the standard Khronos window-system API instead of calling
OSMesa directly. The code that creates the context and surfaces is then the
same code a future hardware driver (or a shared module) would serve.

    #include <EGL/egl.h>
    #include <EGL/eglext_riscos.h>   /* RISC OS additions */
    #include <GL/gl.h>
    link: -lEGL -lOSMesa -lstdc++ -lz -lm      (compile with -fstack-clash-protection)

`tests/egltest.c` is a complete example: a Wimp task with a GL window, a
full screen program, and pbuffer and pixmap use.

## What it provides
| | |
|---|---|
| Version | EGL 1.4, with `EGL_KHR_create_context`, `EGL_KHR_get_all_proc_addresses` and `EGL_RISCOS_wimp_window` |
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
  Pi 4 this still tears (under investigation), so it's off by default.
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

## Limits
- Not thread safe: make all EGL and GL calls from one thread.
- OSMesa can't un-bind a context. After releasing with `eglMakeCurrent(dpy,
  EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)`, don't make GL calls.
- The draw and read surfaces must be the same. No surfaceless contexts, no
  bind-to-texture, no OpenVG, no EGL 1.5 sync objects.
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
