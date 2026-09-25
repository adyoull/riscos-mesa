# Porting Raspberry Pi 1–3 Khronos programs (hello_pi and friends)

**Worked port:** `ports/hello_pi`, built by `build/build-hello-pi.sh`,
with three of the Pi's own examples from raspberrypi/userland (BSD):

- `hello_triangle`: textured cube, OpenGL ES 1.1;
- `hello_triangle2`: fractals, ES 2.0 shaders and a framebuffer object;
- `hello_teapot`: lit, textured model, ES 1.1.

**This route is a porting aid.** The DispmanX compatibility library
exists so that existing Pi code runs on riscos-mesa, on every RISC OS 5
machine including the Pi 4. New RISC OS programs should use the native
EGL types (a Wimp window handle, `EGL_RISCOS_SCREEN_WINDOW`, a sprite):
see `docs/EGL-GUIDE.md` and [Going native](#going-native) below.

## Why rebuild from source

The Pi 1–3 RISC OS binaries (the GCCSDK autobuilder's `!HelloTriangle`
and others) were built for the RISC OS Khronos module, the VideoCore IV
GPU driver. Its stub libraries are linked into each program and call the
module's SWIs, so those binaries can't be pointed at riscos-mesa. Rebuilt
from source against riscos-mesa's headers and libraries, the same code
runs anywhere.

## What the compatibility library provides

`libbcm_host` and the headers in the devkit
(`bcm_host.h`, `interface/vmcs_host/vc_dispmanx.h`, `interface/vcos/vcos.h`,
`EGL/eglext_brcm.h`) cover what Pi Khronos examples use:

- **Start-up and screen:** `bcm_host_init` / `deinit` and
  `graphics_get_display_size` (the current screen mode).
- **Elements:** `vc_dispmanx_display_open` / `close` / `get_info`,
  `update_start` / `submit` / `submit_sync`, `element_add` / `remove` /
  `change_attributes` and `vc_dispmanx_rect_set`.
  - An element is a rectangle of the display: the window in window mode,
    the screen in full screen mode. Each `eglSwapBuffers` plots the surface
    there, scaled from the source rectangle to the destination (full
    screen, after the vsync wait).
  - When the element goes, or the program exits, the desktop underneath
    is redrawn.
- **The native window:** `EGL_DISPMANX_WINDOW_T` passed to
  `eglCreateWindowSurface`, exactly as on the Pi.
- **`eglSaneChooseConfigBRCM`:** Broadcom's closest-match config choice.
  Here it drops the multisample attributes (riscos-mesa has no
  multisampling) and otherwise is `eglChooseConfig`.
- **Window mode (the default in the desktop):** the display is a desktop
  window, 640 pixels wide unless `<App>$Display` or `DispmanX$Display`
  says otherwise (`800x450`, or `full` for the whole screen as on the Pi).
  - `graphics_get_display_size` reports the window's size, so the program
    renders fewer pixels.
  - libEGL plots each frame into the window.
  - The library polls the Wimp after every `eglSwapBuffers`, so an
    unchanged Pi program multitasks. Closing the window ends it.
- **`vcos.h`:** the small part of VideoCore OS that examples use
  (`vc_assert`, `vcos_assert`, `countof`, `vcos_sleep`…), included the
  way the Pi's headers include it.
- **Link lines:** empty `libGLESv2`, `libGLESv1_CM`, `libvcos` and
  `libvchiq_arm`, so Pi link lines work.

## Step by step

1. **Build with the riscos-mesa devkit headers,** unchanged. Include
   `bcm_host.h` before the EGL headers, as Pi code does.
   - A file that includes only the EGL/GLES headers but uses `vc_assert`
     (`hello_teapot`'s `models.c`) gets it on the Pi through the EGL
     headers. Build such files with `-DEGL_RISCOS_DISPMANX`, which does
     the same here.
2. **Link** with the Pi's link line plus `-lOSMesa -lstdc++ -lz -lm`:

   ```
   -lbcm_host -lEGL -lGLESv2 -lvcos -lvchiq_arm -lOSMesa -lstdc++ -lz -lm
   ```

3. **Input:** replace Linux input with RISC OS calls:
   - `/dev/input/mouse0` → `OS_Mouse`;
   - terminal keys → `OS_Byte 121/122` (keyboard scan) or `OS_ReadC`;
   - `SIGINT` to quit → a key check in the loop.

   Reading the hardware directly works both full screen and in window
   mode. In a window it sees the whole machine, so a key pressed for
   another program is seen too.
4. **Paths:** `/opt/vc/src/hello_pi/...` and `./` become the application
   directory: `"/<HelloTeapot$Dir>/teapot.obj.dat"`, with `!Run` setting
   `HelloTeapot$Dir`. UnixLib turns dots in leaf names into `/`, so that
   file is `teapot/obj/dat` on RISC OS (a RISC OS unzip does the same).
5. **Keep changes in `#ifdef __riscos__`,** so the source still builds on
   the Pi and the differences stay easy to review.
6. **Speed:** shader-heavy programs should render at a fraction of the
   screen and let the element's source rectangle scale up. `triangle2`
   renders about 320 pixels wide by default (`HelloTriangle2$Scale`
   overrides that). Fixed-function ES 1.1 programs (`hello_triangle`,
   `hello_teapot`) can run at full size.

## What changed in each worked port

| Program | Change | Why |
| --- | --- | --- |
| all | exit on a key (`OS_Byte 122`) or Menu (`OS_Mouse`); paths via `<App$Dir>` | no terminal or `/dev/input`; files live in the application |
| hello_triangle2 | render at 1/N size (default: about 320 wide); the fractal scale and mouse follow the render size | shaders run on the CPU; the same picture in a window or full screen |
| hello_teapot | a still picture instead of the video texture | the video came from OpenMAX through an `EGLImage`; neither exists here |

Four things in riscos-mesa made the rest of the code work unchanged:

- **Initial API:** EGL's initial API is OpenGL ES, as the spec says
  (`hello_triangle` and `hello_teapot` never call `eglBindAPI`).
- **Precision:** fragment shaders with no default float precision compile
  (`triangle2`), using mediump with a warning.
- **`eglSaneChooseConfigBRCM`:** `hello_teapot` asks for 4x multisampling
  through it.
- **`vcos.h`:** `hello_teapot` needs `vc_assert` and `countof` from it.

## The other hello_pi examples

| Example | Uses | Status |
| --- | --- | --- |
| hello_triangle, hello_triangle2, hello_teapot | EGL, OpenGL ES | ported (`ports/hello_pi`) |
| hello_videocube | OpenGL ES + OpenMAX video texture | portable the way hello_teapot was (a still picture instead of the video) |
| hello_tiger, hello_font | OpenVG (hello_font through vgfont, with DispmanX resources) | not portable: riscos-mesa has no OpenVG |
| hello_dispmanx | DispmanX resources (2D images) | not supported by the compatibility library |
| hello_video, hello_audio, hello_encode, hello_jpeg | OpenMAX IL (the VideoCore codecs) | not portable |
| hello_mmal_encode | MMAL | not portable |
| hello_fft | the GPU's QPUs through the mailbox | not portable (Pi 1–3 hardware only) |
| hello_world | nothing (prints a line) | trivial |

Other Pi Khronos programs follow the same pattern. If they only use EGL,
GLES and DispmanX elements, they port as above. If they need OpenVG,
OpenMAX, MMAL or `EGLImage`, those parts need replacing.

## Going native

The DispmanX set-up in a Pi program is a few lines; the rest is EGL and
GLES, which are the same natively. To turn a port into a proper RISC OS
program:

```c
/* Pi / DispmanX:                                    Native RISC OS:            */
bcm_host_init();                                 /* (nothing)                    */
graphics_get_display_size(0, &w, &h);            /* screen size from OS_ReadVduVariables,
                                                    or your window's size         */
... vc_dispmanx_element_add(...) ...             /* full screen:                 */
nativewindow.element = element;                  /*   native = EGL_RISCOS_SCREEN_WINDOW */
surface = eglCreateWindowSurface(dpy, cfg,       /* in the desktop:              */
                                 &nativewindow,  /*   native = Wimp window handle */
                                 NULL);          /*   and run a Wimp_Poll loop    */
```

- **Full screen** (`EGL_RISCOS_SCREEN_WINDOW`) waits for the vsync and
  plots each frame, like a whole-screen element.
- **In a window,** the program multitasks: draw on null events, and pass
  redraw requests to `eglRedrawWindowRISCOS`.
- `tests/glestest.c` does both, with the same ES 1.1 / 2.0 drawing code
  as the DispmanX test `tests/dmxtest.c`: compare them side by side.

## Results

- **Host rig:** all three ports ran on the fake RISC OS, full screen and
  in window mode (on a fake desktop, closed with the close icon), and
  their pictures were checked: the textured cube, the Julia and
  Mandelbrot fractals, and the textured teapot.
- **Pi 4:** `!HelloTriangle` and `!HelloTriangle2` are in the 20.3.5-5
  tests; hardware results are pending.
