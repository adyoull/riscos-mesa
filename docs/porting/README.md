# Porting OpenGL and EGL programs to riscos-mesa

These guides show how to move existing OpenGL, OpenGL ES and EGL programs
to RISC OS with riscos-mesa. Each guide comes with a worked port in
`ports/`: real, unchanged (or nearly unchanged) upstream code, built by
the scripts in `build/`, so every step in the guides has been done for
real. The ports in the release zips (the Mesa demos, SDL's GL tests and the
Pi's hello_pi examples) all work on a Raspberry Pi 4.

| Where the program comes from | How it gets a window | Guide | Worked port |
| --- | --- | --- | --- |
| Mesa's demos, or anything using a small window library over EGL (eglut, esUtil, your own) | replace the library's window code with a Wimp window | [mesa-demos.md](mesa-demos.md) | `ports/mesa-demos`: eglgears, es1 gears, torus, es2gears, egltri |
| The *OpenGL ES 2.0 Programming Guide* samples (esUtil) | the same, for esUtil | [esbook.md](esbook.md) | `ports/esbook`: all ten LinuxX11 samples |
| GLUT programs (freeglut): tutorials, books, course code | riscos-mesa's freeglut, with a native RISC OS back end; usually no changes | [glut.md](glut.md) | `ports/freeglut`: freeglut's demos (shapes, subwindows, menus, game mode...) |
| SDL 2 programs using OpenGL or OpenGL ES | SDL does it; no window code to change | [sdl2.md](sdl2.md) | SDL's testgl2, testgles, testgles2 |
| Raspberry Pi 1–3 Khronos programs (`bcm_host`, DispmanX) | the DispmanX compatibility library (a desktop window by default, or full screen) | [hello_pi.md](hello_pi.md) | `ports/hello_pi`: hello_triangle, hello_triangle2, hello_teapot |

New RISC OS programs should use riscos-mesa's native EGL directly: a Wimp
window handle, `EGL_RISCOS_SCREEN_WINDOW` (-1) for the whole screen, or a
sprite. See [../EGL-GUIDE.md](../EGL-GUIDE.md). The helpers in these guides
are for bringing existing code across with as few changes as possible, and
each guide ends by showing how to go native.

## What changes in every port

A program that only calls EGL, OpenGL (up to 2.1) and OpenGL ES (1.1,
2.0) compiles unchanged. What changes is what surrounds those calls.

**Build**

- Use GCCSDK GCC 10 (`arm-riscos-gnueabihf-gcc`) with riscos-mesa's
  flags: `-O3 -mtune=cortex-a72 -mfpu=vfpv4 -mfloat-abi=hard
  -fstack-clash-protection`. **`-fstack-clash-protection` is essential:**
  the ELF stack grows a page at a time behind a guard page, and without
  probing, functions with big stack frames jump past it and crash
  seemingly at random (`tools/check-stack-probes.py` checks a binary).
- Link lines:
  - EGL: `-lEGL -lOSMesa -lstdc++ -lz -lm`.
  - SDL2: `-lSDL2 -lOSMesa -lstdc++ -lz -lm` (add `-lGLU` if used).
  - GLUT: `-lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm`.
  - Pi code: `-lbcm_host -lEGL -lOSMesa -lstdc++ -lz -lm`.
  - Never `-pthread`: UnixLib has threads built in, and GCCSDK's GCC
    rejects it.
- Add `-D_GNU_SOURCE` if the code uses GNU extras such as `sincos`. Linux
  builds usually get them by default; UnixLib only declares them when
  asked.
- To use `eglRedrawWindowRISCOS` and the other RISC OS EGL calls, define
  `EGL_EGLEXT_PROTOTYPES` before including `EGL/eglext_riscos.h`.

**Windows and the event loop**

- An EGL window surface on RISC OS is a Wimp window: pass the window
  handle to `eglCreateWindowSurface`.
- Your program runs the Wimp_Poll loop. On a Redraw_Window_Request, pass
  the poll block to `eglRedrawWindowRISCOS`, and EGL draws the window.
- Leave null events unmasked while animating and draw a frame on each
  one; that keeps the desktop multitasking.
- `ports/common/riscos_wimpwin.c` (MIT) is that loop in about 200 lines:
  open a window, poll, get keys. Both helper back ends in these guides
  use it.
- The surface follows the window's visible area. After `eglSwapBuffers`,
  query `EGL_WIDTH` / `EGL_HEIGHT` and call the program's resize code if
  they changed.

**Input**

- Wimp key codes are characters below 0x100. Function and cursor keys are
  0x180 and up:
  - F1–F9 are 0x181–0x189; F10–F12 are 0x1CA–0x1CC.
  - Cursor Left/Right/Down/Up are 0x18C–0x18F.
  - Shift adds 0x10 and Ctrl adds 0x20.
- Pass keys you don't use to `Wimp_ProcessKey`.
- Clicking in the window should take the input focus
  (`Wimp_SetCaretPosition`), or keys won't arrive.

**Output**

- **Printing from a Wimp task opens a command window**, which stops the
  program until a key is pressed. Programs that print frame rates need
  their output sent elsewhere.
- The helpers here send stdout and stderr to a file named by a system
  variable that the `!Run` file sets, and show the newest line in the
  title bar. `ports/sdl2-tests/riscos_output.c` does the same with no
  change to the program at all.

**Files**

- Unix paths work through UnixLib: `textures/brick.tga` becomes
  `textures.brick/tga`.
- Programs usually open files relative to the current directory. Find
  them in the application directory instead: `/<App$Dir>/file.raw`, with
  `App$Dir` set by `!Run`.

**What riscos-mesa doesn't have**

- **Multisampling:** there are no multisample configs. `EGL_SAMPLES` or
  `EGL_SAMPLE_BUFFERS` above 0 makes `eglChooseConfig` return no configs.
  Drop them. Pi code that uses `eglSaneChooseConfigBRCM` gets this done
  for it.
- **Newer APIs:** OpenGL 3.x and OpenGL ES 3.x aren't available.
- **Other Khronos APIs:** OpenVG and OpenMAX aren't available, nor
  `EGLImage` or streaming video into a texture.

**Speed**

Everything runs on the CPU:

- Fixed-function OpenGL and ES 1.1 are quick at desktop window sizes.
- Shaders (ES 2.0, GLSL) run through Mesa's interpreter and are several
  times slower. Keep shader programs in small windows. When full screen,
  render smaller and scale up (with DispmanX, through the source
  rectangle).

## Testing on a Linux host first

`tests/host-harness/egl` has a fake RISC OS: screen memory, windows,
sprites and a scripted Wimp task. It lets a port run against riscos-mesa's
real EGL and a host build of Mesa before it goes near a RISC OS machine.
Every worked port here was run that way, and the pictures checked, before
it went near the Pi. Its README shows how to build against it.
