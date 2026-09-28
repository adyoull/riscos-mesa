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
| Surfaces | Wimp window (visible area or a work area rectangle), the whole screen (sprite plot, direct, or screen banks), pbuffer, sprite pixmap; up to 4096x4096. Visible-area window surfaces are shown through a hardware overlay (VideoOverlay) when there is one, falling back to plotting. Each surface owns its buffers, including depth and stencil (OSMesa buffers, `patches/mesa/*-riscos-osmesa-buffers.patch`) |
| Threads | EGL 1.4's rules: per-thread error, API and current context (one per API); a recursive lock around every call |
| Conformance | Checked with Khronos's dEQP-EGL tests on the host (`tests/host-harness/deqp`); not certified, so `EGL_CONFORMANT` is 0 |
| Registration | The RISC OS extensions' enum values are provisional; `docs/khronos/` has the registration ready to submit |

## Source layout

`egl_riscos.c` is compiled as one file, but its body is in `egl/parts/`,
which it `#include`s in this order:

| File | What's in it |
|---|---|
| `egl_riscos.c` | Headers, limits, the display/config/context/surface types, per-thread state and the lock (`ENTER()`), globals |
| `parts/screen.c` | Screen mode and window state, plotting a frame (window, full screen, damage rectangles, DispmanX) |
| `parts/buffers.c` | Surface buffers, screen banks, pixmap sprites |
| `parts/overlay.c` | Window surfaces shown through a VideoOverlay overlay: creating it, copying frames in, hiding it under other windows, fallbacks |
| `parts/validation.c` | Looking up and checking display, config, surface and context handles |
| `parts/configs.c` | Building the configs, and matching and sorting them for `eglChooseConfig` |
| `parts/current.c` | Binding contexts to surfaces: OSMesa buffers, the current context per thread and API, releasing |
| `parts/api.c` | The EGL 1.4 functions |
| `parts/extensions.c` | Extension functions (sync, locking, images, platform, debug, RISC OS) and `eglGetProcAddress` |

The parts can't be compiled on their own: helpers stay `static`. Add a new
part to the list in `egl_riscos.c`, not to a makefile.

Every public function starts with `ENTER()`, which takes the library's
lock and releases it when the function returns (GCC's cleanup attribute).
The `fail()` and `ok()` helpers set the calling thread's error.

## Testing

After changing anything here, run `tests/run-all.sh` (see its header for
the settings). The parts that exercise EGL are:

- `tests/host-harness/egl`: the library against a host-built OSMesa and a
  fake RISC OS (screen, Wimp, OS_SpriteOp), with about 400 checks of what
  reaches the "screen" (the overlay checks, `harness_ovl.c`, use the fake
  VideoOverlay in `tests/host-harness/ovl/fake_ovl.c`);
- `tests/host-harness/examples`: the devkit's examples, run on the same
  fake;
- `tests/host-harness/glut`: freeglut's RISC OS back end, which uses EGL.

`tests/host-harness/deqp` builds and runs Khronos's dEQP-EGL tests (a long
first build; not part of run-all). Compare with its `expected.txt`.

On a Raspberry Pi: `egltest` and `glestest` in the tests zip, with the
`egl-*` and `gles-*` Obey files.

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
