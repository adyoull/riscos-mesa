# riscos-mesa

Software OpenGL 2.1 for RISC OS 5: Mesa 20.3.5's classic OSMesa, GLU 9.0.1,
and an OpenGL context for SDL 2.26's RISC OS video driver, so SDL2 + GL
programs port without RISC OS-specific GL code. GL windows work as normal
desktop windows (redraws, dragging) and full screen.

**Tested on a Raspberry Pi 4 (RISC OS 5):** all tests pass; a 640x480 lit,
spinning cube runs at about 160 fps in a desktop window, and GL programs
multitask properly (vsync, SDL_Delay and SDL_WaitEvent yield to the desktop).

## What you get
- `libOSMesa.a`: one static library. OpenGL 2.1 compatibility profile,
  GLSL 1.20, classic swrast CPU rasteriser. Link with
  `-lOSMesa -lstdc++ -lz -lm`.
- `libGLU.a`: GLU 1.3.
- SDL2 with `SDL_WINDOW_OPENGL` / `SDL_GL_CreateContext` working on RISC OS.
- `libEGL.a`: EGL 1.4 over OSMesa, so programs can set up GL the standard
  Khronos way: Wimp windows, full screen (optionally straight into screen
  memory), pbuffers and sprites as pixmaps. Link with
  `-lEGL -lOSMesa -lstdc++ -lz -lm`. See [egl/README.md](egl/README.md).

## Download
Releases are numbered after the Mesa version they contain (first: `v20.3.5`).
A rebuild of the same Mesa version would be `v20.3.5-2`, and so on.
Prebuilt test programs and a devkit (static libraries + headers) are on the
[Releases](../../releases) page. The programs are ELF (&E1F) and need
SharedLibs (SOManager), SharedUnixLibrary and ARMEABISupport from PackMan.

## Limits
- OpenGL 2.1 only. Requests for GL 3.0+ or a core profile fail, and so do
  GLES contexts. That's what classic OSMesa grants; GL 3.x would need
  Mesa 25.1's softpipe OSMesa (slower, threaded), which is a later option.
- Pure CPU rendering on one core. Aim for 320x240 to 640x480.
- Best in a 16M-colour screen mode (the GL image then plots with no
  conversion); other screen depths are untested.

## Build (Linux, GCCSDK GCC 10 installed)
    export GCCSDK_ENV=/path/to/gccsdk/env   # has bin/arm-riscos-gnueabihf-gcc
    build/build-zlib.sh      # skip if your env already has libz
    build/build-mesa.sh
    build/build-glu.sh
    build/build-sdl2.sh      # SDL 2.26 + patches/sdl2 (the RISC OS overlay)
    build/build-egl.sh       # libEGL.a + EGL headers
    build/build-tests.sh     # -> stage/tests/*,e1f
Everything installs into `stage/`. No GCCSDK GCC 10 yet? See build/TOOLCHAIN.md. Host needs meson, ninja, python3-mako,
bison, flex, autoconf, automake, libtool.

## Using it from another port
- Compile your own code with `-fstack-clash-protection` as well (see
  `build/env.sh` for the full flags): large stack frames can otherwise crash
  on RISC OS. `tools/check-stack-probes.py yourprog` checks a binary.
- Plain OSMesa: see `tests/osmesatest.c` (renders straight into a 32bpp sprite).
- EGL: see `egl/README.md` and `tests/egltest.c` (a Wimp task with a GL
  window, full screen, pbuffer and pixmap use).
- SDL2: `patches/sdl2/*.p` is a complete RISC OS driver overlay (desktop
  windows, full screen, typing, OpenGL) in GCCSDK autobuilder form; configure
  SDL with `--enable-video-riscos-osmesa` for OpenGL. Do NOT let SDL's
  OpenGL *renderer* (`SDL_VIDEO_RENDER_OGL`) get enabled: it would pick
  software GL over the faster software renderer for every SDL2 app. The
  build script checks this.
- Apps with a GL-or-not choice (e.g. OpenTTD's `sdl-opengl` driver) should
  keep using their non-GL path on RISC OS.

## Tests
| Test | Checks |
|---|---|
| `osmesatest [w h frames]` | GL strings, fixed-function and GLSL fps, saves `gltest` Sprite |
| `orient` | top-down rows + 0x00BBGGRR byte order (exit 0 = pass) |
| `prof` | which GL versions/profiles are granted |
| `glutest` | GLU links and draws |
| `sdlgltest [w h] [-f]` | SDL2 GL desktop window, spinning cube, fps in title; F full screen, Space vsync |
| `egltest [-w [-r] \| -f [-d]]` | EGL: checks (pbuffer, sprite pixmap, errors), desktop window (+ work area surface), full screen (+ direct) |
| `tests/host-harness` | runs the SDL GL glue on Linux with emulated SWIs |
| `tests/host-harness/egl` | runs the EGL library on Linux against a fake Wimp and screen (108 checks) |

## Licences
Mesa: MIT. GLU: SGI Free Software Licence B (MIT-style). SDL: zlib. Patches
and scripts here: MIT. All static-link friendly; keep the notices.
