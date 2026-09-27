Runs `SDL_riscosopengl.c` on an x86-64 Linux host against a host-built
libOSMesa.a. `_kernel_swi` emulates OS_SpriteOp 15 (create sprite) and the
driver's plot is replaced by a check of what would reach the screen.
The glue casts pointers to int (as all RISC OS SWI code does), so the stub
allocator uses MAP_32BIT and the binary is built -no-pie.

    S=<patched SDL-release-2.26.0>; M=<mesa-20.3.5 with include/>
    gcc -no-pie -w -std=gnu99 -DSDL_VIDEO_DRIVER_RISCOS=1 -DSDL_VIDEO_OPENGL=1 \
      -DSDL_VIDEO_OPENGL_OSMESA=1 -I$S/include -I$S/src/video/riscos -I$S/src/video \
      -I$S/src -Ifake -I$M/include -include $S/src/SDL_internal.h \
      harness.c $S/src/video/riscos/SDL_riscosopengl.c -o harness \
      libOSMesa.a -lstdc++ -lz -lm -lpthread && ./harness

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

