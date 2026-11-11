# Mixed programs: the devkit's libraries shared

**Test (from 20.3.5-19).** The devkit's libraries are normally linked into
each program (static libraries, `.a`), and nothing below changes that. A
*mixed* program, as riscos-crossdev 1.11 makes them, still has UnixLib,
libstdc++ and libgcc linked in, but loads OpenGL, EGL, GLU, GLUT, SDL 2 and
OpenAL as shared libraries from **!MesaLibs**, through !SharedLibs
(SOManager and `ld-riscos/so/2`). Every program using the same !MesaLibs
shares one copy of each library in memory, and each program is much
smaller: glbench is about 9 MB linked statically and about 0.5 MB mixed.

riscos-crossdev's `docs/MIXED.md` explains mixed programs in general; this
is what's particular to the devkit.

## What's in the devkit

- `lib/shared/`: the libraries, named by their SONAME, with a `libX.so`
  link to each for the linker:

  | Library | SONAME | Links with |
  |---|---|---|
  | OpenGL 2.1, OpenGL ES 1.1/2.0 (OSMesa) | `libOSMesa.so.8` | |
  | EGL | `libEGL.so.1` | libOSMesa |
  | GLU | `libGLU.so.1` | libOSMesa |
  | freeglut | `libglut.so.3` | libEGL, libOSMesa |
  | SDL 2.32.10 | `libSDL2-2.0.so.0` | libEGL, libOSMesa |
  | OpenAL Soft 1.19.1 | `libopenal.so.1` | libSDL2 |

  zlib is inside libOSMesa, hidden: a program that uses zlib itself links
  `libz.a` as before. The DispmanX library (`libbcm_host.a`), the empty
  GLES libraries, `libSDL2main.a` and `libfreeglut-gles.a` stay static only.
- `riscos/!MesaLibs`: the same libraries, stripped, as RISC OS files
  (`lib.libOSMesa/so/8` ...), with a `!Boot` that sets `MesaLibs$Path`.
  Users put it in `!Boot.Resources`. It's also a download of its own,
  `riscos-mesa-MesaLibs-VERSION.zip`.

## Linking

With a riscos-crossdev 1.11 or later toolchain, compile as usual and link
with `-rdynamic` instead of `-static`, with `lib/shared` before `lib`:

```
DEVKIT=/path/to/riscos-mesa-devkit
STUBS=$(arm-riscos-gnueabihf-gcc -print-file-name=mixed-stubs)
MIXED="-rdynamic -Wl,-rpath-link=$STUBS -L$DEVKIT/lib/shared"

# OpenGL through OSMesa
arm-riscos-gnueabihf-gcc -O2 -I$DEVKIT/include prog.c -o prog $MIXED -lOSMesa -lm
# EGL
... $MIXED -lEGL -lOSMesa -lm
# SDL 2 with OpenGL
... -I$DEVKIT/include/SDL2 ... $MIXED -lSDL2 -lEGL -lOSMesa -ldl -lm
# OpenAL
... $MIXED -lopenal -lSDL2 -lEGL -lOSMesa -ldl -lm
```

(riscos-crossdev's `riscos.mk` has these as `$(RISCOS_MIXED_LDFLAGS)`.)

- **`-lstdc++` and `-lz` aren't needed:** Mesa's C++ parts get libstdc++
  from the program, which the toolchain links in for any `-rdynamic` link,
  and zlib is inside libOSMesa.
- **SDL needs `-ldl`:** the shared SDL holds all of SDL, including
  `SDL_LoadObject`, which calls `dlopen`. A static SDL only brought that in
  for programs that used it.
- **Name every library the program calls itself.** The loader finds the
  ones they need in turn (libSDL2 needs libEGL, which needs libOSMesa).
- The program stays an ELF file (filetype &E1F, stripped). Don't convert it
  with `elf2aif`: !SharedLibs runs it.

## Checking

riscos-crossdev's checks, on the unstripped program and the pack:

```
checks/check-all.sh prog
checks/check-mixed-package.sh prog /path/to/!MesaLibs/lib
```

The second says whether every library the program needs, directly or
through another, is in !MesaLibs, and whether every name they need is
defined. `build/build-shared.sh` runs both on the mixed test programs.

## !Run

The program's `!Run` boots !SharedLibs and !MesaLibs if they haven't been,
checks for `ld-riscos/so/2`, and tells the loader where !MesaLibs is, in a
variable named after the program's leaf name (`mygame$LD$Env` for a program
called `mygame`):

```
If "<SharedLibs$Dir>" = "" Then IfThere Boot:Resources.!SharedLibs.!Boot Then Run Boot:Resources.!SharedLibs.!Boot
If "<MesaLibs$Dir>" = "" Then IfThere Boot:Resources.!MesaLibs.!Boot Then Run Boot:Resources.!MesaLibs.!Boot
Unset MyGame$SO2
IfThere SharedLibs:lib.ld-riscos-eabihf/so Then Set MyGame$SO2 1
If "<MyGame$SO2>" = "" Then Error MyGame needs a !SharedLibs with ld-riscos/so/2 (GCCSDK 8 or later, as in Iris) in !Boot.Resources
If "<MesaLibs$Dir>" = "" Then Error MyGame needs !MesaLibs in !Boot.Resources
Set mygame$LD$Env "LD_LIBRARY_PATH=/MesaLibs:"
WimpSlot -min 16000K -max 16000K
Run <MyGame$Dir>.mygame %*0
```

The `WimpSlot` line is needed: without it, a program started from the
Filer stops with "Wimpslot not big enough to run ELF program".

A program with libraries of its own, or one that also uses !GTKLibs, lists
the directories with a **semicolon** between them, its own first:
`"LD_LIBRARY_PATH=/MyGameLibs:;/MesaLibs:"` or
`"LD_LIBRARY_PATH=/GTKLibs:;/MesaLibs:"`. A colon alone between them
(`/MyGameLibs:/MesaLibs:`) doesn't work: the program stops with "can't
load library". (Tested on RISC OS on Linux, SOManager 3.04, with libEGL in
one directory and libOSMesa in another: each loaded, and SOManager listed
each under its own directory's name, so other programs using the same
!MesaLibs still share it.) A path variable naming both directories would
also be found, but its libraries would be registered under that variable's
name and not shared with other programs.

Threaded programs (SDL sound, OpenAL) still ship and load PThreadTicker,
as static ones do.

**Ship !MesaLibs for users to install, but don't put a copy of the
libraries inside each app:** SOManager shares a library between programs
only when they load the same file.

## Users need

- !SharedLibs with `ld-riscos/so/2` (GCCSDK 8 or later; Iris includes one)
  in `!Boot.Resources`. riscos.info's SharedLibs 4.0 has only
  `ld-riscos/so/1` and won't run these programs.
- !MesaLibs in `!Boot.Resources`.
- SharedUnixLibrary 1.16 or later, and ARMEABISupport.
- On RISC OS on Linux, NativeSupport 0.33 or later.

## Speed

The shared libraries are position-independent code, which reaches its
globals through a table. When this was tried in October 2026, glbench was
about 2% slower that way (the GLSL code about 5%). The mixed test programs
(`riscos-mesa-tests-mixed-VERSION.zip`, `mx-glbench`) are there to measure
it against the static ones.

## How they're built

`build/build-shared.sh` (or `SHARED=1 build/build-all.sh`) builds every
library a second time, position-independent, into `stage-pic/`, then links
each into a `.so` with `-shared -nodefaultlibs` (no UnixLib, libstdc++ or
libgcc of its own; the start files are kept, as Mesa's C++ needs
`__dso_handle`) and `-lgcc`. One Mesa source file, `swrast/s_triangle.c`,
is compiled with `-fno-gcse-lm` in that build, to get round a GCC 10
internal error with `-fPIC` (patches/mesa/README, riscos-pic).
!MesaLibs' icon is drawn by `tools/mkmesalibsicon.py` (an original
drawing: a mesa with an OpenGL-shaded triangle rising behind it), which
needs Python's Pillow.
