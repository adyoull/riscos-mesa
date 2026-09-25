# Porting GLUT programs (freeglut)

**Worked port:** freeglut 3.8.0's own demos, built from their unchanged
source by `build/build-ports.sh` into `stage/ports/freeglut` (the
`riscos-mesa-glut` zip): `!Shapes`, `!One`, `!Subwin`, `!Lorenz`,
`!Fractals`, `!Resizer`, `!View3D` and `!Keyboard`.

GLUT is the small library most OpenGL examples, tutorials and books use to
open a window and read the keyboard and mouse (the *OpenGL Programming
Guide*'s samples, NeHe-style tutorials, university course code...).
riscos-mesa builds freeglut, the open source GLUT, with a **native RISC OS
back end** (`glut/riscos/`), so GLUT programs usually build with no
changes and behave like RISC OS programs:

- each GLUT window is a Wimp window, and the program multitasks;
- subwindows are drawn inside their window (nested as deep as you like);
- GLUT menus are real Wimp menus;
- drawing goes through riscos-mesa's native EGL.

It's a back end like freeglut's X11, Windows or Wayland ones, not a
translation layer: nothing pretends to be X.

## Step by step

1. **Build.** Use GCCSDK GCC 10 with riscos-mesa's flags (see
   [README.md](README.md)) and link:

       -lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm

   `#include <GL/glut.h>` or `<GL/freeglut.h>` as the program does.
   Add `-D_GNU_SOURCE` if it uses GNU extras.

2. **Text output.** GLUT programs print (help text, frame rates). From a
   Wimp task that opens a command window, so link
   `ports/sdl2-tests/riscos_output.c` with `-DOUTPUT_VAR='"App$Output"'`
   and set the variable in `!Run`; the output goes to a file. The demos'
   `!Run` files (`ports/freeglut/riscos/`) show how.

3. **Files.** Programs that open data files relative to the current
   directory need the application's path instead, as in every port.
   `!Fractals` passes its data file on the command line:

       Do Run <Fractals$Dir>.!RunImage /<Fractals$Dir>/fractals.dat

   (`Do`, because `Run` doesn't expand `<variables>` in its arguments.)

4. **`!Run`.** Set `<App>$Dir`, check for SharedUnixLibrary and
   ARMEABISupport, and run the program. The task name comes from the
   application directory (`!Shapes` gives "Shapes"); `FreeGLUT$TaskName`
   overrides it.

That's all for most programs.

## How GLUT maps onto RISC OS

**Windows**

- `glutCreateWindow` opens a Wimp window with a title bar, close, back,
  toggle-size and adjust-size icons. `glutInitWindowPosition` places the
  client area (pixels from the top left of the screen); without it windows
  open near the centre.
- Resizing the window by hand calls the reshape callback, and the next
  frame is drawn at the new size.
- `glutFullScreen` and game mode (`glutEnterGameMode`) replace it with a
  window without a title bar that covers the screen. **The screen mode
  isn't changed** (that would change it for every task): the game mode
  size is the desktop's, and other tasks keep running underneath.
- `GLUT_BORDERLESS` in the display mode gives windows without a title bar.
- `glutIconifyWindow` closes the window, like `glutHideWindow`.

**Display modes**

- `GLUT_RGB`/`GLUT_RGBA`, `GLUT_DOUBLE` or `GLUT_SINGLE`, `GLUT_DEPTH`,
  `GLUT_STENCIL` and `GLUT_ALPHA` are supported.
- **`GLUT_SINGLE` works**: the window is shown after each display
  callback, and every 20 ms while an idle or timer callback runs, so
  programs that draw with `glFlush` and never swap appear as they should.
- Not available: `GLUT_MULTISAMPLE` (you get an ordinary window, as GLUT
  gives on displays without it), `GLUT_ACCUM` (no accumulation buffer:
  `glAccum` reports `GL_INVALID_OPERATION`), `GLUT_STEREO`, `GLUT_INDEX`
  (colour index), and overlays.
- Contexts: OpenGL 2.1 (compatibility). `glutInitContextVersion(3, x)` or
  a core profile fails, as on any driver without them.

**Mouse and keyboard**

- Buttons: Select is `GLUT_LEFT_BUTTON`, Menu `GLUT_MIDDLE_BUTTON` and
  Adjust `GLUT_RIGHT_BUTTON`, their places on the mouse (as in SDL).
- The scroll wheel calls the mouse wheel callback, or (with none) the
  mouse callback with buttons 3 (up) and 4 (down), like X11 GLUT.
- Motion, passive motion and entry callbacks, button releases and key
  releases (`glutKeyboardUpFunc`, `glutSpecialUpFunc`) all work. The Wimp
  only reports presses and clicks, so freeglut reads the pointer and
  keyboard after each poll, and polls every 20 ms while something is held
  or the pointer is over a window that wants motion.
- Special keys: F1–F12, the cursor keys, Page Up, Page Down, Home, Copy
  (`GLUT_KEY_END`), Insert, and Shift, Ctrl and Alt on their own
  (`GLUT_KEY_SHIFT_L` and so on, left and right). `glutGetModifiers` works
  in input callbacks.
- Keys the program has no callback for go on to other tasks
  (`Wimp_ProcessKey`), so F12 and hot keys still work.
- `glutIgnoreKeyRepeat` / `glutSetKeyRepeat` work.
- Keys go to the (sub)window under the pointer, in the window that has the
  input focus. Clicking a window gives it the focus.
- `glutSetCursor(GLUT_CURSOR_NONE)` hides the pointer over the window.
  Other cursor shapes show the normal pointer. `glutWarpPointer` works.

**Menus**

- `glutCreateMenu`, `glutAddMenuEntry`, `glutAddSubMenu`,
  `glutAttachMenu`... build the menus as usual; clicking a button with a
  menu attached opens it as a **Wimp menu** at the pointer, titled with
  the task name.
- **Menu always opens a menu:** the one attached to the middle button, or
  else to the right or left button. So programs that attach their menu to
  `GLUT_RIGHT_BUTTON` (most do) work the RISC OS way.
- Choosing with Adjust keeps the menu open. Menus can be changed from the
  callback (a `glutChangeToMenuEntry` shows next time the menu opens).
- `glutMenuStatusFunc` / `glutMenuStateFunc` are called when a menu opens
  and closes.

**Other**

- Timers, idle callbacks, `glutPostRedisplay`, `glutGet`,
  `glutDeviceGet`, window stacking (`glutPushWindow`/`glutPopWindow`),
  titles, `glutMainLoopEvent`, `glutLeaveMainLoop` and
  `GLUT_ACTION_ON_WINDOW_CLOSE` work as documented.
- The geometric shapes, teapot, bitmap and stroke fonts all work.
- `glutGet(GLUT_ELAPSED_TIME)` has centisecond resolution (the RISC OS
  monotonic clock).
- No joystick, spaceball, dials or tablet (`glutDeviceGet` says so).
- `GLUT_FPS` (the environment variable) prints frame rates, so use it with
  the output redirected.

## OpenGL ES programs

`libfreeglut-gles.a` is the same library over OpenGL ES 1.1 and 2.0, as
freeglut's own ES builds are. Build with `-DFREEGLUT_GLES` (the headers
then include `GLES/gl.h` and `GLES2/gl2.h`) and link:

    -lfreeglut-gles -lEGL -lOSMesa -lstdc++ -lz -lm

`glutInitContextVersion(2, 0)` gives ES 2.0, the default ES 1.1. Menus
work (they're Wimp menus); GLUT's text drawing doesn't, as in every ES
build of freeglut.

## Things to check in the program

- **Speed.** Rendering is on the CPU. Fixed-function programs are fine at
  400x400 or so; big windows full of shaded, lit geometry are slow.
  Programs that ask for huge default windows are worth making smaller.
- **`GLUT_ACCUM`.** Motion blur, depth-of-field and anti-aliasing demos
  from the *Red Book* use the accumulation buffer, which isn't available.
- **Reading stdin.** Programs that ask questions on the console (a few
  demos do: `CallbackMaker` waits for Return at the start) would open a
  command window and stop the desktop. Take those parts out.
- **Blocking loops.** A program that never returns to `glutMainLoop`
  (a long calculation in a callback) stops the desktop meanwhile, as in
  any Wimp task.

## Going native

GLUT hides the window system. That's what makes the ports painless, but a
RISC OS program written from scratch should use the native EGL with its
own Wimp_Poll loop: an icon bar icon, proper RISC OS menus built by the
program, drag and drop, and saving. See [../EGL-GUIDE.md](../EGL-GUIDE.md).

## Results

All of freeglut's demos that the renderer can support run on the host test
harness, with the fake Wimp driving them (keys, clicks, drags, the wheel,
resizing, menus and submenus): `tests/host-harness/glut/run.sh` checks 23
behaviours. `smooth_opengl3` needs OpenGL 3.2 and `accum` the
accumulation buffer, so those two can't work. Results on a Raspberry Pi
are still to come.
