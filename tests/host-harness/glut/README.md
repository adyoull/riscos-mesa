freeglut host test: builds freeglut 3.8.0 with its RISC OS back end
(`glut/riscos`) for Linux against the fake RISC OS in `../egl`, then runs
freeglut's own demos through `portrun` with scripted input and checks what
they report:

- `One`: game mode, leaving it with Escape, Wimp menus from GLUT menus,
  the Menu button, Adjust over a nested subwindow, submenu choices;
- `CallbackMaker`: key presses and releases, F1, F12, Page Up, cursor
  keys, Home, Copy/End, Select down/drag/up, the scroll wheel, the window
  resized by the user, subwindow positions;
- `subwin`, `fractals_random` (a single-buffered window drawn from the
  idle callback);
- `es2tri.c`: the OpenGL ES 2.0 build.

    build/build-freeglut.sh      # (for the patched freeglut source in src/)
    M=<host Mesa, as in ../egl/README.md> GLU=<host libGLU.a> \
    STAGE=<stage with the GLES headers> tests/host-harness/glut/run.sh

A host libGLU.a: clone glu-9.0.1 (the commit build-glu.sh pins) and
compile its `src/` `.c` and `.cc` files with gcc/g++ (`-Iinclude
-Isrc/include -Isrc/libnurbs/internals -Isrc/libnurbs/interface
-Isrc/libnurbs/nurbtess -DLIBRARYBUILD`), leaving out
`libtess/priorityq-heap.c` (it is included by `priorityq.c`).

Expected: `0 failures`. The screens are saved as PPM files in `$OUT`
(default /tmp/glut-host).
