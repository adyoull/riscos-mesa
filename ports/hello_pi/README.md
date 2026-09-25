# Raspberry Pi hello_pi examples, rebuilt against riscos-mesa

Three of the Raspberry Pi's `hello_pi` examples, rebuilt from source to run
on riscos-mesa's EGL, OpenGL ES and DispmanX compatibility library:

- `hello_triangle/triangle.c`: textured spinning cube, OpenGL ES 1.1.
- `hello_triangle2/triangle2.c`: Mandelbrot set drawn to a texture through
  a framebuffer object, then a Julia set over it that follows the mouse
  (OpenGL ES 2.0 shaders).
- `hello_teapot/`: a lit, textured teapot model (OpenGL ES 1.1). The Pi
  version played a video on it through OpenMAX; this one wears a still
  picture.
- `libs/revision`: the Pi board revision helper the examples call.
- `riscos/`: the `!Run` files and the ReadMe for the zip.

The sources are from raspberrypi/userland, commit a54a0dbb (BSD licence:
`LICENCE-userland`), as are the Djenne, Gaudi and Lucca textures and the
teapot model. The RISC OS changes are in `#ifdef __riscos__`: exit keys
and paths, as in the GCCSDK autobuilder's Khronos ports, plus a reduced
render size for triangle2 (`HelloTriangle2$Scale`) and the still texture
for the teapot.

In the desktop, riscos-mesa's DispmanX library shows each program's
"display" in a desktop window (640 pixels wide), so they multitask. Each
`!Run` file can choose another size, or `full` for the whole screen as on
the Pi. All three work on a Raspberry Pi 4.

`build/build-hello-pi.sh` builds `stage/hello_pi/!HelloTriangle`,
`!HelloTriangle2` and `!HelloTeapot` with the Pi's own link line plus
`-lOSMesa -lstdc++ -lz -lm`.

**How to port other Pi programs, and what can't be ported:**
[docs/porting/hello_pi.md](../../docs/porting/hello_pi.md).

This is a porting aid. New RISC OS programs should use the native EGL
types (a Wimp window handle, -1 for the whole screen, a sprite). See
`docs/EGL-GUIDE.md`.
