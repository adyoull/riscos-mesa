# Porting SDL 2 programs that use OpenGL or OpenGL ES

**Worked port:** SDL 2.26's own GL test programs, built from their
unchanged source by `build/build-ports.sh` into `stage/ports/sdl2-tests`:

- `testgl2`: desktop OpenGL;
- `testgles`: OpenGL ES 1.1;
- `testgles2`: OpenGL ES 2.0 shaders.

This is the easiest route to RISC OS. SDL already has a RISC OS video
driver, and riscos-mesa's SDL2 adds OpenGL to it, so a program that gets
its window and context from SDL has no window code to change.

## What SDL gives you

- **Contexts:** `SDL_GL_CreateContext` in a desktop window or full screen:
  - desktop OpenGL 2.1 (compatibility);
  - OpenGL ES 1.1 and 2.0: `SDL_GL_CONTEXT_PROFILE_MASK` =
    `SDL_GL_CONTEXT_PROFILE_ES` with major version 1 or 2.
- **Refused:** core profiles and OpenGL 3.x or ES 3.x. Context creation
  fails, as it does on any driver that can't provide them.
- **Swapping and vsync:** `SDL_GL_SwapWindow`. `SDL_GL_SetSwapInterval(1)`
  paces a window without stopping other tasks, and waits for the vsync
  when full screen.
- **Functions:** `SDL_GL_GetProcAddress` finds every GL and GLES function.
  They're also linked directly, so code that calls `glClear` without
  loading it works too.
- **Windows:** resizing, switching to full screen and back, and EX0 EY0
  (180 dpi) scaling are handled by the driver.

## Step by step

1. **Link riscos-mesa's SDL2 and Mesa:**

   ```sh
   $CC $RO_CFLAGS -I$STAGE/include -I$STAGE/include/SDL2 -static prog.c \
       -o '!Prog/!RunImage,e1f' -L$STAGE/lib -lSDL2 -lOSMesa -lstdc++ -lz -lm
   ```

   Add `-lGLU` before `-lOSMesa` if the program uses GLU, and
   `-lSDL2_test` for programs built on SDL's test framework.
2. **Pass the defines the program's configure script would.** SDL's test
   programs compile to "No OpenGL support on this system" unless their
   configure defines `HAVE_OPENGL`, `HAVE_OPENGLES` or `HAVE_OPENGLES2`.
   Pass these by hand (`-DHAVE_OPENGL` etc.). Other programs have their
   own feature macros; look for them in `config.h` or the configure
   output.
3. **Send the log somewhere.** `SDL_Log` writes to stderr, and printing
   from a Wimp task opens a command window.
   `ports/sdl2-tests/riscos_output.c` fixes that without touching the
   program:
   - it's a constructor that runs before `main`;
   - if a variable (named at compile time, e.g.
     `-DOUTPUT_VAR='"TestGL2$Output"'`) is set, stdout and stderr go to
     that file.

   Link it in, and set the variable in `!Run`:

   ```
   Set TestGL2$Dir <Obey$Dir>
   Set TestGL2$Output /|<TestGL2$Dir>/Output
   Run <TestGL2$Dir>.!RunImage
   ```

4. **Data files:** open them from the application directory
   (`/<App$Dir>/...`), or use `SDL_GetBasePath()`.

## Things to check in the program

- **Loading GL functions:** programs that use a loader (glad, GLEW,
  epoxy) must load through `SDL_GL_GetProcAddress`. There's no
  `dlopen("libGL.so")` on RISC OS.
- **Multisampling:** `SDL_GL_MULTISAMPLEBUFFERS` / `SAMPLES` are
  ignored: the context is made without antialiasing, and
  `SDL_GL_GetAttribute` (which asks the context) reports 0.
- **GL versions:** a program that insists on a core profile or GL 3.x
  needs a fallback path, or it won't run.
- **Speed:** keep windows moderate.
  - Fixed-function GL and ES 1.1 are quick at 640x480.
  - GLSL and ES 2.0 are several times slower.
  - Full screen at a large mode is a lot of pixels: offer a smaller
    window, or render to a smaller framebuffer and scale up.
- **SDL's GL renderer:** riscos-mesa's SDL deliberately leaves SDL's
  OpenGL *renderer* out. `SDL_CreateRenderer` programs use SDL's faster
  software renderer, and only programs that call GL themselves use Mesa.

## Going native

SDL is a fine way to write a RISC OS GL program: the driver multitasks,
handles the Wimp and uses riscos-mesa underneath. To use EGL directly
instead, for GL views in a window of your own, sprites or several
surfaces, see `docs/EGL-GUIDE.md`.

## Results

The three SDL test programs cross-build unchanged with the steps above.
The SDL host harness (`tests/host-harness`) covers the driver's GL
context creation for desktop GL, ES 1.1 and ES 2.0, and `sdlgltest` (the
same pattern) has run on a Raspberry Pi 4 at 153 fps in a 640x480 window.
The test programs themselves haven't been run on RISC OS hardware yet.
