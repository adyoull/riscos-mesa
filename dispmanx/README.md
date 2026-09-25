# DispmanX compatibility (libbcm_host)

**A porting aid, not the RISC OS API.** New programs use OpenGL ES (or
desktop GL) through the native RISC OS EGL types - a Wimp window handle,
`EGL_RISCOS_SCREEN_WINDOW` or a sprite - exactly as for desktop GL: see
`docs/EGL-GUIDE.md` and `tests/glestest.c`. This library only exists so
that existing Raspberry Pi code runs without rewriting its window set-up.
Linking it changes nothing for native code: libEGL checks for a Wimp
window handle or -1 first, and a program can use both kinds of window.

Programs written for the Raspberry Pi's Khronos stack (the userland
`hello_pi` examples and ports based on them) get their window through
DispmanX and `bcm_host`, then use EGL and OpenGL ES. This library lets
that code build and run with riscos-mesa's software GL on any RISC OS 5
machine, with no source changes in the usual case.

    compile: -I<devkit>/include                (bcm_host.h, interface/...)
    link:    -lbcm_host -lEGL -lOSMesa -lstdc++ -lz -lm

`libGLESv2`, `libGLESv1_CM`, `libvcos` and `libvchiq_arm` are supplied
empty, so an existing Pi link line such as
`-lbcm_host -lEGL -lGLESv2 -lvcos -lvchiq_arm` works once
`-lOSMesa -lstdc++ -lz -lm` is added at the end. Compile with
`-fstack-clash-protection` as for any GCCSDK GCC 10 program.

## What's supported

| Call | On RISC OS |
| --- | --- |
| `bcm_host_init`, `bcm_host_deinit` | Set up the clean-up at exit |
| `graphics_get_display_size` | The current screen mode's size |
| `vc_dispmanx_display_open`, `_open_mode`, `_close`, `_get_info` | One display: the screen |
| `vc_dispmanx_update_start`, `_submit`, `_submit_sync` | Changes take effect at once; `_submit` calls its callback straight away |
| `vc_dispmanx_element_add` | Records where the output goes: destination rectangle (pixels from the top left) and source rectangle (16.16) |
| `vc_dispmanx_element_change_attributes` | Destination, source, opacity (0 hides it) |
| `vc_dispmanx_element_remove` | Redraws the desktop under it |
| `vc_dispmanx_rect_set` | As on the Pi |
| `vc_dispmanx_vsync_callback` | Not available: returns -1 |

`eglCreateWindowSurface` accepts a pointer to an `EGL_DISPMANX_WINDOW_T`
(element, width, height). The surface is `width` x `height` pixels; on each
`eglSwapBuffers` it waits for vsync (the swap interval, 1 by default) and
plots the source rectangle scaled to the destination rectangle, so the
common trick of rendering at half resolution and scaling up works. When the
element is removed, or the program exits, the desktop underneath is
redrawn.

`bcm_host.h` makes `EGLNativeWindowType` a pointer (as on the Pi), so
include it before the EGL headers, as the Pi examples do, or compile with
`-DEGL_RISCOS_DISPMANX`. It stops with an error if the order is wrong.

## Differences from the Pi

- Output is plotted into the screen, not composited by a GPU: the program
  doesn't multitask (neither did it on the Pi), and in the desktop it
  paints over whatever is underneath until it exits.
- Layers, alpha blending between elements, transforms (rotation, flips),
  clamping and DispmanX resources (2D images, `vc_dispmanx_resource_*`)
  aren't supported. An element whose source is a resource shows nothing.
- No OpenMAX, MMAL or other VideoCore services.
- Rendering is Mesa's software rasteriser: OpenGL ES 1.1 is quick;
  ES 2.0 programs are all shaders, which are slow on the CPU.

`tests/dmxtest.c` is a complete example, written the way `hello_triangle`
is.
