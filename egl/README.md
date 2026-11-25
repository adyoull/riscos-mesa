# EGL for RISC OS (riscos-mesa): how the library is built

`libEGL.a` is EGL 1.4 on top of Mesa's OSMesa. It lets a program set up
OpenGL, OpenGL ES 1.1 and OpenGL ES 2.0 through the standard Khronos
window-system API instead of calling OSMesa directly. The native types are
RISC OS ones: a Wimp window handle, the whole screen, a sprite.

**To use it, read these instead of this file:**

- the devkit's beginner's guide and examples ([devkit/README.md](../devkit/README.md),
  [devkit/examples](../devkit/examples)), if you're new to it;
- the programming guide, [docs/EGL-GUIDE.md](../docs/EGL-GUIDE.md): the full
  reference, covering windows, views inside windows, full screen, sprites,
  pbuffers, OpenGL ES, every extension, limits, speed and troubleshooting;
- the [porting guides](../docs/porting/README.md), for existing code.

This file is for people working on the library itself.

## In brief

| | |
|---|---|
| API | EGL 1.4, plus the extensions listed in the guide, `EGL_RISCOS_wimp_window`, `EGL_RISCOS_platform_wimp` and `EGL_RISCOS_overlay` |
| Client APIs | OpenGL 2.1 compatibility (GLSL 1.20); OpenGL ES 1.1 and 2.0 (GLSL ES 1.00), through riscos-mesa's OSMesa ES profile patch |
| Configs | 8: RGBA 8888 with depth/stencil 0/0, 16/0, 24/0, 24/8, in each of the two RISC OS 32bpp colour orders |
| Surfaces | Wimp window (visible area or a work area rectangle), the whole screen (sprite plot, direct, or screen banks), pbuffer, sprite pixmap; up to 4096x4096. Visible-area window surfaces can be shown through a hardware overlay (VideoOverlay; opt-in with `EGL_OVERLAY_RISCOS`), falling back to plotting. Visible-area and full screen surfaces can have a render size (`EGL_RENDER_WIDTH/HEIGHT_RISCOS`), stretched by the overlay or the sprite plot (`OS_SpriteOp 52`). Window surfaces can have a window scale (`EGL_WINDOW_SCALE_RISCOS`, `eglWindowScaleRISCOS`): each pixel shown 2x2 on high resolution (EX0 EY0) desktops, so windows are the size they are in a normal mode. Each surface owns its buffers, including depth and stencil (OSMesa buffers, `patches/mesa/*-riscos-osmesa-buffers.patch`) |
| Threads | EGL 1.4's rules: per-thread error, API and current context (one per API); a recursive lock around every call |
| Conformance | Checked with Khronos's dEQP-EGL tests on the host (`tests/host-harness/deqp`); not certified, so `EGL_CONFORMANT` is 0 |
| Registration | The RISC OS extensions' enum values are provisional; `docs/khronos/` has the registration ready to submit |

## Source layout

`egl_riscos.c` is compiled as one file, but its body is in `egl/parts/`,
which it `#include`s in this order:

| File | What's in it |
|---|---|
| `egl_internal.h` | What the parts share: limits, SWI numbers and reason codes, the display/config/context/surface types, the lock macros (`ENTER()`, `LOCK()`), and the prototypes of the helpers one part uses from another, grouped by the part that defines them |
| `egl_riscos.c` | The globals, per-thread state, the lock and error reporting, then the `#include`s of the parts |
| `parts/screen.c` | Screen mode and window state, plotting a frame (window, full screen, damage rectangles, DispmanX) |
| `parts/buffers.c` | Surface buffers, screen banks, pixmap sprites |
| `parts/overlay.c` | Window surfaces shown through a VideoOverlay overlay: creating it, copying frames in, hiding it under other windows, fallbacks |
| `parts/risc3d.c` | Contexts drawn by RISC3D, a hardware path, instead of OSMesa (below) |
| `parts/validation.c` | Looking up and checking display, config, surface and context handles |
| `parts/configs.c` | Building the configs, and matching and sorting them for `eglChooseConfig` |
| `parts/current.c` | Binding contexts to surfaces: OSMesa buffers, the current context per thread and API, releasing |
| `parts/api.c` | The EGL 1.4 functions |
| `parts/extensions.c` | Extension functions (sync, locking, images, platform, debug, RISC OS) and `eglGetProcAddress` |

The library is built as one file (`egl_riscos.c`), so its helpers stay
`static` and invisible to programs. Each part also includes
`egl_internal.h` itself (a no-op in the build), so an editor or clangd can
follow a part on its own. When a part starts using a helper from another,
add its prototype to `egl_internal.h`. Add a new part to the list in
`egl_riscos.c`, not to a makefile.

Every public function starts with `ENTER()`, which takes the library's
lock and releases it when the function returns (GCC's cleanup attribute).
The `fail()` and `ok()` helpers set the calling thread's error.

## RISC3D: a hardware path behind the same EGL

Where the RISC3D module has a driver registered (on RISC OS on Linux, its
Linux3D driver, which has Linux's graphics driver draw), `eglCreateContext`
gives OpenGL contexts (up to 3.1, compatibility profile) and OpenGL ES
1.1, 2.0, 3.0 and 3.1 contexts to RISC3D instead of OSMesa (ES 1.1 needs
RISC3D 0.4's rol-3d, GL 3 and ES 3 0.5's, and a renderer that has them;
only RISC3D gives GL 3 and ES 3: without it they are refused, as before). Programs don't change: they call EGL and GL as before.
Anywhere else, with `RISC3D$Disable` or `LIBGL_ALWAYS_SOFTWARE` set (system
variables; `LIBGL_ALWAYS_SOFTWARE` as Mesa's, 0, false or no don't count),
or if RISC3D can't make the context, OSMesa draws exactly as before.

**Programs don't choose the renderer: RISC3D decides.** Whether a
context is drawn on a GPU or in software follows only from what RISC3D
has loaded on the machine (!RISC3D's !Boot loads RISC3D and the GPU's
driver at start-up), and from the user's `LIBGL_ALWAYS_SOFTWARE` /
`RISC3D$Disable`. A program needs no code for it, and one build draws on
the GPU where RISC3D has one and in software everywhere else. (EGL's
`EGL_CONTEXT_SOFTWARE_RISCOS` attribute is only for the rare program that
needs a context to draw straight from a sprite's memory, which a GPU
context can't.)

**Two kinds of RISC3D driver.** RISC3D is the interface GPU drivers
register with. A command-stream driver (Linux3D, on RISC OS on Linux)
takes the GL calls RISC3D's client writes; a DRM device (a GPU module:
V3DRender on the Pi 4, natively) takes none, and the program runs Mesa's
driver for that GPU itself: the GPU library, `libMesaGPU` (Mesa 26.x with
only its GPU drivers and every symbol hidden, `build/build-v3d.sh`,
`v3d/`). `osmesa/r3d_switch.c` gives RISC3D's client API (`r3d.h`) to
this part and to OSMesa's routing either way, so little here differs:
with the GPU library, a window surface's frames are drawn straight into
the surface's own memory (its sprite, screen memory or a screen bank,
which V3DRender maps into the GPU: `r3d_draw_into`, parts/risc3d.c
`r3dw_place`), and other surfaces' frames are read back as the command
stream's are. Window sprites are page-aligned for it. The GPU library is
used when RISC3D's driver is a DRM device it has a driver for (only
"v3d" so far) and the program has it: a static program links it with
`-Wl,-u,riscos_mesa_gpu -lMesaGPU` (about 15 MB), a mixed one with
`-lMesaGPU` (`libMesaGPU.so.1` in !MesaLibs); without it, such a machine
draws in software. The GPU module is only ever reached through RISC3D.
`v3d/README.md` has the rest.

- **One choice.** `riscos_mesa_pick_renderer(api, major, minor)`
  (`EGL/eglext_riscos.h`) makes it, and `eglCreateContext`, `EGL_VERSION`
  and SDL's GL windows all ask it: `RISCOS_MESA_SOFTWARE` or
  `RISCOS_MESA_RISC3D`, for either kind of RISC3D driver (below;
  `RISCOS_MESA_V3D` is never returned). OpenGL 3 and OpenGL ES 3 stay with OSMesa,
  which refuses them as before. Where RISC3D's driver itself draws in
  software (`RISC3D_Info` r4 bit 1: Linux's Mesa with no GPU driver, as
  under QEMU; riscos-risc3d 0.7-1 and later report it), only GL 3 and
  GLES 3 go to RISC3D: our own renderer is much faster than a software
  one whose frames are read back. `RISC3D$Force` set sends everything to
  RISC3D anyway (for testing). It is in libOSMesa (`osmesa/pick.c`), with
  RISC3D's client library, because OSMesa follows it too:
- **Programs that use OSMesa directly** (`OSMesaCreateContext` and
  `OSMesaMakeCurrent` on their own buffer) draw through RISC3D as well,
  unchanged (`osmesa/risc3d_route.c`, Mesa patch `riscos-osmesa-risc3d`).
  The program's buffer is the RISC3D context's framebuffer (`r3d_target`,
  RISC3D 0.7) and the frame is copied into it at `glFinish` and `glFlush`,
  when another context or buffer is made current, and for
  `OSMesaGetColorBuffer`; `OSMesaGetDepthBuffer` reads the depth back.
  Software draws, as before, contexts with accumulation buffers, colour
  index, core profiles or a share list, buffers of other component types
  (`GL_FLOAT`, `GL_UNSIGNED_SHORT`), and EGL's own OSMesa contexts
  (`OSMESA_SOFTWARE_RISCOS`). A program that writes into its buffer itself
  between frames would see that overwritten: `LIBGL_ALWAYS_SOFTWARE` keeps
  it in software.

- **How the calls get there.** The `gl*` entry points are glapi's stubs,
  calling through the current dispatch table. For a RISC3D context that
  table is built once from the client library's list of functions
  (`r3d_gl_functions`, by name: `OSMesaMakeDispatchRISCOS`, Mesa patch
  `riscos-dispatch`) and set when the context becomes current, so a call
  costs no more than with OSMesa.
- **Frames.** `eglSwapBuffers` takes the finished frame with `r3d_swap` into
  the window surface's pixels (converting to the surface's colour order and
  row length), then shows it as before: plot, overlay or screen bank.
  `EGL$Overlapped` set to 1 when a context is made uses
  `r3d_swap_overlapped` instead: no wait for the GPU, a frame of latency.
- **Pbuffers and pixmaps.** `glFinish` (and `eglWaitClient`/`eglWaitGL`,
  which call it) copies the frame into a pbuffer or pixmap being drawn, so
  its pixels are there as they are with OSMesa; so does `eglCopyBuffers`.
- **Zero copy for windows.** Where RISC3D can put a context's frames on a
  display plane (RISC OS on Linux with rol-fb's overlays: `r3d_overlay`),
  a visible-area window surface's frames go there and nothing is read
  back. The picture is under the desktop and shows where the desktop is the
  key colour, so EGL paints the window's visible area in it (and repaints
  it in redraws), and windows and menus over it clip it with nothing more
  to do. Only in a 16 million colour mode, with a swap interval above 0
  (the plane is paced by the display), without `EGL$Overlapped`, unless
  the program set `EGL_OVERLAY_RISCOS` to `EGL_FALSE` or `EGL$Overlay` is
  off. If the plane stops (a mode change), frames are read back again and
  the window replotted.
- **EGLImage.** `glEGLImageTargetTexture2DOES` on a RISC3D context copies
  the sprite into the texture at the call: RISC3D can't draw from RISC OS
  memory, so a later change to the sprite needs the call again. A program
  that relies on the texture following the sprite asks for an OSMesa
  context with `EGL_CONTEXT_SOFTWARE_RISCOS` (`EGL_RISCOS_renderer`);
  `eglQueryContext` with the same name says which renderer a context got.
  With the GPU library (natively, from 20.3.5-23) an image over the
  program's own memory (`EGL_RISCOS_image_memory`) is the library's own:
  the GPU draws into it in place through
  `glEGLImageTargetRenderbufferStorageOES`, or samples it through
  `glEGLImageTargetTexture2DOES`, with no copy (`egl/include/EGL/eglext_riscos.h`).
- **Strings.** RISC3D answers `GL_VERSION` with what its stream carries
  ("2.1 RISC3D (...)", "OpenGL ES 2.0 RISC3D (...)") and trims
  `GL_EXTENSIONS`; EGL adds `GL_OES_EGL_image`, whose function it gives.
  `EGL_VERSION` says when RISC3D draws.
- **Sharing objects:** with the GPU library (natively, from 20.3.5-23) a
  context created sharing a RISC3D context's objects is another RISC3D
  context sharing them (`r3d_create_shared`, Mesa's own sharing); with
  RISC3D's command stream, the same through RISC3D client 0.12 when
  `r3d_can_share()` (RISC3D and Linux3D 0.06, rol-3d 0.20, from
  20.3.5-23), else `EGL_BAD_MATCH`. Sharing with an OSMesa context gives
  an OSMesa one.
- **Threads:** with the GPU library, each thread has its own current
  context: several threads draw at once (`osmesa/r3d_switch.c` keeps the
  current context per thread, and Mesa 26's glapi switches to per-thread
  dispatch once a second thread makes a context current; one thread costs
  nothing more). With the command stream, RISC3D client 0.12 keeps the
  current context per thread too (`r3d_make_current` is the calling
  thread's). EGL's own idea of RISC3D's current context is the calling
  thread's (`r3dw_current`).

The client library is in `risc3d/` (RISC3D client 0.13, MIT), built into libOSMesa by
`build/build-mesa.sh`. The host harness checks the wiring against a fake
RISC3D (`test_risc3d`), and `tests/host-harness/egl/r3d_e2e.sh` runs it
end to end with RISC3D's real client and Linux helper, drawn by the build
machine's Mesa (run-all's `risc3d` step, given `R3D_SRC=`).

## Testing

After changing anything here, run `tests/run-all.sh` (see its header for
the settings). The parts that exercise EGL are:

- `tests/host-harness/egl`: the library against a host-built OSMesa and a
  fake RISC OS (screen, Wimp, OS_SpriteOp), with over 400 checks of what
  reaches the "screen" (the overlay checks, `harness_ovl.c`, use the fake
  VideoOverlay in `tests/host-harness/ovl/fake_ovl.c`);
- `tests/host-harness/examples`: the devkit's examples, run on the same
  fake;
- `tests/host-harness/glut`: freeglut's RISC OS back end, which uses EGL.

`tests/host-harness/deqp` builds and runs Khronos's dEQP-EGL tests (a long
first build; not part of run-all). Compare with its `expected.txt`.

On a Raspberry Pi: `egltest` and `glestest` in the tests zip, with the
`egl-*` and `gles-*` Obey files (`egl-overlay` and `egl-scaled*` for
overlays and the render size), and `sdlgltest`'s `sdl-scaled*` for
SDL's GL windows through EGL. The extension spec draft is
`docs/khronos/EGL_RISCOS_overlay.txt`.

## Design notes: toward a common RISC OS GL interface

Cameron Cawley's ROOL thread "Generic OpenGL interface" (July 2024) asked
how one application could target every GL implementation on RISC OS.
This library is one concrete answer to the parts that can be settled now:

1. **Native types.** The Khronos module ties them to DispmanX, which only
   exists on Pi 1-3. These are about RISC OS itself: Wimp window handles,
   the whole screen, sprites. Any back end can use them. A hardware driver
   would render into its own buffers and do the same
   Wimp_UpdateWindow/redraw work (or use an overlay) behind the same API.
2. **Wimp integration.** GL output lives in the window's own redraw cycle,
   so covering, dragging and scrolling work. No layer sits on top of the
   desktop, and nothing is left behind when a program crashes.
3. **Desktop GL and GLES.** Classic swrast runs GL 2.1, ES 1.1 and ES 2.0
   contexts. With the DispmanX compatibility library, GLES code written for
   the Pi 1-3 Khronos module runs on every machine.
4. **Linking.** Today it's static (`libEGL.a` + `libOSMesa.a`). The API
   boundary is the standard Khronos one, so the implementation can later
   move behind a relocatable module with a function table (like the Shared
   C Library's stubs, which avoid a SWI per GL call) or into SOManager
   shared libraries, without changing application source.
5. **Per-process state.** Each program has its own copy of the library, so
   GL state is per task by construction. A shared module version will need
   a context per client task.
