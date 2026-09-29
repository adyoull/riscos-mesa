# Porting SDL 2 programs that use OpenGL or OpenGL ES

**Worked port:** SDL 2.26's own GL test programs (and `loopwave`, for
sound), built from their unchanged source by `build/build-ports.sh` into
`stage/ports/sdl2-tests`:

- `testgl2`: desktop OpenGL;
- `testgles`: OpenGL ES 1.1;
- `testgles2`: OpenGL ES 2.0 shaders.

This is the easiest route to RISC OS. SDL already has a RISC OS video
driver, and riscos-mesa's SDL2 adds OpenGL to it (link `-lEGL` too: it
can show GL windows through EGL), so a program that gets its window and
context from SDL has no window code to change.

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
- **Render size:** the hint `"SDL_RISCOS_GL_RENDER_SIZE"` = `"640x480"`
  (`SDL_SetHint` before creating the window, or a system variable of that
  name, e.g. set from `!Run`) makes GL render at that size, stretched to
  fill the window, or the screen when full screen. The program sees a
  640x480 window: `SDL_GetWindowSize`, `SDL_GL_GetDrawableSize`, window
  events and mouse positions are all in render pixels, while the desktop
  window keeps the size the program asked for. Rendering cost follows
  the render size, so this is the big speed-up for a game in a large
  window or full screen.
- **Hardware overlay:** the hint `"SDL_RISCOS_GL_OVERLAY"` = `"1"` shows
  the window through the display's video overlay (VideoOverlay, on a
  Raspberry Pi): no plot per frame, no tearing, and a render size is
  scaled by the hardware. `"0"` refuses it; unset, `EGL$Overlay` decides.
  Everything falls back to the sprite plot without it (see the EGL
  guide's "Hardware overlays").
- These two hints (and `"SDL_RISCOS_GL_EGL"` = `"1"`) show the GL window
  through riscos-mesa's EGL; without them GL renders into the window's
  sprite as it always has. Load the VideoOverlay module in `!Run` with
  `RMEnsure VideoOverlay 0.00 IfThere System:Modules.VideoOverlay Then RMLoad System:Modules.VideoOverlay`.
  - **On a Pi 4** (sdlgltest's lit cube in a 1024x768 window, 2026-09-29):
    78 fps rendering at 1024x768 the old way; 134 fps at 640x480
    stretched by the sprite plot; 214 fps at 640x480 stretched by the
    overlay. Mouse positions were right in every corner. Menus over the
    overlay, full screen and mode changes haven't been tried on a Pi yet.
  - **Call `SDL_PollEvent` (or `SDL_PumpEvents`) between frames.** With an
    overlay, a frame that would have to wait for a vsync isn't waited
    for: it's held and shown from the event loop once the vsync has
    passed, so `SDL_GL_SwapWindow` never stops the desktop.
  - **No accumulation buffers** on the EGL path.
  - **Anything drawn on the screen over the picture is hidden** under a
    hardware overlay, and while another window overlaps the GL window the
    frames are plotted instead, which is slower. Keep a program's other
    windows beside the GL window.
- **Windows:** resizing, switching to full screen and back, and EX0 EY0
  (180 dpi) scaling are handled by the driver.
  The hint `"SDL_RISCOS_WINDOW_SCALE"` (`SDL_SetHint`, or a system
  variable of that name; `SDL$WindowScale` also works) sets the scale;
  `SDL_ShowWindow` / `SDL_HideWindow` work.
- **Quitting:** the close icon sends `SDL_WINDOWEVENT_CLOSE` (and SDL then
  sends `SDL_QUIT` if it was the last window), the icon bar menu's Quit
  sends `SDL_QUIT`. A desktop shutdown or the Task Manager's Quit also
  sends `SDL_QUIT`, so the program can save or confirm first; the
  shutdown carries on once it has quit. A program that keeps running
  after Message_Quit is ended at its next event pump, as RISC OS requires.
- **Starting from a TaskWindow:** a desktop window can't be opened there
  (the TaskWindow already is the program's Wimp task): `SDL_CreateWindow`
  fails with "Can't open a desktop window from a TaskWindow: start the
  program with *WimpTask". Start SDL programs from `!Run`, or type
  `*WimpTask Run prog args`.
- **Mouse clicks** are never lost, even at a few frames a second: a click
  pressed and released between two event pumps is reported as a press and
  a release where it happened.
- **Sound:** `SDL_OpenAudioDevice` / `SDL_OpenAudio` play through the RISC
  OS audio driver, over SharedSoundBuffer (it mixes with other programs'
  sound and resamples to the hardware rate). SharedSound is part of RISC
  OS; users install SharedSoundBuffer and StreamManager themselves:
  - **Download:** the `ssb.zip` download on Andrew Sellors' RDPClient
    page, <https://orac.co.uk/software/rdpclient/rdpclient.html>. Merge its `!System` into yours.
  - **Background:** John Duffell's own site (now on the Internet Archive)
    has more details: <https://web.archive.org/web/20110920080106/http://www.duffell.riscos.me.uk/>

  Load the modules in `!Run`:

  ```
  RMEnsure SharedSound 1.07 IfThere System:Modules.SSound Then RMLoad System:Modules.SSound
  RMEnsure StreamManager 0.03 IfThere System:Modules.StreamMan Then RMLoad System:Modules.StreamMan
  RMEnsure SharedSoundBuffer 0.07 IfThere System:Modules.SSBuffer Then RMLoad System:Modules.SSBuffer
  ```

  Without them SDL falls back to its `dsp` driver (DigitalRenderer).
  `SDL_AUDIODRIVER=riscos` or `dsp` forces one. `!LoopWave`
  (SDL's loopwave test) is the worked example.
- **OpenAL:** programs that use OpenAL (games with 3D sound) link the
  devkit's `libopenal.a` (OpenAL Soft 1.19.1): `-lopenal -lSDL2 -lEGL -lOSMesa
  -lstdc++ -lz -lm`. It plays through SDL's sound, so the modules above
  are needed the same way. OpenAL mixes in SDL's audio thread: link with
  UnixLib 5.0.1 or later and load PThreadTicker (see the README). OpenAL's error
  messages go to stderr, which opens a command window in the desktop; set
  `ALSOFT_LOGFILE` to a file in `!Run` to catch them there. `tests/altest.c`
  is a small complete example.
- **MIDI music:** RISC OS has no General MIDI synthesiser of its own.
  [riscos-midisynth](https://github.com/adyoull/riscos-midisynth) plays
  `.mid` files through a SoundFont; its `midisynth_render` output can be
  mixed into an SDL audio callback. OpenTTD's RISC OS port uses it.

## Step by step

1. **Link riscos-mesa's SDL2 and Mesa:**

   ```sh
   $CC $RO_CFLAGS -I$STAGE/include -I$STAGE/include/SDL2 -static prog.c \
       -o '!Prog/!RunImage,e1f' -L$STAGE/lib -lSDL2 -lEGL -lOSMesa -lstdc++ -lz -lm
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

4. **Name and icon:** the task name, the icon bar sprite and the icon
   bar menu title come from the application directory: `!MyGame` gives
   "MyGame" and the `!MyGame` sprite, or the generic application sprite if
   there's none. For your own icon, supply `!Sprites` and add
   `IconSprites <MyGame$Dir>.!Sprites` to `!Run`. `SDL_HINT_APP_NAME`
   changes the name.
5. **Data files:** open them from the application directory
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
  - Textures: see "Speed" in [README.md](README.md). `GL_CLAMP_TO_EDGE`
    instead of `GL_CLAMP`, and `glHint(GL_PERSPECTIVE_CORRECTION_HINT,
    GL_FASTEST)` for mipmapped scenes, keep games on the fast path.
- **Threads:** SDL's sound runs in a thread. Link with UnixLib 5.0.1 or
  later and load the PThreadTicker module (see [README.md](README.md)).
- **SDL's GL renderer:** riscos-mesa's SDL deliberately leaves SDL's
  OpenGL *renderer* out. `SDL_CreateRenderer` programs use SDL's faster
  software renderer, and only programs that call GL themselves use Mesa.

## Going native

SDL is a fine way to write a RISC OS GL program: the driver multitasks,
handles the Wimp and uses riscos-mesa underneath. To use EGL directly
instead, for GL views in a window of your own, sprites or several
surfaces, see `docs/EGL-GUIDE.md`.

## Results

- **Raspberry Pi 4 (20.3.5-5):** SDL's three GL test programs (`!TestGL2`,
  `!TestGLES`, `!TestGLES2` in the ports zip) work in desktop windows.
  Each shows under its own name, with its own icon bar icon.
- **Build:** they cross-build unchanged with the steps above.
- **Host rig:** the SDL host harness (`tests/host-harness`) covers the
  driver's GL context creation for desktop GL, ES 1.1 and ES 2.0.
- **For comparison:** `sdlgltest`, the same pattern, ran at 153 fps in a
  640x480 window on the Pi 4, measured before the 20.3.5-7 speed-ups.
