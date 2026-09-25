# riscos-osmesa

Software OpenGL 2.1 for RISC OS 5: Mesa 20.3.5's classic OSMesa, GLU 9.0.1,
and an OpenGL context for SDL 2.26's RISC OS video driver, so SDL2 + GL
programs port without RISC OS-specific GL code.

## What you get
- `libOSMesa.a`: one static library. OpenGL 2.1 compatibility profile,
  GLSL 1.20, classic swrast CPU rasteriser. Link with
  `-lOSMesa -lstdc++ -lz -lm`.
- `libGLU.a`: GLU 1.3.
- SDL2 with `SDL_WINDOW_OPENGL` / `SDL_GL_CreateContext` working on RISC OS.

## Limits
- OpenGL 2.1 only. Requests for GL 3.0+ or a core profile fail, and so do
  GLES contexts. That's what classic OSMesa grants; GL 3.x would need
  Mesa 25.1's softpipe OSMesa (slower, threaded), which is a later option.
- Pure CPU rendering on one core. Aim for 320x240 to 640x480.
- The GL window sprite is always 32bpp. Best in a 16M-colour mode;
  plotting it into other screen depths is untested.

## Build (Linux, GCCSDK GCC 10 installed)
    export GCCSDK_ENV=/path/to/gccsdk/env   # has bin/arm-riscos-gnueabihf-gcc
    build/build-zlib.sh      # skip if your env already has libz
    build/build-mesa.sh
    build/build-glu.sh
    build/build-sdl2.sh      # or apply patches/sdl2 to your own SDL overlay
    build/build-tests.sh     # -> stage/tests/*,e1f
Everything installs into `stage/`. No GCCSDK GCC 10 yet? See build/TOOLCHAIN.md. Host needs meson, ninja, python3-mako,
bison, flex, autoconf, automake, libtool.

## Using it from another port
- Plain OSMesa: see `tests/osmesatest.c` (renders straight into a 32bpp sprite).
- SDL2: configure SDL with `--enable-video-riscos-osmesa`. Do NOT let SDL's
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
| `sdlgltest [w h]` | SDL2 GL window, spinning cube, fps; Space toggles vsync |
| `tests/host-harness` | runs the SDL GL glue on Linux with emulated SWIs |

## Licences
Mesa: MIT. GLU: SGI Free Software Licence B (MIT-style). SDL: zlib. Patches
and scripts here: MIT. All static-link friendly; keep the notices.
