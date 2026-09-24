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
