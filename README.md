# riscos-mesa

OpenGL for RISC OS 5: **OpenGL 2.1, OpenGL ES 1.1 and OpenGL ES 2.0**,
rendered on the CPU by Mesa 20.3.5, with a native RISC OS **EGL** to set
them up. It runs on RISC OS 5 machines with an ARMv7 or later CPU and
VFPv3 floating point: Raspberry Pi 2, 3 and 4, and Cortex-A8/A9/A15
machines such as the BeagleBoard-xM, PandaBoard, ARMini, i.MX6 boards and
Titanium. Not on the Pi 1 or Zero (ARMv6).

**Where RISC3D has a GPU driver, the same programs draw on the GPU** (up
to OpenGL ES 3.1, and OpenGL 3.1 under RISC OS on Linux): on a Raspberry
Pi 4 running RISC OS 5 through the V3DRender module and riscos-mesa's GPU
library, `libMesaGPU` (Mesa 26.1.6's v3d driver), and under RISC OS on
Linux through Linux's own driver. See [The GPU](#the-gpu).

**Tested on a Raspberry Pi 4 (RISC OS 5).** EGL in desktop windows and
full screen, OpenGL ES 1.1 and 2.0, SDL2, freeglut, the Raspberry Pi
examples, the ported demos and the devkit's six examples all run there. A
640x480 lit, spinning cube runs at about 225 fps in a desktop window, and
GL programs multitask properly. In 20.3.5-10, a 1024x768 SDL window
rendering at 640x480 goes from 78 fps to 214 fps through the hardware
overlay, and SDL draws sprites with soft edges about 1.6 times as fast.
20.3.5-12 draws faster with the same picture: on a Pi 4, GLSL shaders
take 29% less time, fog half, depth clears 62% less and lit drawing
12-43% less. 20.3.5-13 loads the ETC1 textures that Pi ES programs
ship, runs ES 2.0 shaders in a desktop GL context, and lets a full
screen program work instead of waiting for the vsync in its swap (5.6 ms
a frame back with a light scene on a Pi 4). 20.3.5-14 uses NEON for
textures, transparency and GLSL shaders: on a Pi 4 a bilinear texture
draws in half the time, blending in half, and shaders twice as
fast, with the same picture (shaders within a step in 255; they're now
compiled to ARM code by a small JIT of our own, explained in the EGL
guide's "How shaders are made faster"); TORCS,
YSFlight and Warzone 2100 run 14-30% faster. 20.3.5-15 speeds up 3D
scenes: the depth test, fog and the alpha test use NEON, depth-tested
rows of pixels take a direct route, and pixels that end up hidden aren't
textured; glbench's racing-game-like scene draws 14% faster than with
20.3.5-14 on a Pi 4, with the same picture. 20.3.5-16 opens EGL and
GLUT windows at the right size on high resolution (EX0 EY0, "180 dpi")
desktops, as SDL windows already were: each pixel is shown 2x2, so a
window is the size it is in a normal mode and costs no more to draw.
20.3.5-17 gives SDL programs that draw in software (2D games using
`SDL_GetWindowSurface`) the same render size and hardware overlay, as an
option: the game draws at its own size and the Pi's display scales it, in
a window or full screen, sending only what changed each frame (under half
a millisecond a frame on a Pi 4). It also stops the icon bar flashing over
full screen SDL and GLUT games, and `*Show EGL$OverlayStatus` says how a
program's picture is being shown. 20.3.5-18 moves to SDL 2.32.10 and
UnixLib 5.0.3.6: SDL programs get focus events, pointer warping and
relative mouse mode (mouse look), and SDL's and OpenAL's threads sleep
while they wait instead of spinning; OpenAL mixes with NEON. Not tried on
a Pi yet: the overlay across mode changes, and GLUT's game mode at the
requested size. 20.3.5-19 sends programs' drawing to the GPU through
RISC3D under RISC OS on Linux, and adds shared libraries for "mixed"
programs (!MesaLibs). 20.3.5-20 does it natively on a Pi 4: a 640x480
window at 365 fps, full screen at the display's 60 fps, glbench's scenes
in 1.6-2.8 ms a frame against 9.7-13.6 ms in software. 20.3.5-21 puts the
loader mixed programs need into !MesaLibs.

## The GPU

**Programs don't choose between the GPU and software: RISC3D decides.**
RISC3D (module 0.05 or later, from [riscos-risc3d](https://github.com/adyoull/riscos-risc3d),
!RISC3D in `!Boot.Resources`) loads the machine's 3D driver at start-up,
and riscos-mesa's EGL and OSMesa draw with whatever it has:

| Where | RISC3D's driver | Who draws |
|---|---|---|
| Raspberry Pi 4, 400 or CM4, RISC OS 5 | V3DRender ([riscos-v3drender](https://github.com/adyoull/riscos-v3drender), 0.15 or later) | the GPU, through the program's GPU library, `libMesaGPU` |
| RISC OS on Linux (Pi 4, Pi 5...) | Linux3D | the GPU, through Linux's own Mesa |
| anywhere else, or no !RISC3D | none | riscos-mesa's software renderer, as always |

- **On native RISC OS the GPU's driver runs inside the program**: Mesa
  26.1.6's v3d driver, as the GPU library. It sits beside Mesa 20.3.5 with
  its names hidden, and sends its work to V3DRender through RISC3D.
- **A program carries it by linking it once:** `-lMesaGPU` after
  `-lOSMesa` for a mixed program (`libMesaGPU.so.1` is in !MesaLibs), or
  `-Wl,-u,riscos_mesa_gpu -lMesaGPU` for a static one (about 15 MB). It
  doesn't make the program use the GPU; it lets RISC3D give it one.
- **Programs linked without it**, including everything linked before
  20.3.5-20, draw in software on a native Pi 4, and still use the GPU
  under RISC OS on Linux.
- **The user can keep everything in software** with the system variables
  `LIBGL_ALWAYS_SOFTWARE` or `RISC3D$Disable`.
- **What it gives:**
  - on the GPU: OpenGL 2.1 (compatibility) and OpenGL ES 1.1 to 3.1
    natively, and up to OpenGL 3.1 under RISC OS on Linux;
  - windows, full screen and screen banks are drawn straight into their
    own memory.

[v3d/README.md](v3d/README.md) describes the GPU library: how it's built,
what it does, and the `V3DM$Stats` and `V3DM$Why` variables that show
where a GPU program's time goes. [egl/README.md](egl/README.md) has
RISC3D's side of EGL.

## Three ways in

1. **EGL: the way to write RISC OS GL and GLES programs.**
   - `libEGL.a` is EGL 1.4 with the common extensions.
   - The native window is a Wimp window handle, or
     `EGL_RISCOS_SCREEN_WINDOW` for the whole screen. Sprites are pixmaps
     (GL draws into them) and images (textures that read them in place,
     for video), and pbuffers are supported.
   - Your program runs its own Wimp_Poll loop, and EGL does the plotting.
   - Opt-in on the Pi: a window shown through the display's hardware
     overlay (VideoOverlay), and a render size smaller than the window,
     stretched to fill it. See the guide's
     [Hardware overlays](docs/EGL-GUIDE.md#hardware-overlays-egl_riscos_overlay).
   - New to it? Start with the devkit's [beginner's guide](devkit/README.md)
     and [examples](devkit/examples). The [programming guide](docs/EGL-GUIDE.md)
     is the full reference.
2. **SDL2.** SDL 2.32's RISC OS video driver gains OpenGL and OpenGL ES
   contexts (`SDL_GL_CreateContext`). SDL programs port without RISC OS
   specific GL code. By default the driver renders into the window's
   sprite and multitasks in a desktop window. Hints (or system variables)
   switch a GL window to EGL for a smaller render size and the hardware
   overlay: see [docs/porting/sdl2.md](docs/porting/sdl2.md).
   Desktop-size full screen is a borderless window that keeps the
   desktop multitasking, and SDL's 2D drawing uses the CPU's NEON or SIMD
   instructions (about 1.6 times as fast for sprites with soft edges on a
   Pi 4).
3. **Porting aids for existing code.** They sit alongside the native EGL
   and don't replace it:
   - **freeglut (GLUT)** with a native RISC OS back end: GLUT windows are
     Wimp windows, GLUT menus are Wimp menus, and GLUT programs usually
     build unchanged. Game mode draws at the resolution the program asks
     for, stretched over the screen (through the hardware overlay on a
     Pi). See [glut/README.md](glut/README.md).
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
| `libEGL.a` | EGL 1.4 for RISC OS. Contexts: OpenGL 2.1 (compatibility), OpenGL ES 1.1, ES 2.0 (GLSL ES 1.00). Surfaces: Wimp windows (visible area or work area views), full screen (sprite plot after vsync, or straight into screen memory), pbuffers, sprite pixmaps. Extensions: sync objects, surfaceless contexts, buffer age, swap with damage, partial update, surface locking, sprites as textures with no copy (EGLImage, for video), debug callbacks, platform displays; RISC OS hardware overlays (VideoOverlay) and a render size scaled to the window or screen, both opt-in | `-lEGL -lOSMesa -lstdc++ -lz -lm` |
| `libOSMesa.a` | Mesa 20.3.5 classic OSMesa (swrast), one static library: OpenGL 2.1 + GLSL 1.20, OpenGL ES 1.1 and 2.0 | `-lOSMesa -lstdc++ -lz -lm` |
| `libGLU.a` | GLU 1.3 (9.0.1) | `-lGLU` before `-lOSMesa` |
| `libSDL2.a` | SDL 2.32.10 with the RISC OS drivers: desktop windows and full screen, OpenGL and OpenGL ES contexts (optionally through EGL, for a smaller render size and the hardware overlay), typing, the scroll wheel, 180 dpi desktops, cooperative multitasking, and sound through SharedSoundBuffer (mixes with other programs) | `-lSDL2 -lEGL -lOSMesa -lstdc++ -lz -lm` |
| `libopenal.a` | OpenAL Soft 1.19.1: OpenAL 1.1 with the EFX effects and positioned (3D) sources, mixed in software and played through SDL's sound driver (so it mixes with other programs) | `-lopenal -lSDL2 -lEGL -lOSMesa -lstdc++ -lz -lm` |
| `libglut.a` | freeglut 3.8.0 with a native RISC OS back end: Wimp windows and subwindows, Wimp menus, full screen and game mode, keyboard (with key releases), mouse, wheel. `libfreeglut-gles.a` is the OpenGL ES build (`-DFREEGLUT_GLES`) | `-lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm` |
| `libbcm_host.a` | DispmanX compatibility, a porting aid for Raspberry Pi 1–3 Khronos code. Empty `libGLESv2`, `libGLESv1_CM`, `libvcos` and `libvchiq_arm` come with it so Pi link lines work | `-lbcm_host -lEGL -lOSMesa -lstdc++ -lz -lm` |
| `libMesaGPU.a` | The GPU library: Mesa 26.1.6's v3d driver for a Pi 4's GPU on native RISC OS, used when RISC3D has V3DRender loaded (see [The GPU](#the-gpu)), never otherwise. About 15 MB | add `-Wl,-u,riscos_mesa_gpu -lMesaGPU` after `-lOSMesa` |

**Shared libraries** (a test, from 20.3.5-19): the devkit's `lib/shared`
has every library above except `libbcm_host` as a RISC OS shared library
(`libOSMesa.so.8`, `libEGL.so.1`, `libGLU.so.1`, `libglut.so.3`,
`libSDL2-2.0.so.0`, `libopenal.so.1`, `libMesaGPU.so.1`). A *mixed*
program has UnixLib linked in and loads these from **!MesaLibs** in
`!Boot.Resources`, so programs share one copy and each is much smaller.
!MesaLibs also carries the `ld-riscos/so/2` loader and SOManager 3.04 and
puts them into !SharedLibs when it boots (20.3.5-21). See
[docs/porting/mixed.md](docs/porting/mixed.md).

Headers: `EGL/` (with `EGL/eglext_riscos.h` for the RISC OS additions),
`GL/` (with `GL/glut.h` and `GL/freeglut*.h`), `GLES/`, `GLES2/`, `KHR/`,
`SDL2/`, `AL/`, `bcm_host.h` and `interface/`.

OpenAL mixes in SDL's audio thread, so a program using it has more than
one thread: link it with UnixLib 5.0.1 or later
([riscos-unixlib](https://github.com/adyoull/riscos-unixlib)), which has the
pthread ticker fix, and load the PThreadTicker module (`riscos/PThrTicker`
in the devkit) from its `!Run`, or it can crash other tasks when it
multitasks. (That applies to any threaded program, SDL sound included.)
Everything riscos-mesa ships is linked with UnixLib 5.0.3.7 (5.0.3.6 in 20.3.5-18, 5.0.3.3 from 20.3.5-14 to 20.3.5-17, 5.0.3.1 from 20.3.5-11 to 20.3.5-13, 5.0.1 from 20.3.5-8 to 20.3.5-10).

## Download

The GitHub [Releases](../../releases) page has, for each release:

| File | Contents |
| --- | --- |
| `riscos-mesa-MesaLibs-VERSION.zip` | !MesaLibs, for `!Boot.Resources`: the shared libraries mixed programs load, with the GPU library and the so/2 loader |
| `riscos-mesa-devkit-VERSION.tgz` | everything for building your own programs: the static and shared libraries (with `libMesaGPU`) and headers, a beginner's guide, six commented example programs with a Makefile, `sdl2-config`, pkg-config files and `LICENCES.txt` |
| `riscos-mesa-examples-VERSION.zip` | the devkit's six example programs, built and ready to run (`!GLFull`, `!GLWindow`, `!GLShaders`, `!SDLSpin`, `!Teapot`, `!Tune`) |
| `riscos-mesa-tests-VERSION.zip` | the test programs and their Obey files, with a ReadMe |
| `riscos-mesa-tests-mixed-VERSION.zip` | the test programs linked mixed, with !MesaLibs, including the GPU tests (`gpu-egltest`, `gpu-glestest`, `gpu-glbench`) |
| `riscos-mesa-hello_pi-VERSION.zip` | the Raspberry Pi's `hello_triangle`, `hello_triangle2` and `hello_teapot`, rebuilt |
| `riscos-mesa-ports-VERSION.zip` | Mesa's EGL demos (gears, torus, ...) and SDL's GL test programs, in desktop windows, and SDL's loopwave sound test |
| `riscos-mesa-glut-VERSION.zip` | freeglut's demos (shapes, Lorenz, fractals, subwindows, menus, game mode...) over riscos-mesa's freeglut |

The programs are ELF (&E1F) and need SharedUnixLibrary and ARMEABISupport
(and SharedLibs for the test programs) from PackMan. For sound, install
SharedSoundBuffer and StreamManager (SharedSound is part of RISC OS; only
these two need installing):

- **Download:** the `ssb.zip` download on Andrew Sellors' RDPClient page,
  <https://orac.co.uk/software/rdpclient/rdpclient.html>. Merge its `!System` into yours.
- **Background:** John Duffell's own site (now on the Internet Archive)
  has more details: <https://web.archive.org/web/20110920080106/http://www.duffell.riscos.me.uk/>

The zips store RISC OS filetypes. Releases are numbered after the Mesa version they contain
(`v20.3.5`, then `v20.3.5-2` for the next build of it, and so on). See
[CHANGELOG.md](CHANGELOG.md).

## Limits

- **API versions:** in software, OpenGL 2.1, OpenGL ES 1.1 and ES 2.0;
  requests for GL 3.0+, a core profile or ES 3.x fail (that's what classic
  OSMesa grants). On the GPU, up to OpenGL ES 3.1, and OpenGL 3.1 under
  RISC OS on Linux (natively, OpenGL 2.1). See [The GPU](#the-gpu).
- **Speed:** in software, rendering is on the CPU, on one core.
  - Fixed-function GL and ES 1.1 are quick at desktop window sizes: aim
    for 320x240 to 640x480.
  - Shaders (GLSL, ES 2.0) run through Mesa's interpreter, several pixels
    at a time (with NEON where the CPU has it), and are several times
    slower.
- **Processors:** ARMv7 or later with VFPv3 (from 20.3.5-8; 20.3.5-7 and
  earlier need VFPv4, so a Pi 2 or later). Pi 2, 3 and 4 and Cortex-A15
  boards should work; tested on a Pi 4. Cortex-A8/A9 boards run the same
  code (checked under an emulated Cortex-A8, not yet on real hardware),
  but their FPUs are much slower, the A8's especially. Not the Pi 1 or
  Zero: they're ARMv6.
- **Not available:** multisampling, OpenVG, OpenMAX.
- **Screen modes:** best in a 16M-colour mode, where the GL image plots
  with no conversion. EX0 EY0 (180 dpi) desktops are handled.

## Build (Linux, GCCSDK GCC 10 installed)

The simplest way is riscos-crossdev's container image, which has the
compiler (GCCSDK GCC 10.2 with riscos-unixlib): from a clean checkout,

    docker run --rm -v "$PWD":/src ghcr.io/adyoull/riscos-crossdev:1.11 build/ci.sh [VERSION]

builds everything, makes the release zips and devkit in `dist/` and runs
riscos-crossdev's checks on the test programs. GitHub runs the same
(`.github/workflows/riscos-build.yml`); `.riscos-deps` names the
riscos-crossdev release it uses, and a daily workflow
(`riscos-deps-update.yml`) opens a pull request when there's a newer one.
With a toolchain of your own:

    export GCCSDK_ENV=/path/to/gccsdk/env   # has bin/arm-riscos-gnueabihf-gcc
    build/build-all.sh [VERSION]            # everything below, in order

`build-all.sh` logs each step to `stage/logs/` and stops at the first
failure; with a VERSION it also runs `package.sh`. `SKIP_ZLIB=1` leaves out
zlib; `SHARED=1` also makes the shared libraries, !MesaLibs and the mixed
tests (`build-shared.sh`). The steps it runs, which can also be run one at a time (after
changing only `egl/`, say, run `build-egl.sh` and the steps after it):

    build/build-zlib.sh      # skip if your env already has libz
    build/build-mesa.sh
    build/build-v3d.sh       # the GPU library, libMesaGPU (Mesa 26.1.6, v3d; meson 1.4+ as MESON=)
    build/build-glu.sh
    build/build-sdl2.sh      # SDL 2 + patches/sdl2 (the RISC OS overlay)
    build/build-openal.sh    # OpenAL Soft 1.19.1 + patches/openal (SDL2 output)
    build/build-egl.sh       # libEGL.a, libbcm_host.a + headers
    build/build-freeglut.sh  # libglut.a, libfreeglut-gles.a (freeglut 3.8.0 + glut/riscos)
    build/build-tests.sh     # -> stage/tests/*,e1f
    build/build-hello-pi.sh  # Pi hello_triangle/2, hello_teapot -> stage/hello_pi
    build/build-ports.sh     # ported examples (docs/porting) -> stage/ports
    build/build-shared.sh    # with SHARED=1: lib/shared, !MesaLibs, the mixed tests
    build/package.sh VERSION # the release zips and devkit -> dist/

Everything installs into `stage/`. No GCCSDK GCC 10 yet? See
[build/TOOLCHAIN.md](build/TOOLCHAIN.md), or use riscos-crossdev's
(`$RISCOS_TOOLCHAIN`). The host needs meson, ninja, python3-mako, bison,
flex, autoconf, automake, libtool and cmake, and for the GPU library meson
1.4 or later with PyYAML (`build/ci.sh` installs meson, Mako and PyYAML
itself if they're missing).

**Working on riscos-mesa itself:** [docs/DECISIONS.md](docs/DECISIONS.md)
explains why things are the way they are (Mesa 20.3.5, EGL as the API,
the build flags, what's opt-in), and [docs/RELEASING.md](docs/RELEASING.md)
how a release is made; [docs/ALLOCATIONS.md](docs/ALLOCATIONS.md) lists
the RISC OS names reserved for it. Each part has its own README: `egl/`,
`patches/mesa/`, `patches/sdl2/` (including how to change the SDL
overlay), `glut/`, `dispmanx/`, `v3d/` (the GPU library),
`tests/host-harness/`.

## Using it from another port

- **New to this?** Start with the devkit's beginner's guide,
  [devkit/README.md](devkit/README.md), and its example programs in
  [devkit/examples](devkit/examples): from an empty folder to a running
  RISC OS application, with the reasons for every step.
- **Stack probes:** compile your own code with `-fstack-clash-protection`
  too (see `build/env.sh` for the full flags). Large stack frames can
  otherwise crash on RISC OS, and `tools/check-stack-probes.py yourprog`
  checks a binary.
- **New programs:** use EGL. See [docs/EGL-GUIDE.md](docs/EGL-GUIDE.md),
  `tests/egltest.c` and `tests/glestest.c`.
- **GLUT programs:** link `-lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm`;
  see [docs/porting/glut.md](docs/porting/glut.md).
- **Existing programs:** see [docs/porting/](docs/porting/README.md).
- **MIDI music** for ported games: see
  [riscos-midisynth](https://github.com/adyoull/riscos-midisynth), a
  SoundFont-based General MIDI synthesiser library.
- **SDL2:**
  - `patches/sdl2/*.p` is a complete RISC OS driver overlay in GCCSDK
    autobuilder form, shared with riscos-openttd.
  - Configure SDL with `--enable-video-riscos-osmesa` for OpenGL.
  - Don't let SDL's OpenGL *renderer* (`SDL_VIDEO_RENDER_OGL`) get
    enabled: it would pick software GL over the faster software renderer
    for every SDL2 program. The build script checks this.
  - GL windows can opt in to EGL's render size and hardware overlay with
    `SDL_RISCOS_GL_RENDER_SIZE` = `"WxH"` and `SDL_RISCOS_GL_OVERLAY` =
    `"1"` (hints, or system variables of the same names). Without them
    nothing changes.
  - Programs with a GL-or-not choice (e.g. OpenTTD's `sdl-opengl` driver)
    should keep using their non-GL path on RISC OS.
- **Plain OSMesa:** see `tests/osmesatest.c`, which renders straight into a
  32bpp sprite.

## Tests

| Test | Checks |
|---|---|
| `orient`, `prof`, `glutest` | pixel order and row order; which GL versions are granted; GLU |
| `osmesatest`, `glbench` | GL strings, fixed-function and GLSL speed |
| `egltest` (`egl-*` Obey files) | EGL checks and extensions; desktop window (+ work area view, damage demo); full screen (+ direct, screen banks); hardware overlay and render size (`egl-overlay`, `egl-scaled*`) |
| `ovltest` (`ovl-*`) | the Pi's VideoOverlay module itself: memory speed, tearing, windows and menus over an overlay |
| `glestest` (`gles-*`) | OpenGL ES 1.1 and 2.0 in a desktop window, full screen and into a sprite |
| `dmxtest` (`dmx-*`) | the DispmanX compatibility library, full screen and window mode |
| `sdlgltest` | SDL2 GL in a desktop window: fps in the title, F full screen, D desktop-size full screen, Space vsync; `-S WxH` render size and `-V` overlay (`sdl-scaled`, `sdl-scaled-plot`, `sdl-plain`) |
| `gpu-egltest`, `gpu-glestest`, `gpu-glbench` (tests-mixed) | egltest, glestest and glbench linked with the GPU library: on a Pi 4 with !RISC3D they draw on the GPU, elsewhere in software |
| `v3dtri`, `v3dgles2` (`v3d/tests`) | the GPU library on its own, without EGL (bring-up) |
| `hello_pi`, `ports` and `glut` zips | real programs: the Pi examples, Mesa's demos, SDL's GL tests, freeglut's demos |
| `tests/host-harness` | SDL's GL windows on Linux with emulated SWIs: the sprite path and the EGL path, with a render size and a fake VideoOverlay; `sdl-wimp` checks the driver's Wimp events and mouse scaling |
| `tests/host-harness/egl` | libEGL and libbcm_host on Linux against a fake screen, Wimp and SpriteOp (over 400 checks, including hardware overlays against a fake VideoOverlay); `portrun.c` runs whole ported programs |
| `tests/host-harness/glut` | freeglut's RISC OS back end on Linux: freeglut's demos driven by scripted keys, clicks, drags, menus, the wheel and resizing (23 checks) |
| `tests/host-harness/deqp` | Khronos's EGL conformance tests (dEQP-EGL) against riscos-mesa's EGL on the fake RISC OS |
| `tests/host-harness/egl/r3d_e2e.sh` | EGL and OSMesa through RISC3D's real client and Linux helper, end to end |
| `tests/host-harness/mesa` | the Mesa patches: a hash of every image in a few thousand rendering cases, on the host and on the RISC OS build under qemu-arm; instructions per frame for glbench's scenes (performance regressions) |

The host tests run on any Linux x86-64 machine, without the RISC OS
toolchain: `tests/host-setup.sh` once (it fetches and patches the sources
and builds a host Mesa, about 10 minutes), then `tests/run-all.sh`. GitHub
runs the same on every push (`.github/workflows/host-tests.yml`).

## Licences

- **riscos-mesa's own files** are MIT (`LICENSE`): the EGL library, the
  DispmanX compatibility library, the GPU library's own parts (`v3d/libdrm`,
  `v3d/frontend`, `v3d/gpu`, `osmesa/r3d_switch.c`), the porting helpers in `ports/`, test
  programs, tools, build scripts and docs.
- **Patches** keep the licence of the files they change:
  - `patches/mesa` and `v3d/patches` are MIT, like Mesa. `patches/mesa`'s
    one change to `include/c11/threads_posix.h` is Boost.
  - `patches/sdl2` is zlib, like SDL.
  - `patches/openal` is LGPL v2 or later, like OpenAL Soft.
  - The RISC OS section of `EGL/eglplatform.h` is under the Khronos
    licence.
- **freeglut's RISC OS back end** (`glut/riscos`) is MIT, like freeglut;
  `patches/freeglut` keeps freeglut's licence.
- **Ported programs** keep theirs: Raspberry Pi userland BSD, mesa-demos
  MIT, SDL's tests zlib, freeglut's demos MIT/X.
- **Third-party components:**
  - Mesa 20.3.5 and, in the GPU library, Mesa 26.1.6: MIT (some parts
    Boost / SGI Free B; Mesa 26's xxHash BSD-2, BLAKE3 CC0).
  - libdrm's headers and the DRM uAPI headers (GPU library): MIT.
  - RISC3D's client library (in libOSMesa): MIT.
  - In !MesaLibs only: GCCSDK's `ld-riscos` loader (its own BSD-style
    notice, `SharedLibs.COPYRIGHT-ld`) and SOManager.
  - Khronos headers: Khronos MIT-style.
  - GLU: SGI Free Software Licence B.
  - freeglut: MIT/X style.
  - SDL and zlib: zlib.
  - OpenAL Soft: GNU LGPL v2 or later. It is linked statically, so, as
    for UnixLib, closed source programs using it must offer their object
    files for relinking.
  - UnixLib, linked into every GCCSDK program: BSD with some LGPL v2
    parts, so closed source programs must offer their object files for
    relinking.
  - GCC runtime: GPL v3 with the Runtime Library Exception.

[LICENCES.txt](LICENCES.txt) has all the texts. It ships in the tests zip
(`Licences`) and the devkit; include it with programs you build.
