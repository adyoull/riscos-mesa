Runs `SDL_riscosopengl.c` (SDL's GL windows: the sprite path, and the
opt-in EGL path, riscos-mesa EGL window surfaces) on an x86-64 Linux
host: the driver file, the real EGL
(`egl/egl_riscos.c`), a host-built OSMesa, the EGL harness's fake RISC OS
(`egl/fake_riscos.c`) and the fake VideoOverlay (`ovl/fake_ovl.c`). It
checks the sprite path (no hints: GL renders into the window's sprite,
plotted by the driver) and, on the EGL path, what reaches the fake
screen: a desktop window at 1:1, a render
size (`SDL_RISCOS_GL_RENDER_SIZE`) stretched over the window and over the
screen full screen, redraws, the other colour order, an overlay
(`SDL_RISCOS_GL_OVERLAY`) scaled by the display with frames held for a
vsync and shown from the event loop, OpenGL ES, and swap interval pacing.
The code casts pointers to int (as all RISC OS SWI code does), so it runs
on a stack below 2 GB with malloc kept on the brk heap, built -no-pie.
The build line is `sdl_harness` in `tests/run-all.sh`.

## `sdl-wimp/`: the driver's Wimp event handling

`sdl-wimp/run.sh` compiles `SDL_riscosevents.c` into `wimp-events.c`, which
hands it Wimp_Poll blocks and records the SWIs it calls and the SDL events
it sends: Message_PreQuit (acknowledged; the desktop shutdown restarted with
Ctrl-Shift-F12 only when the program quits in answer to it), Message_Quit
(SDL_APP_TERMINATING and SDL_QUIT, then the driver quits for a program that
carries on), the close icon (SDL_WINDOWEVENT_CLOSE), the icon bar menu's
Quit, and the keys passed on to the Wimp. Like the GL harness it runs on a
stack below 2 GB and is built -no-pie, as the driver passes blocks to SWIs
as 32-bit addresses.

    SDL=<patched SDL-release-2.26.0> tests/host-harness/sdl-wimp/run.sh


## `openal/`: OpenAL Soft through SDL's sound

`openal/run.sh` builds the devkit's OpenAL Soft 1.19.1 (with
`patches/openal`, the same options as `build/build-openal.sh`) and SDL 2.26
(audio only) for the host, then runs `tests/altest.c` with SDL's `disk`
audio driver, which writes the mixed sound to a file (OpenAL is told to mix
16-bit stereo at 22050 Hz through an `ALSOFT_CONF` file).
`openal/check-tones.py` checks the recording: 440 Hz in the middle, 660 Hz
on the left and 880 Hz on the right, about a second each, in that order.
The host builds are kept in `OUT` and redone when the patch changes.

    SRC=<where build-sdl2.sh and build-openal.sh left their sources> tests/host-harness/openal/run.sh
