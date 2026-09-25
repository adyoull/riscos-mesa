# hello_triangle and hello_triangle2, rebuilt against riscos-mesa

Two of the Raspberry Pi's `hello_pi` examples, rebuilt from source to run on
riscos-mesa's EGL, OpenGL ES and DispmanX compatibility library. They show
that existing Pi Khronos code ports with little or no change, and they're
a template for porting other programs written for the Pi 1–3 stack.

This is a porting aid. New RISC OS programs should use the native EGL
types (a Wimp window handle, -1 for the whole screen, a sprite). See
`docs/EGL-GUIDE.md`.

## Why rebuild?

The `!HelloTriangle` binaries built for the Pi's RISC OS Khronos module
can't use riscos-mesa as they are. They call that module's SWIs, and its
stub libraries are linked into them statically. Code rebuilt from source
against riscos-mesa's headers and libraries runs on any RISC OS 5 machine,
including the Pi 4, which never had the Khronos stack.

## What's here

- `hello_triangle/triangle.c`: textured spinning cube, OpenGL ES 1.1.
- `hello_triangle2/triangle2.c`: Mandelbrot set drawn to a texture through
  a framebuffer object, then a Julia set over it that follows the mouse.
  It uses OpenGL ES 2.0 shaders.
- `libs/revision`: the Pi board revision helper that triangle2 calls.
- `riscos/`: the `!Run` files for the two applications.

The sources are from raspberrypi/userland, commit a54a0dbb (BSD licence:
`LICENCE-userland`). The Djenne, Gaudi and Lucca textures come from the
same place.

## Changes from the Pi originals

These are the small RISC OS changes the GCCSDK autobuilder made for the
Khronos module port, applied again here:

- **Exit:**
  - `hello_triangle` exits on any key (`OS_Byte 122`).
  - `hello_triangle2` reads the mouse with `OS_Mouse` and exits on Menu.
- **Screen:**
  - The window surface is single buffered.
  - The cube's clear colour has alpha 0.5.
  - `hello_triangle` registers `exit_func` with `atexit`, so the screen is
    cleared however it exits.
- **Textures:** they're opened from `<HelloTriangle$Dir>`.

riscos-mesa adds one change to `triangle2.c`, in `#ifdef __riscos__`. The
shaders run on the CPU, so by default it renders about 320 pixels wide and
DispmanX scales that up to the screen. The fractal scale is multiplied to
match, so the picture is the same. To render at 1/N of the screen size, set
`HelloTriangle2$Scale` to N (1 = full size).

Two changes in riscos-mesa itself made the unmodified code work:

- **Initial API:** the initial EGL API is OpenGL ES, as the EGL spec says.
  `hello_triangle` never calls `eglBindAPI`.
- **Shader precision:** Mesa accepts a fragment shader with no default
  float precision, and uses mediump with a warning. The Pi's compiler
  accepted these, and `triangle2`'s shaders have no precision line. This is
  in `patches/mesa`.

## Building

`build/build-hello-pi.sh` (after `build-mesa.sh` and `build-egl.sh`) makes
`stage/hello_pi/!HelloTriangle` and `!HelloTriangle2`. The link line is the
Pi's own, with the riscos-mesa libraries added:

    -lbcm_host -lEGL -lGLESv2 -lvcos -lvchiq_arm -lOSMesa -lstdc++ -lz -lm

## Porting another Pi program the same way

1. Build it with the riscos-mesa devkit's headers. `bcm_host.h`,
   `EGL/egl.h`, `GLES/gl.h`, `GLES2/gl2.h` and
   `interface/vmcs_host/vc_dispmanx.h` are all there.
2. Add `-lOSMesa -lstdc++ -lz -lm` to its link line.
3. Replace Linux input (`/dev/input/*`, terminal keys) with RISC OS calls
   (`OS_Mouse`, `OS_Byte 121/122`, `OS_ReadC`).
4. Make file paths RISC OS paths, for example `/<App$Dir>/file.raw`,
   which UnixLib opens as `<App$Dir>.file/raw`.
5. If it draws with shaders over a large area, render smaller and let the
   DispmanX source rectangle scale it up.
