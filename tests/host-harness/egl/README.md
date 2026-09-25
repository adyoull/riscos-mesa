EGL host harness: runs `egl/egl_riscos.c` on an x86-64 Linux host against a
host-built Mesa 20.3.5 OSMesa, with a fake RISC OS (`fake_riscos.c`): a
32bpp screen in memory in either colour order, Wimp windows with visible
area and scroll offsets (one redraw rectangle each), OS_SpriteOp 15 and 34,
mode/VDU variables and OS_Byte 19. It checks configs and their sort order,
pbuffer and pixmap (sprite) rendering, where window surfaces land on the
screen (scrolling, resizing, work area surfaces, the redraw helper), full
screen double-buffered and direct, both colour orders, errors and deferred
destruction.

Pointers pass through 32-bit SWI registers as on RISC OS, so everything
must stay below 2 GB. That's why it's built with -no-pie, malloc is kept on the brk
heap in one arena, and the tests run on a thread whose stack is at 1.5 GB.

    M=<mesa-20.3.5 source with a host build in $M/build, e.g.
       meson setup build -Dosmesa=classic -Ddri-drivers= -Dgallium-drivers=
       -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled
       -Dplatforms= -Dgles1=disabled -Dgles2=disabled -Dllvm=disabled>
    O=$M/build/src/mesa/drivers/osmesa
    gcc -no-pie -O1 -w -std=gnu99 -Ifake -I../../../egl/include -I$M/include \
      harness.c fake_riscos.c ../../../egl/egl_riscos.c -o harness \
      -L$O -lOSMesa -lpthread -Wl,-rpath,$O && ./harness

Expected: `90 checks, 0 failures: ALL PASS`.
