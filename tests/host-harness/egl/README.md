EGL host harness: runs `egl/egl_riscos.c` on an x86-64 Linux host against a
host-built Mesa 20.3.5 OSMesa, with a fake RISC OS (`fake_riscos.c`): a
32bpp screen in memory in either colour order, Wimp windows with visible
area and scroll offsets (one redraw rectangle each), OS_SpriteOp 15 and 34,
mode/VDU variables and OS_Byte 19. It checks configs and their sort order,
pbuffer and pixmap (sprite) rendering, where window surfaces land on the
screen (scrolling, resizing, work area surfaces, the redraw helper), full
screen double-buffered and direct, both colour orders, errors and deferred
destruction, OpenGL ES 1.1 and 2.0 contexts, the DispmanX compatibility
library (compiled as Pi code is, in harness_es.c), and the extensions: client and platform display calls,
surfaceless contexts, sync objects, buffer age, swap with damage (window
and full screen), partial update, surface locking and the debug callback.

Pointers pass through 32-bit SWI registers as on RISC OS, so everything
must stay below 2 GB. That's why it's built with -no-pie, malloc is kept on the brk
heap in one arena, and the tests run on a thread whose stack is at 1.5 GB.

    M=<mesa-20.3.5 source with a host build in $M/build, e.g.
       meson setup build -Dosmesa=classic -Ddri-drivers= -Dgallium-drivers=
       -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled
       -Dplatforms= -Dgles1=disabled -Dgles2=disabled -Dllvm=disabled>
    (with patches/mesa applied: the ES tests need OSMESA_ES1/ES2_PROFILE)
    O=$M/build/src/mesa/drivers/osmesa; X=../../../dispmanx
    F="-no-pie -O1 -w -std=gnu99 -Ifake -I$X/include -I$X -I../../../egl/include -I$M/include"
    gcc $F -D__riscos__ -c harness_es.c $X/bcm_host.c
    gcc $F harness.c fake_riscos.c ../../../egl/egl_riscos.c harness_es.o bcm_host.o \
      -o harness -L$O -lOSMesa -lpthread -Wl,-rpath,$O && ./harness

Expected: `265 checks, 0 failures: ALL PASS`.

## Running a port (docs/porting)

`portrun.c` runs a whole ported program on the fake RISC OS:

- The fake answers Wimp_Initialise, CreateWindow, OpenWindow and Poll
  from a script. It sends a redraw, then FRAMES null events, then the
  KEYS script, then a close request. The script can press keys, resize
  the window, click (Select, Menu, Adjust) at a position, move the pointer,
  release the buttons, turn the scroll wheel and choose from an open Wimp
  menu (see `portrun.c`; `MENUS=1` prints each menu opened). The fake also
  answers Wimp_GetPointerInfo, Wimp_CreateMenu, OS_Pointer 2 and OS_Byte
  121.
- The screen is saved as a PPM file for checking.

For example, mesa-demos' es2gears over `ports/mesa-demos/eglut_riscos.c`:

    D=<mesa-demos-9.0.0>/src; P=../../../ports
    F="$F -D__riscos__ -I$P/common -I$P/mesa-demos/upstream/eglut -I$P/mesa-demos/upstream/util"
    gcc $F -c ../../../egl/egl_riscos.c $P/common/riscos_wimpwin.c
    gcc $F -Dmain=app_main -c $P/mesa-demos/upstream/eglut/eglut.c \
        $P/mesa-demos/eglut_riscos.c $P/mesa-demos/upstream/opengles2/es2gears.c \
        $P/mesa-demos/upstream/util/matrix.c
    gcc -no-pie -w -I. -Ifake -c fake_riscos.c portrun.c
    gcc -no-pie *.o -o es2gears -L$O -lOSMesa -lpthread -lm -Wl,-rpath,$O
    FRAMES=100 KEYS=0x18C,r1000x700,32 PPM=out.ppm ./es2gears

(`fake_riscos.c` and `portrun.c` are built without `__riscos__`; the port
and `egl_riscos.c` with it.)

freeglut's RISC OS back end has its own run script built on this fake:
`../glut/run.sh` (see `glut/README.md`).
