# riscos-mesa devkit: a beginner's guide

This devkit lets you write programs that draw 3D (and 2D) graphics with
**OpenGL** on RISC OS 5. It also includes SDL2, GLUT and OpenAL (sound).

This guide takes you from nothing to a working program. It explains
*why* things are done the way they are, not just *what* to type, so
that when something goes wrong you have a good idea where to look.

If you only read one section, read
[Your first program in ten minutes](#your-first-program-in-ten-minutes).

---

## The big picture

**OpenGL** is a standard way of describing a picture to a computer:
"here's a triangle, these are its corners and colours; now draw it from
over here". It's the same on Windows, Linux, macOS, phones and now RISC
OS, which is why so many games, tools and tutorials use it.

Most computers have a graphics chip to do the drawing. RISC OS doesn't
have drivers for one, so this devkit does the drawing on the main
processor instead, using **Mesa**, the same software that Linux uses. It
is slower than a graphics chip, but it's complete and exact: OpenGL 2.1,
OpenGL ES 1.1 and OpenGL ES 2.0.

OpenGL only knows how to draw. It doesn't know about windows, screens or
RISC OS. Something has to connect the two, and in this devkit that's
**EGL**:

```
  your program ---- "draw a triangle" ----> OpenGL (Mesa)
       |                                        |
       |  "draw into this window,               |  draws pixels into
       |   show the frame now"                  |  a hidden picture
       v                                        v
      EGL  -------- "show it" --------->  the RISC OS desktop
```

Three EGL words you'll meet everywhere:

- a **config** describes the pixels: how many bits of colour, whether
  there's a depth buffer (for 3D) and so on;
- a **surface** is the picture you draw on: a window, the whole screen,
  or a sprite;
- a **context** is OpenGL's memory: the current colour, matrices,
  textures, shaders. Making a context **current** on a surface means
  "draw there from now on".

EGL is itself a standard (made by Khronos, the group behind OpenGL),
with a few RISC OS additions for Wimp windows. If you prefer, SDL2 or
GLUT can do EGL's job for you; see
[Which way should I draw?](#which-way-should-i-draw).

---

## What's in the box

| Folder or file | What it is |
| --- | --- |
| `README.md`, `ReadMe` | This guide (the same text; `ReadMe` is for reading on RISC OS) |
| `examples/` | Six small, fully commented programs, and a Makefile that builds them into RISC OS applications |
| `docs/` | The EGL programming guide (the full reference) and the porting guides |
| `include/` | The header files (`GL/gl.h`, `EGL/egl.h`, `SDL2/SDL.h`, `AL/al.h`...) |
| `lib/` | The libraries, ready to link into your programs |
| `lib/pkgconfig/` | Files that tell `pkg-config` how to use each library |
| `bin/sdl2-config` | Tells SDL programs' build scripts how to use SDL |
| `bin/mkrozip.py` | Makes zip files that keep RISC OS filetypes |
| `riscos/PThrTicker` | The PThreadTicker module, for programs with threads (see [Programs with threads](#threads)); copy it into your application |
| `LICENCES.txt` | The licences of everything here. Ship it with your programs |
| `VERSION` | Which release this is |

The libraries:

| Library | What it does | Link with |
| --- | --- | --- |
| `libOSMesa.a` | OpenGL 2.1, OpenGL ES 1.1 and 2.0 (Mesa) | always needed for drawing |
| `libEGL.a` | EGL: connects OpenGL to RISC OS windows and the screen | `-lEGL -lOSMesa -lstdc++ -lz -lm` |
| `libGLU.a` | Handy extras for OpenGL (perspective, tessellation...) | add `-lGLU` before `-lOSMesa` |
| `libSDL2.a` | SDL 2.26: windows, keyboard, mouse, sound, OpenGL | `-lSDL2 -lOSMesa -lstdc++ -lz -lm` (this SDL has OpenGL built in, so it always needs Mesa) |
| `libglut.a` | freeglut 3.8: the classic GLUT toolkit | `-lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm` |
| `libopenal.a` | OpenAL Soft: positioned 3D sound (it plays through SDL) | `-lopenal -lSDL2 -lOSMesa -lstdc++ -lz -lm` |
| `libbcm_host.a` and friends | Lets existing Raspberry Pi (Pi 1-3) OpenGL ES programs run unchanged. For porting only: new programs should use EGL directly | see the porting guides |

---

## What you need

**To build programs:** a Linux computer (a virtual machine or Docker
container on a Mac or Windows PC is fine) with **GCCSDK's GCC 10**, the
compiler that makes RISC OS programs (`arm-riscos-gnueabihf-gcc`).

This is called **cross-compiling**: building, on one kind of computer,
programs that run on another. The compiler and Mesa are big, and a Linux
PC builds a program in seconds. Building *on* a RISC OS machine isn't
covered here.

Getting GCCSDK GCC 10 is the one fiddly step. If you already have it
(for example from another port's build kit), you're set. If not, the
riscos-mesa repository's `build/TOOLCHAIN.md` explains how to build it
from GCCSDK's sources, which takes about half an hour of the computer's
time. Check that it works with:

```sh
arm-riscos-gnueabihf-gcc --version
```

**To run them:** a RISC OS 5 machine with an ARMv7 or later processor:
a Raspberry Pi 2, 3 or 4, or a board with a Cortex-A8, A9 or A15
(BeagleBoard-xM, PandaBoard, ARMini, i.MX6 boards, Titanium). A Pi 1,
Zero or Zero W won't do: their older ARMv6 processor lacks instructions
the compiler uses. (The Zero 2 W is fine.)

Install two modules from PackMan: **SharedUnixLibrary** (part of
UnixLib, the C library programs are built with) and **ARMEABISupport**
(memory and support routines that GCC 10's programs rely on). The
examples' `!Run` files load them, and stop with a clear message if
they're missing.

For sound, install **SharedSoundBuffer** and **StreamManager** (John
Duffell's freeware: the `ssb.zip` download on Andrew Sellors' RDPClient
page, <https://orac.co.uk/software/rdpclient/rdpclient.html>) into `!System`. They mix your program's sound with
everyone else's. Without them, SDL falls back to DigitalRenderer.

---

## Your first program in ten minutes

1. **Unpack the devkit** somewhere on the Linux machine:

   ```sh
   tar xzf riscos-mesa-devkit-VERSION.tgz
   cd riscos-mesa-devkit-VERSION/examples
   ```

2. **Tell the build where the compiler is.** GCCSDK installs into a
   folder usually called `env`, with the compiler in `env/bin`. Either
   name that folder:

   ```sh
   export GCCSDK_ENV=$HOME/gccsdk/env
   ```

   or put its `bin` folder on your `PATH`:

   ```sh
   export PATH=$HOME/gccsdk/env/bin:$PATH
   ```

   (Change `$HOME/gccsdk/env` to wherever yours is.)

3. **Build the examples:**

   ```sh
   make zip
   ```

   This builds six ready-to-run applications in `build/` (`!GLFull`,
   `!GLWindow`, `!GLShaders`, `!SDLSpin`, `!Teapot`, `!Tune`) and packs
   them into `build/examples.zip`.

4. **On the RISC OS machine, install SharedUnixLibrary and
   ARMEABISupport from PackMan** if you haven't already.

5. **Copy `examples.zip` to the RISC OS machine** (network share, USB
   stick, email...), open it with SparkFS or the `unzip` command, and
   **double-click `!GLWindow`**. You should see a triangle spinning in a
   window that you can move, resize and close.

6. **Change something.** Open `2-window/window.c`, find
   `glColor3f(1, 0, 0)` (the red corner) and make it
   `glColor3f(1, 1, 0)`: that corner turns yellow. Run `make zip`
   again and copy it across. That's the whole cycle.

Once the compiler works, this takes about ten minutes.

---

## Which way should I draw?

There are four ways to draw, plus one for sound. All of them draw with
the same OpenGL; they differ in who looks after the window.

| You want to... | Use | Example |
| --- | --- | --- |
| Write a new RISC OS desktop program with a 3D view | **EGL and the Wimp** | 2 (and 3 for shaders) |
| Take over the whole screen (a game or demo) | **EGL, full screen** | 1 |
| Port a program that already uses SDL, or write one that also builds on Linux or Windows | **SDL2** | 4 |
| Follow a textbook or tutorial that uses GLUT, or port a GLUT program | **GLUT** | 5 |
| Play sound, positioned in 3D | **OpenAL** (with any of the above) | 6 |

Then there's the choice of **which OpenGL**:

- **OpenGL 2.1** (`GL/gl.h`, examples 1, 2, 4, 5), including the "fixed
  function" style (`glBegin`, `glColor`, `glLight`...). On this devkit
  it's the **fastest** style, because Mesa has hand-tuned paths for it.
  Most older games and tutorials use it.
- **OpenGL ES 2.0** (`GLES2/gl2.h`, example 3): everything is drawn by
  small programs you write, called shaders. This is what phones, WebGL and
  the Raspberry Pi's own chip use, so modern tutorials and ports need it.
  Shaders run in an interpreter here, so they're several times slower.
- **OpenGL ES 1.1** (`GLES/gl.h`) is for porting old mobile code.

---

## The examples, one by one

Read them in order: each one builds on the one before, and the comments
explain each line as it comes up.

1. **`1-fullscreen`**: the five EGL steps that every program does
   (display, config, surface, context, make current), then drawing and
   showing frames. It takes over the screen for five seconds.
2. **`2-window`**: the same triangle in a desktop window. You learn the
   Wimp poll loop, redraws, resizing, quitting properly and how to
   animate without slowing other programs down. **Start your own
   desktop programs from this one.**
3. **`3-shaders`**: example 2 redone with OpenGL ES 2.0: a vertex shader
   and a fragment shader, vertex arrays and uniforms.
4. **`4-sdl2`**: a resizable SDL window with OpenGL, Escape to quit.
5. **`5-glut`**: a lit teapot with depth testing, a timer, the keyboard
   and a GLUT menu (which appears as a Wimp menu).
6. **`6-sound`**: a three-note tune through OpenAL, moving from the left
   speaker to the right.

Build one with `make 2-window` (the folder name), or all of them with
`make`. Each becomes an application in `build/`, made from the program
plus the `!Run` and `!Help` files in its folder.

To start your own program, say `mygame` from example 2:

1. Copy the folder: `cp -r 2-window mygame`, and rename `window.c` to
   `mygame.c`.
2. Put the application's name (without the `!`) in `mygame/APP`, e.g.
   `MyGame`.
3. In `mygame/!Run,feb` and `mygame/!Help,fff`, change `GLWindow` to
   `MyGame` everywhere.
4. In the Makefile, add `mygame` to the `EXAMPLES :=` line, and add a
   line like the example's (the name before the colon must be the
   folder's name):

   ```make
   mygame: ; $(call build,$@,mygame.c,$(GL_LIBS))
   ```

Then `make mygame` builds `build/!MyGame`.

---

## The build flags, and why

This is, slightly simplified, the command the Makefile runs for
example 2:

```sh
arm-riscos-gnueabihf-gcc -O2 -Wall -mfpu=vfpv3 -mfloat-abi=hard -fstack-clash-protection \
    -I<devkit>/include window.c -o '!RunImage,e1f' \
    -static -s -L<devkit>/lib -lEGL -lOSMesa -lstdc++ -lz -lm
```

Every part is there for a reason.

| Part | What it does | Why |
| --- | --- | --- |
| `-O2` | Optimise | Faster code. The libraries themselves are built with `-O3`. |
| `-Wall` | Warn about likely mistakes | Cheap insurance: keep it on. |
| `-fstack-clash-protection` | Stops random crashes | **Don't leave this out.** See below. |
| `-mfpu=vfpv3` | Use the hardware floating point unit, VFPv3 version | Every ARMv7 RISC OS machine has at least VFPv3. The newer VFPv4 would stop your program running on the Cortex-A8 and A9 boards (BeagleBoard-xM, PandaBoard, ARMini, i.MX6), and on a Pi 4 it measured no faster. |
| `-mfloat-abi=hard` | Pass numbers with fractions (`float`, `double`) in the floating point unit's registers | GCCSDK's `arm-riscos-gnueabihf` target (the `hf` means hard float) and all the devkit's libraries work this way. Code built differently can't be linked with them. |
| `-I<devkit>/include` | Where the header files are | So `#include <GL/gl.h>` finds the devkit's copy. |
| `-static` | Put all the libraries inside the program | Your program is then one file that runs on any RISC OS 5 machine with the two modules. There's nothing else to install and no library versions to clash. The price is size: about 9 MB, mostly Mesa. |
| `-s` | Strip the debugging names | Makes the program about a third smaller. Leave it out while you're debugging a crash. |
| `-lEGL -lOSMesa -lstdc++ -lz -lm` | The libraries, **in this order** | The linker reads left to right and only takes what's been asked for so far, so each library must come before the ones it uses: EGL uses Mesa, and Mesa uses the C++ runtime (`stdc++`: part of Mesa's shader compiler is written in C++), zlib (`z`) and the maths library (`m`). |
| `,e1f` | The file's RISC OS filetype | See [Moving files to RISC OS](#moving-files-to-risc-os). |

**Why `-fstack-clash-protection` matters.** A program's stack (where
functions keep their local variables) grows as needed: RISC OS adds
memory to it a page (4 KB) at a time, when something touches the page
just below it. A function with more than 4 KB of local variables can
jump straight over that page and land in memory that isn't there. The
program then dies seemingly at random ("abort on data transfer",
"illegal instruction"), often far from the real cause. This flag makes
the compiler step down a page at a time. Mesa has several such
functions; your code might too.

Two more rules:

- **Never use `-pthread`.** GCCSDK's GCC rejects it. Threads are built
  into UnixLib, the C library, so nothing needs adding.
- **Don't mix in code built with a different float setting.** Soft
  float objects won't link with hard float ones.

### Using other build systems (skip this until you port something)

Many programs you port will come with their own build scripts. The
devkit includes the tools those scripts look for:

- **pkg-config:** `export PKG_CONFIG_PATH=<devkit>/lib/pkgconfig` (and
  `PKG_CONFIG_LIBDIR` to the same, so nothing from Linux is picked up).
  Then `pkg-config --cflags --libs egl` (or `gl`, `glesv2`, `glu`,
  `sdl2`, `glut`, `openal`, `zlib`) gives the right flags. The files
  work out their own location, so the devkit can live anywhere.
- **sdl2-config:** put `<devkit>/bin` on your `PATH`. Scripts that run
  `sdl2-config --cflags --libs` will then get the devkit's SDL.
- **CMake, autoconf and the like:** set the compiler to
  `arm-riscos-gnueabihf-gcc` and add the flags above to `CFLAGS`
  (including `-fstack-clash-protection`). Link with `-static`. If the
  build produces files without `,e1f`, add it when you copy the program
  across.

---

## Making a RISC OS application

RISC OS programs usually come as an **application**: a folder whose name
starts with `!`. When you double-click it, RISC OS runs the `!Run` file
inside. The examples' applications contain:

| File | What it is |
| --- | --- |
| `!Run` | An Obey file (a list of commands) that RISC OS runs when you double-click the application |
| `!RunImage` | The program itself |
| `!Help` | Text shown when you choose Help from the Filer menu |

A `!Sprites` file (the application's icon) is optional. Make one with
Paint if you like.

Here is example 2's `!Run`, with what each line is for:

```
Set GLWindow$Dir <Obey$Dir>
```
Remembers where the application is. `<Obey$Dir>` is the folder this
`!Run` file is in.

```
RMEnsure SharedUnixLibrary 1.16 RMLoad System:Modules.SharedULib
RMEnsure SharedUnixLibrary 1.16 Error !GLWindow needs SharedUnixLibrary 1.16 or later (from PackMan)
```
"If the module isn't running, load it; if it still isn't, stop with this
message." SharedUnixLibrary is part of UnixLib, the C library the
program is built with. Without these lines, a missing module shows up
as a confusing crash instead of a clear message.

```
RMEnsure ARMEABISupport 0.00 RMLoad System:Modules.ARMEABISupport
RMEnsure ARMEABISupport 0.00 Error !GLWindow needs ARMEABISupport (from PackMan)
```
The same for ARMEABISupport, which provides memory and support routines
that GCC 10's programs rely on.

```
Run <GLWindow$Dir>.!RunImage
```
Starts the program. Always start a program with `Run` and its full path:
in an Obey file, a bare path can be misread as an abbreviated command
(`A.MyProg` would run `*Append`).

The sound example adds lines that load the sound modules, and the
PThreadTicker module that programs with threads need (see [Programs
with threads](#threads)). Look at `6-sound/!Run,feb`.

### Moving files to RISC OS

Linux doesn't store RISC OS filetypes, so the convention is to put the
type at the end of the name after a comma: `!RunImage,e1f` is an ELF
program (&E1F), `!Run,feb` is an Obey file (&FEB), and `!Help,fff` is
text (&FFF). Two ways to get the types across:

- **A zip made with `mkrozip.py`** (`make zip` does this). It stores
  each file's real type in the zip, so SparkFS or `unzip` on RISC OS
  sets the types and removes the `,xxx` from the names.
- **A network share** (LanManFS, Sunfish, NFS) that understands the
  `,xxx` convention. Copy the application folder straight across.

Don't make the zip with Linux's own `zip` command: the files would
arrive with names like `!Run,feb` and no type, and double-clicking the
application wouldn't work.

---

## Things that trip people up

**"Window Manager is currently in use" when starting a desktop
program.** You started it from a TaskWindow. A TaskWindow is already a
Wimp task, and the program can't be a second one inside it. Start it by
double-clicking its application, or with `*WimpTask Run ...`.
Full-screen programs and ones that open no window work from a
TaskWindow.

**A window appears showing my program's output.** A desktop program
that prints to `stdout` makes RISC OS open a window to show the text.
That's handy for error messages (the examples print one if something
fails), but in normal running write messages to a file, or show them in
your window's title bar.

**The whole machine freezes while my program runs.** RISC OS
multitasks cooperatively: while your code runs, nothing else does,
until you call `Wimp_Poll` (SDL and GLUT call it for you inside their
event functions). Poll at least once a frame, and don't do long jobs
between polls. Pace animation with `Wimp_PollIdle`, as example 2 does,
rather than drawing as fast as possible.

**My window goes blank when another window is dragged across it.** The
desktop doesn't remember what was in your window. When it asks you to
redraw (reason code 1 from `Wimp_Poll`), call `eglRedrawWindowRISCOS`;
EGL keeps your last frame and repaints it. See example 2.

**My full-screen program leaves a mess on the desktop.** Full-screen
drawing goes over the desktop, and the desktop doesn't know. Make the
program a desktop task for its run (`Wimp_Initialise` at the start: it
needn't open a window), and when it finishes, ask the Window Manager to
repaint the whole screen (`Wimp_ForceRedraw` on window -1), then leave
with `Wimp_CloseDown`. Example 1 shows how. The request must come from a
task: a plain program asking leaves parts of the old picture behind.

**No vsync in a window.** In a desktop window, `eglSwapBuffers` shows
the frame straight away and never waits for the screen's refresh.
Waiting would stop every other program. Full screen, it waits (set how
often with `eglSwapInterval`).

**`eglMakeCurrent` fails with `EGL_BAD_MATCH`.** The surface and the
context were made from different configs: the colour order, depth or
stencil size don't match. Make both from the same config.

**Nothing is drawn, only the background colour.** Check `glViewport`
after the window changes size (read `EGL_WIDTH` and `EGL_HEIGHT` from
the surface each frame). Check that you cleared the depth buffer if
depth testing is on. And check that what you draw is between -1 and 1
after your transforms, since anything outside is cut off.

**Crashes that move around when you change unrelated code.** Almost
always a missing `-fstack-clash-protection` somewhere. Every file of
your program needs it, including libraries you build yourself.

<a id="threads"></a>
**Programs with threads (including any using SDL sound or OpenAL, such
as example 6).** UnixLib switches between a program's threads with a
timer, called the "ticker". The desktop swaps programs in and out of
memory, and the timer can go off while *another* program is in memory.
In UnixLib before 5.0.1, the ticker's code lived inside your program,
so at that moment it wasn't there, and the other program crashed.
Two things put that right:

1. **Link with UnixLib 5.0.1 or later** (github.com/adyoull/riscos-unixlib).
   It keeps the ticker's code where it's always in memory. Build your
   GCCSDK with it, or copy its `libunixlib.a` into your GCCSDK (that
   project's README says how), or ask whoever supplied your GCCSDK
   which UnixLib it has.
2. **Ship the PThreadTicker module with your application** and load it
   in `!Run` before the program starts, as example 6 does:

   ```
   RMEnsure PThreadTicker 0.01 RMLoad <MyGame$Dir>.PThrTicker
   ```

   The module is in this devkit's `riscos/` folder (648 bytes, BSD
   licence; its ReadMe and Licence are there too). A module is always
   in memory, which makes it the proper home for the ticker's code.
   Without it, UnixLib 5.0.1 copies the code into a block of the module
   area and runs it from there. That works too, but running code from a
   data block is the more fragile of the two, so ship the module.

Single-threaded programs (examples 1 to 5) aren't affected.

---

## Making it fast

The drawing is done by the processor, so every pixel costs time.

- **Draw a smaller picture.** A 480x360 window has just over half the
  pixels of a 640x480 one, and takes a little over half as long to draw.
- **Prefer fixed-function OpenGL to shaders** where speed matters (see
  [Which way should I draw?](#which-way-should-i-draw)).
- **Textures:** power-of-two sizes (64, 128, 256...), `GL_CLAMP_TO_EDGE`
  rather than `GL_CLAMP`, and `GL_LINEAR` or `GL_NEAREST` filtering use
  the fast paths. The EGL guide (`docs/EGL-GUIDE.md`) lists the rest.
- **Don't draw frames nobody sees.** Pace with `Wimp_PollIdle` (about
  50 frames a second is plenty), and draw only when something changes if
  the picture is still.
- **Measure** on your own machine: `glbench` in the riscos-mesa test
  programs (the `riscos-mesa-tests` zip on the project's releases page)
  times six typical scenes.

---

## Learning more

- **OpenGL itself:** any OpenGL 2.1 or OpenGL ES 2.0 tutorial works
  here: the classic NeHe tutorials, the OpenGL "Red Book" (older
  editions cover 2.1), or WebGL tutorials for ES 2.0. The drawing code
  is the same on every system; only the window setup differs, and that's
  what these examples show.
- **In this devkit's `docs/` folder:**
  - `EGL-GUIDE.md`: everything about EGL on RISC OS: full screen
    options, views inside windows, sprites, pbuffers, video textures,
    threads, extensions and the complete troubleshooting table.
  - `porting/`: step-by-step guides to porting Raspberry Pi programs,
    SDL programs, GLUT programs and Mesa's demos. Their worked examples'
    source is in the riscos-mesa repository (github.com/adyoull/riscos-mesa,
    `ports/`), and the built programs are in the release's ports zip.
- **In the repository:** `README.md` and `CHANGELOG.md` (what's in each
  release), and the test programs' source (`tests/`).

---

## Licences

The example programs are under the MIT licence: use them as the start of
your own programs, commercial or not.

The libraries have their own licences, all listed in `LICENCES.txt`.
Ship that file with your programs. In short: Mesa, GLU, SDL, freeglut
and zlib are permissive (MIT-style). OpenAL Soft and parts of UnixLib
are LGPL: open source programs are fine as they are, while a closed
source program must offer its object files so that users can relink it
with a newer library. The PThreadTicker module is under a BSD licence:
put its `PThreadTicker-Licence` file next to it in your application, as
the Makefile does for example 6.
