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
Quit, the keys passed on to the Wimp, and keys read from a fake keyboard
(OS_Byte 121/129/196): a tap gives one SDL_KEYDOWN and no repeats, a held
key repeats after the keyboard's delay and at its rate, only the newest key
repeats, *FX 11,0 stops repeats, and a slow frame gives one repeat, not a
burst. Like the GL harness it runs on a
stack below 2 GB and is built -no-pie, as the driver passes blocks to SWIs
as 32-bit addresses.

    SDL=<patched SDL-release-2.26.0> tests/host-harness/sdl-wimp/run.sh


## `sdl-arm/`: SDL's ARM SIMD and NEON blitters

`sdl-arm/run.sh` builds the patched SDL tree for ARM Linux with the same
blitter options as `build/build-sdl2.sh` and runs `armblit.c` under
qemu-arm, on an emulated Cortex-A8 with NEON and again with NEON off
(ARMv6 SIMD only). Through SDL's own API it checks that sprites with
alpha onto a surface without alpha use the ARM routine, with colours
within 1 of the exact blend (SDL's C code is checked alongside); that onto
a surface with alpha SDL's C code is used, alpha and all (the riscos-mesa
guard in `SDL_blit_A.c`: remove it and this fails); ARGB onto RGB565;
`SDL_FillRect` at 8, 16 and 32 bpp and two pixel conversions exactly;
and that nothing outside a blit changes. The blitter code and SDL's choice
of routine are the same as on RISC OS; only SDL's CPU check differs.
Needs `qemu-arm` and `arm-linux-gnueabihf-gcc`; run by `tests/run-all.sh`
with `ARM=1`.

    SDL=<patched SDL-release-2.26.0> tests/host-harness/sdl-arm/run.sh


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


## `qemu/`: RISC OS's alignment rules for the ARM checks

RISC OS aborts on an unaligned load or store; Linux and plain qemu-arm
don't. `qemu/build-qemu.sh` builds QEMU 8.2.2 with a patch that traps them
as RISC OS does (`QEMU_ARM_ALIGN_TRAP=1`, with `QEMU_ARM_ALIGN_IGNORE`
exempting code ranges). `qemu/trapped-ranges.py` works out, from a static
program's link map, the ranges to exempt, so that only libOSMesa and the
check itself are trapped (glibc's string functions use unaligned loads).
With `QEMU_ALIGN` set, `mesa/arm/run-arm.sh` runs every check this way; an
unaligned access stops it with SIGBUS (exit status 135). The checks that
deliberately use byte-misaligned buffers skip those cases, as RISC OS
colour buffers are word aligned. Details in `qemu/README.md`.

    tests/host-harness/qemu/build-qemu.sh /tmp/qemu-at
    QEMU_ALIGN=/tmp/qemu-at/qemu-arm tests/host-harness/mesa/arm/run-arm.sh
