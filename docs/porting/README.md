# Porting OpenGL and EGL programs to riscos-mesa

These guides show how to move existing OpenGL, OpenGL ES and EGL programs
to RISC OS with riscos-mesa. Each guide comes with a worked port in
`ports/`: real, unchanged (or nearly unchanged) upstream code, built by
the scripts in `build/`, so every step in the guides has been done for
real. The ports in the release zips (the Mesa demos, SDL's GL tests and the
Pi's hello_pi examples) all work on a Raspberry Pi 4.

If you haven't built anything with the devkit yet, do its beginner's guide
first (`README.md` at the top of the devkit, `devkit/README.md` in the
repository): it covers the compiler, the build flags and how a RISC OS
application is put together, which these guides take as read.

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
  flags: `-O3 -mtune=cortex-a72 -mfpu=vfpv3 -mfloat-abi=hard
  -fstack-clash-protection` (VFPv3 so the program also runs on
  Cortex-A8/A9 machines; VFPv4 measured no faster on a Pi 4). **`-fstack-clash-protection` is essential:**
  the ELF stack grows a page at a time behind a guard page, and without
  probing, functions with big stack frames jump past it and crash
  seemingly at random (`tools/check-stack-probes.py` checks a binary).
- Link lines:
  - EGL: `-lEGL -lOSMesa -lstdc++ -lz -lm`.
  - SDL2: `-lSDL2 -lEGL -lOSMesa -lstdc++ -lz -lm` (add `-lGLU` if used).
  - GLUT: `-lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm`.
  - Pi code: `-lbcm_host -lEGL -lOSMesa -lstdc++ -lz -lm`.
  - OpenAL (sound, through SDL): `-lopenal -lSDL2` before the rest of the
    SDL2 line.
  - Never `-pthread`: UnixLib has threads built in, and GCCSDK's GCC
    rejects it.
- Add `-D_GNU_SOURCE` if the code uses GNU extras such as `sincos`. Linux
  builds usually get them by default; UnixLib only declares them when
  asked.
- To use `eglRedrawWindowRISCOS` and the other RISC OS EGL calls, define
  `EGL_EGLEXT_PROTOTYPES` before including `EGL/eglext_riscos.h`.
- **CMake:** setting `CMAKE_C_FLAGS` / `CMAKE_CXX_FLAGS` on the command line
  replaces the toolchain file's flags, `-mfpu` and
  `-fstack-clash-protection` included. Add flags in the toolchain file,
  or append (`-DCMAKE_C_FLAGS_INIT=...`).
- **Older C++:** GCC 6 and later delete `if (this == NULL)` tests, which
  some old engines rely on. `-fno-delete-null-pointer-checks` keeps them
  (YSFlight crashed without it).
- **Build paths:** `__FILE__` (in `assert`s and logging) puts the build
  machine's directories into the program. `-ffile-prefix-map=<your
  source directory>=<name>` replaces them; riscos-mesa's builds do this.
- **Programs started from a TaskWindow:** a big ELF program can fail with
  "Wimpslot not big enough to run ELF program". Converting it to AIF
  (`elf2aif`; GCCSDK's needs a fix for programs over 32 MB, which the
  riscos-openttd port carries) and setting a WimpSlot in `!Run` avoids it.

**Windows and the event loop**

- An EGL window surface on RISC OS is a Wimp window: pass the window
  handle to `eglCreateWindowSurface`.
- A program with windows is its own Wimp task, so start it from the Filer
  (`!Run`) or with `*WimpTask`, not by typing its name in a TaskWindow:
  there `Wimp_Initialise` fails ("Window Manager is currently in use").
  SDL, freeglut, the helpers here and libbcm_host say so when it happens.
  A program that must also work when typed in a TaskWindow can start
  itself again with `Wimp_StartTask` (riscos-ffmpeg's ffplay does).
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
- **Quitting:** exit on a Close_Window_Request (or hide the window) and
  always on Message_Quit. A program with unsaved work can object to a
  desktop shutdown by acknowledging Message_PreQuit (see the EGL guide).
  SDL and freeglut programs get this from their libraries.

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
- **RISC OS lets a file be open for writing only once.** Sending stdout
  and stderr to the same file with two `freopen` calls fails on the
  second, and a failed `freopen` leaves that stream closed: everything
  written to it is lost. Open the file once and `dup2` it to the other
  stream, as the helpers do (from 20.3.5-12; found by the Warzone 2100
  and Freeciv ports).
- **Don't set a system variable other programs read** (such as a
  program-wide log or icon variable): set one named after your program.
  A shared one is inherited by every program started afterwards.

**Threads**

- Threads work (UnixLib has pthreads), but they share one CPU core and
  switch on a timer. A program with more than one thread, including any
  that uses SDL sound or OpenAL, should be linked with UnixLib 5.0.1 or
  later (github.com/adyoull/riscos-unixlib), which has the pthread ticker
  fix, and load the PThreadTicker module from its `!Run`:
  `RMEnsure PThreadTicker 0.01 RMLoad <App$Dir>.PThrTicker` (the module is
  in the devkit's `riscos/` folder; copy it into the application). Without
  the fix the thread switcher can crash other tasks while the program
  multitasks.
- Long-lived worker threads buy nothing on one core; running the work in
  the main loop is often simpler.
- EGL follows EGL 1.4's thread rules (from 20.3.5-8): each thread has
  its own current context, so a program that loads textures in a second
  thread with a shared context ports as it is. It won't render faster.

**Memory**

- **A dynamic area holds at most 128 MB** on RISC OS 5, whatever size is
  asked for. UnixLib 5.0.3.1 and later carry the heap on into further areas
  (`<App> Heap 2`, `3`...), so `malloc` can use more; a program that
  makes its own dynamic area for a bigger heap gets 128 MB.
- **Contiguous memory** (OS_Memory 12, then OS_DynamicArea 21, for DMA or
  the GPU) can take the page at &8000 the program runs from; RISC OS then
  moves the program, which ARMEABISupport 1.08 doesn't notice. See
  "Crashes" below. Ask for memory wholly below or above that page.

**Keeping the desktop alive**

- During a long load, process events (`SDL_PumpEvents`, or one
  `Wimp_Poll`) about every 50 ms, or the whole desktop stops until it
  finishes.
- Don't spin while waiting: wait in `Wimp_PollIdle` (SDL's `SDL_Delay`
  and `SDL_WaitEvent` do). UnixLib's `select()` busy-waits in a Wimp task.

**Crashes**

- **"EMT trap" (code 6) when a program starts**, at its first stack
  growth, often inside UnixLib's `__riscosify`, whatever the program: an
  earlier program's record in ARMEABISupport 1.08 was left behind when
  RISC OS moved that program (see Memory). Restart the computer;
  `*ARMEABISupport_Info` lists the records. It isn't a fault in the
  program that crashes.
- **A crash report:** a handler for SIGSEGV, SIGBUS, SIGILL and SIGEMT
  that prints `_kernel_last_oserror()` and calls `__write_backtrace`,
  then `_exit(2)`, tells you where (riscos-openttd does this).
- **Unaligned accesses** abort on RISC OS but not on Linux. Test the
  ARM code under a QEMU that traps them as RISC OS does
  (`tests/host-harness/qemu`, from 20.3.5-12).

**Other programs**

- **Avoid `popen()` and `system()`.** UnixLib runs the command as a `*`
  command inside the program's own memory; a Unix command such as `which`
  fails ("File 'which' not found"), and it can crash the program ("abort on
  data transfer" in the ROM was seen with Warzone 2100). Take such calls
  out, or start other programs with `Wimp_StartTask`.

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
- **Other Khronos APIs:** OpenVG and OpenMAX aren't available.
- **EGL images:** only of sprites (`EGL_KHR_image_pixmap`), as texture
  storage used in place: the way to stream video into a texture (decode
  each frame into the sprite). Images made from GL textures or
  renderbuffers (`EGL_GL_TEXTURE_2D_KHR`, as Pi code hands to OpenMAX)
  aren't available.

**Speed**

Everything runs on the CPU:

- Fixed-function OpenGL and ES 1.1 are quick at desktop window sizes.
- Shaders (ES 2.0, GLSL) run through Mesa's interpreter and are several
  times slower. Keep shader programs in small windows. When full screen,
  render smaller and scale up (with DispmanX, through the source
  rectangle).
- **Textured triangles have a fast path** in Mesa's software renderer. It
  takes a single 2D texture (RGB or RGBA, power-of-two size) in
  `GL_REPEAT` or `GL_CLAMP_TO_EDGE` mode, with fog or without. Games get
  much of their speed from staying on it:
  - Prefer `GL_CLAMP_TO_EDGE` to `GL_CLAMP` with `GL_LINEAR` filtering
    (`GL_CLAMP` blends in the border colour at the edges, which only the
    slow path does).
  - Mipmaps, and different minification and magnification filters, use
    the slow path unless the program sets
    `glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST)`. Then riscos-mesa
    chooses one mipmap level per triangle (close to exact when triangles
    are small, as in terrain and models) and corrects perspective every
    16 pixels instead of every pixel.
  - Asking for compressed textures (`GL_COMPRESSED_RGBA` and the other
    generic formats) costs nothing: they are stored uncompressed. ETC1
    textures in OpenGL ES (the Raspberry Pi GPU's format, from 20.3.5-13)
    are decoded once, when loaded, and then cost nothing either.
    Explicit S3TC formats are stored compressed and decoded for every
    texel, which is slow; avoid them.
  - More than one texture unit, texture environment combiners, or
    shaders use the slow path.
- **A texture replaced every frame** (video, emulator screens) costs a
  whole copy each time with `glTexSubImage2D`. With EGL, make the picture
  a sprite, bind an image of it to the texture once and write each frame
  straight into the sprite: see "Video frames as textures" in the
  [EGL guide](../EGL-GUIDE.md#using-the-extensions).
- The [EGL guide](../EGL-GUIDE.md#limits-performance-and-troubleshooting)
  has the Pi 4 benchmark figures.

## Testing on a Linux host first

`tests/host-harness/egl` has a fake RISC OS: screen memory, windows,
sprites and a scripted Wimp task. It lets a port run against riscos-mesa's
real EGL and a host build of Mesa before it goes near a RISC OS machine.
Every worked port here was run that way, and the pictures checked, before
it went near the Pi. Its README shows how to build against it.
