riscos-mesa devkit documentation
================================

Start with ../README.md (the beginner's guide) and ../examples.

  EGL-GUIDE.md       The EGL programming guide: the full reference for
                     OpenGL and OpenGL ES on RISC OS through EGL.
  porting/           Guides to porting existing programs: Raspberry Pi
                     (DispmanX) code, SDL2 programs, GLUT programs, Mesa's
                     demos and the OpenGL ES 2.0 Programming Guide's
                     samples. Start with porting/README.md.

The GPU: programs draw on a GPU wherever RISC3D has one, with no code for
it. On a native Raspberry Pi 4 they also need the GPU library, libMesaGPU
(lib/libMesaGPU.a, lib/shared/libMesaGPU.so): see ../README.md, the
devkit's guide, and porting/mixed.md.

These are Markdown files: plain text with a little formatting, readable
in any text editor. Links to files such as ports/... or tests/... point
into the riscos-mesa source repository, which also has the worked ports'
source; the built programs are in the release's ports and tests zips.
