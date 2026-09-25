# riscos-mesa

Software OpenGL for RISC OS 5: **OpenGL 2.1, OpenGL ES 1.1 and OpenGL ES
2.0**, rendered on the CPU by Mesa 20.3.5, with a native RISC OS **EGL**
to set them up. It runs on RISC OS 5 machines with an ARMv7 CPU and VFPv4
(Raspberry Pi 2, 3 and 4, and similar).

**Tested on a Raspberry Pi 4 (RISC OS 5):** release 20.3.5-5 passes all
its tests. That covers EGL in desktop windows and full screen, OpenGL ES
1.1 and 2.0, SDL2, the Raspberry Pi examples and the ported demos. A
640x480 lit, spinning cube runs at about 225 fps in a desktop window, and
GL programs multitask properly.

## Three ways in

1. **EGL: the way to write RISC OS GL and GLES programs.**
   - `libEGL.a` is EGL 1.4 with the common extensions.
   - The native window is a Wimp window handle, or
     `EGL_RISCOS_SCREEN_WINDOW` for the whole screen. Sprites are pixmaps,
     and pbuffers are supported.
   - Your program runs its own Wimp_Poll loop, and EGL does the plotting.
   - See the [programming guide](docs/EGL-GUIDE.md) and
     [egl/README.md](egl/README.md).
2. **SDL2.** SDL 2.26's RISC OS video driver gains OpenGL and OpenGL ES
   contexts (`SDL_GL_CreateContext`). SDL programs port without RISC OS
   specific GL code. The driver uses OSMesa directly and multitasks in a
   desktop window.
3. **Porting aids for existing code.** They sit alongside the native EGL
   and don't replace it:
   - **freeglut (GLUT)** with a native RISC OS back end: GLUT windows are
     Wimp windows, GLUT menus are Wimp menus, and GLUT programs usually
     build unchanged. See [glut/README.md](glut/README.md).
   - [Porting guides](docs/porting/README.md), each with a worked port in
     `ports/`: GLUT programs (freeglut's demos), Mesa's EGL demos (eglut),
     the *OpenGL ES 2.0 Programming Guide* samples (esUtil), SDL 2 GL
     programs, and Raspberry Pi code.
   - **DispmanX compatibility (`libbcm_host`)** for EGL/GLES code written
     for the Raspberry Pi 1–3 Khronos stack. It builds unchanged, and in
     the desktop it runs in a multitasking window. See
     [dispmanx/README.md](dispmanx/README.md).

### A GL window in a few lines of EGL

```c
EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
eglInitialize(dpy, NULL, NULL);
eglChooseConfig(dpy, attrs, &cfg, 1, &n);      /* EGL_RENDERABLE_TYPE, depth ... */
eglBindAPI(EGL_OPENGL_API);                    /* or EGL_OPENGL_ES_API + CLIENT_VERSION 1/2 */
ctx  = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, NULL);
surf = eglCreateWindowSurface(dpy, cfg, wimp_window_handle, NULL);
eglMakeCurrent(dpy, surf, surf, ctx);
/* Wimp_Poll loop: draw + eglSwapBuffers on null events,
   eglRedrawWindowRISCOS(dpy, block) on Redraw_Window_Request */
```

Link with `-lEGL -lOSMesa -lstdc++ -lz -lm`. `tests/egltest.c` (desktop GL)
and `tests/glestest.c` (OpenGL ES) are complete programs.

## What you get

| Library | What it is | Link with |
| --- | --- | --- |
| `libEGL.a` | EGL 1.4 for RISC OS. Contexts: OpenGL 2.1 (compatibility), OpenGL ES 1.1, ES 2.0 (GLSL ES 1.00). Surfaces: Wimp windows (visible area or work area views), full screen (sprite plot after vsync, or straight into screen memory), pbuffers, sprite pixmaps. Extensions: sync objects, surfaceless contexts, buffer age, swap with damage, partial update, surface locking, debug callbacks, platform displays | `-lEGL -lOSMesa -lstdc++ -lz -lm` |
| `libOSMesa.a` | Mesa 20.3.5 classic OSMesa (swrast), one static library: OpenGL 2.1 + GLSL 1.20, OpenGL ES 1.1 and 2.0 | `-lOSMesa -lstdc++ -lz -lm` |
| `libGLU.a` | GLU 1.3 (9.0.1) | `-lGLU` before `-lOSMesa` |
| `libSDL2.a` | SDL 2.26 with the RISC OS driver: desktop windows and full screen, OpenGL and OpenGL ES contexts, typing, the scroll wheel, 180 dpi desktops, cooperative multitasking | `-lSDL2 -lOSMesa -lstdc++ -lz -lm` |
| `libglut.a` | freeglut 3.8.0 with a native RISC OS back end: Wimp windows and subwindows, Wimp menus, full screen and game mode, keyboard (with key releases), mouse, wheel. `libfreeglut-gles.a` is the OpenGL ES build (`-DFREEGLUT_GLES`) | `-lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm` |
| `libbcm_host.a` | DispmanX compatibility, a porting aid for Raspberry Pi 1–3 Khronos code. Empty `libGLESv2`, `libGLESv1_CM`, `libvcos` and `libvchiq_arm` come with it so Pi link lines work | `-lbcm_host -lEGL -lOSMesa -lstdc++ -lz -lm` |

Headers: `EGL/` (with `EGL/eglext_riscos.h` for the RISC OS additions),
`GL/` (with `GL/glut.h` and `GL/freeglut*.h`), `GLES/`, `GLES2/`, `KHR/`,
`SDL2/`, `bcm_host.h` and `interface/`.

## Download

The GitHub [Releases](../../releases) page has, for each release:

| File | Contents |
| --- | --- |
| `riscos-mesa-devkit-VERSION.tgz` | the static libraries, headers and `LICENCES.txt`, for building your own programs |
| `riscos-mesa-tests-VERSION.zip` | the test programs and their Obey files, with a ReadMe |
| `riscos-mesa-hello_pi-VERSION.zip` | the Raspberry Pi's `hello_triangle`, `hello_triangle2` and `hello_teapot`, rebuilt |
| `riscos-mesa-ports-VERSION.zip` | Mesa's EGL demos (gears, torus, ...) and SDL's GL test programs, in desktop windows |
| `riscos-mesa-glut-VERSION.zip` | freeglut's demos (shapes, Lorenz, fractals, subwindows, menus, game mode...) over riscos-mesa's freeglut |

The programs are ELF (&E1F) and need SharedUnixLibrary and ARMEABISupport
(and SharedLibs for the test programs) from PackMan. The zips store RISC OS
filetypes. Releases are numbered after the Mesa version they contain
(`v20.3.5`, then `v20.3.5-2` for the next build of it, and so on). See
[CHANGELOG.md](CHANGELOG.md).

## Limits

- **API versions:** OpenGL 2.1, OpenGL ES 1.1 and ES 2.0. Requests for GL
  3.0+, a core profile or ES 3.x fail. That's what classic OSMesa grants;
  GL 3.x would need Mesa 25.1's softpipe OSMesa, a later option.
- **Speed:** rendering is on the CPU, on one core.
  - Fixed-function GL and ES 1.1 are quick at desktop window sizes: aim
    for 320x240 to 640x480.
  - Shaders (GLSL, ES 2.0) run through Mesa's interpreter and are several
    times slower.
- **Not available:** multisampling, OpenVG, OpenMAX.
- **Screen modes:** best in a 16M-colour mode, where the GL image plots
  with no conversion. EX0 EY0 (180 dpi) desktops are handled.

## Build (Linux, GCCSDK GCC 10 installed)

    export GCCSDK_ENV=/path/to/gccsdk/env   # has bin/arm-riscos-gnueabihf-gcc
    build/build-zlib.sh      # skip if your env already has libz
    build/build-mesa.sh
    build/build-glu.sh
    build/build-sdl2.sh      # SDL 2.26 + patches/sdl2 (the RISC OS overlay)
    build/build-egl.sh       # libEGL.a, libbcm_host.a + headers
    build/build-freeglut.sh  # libglut.a, libfreeglut-gles.a (freeglut 3.8.0 + glut/riscos)
    build/build-tests.sh     # -> stage/tests/*,e1f
    build/build-hello-pi.sh  # Pi hello_triangle/2, hello_teapot -> stage/hello_pi
    build/build-ports.sh     # ported examples (docs/porting) -> stage/ports
    build/package.sh VERSION # the release zips and devkit -> dist/

Everything installs into `stage/`. No GCCSDK GCC 10 yet? See
[build/TOOLCHAIN.md](build/TOOLCHAIN.md). The host needs meson, ninja,
python3-mako, bison, flex, autoconf, automake and libtool.

## Using it from another port

- **Stack probes:** compile your own code with `-fstack-clash-protection`
  too (see `build/env.sh` for the full flags). Large stack frames can
  otherwise crash on RISC OS, and `tools/check-stack-probes.py yourprog`
  checks a binary.
- **New programs:** use EGL. See [docs/EGL-GUIDE.md](docs/EGL-GUIDE.md),
  `tests/egltest.c` and `tests/glestest.c`.
- **GLUT programs:** link `-lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm`;
  see [docs/porting/glut.md](docs/porting/glut.md).
- **Existing programs:** see [docs/porting/](docs/porting/README.md).
- **SDL2:**
  - `patches/sdl2/*.p` is a complete RISC OS driver overlay in GCCSDK
    autobuilder form, shared with riscos-openttd.
  - Configure SDL with `--enable-video-riscos-osmesa` for OpenGL.
  - Don't let SDL's OpenGL *renderer* (`SDL_VIDEO_RENDER_OGL`) get
    enabled: it would pick software GL over the faster software renderer
    for every SDL2 program. The build script checks this.
  - Programs with a GL-or-not choice (e.g. OpenTTD's `sdl-opengl` driver)
    should keep using their non-GL path on RISC OS.
- **Plain OSMesa:** see `tests/osmesatest.c`, which renders straight into a
  32bpp sprite.

## Tests

| Test | Checks |
|---|---|
| `orient`, `prof`, `glutest` | pixel order and row order; which GL versions are granted; GLU |
| `osmesatest`, `glbench` | GL strings, fixed-function and GLSL speed |
| `egltest` (`egl-*` Obey files) | EGL checks and extensions; desktop window (+ work area view, damage demo); full screen (+ direct, screen banks) |
| `glestest` (`gles-*`) | OpenGL ES 1.1 and 2.0 in a desktop window, full screen and into a sprite |
| `dmxtest` (`dmx-*`) | the DispmanX compatibility library, full screen and window mode |
| `sdlgltest` | SDL2 GL in a desktop window: fps in the title, F full screen, Space vsync |
| `hello_pi`, `ports` and `glut` zips | real programs: the Pi examples, Mesa's demos, SDL's GL tests, freeglut's demos |
| `tests/host-harness` | the SDL GL glue on Linux with emulated SWIs |
| `tests/host-harness/egl` | libEGL and libbcm_host on Linux against a fake screen, Wimp and SpriteOp (265 checks); `portrun.c` runs whole ported programs |
| `tests/host-harness/glut` | freeglut's RISC OS back end on Linux: freeglut's demos driven by scripted keys, clicks, drags, menus, the wheel and resizing (23 checks) |

## Licences

- **riscos-mesa's own files** are MIT (`LICENSE`): the EGL library, the
  DispmanX compatibility library, the porting helpers in `ports/`, test
  programs, tools, build scripts and docs.
- **Patches** keep the licence of the files they change:
  - `patches/mesa` is MIT, like Mesa. Its one change to
    `include/c11/threads_posix.h` is Boost.
  - `patches/sdl2` is zlib, like SDL.
  - The RISC OS section of `EGL/eglplatform.h` is under the Khronos
    licence.
- **freeglut's RISC OS back end** (`glut/riscos`) is MIT, like freeglut;
  `patches/freeglut` keeps freeglut's licence.
- **Ported programs** keep theirs: Raspberry Pi userland BSD, mesa-demos
  MIT, SDL's tests zlib, freeglut's demos MIT/X.
- **Third-party components:**
  - Mesa: MIT (some parts Boost / SGI Free B).
  - Khronos headers: Khronos MIT-style.
  - GLU: SGI Free Software Licence B.
  - freeglut: MIT/X style.
  - SDL and zlib: zlib.
  - UnixLib, linked into every GCCSDK program: BSD with some LGPL v2
    parts, so closed source programs must offer their object files for
    relinking.
  - GCC runtime: GPL v3 with the Runtime Library Exception.

[LICENCES.txt](LICENCES.txt) has all the texts. It ships in the tests zip
(`Licences`) and the devkit; include it with programs you build.
