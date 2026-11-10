#!/bin/bash
# Shared libraries (.so) of the devkit's libraries, for "mixed" programs
# (riscos-crossdev 1.11 and later; docs/porting/mixed.md): UnixLib,
# libstdc++ and libgcc are linked into the program as usual, and
# OSMesa, EGL, GLU, GLUT, SDL 2 and OpenAL are shared libraries that
# !SharedLibs (SOManager and ld-riscos/so/2) loads when it starts. Programs
# that use the same library file then share one copy of it in memory.
#
#   build/build-shared.sh        (after build/build-all.sh)
#
# 1. Every library is built again, position-independent (RO_PIC=1, into
#    stage-pic/, in build-ro-pic directories; the static build is left as
#    it is). SHARED_SKIP_PIC=1 skips this, to remake only steps 2 and 3.
# 2. $STAGE/shared/: the .so files, named by their SONAME (libOSMesa.so.8
#    ...), with a libX.so link to each for linking, and !MesaLibs, the
#    library pack that goes in !Boot.Resources.
# 3. $STAGE/tests-mixed/: some of the test programs linked mixed, with
#    Obey files to run them, and riscos-crossdev's checks run on them.
#
# Needs a riscos-crossdev 1.11 or later toolchain (its mixed-link support
# and stand-ins); RCD_DIR=<riscos-crossdev checkout> for its checks.
set -euo pipefail
B=$(cd "$(dirname "$0")" && pwd)
source "$B/env.sh"
R=$(cd "$B/.." && pwd)
PIC=$R/stage-pic/lib
OUT=$STAGE/shared
LIB=$OUT/lib                       # libX.so.N and libX.so links
PACK=$OUT/!MesaLibs                # the library pack for !Boot.Resources
STUBS=$GCCSDK_ENV/$HOST/lib/mixed-stubs
LOGS=$STAGE/logs
mkdir -p "$LOGS"
[ -d "$STUBS" ] || { echo "this toolchain has no mixed-link support ($STUBS): riscos-crossdev 1.11 or later is needed" >&2; exit 1; }

# ---- 1. the position-independent build ----
if [ -z "${SHARED_SKIP_PIC:-}" ]; then
  for s in zlib mesa glu sdl2 openal egl freeglut; do
    printf "%-14s " "$s (PIC)"
    if env -u STAGE RO_PIC=1 "$B/build-$s.sh" > "$LOGS/$s-pic.log" 2>&1; then
      echo ok
    else
      echo "FAILED - end of $LOGS/$s-pic.log:"; tail -20 "$LOGS/$s-pic.log"; exit 1
    fi
  done
fi

# ---- 2. the shared libraries ----
rm -rf "$OUT"; mkdir -p "$LIB"

# make_so SONAME LINKNAME ARCHIVE [LIBRARIES IT USES...]
# Every object in the archive goes in, once (tools/ar-unique.py: Mesa's
# folded libOSMesa.a holds some objects twice, harmless in a static link
# but duplicate definitions here).
# -nodefaultlibs: no UnixLib, libstdc++ or libgcc of its own (the program
# has them, and exports them to its libraries); only libgcc's helpers are
# linked in, privately, as riscos-crossdev's RISCOS_SO_LIBS does. Unlike
# -nostdlib it keeps the shared-library start files (crtbeginS.o), which
# define __dso_handle: C++ code with static objects (Mesa's GLSL compiler)
# registers their destructors with it.
# --gc-sections: only what the exported functions reach (Mesa and SDL are
# compiled with a section per function, and export only their API).
# The libraries it uses become its NEEDED entries.
make_so() {
  local soname=$1 linkname=$2 archive=$3; shift 3
  local objs=$OUT/obj/$soname
  python3 "$R/tools/ar-unique.py" "$archive" "$objs" > "$objs.list"
  $CC -shared -nodefaultlibs -Wl,--gc-sections -Wl,-soname,"$soname" -o "$LIB/$soname" \
      @"$objs.list" "$@" -lgcc
  ln -sf "$soname" "$LIB/$linkname"
  printf "%-22s %6d KB\n" "$soname" $(( $(stat -c %s "$LIB/$soname") / 1024 ))
}
mkdir -p "$OUT/obj"
# zlib (Mesa uses its crc32) goes inside libOSMesa, hidden: programs that
# use zlib themselves link the devkit's libz.a as before.
make_so libOSMesa.so.8      libOSMesa.so "$PIC/libOSMesa.a" "$PIC/libz.a" -Wl,--exclude-libs,libz.a
make_so libEGL.so.1         libEGL.so    "$PIC/libEGL.a"    -L"$LIB" -lOSMesa
make_so libGLU.so.1         libGLU.so    "$PIC/libGLU.a"    -L"$LIB" -lOSMesa
make_so libglut.so.3        libglut.so   "$PIC/libglut.a"   -L"$LIB" -lEGL -lOSMesa
make_so libSDL2-2.0.so.0    libSDL2.so   "$PIC/libSDL2.a"   -L"$LIB" -lEGL -lOSMesa
make_so libopenal.so.1      libopenal.so "$PIC/libopenal.a" -L"$LIB" -lSDL2

# Each library: a SONAME, no shared UnixLib of its own, no text relocations
# (all of it PIC), and none of UnixLib's functions
if [ -n "${RCD_DIR:-}" ]; then
  RISCOS_TOOLCHAIN=$GCCSDK_ENV "$RCD_DIR/checks/check-shared-lib.sh" "$LIB"/*.so.*
else
  echo "note: RCD_DIR not set: riscos-crossdev's check-shared-lib.sh not run" >&2
fi

# The pack: the libraries under their SONAMEs (lib.libOSMesa/so/8 on RISC
# OS), stripped of everything a program doesn't need to link to them
mkdir -p "$PACK/lib"
for f in "$LIB"/*.so.*; do
  $STRIP --strip-unneeded -o "$PACK/lib/$(basename "$f"),e1f" "$f"
done
cp "$R/shared/!MesaLibs/"* "$PACK/"
echo "pack: $PACK"

# ---- 3. test programs, linked mixed ----
T=$R/tests
M=$STAGE/tests-mixed
rm -rf "$M"; mkdir -p "$M"
INC="-I$STAGE/include -I$STAGE/include/SDL2"
# -rdynamic: a dynamic program, with UnixLib (and libstdc++, which Mesa
# needs) still linked in and exported to the libraries; the stand-ins
# directory as riscos-crossdev's RISCOS_MIXED_LDFLAGS has it
MIXED="-rdynamic -Wl,-rpath-link=$STUBS -L$LIB"
mixed() {   # mixed NAME SOURCES... -- LIBRARIES...
  local name=$1; shift
  local src=() libs=()
  while [ "$1" != -- ]; do src+=("$1"); shift; done; shift
  libs=("$@")
  $CC $RO_CFLAGS $INC "${src[@]}" -o "$M/$name" $MIXED "${libs[@]}" -lm
}
mixed glbench   "$T/glbench.c" "$T/hrtime.c"                 -- -lOSMesa
mixed glutest   "$T/glutest.c"                               -- -lGLU -lOSMesa
mixed egltest   "$T/egltest.c" "$T/hrtime.c"                 -- -lEGL -lOSMesa
mixed glestest  "$T/glestest.c" "$T/es_cube.c" "$T/hrtime.c" -- -lEGL -lOSMesa
# SDL: -ldl as well. The shared SDL holds all of SDL, SDL_LoadObject
# included, which calls dlopen; a static SDL only brings it in for programs
# that use it. (UnixLib's libdl.a: the program's own, as with UnixLib.)
mixed sdlgltest "$T/sdlgltest.c" "$T/hrtime.c"               -- -lSDL2 -lEGL -lOSMesa -ldl
mixed sdlfbtest "$T/sdlfbtest.c" "$T/hrtime.c"               -- -lSDL2 -lEGL -lOSMesa -ldl
mixed altest    "$T/altest.c"                                -- -lopenal -lSDL2 -lEGL -lOSMesa -ldl

# Checks on the unstripped programs and on the package as it will be
# shipped: every library each program needs, directly or through another,
# is in the pack, and every name the libraries need is defined
if [ -n "${RCD_DIR:-}" ]; then
  for p in "$M"/*; do
    RISCOS_TOOLCHAIN=$GCCSDK_ENV "$RCD_DIR/checks/check-all.sh" "$p" > "$LOGS/check-$(basename "$p").log" 2>&1 ||
      { echo "check-all.sh failed for $p:"; tail -20 "$LOGS/check-$(basename "$p").log"; exit 1; }
    RISCOS_TOOLCHAIN=$GCCSDK_ENV "$RCD_DIR/checks/check-mixed-package.sh" "$p" "$PACK/lib" > "$LOGS/package-$(basename "$p").log" 2>&1 ||
      { echo "check-mixed-package.sh failed for $p:"; cat "$LOGS/package-$(basename "$p").log"; exit 1; }
  done
  echo "riscos-crossdev's checks: all passed"
fi
mkdir -p "$STAGE/elf-mixed"
for p in "$M"/*; do
  cp "$p" "$STAGE/elf-mixed/"
  $STRIP -o "$p,e1f" "$p" && rm "$p"
done
cp "$T"/mixed/*,feb "$T/mixed/ReadMe,fff" "$M/"
cp -r "$PACK" "$M/"
cp "$R/devkit/riscos/PThrTicker,ffa" "$M/"
ls -la "$M"
