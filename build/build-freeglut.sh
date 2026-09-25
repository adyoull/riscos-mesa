#!/bin/bash
# freeglut 3.8.0 with a native RISC OS back end (glut/riscos/: Wimp windows,
# riscos-mesa's EGL, Wimp menus) -> $STAGE/lib/libglut.a and GL/glut.h,
# GL/freeglut*.h; also $STAGE/lib/libfreeglut-gles.a (the same over OpenGL
# ES 1.1/2.0: build programs against it with -DFREEGLUT_GLES; menus work,
# GLUT's text drawing doesn't, as in other ES builds of freeglut).
# Needs build-mesa.sh, build-glu.sh (GL/glu.h) and build-egl.sh first.
# Apps link: -lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm
#   (ES: -lfreeglut-gles -lEGL -lOSMesa -lstdc++ -lz -lm)
set -euo pipefail
source "$(dirname "$0")/env.sh"
R=$(cd "$(dirname "$0")/.." && pwd)
cd "$SRC"
[ -d freeglut-3.8.0 ] || git clone -q --depth 1 --branch v3.8.0 \
  https://github.com/freeglut/freeglut.git freeglut-3.8.0
[ "$(git -C freeglut-3.8.0 rev-parse HEAD)" = 3db1649ce1f5e42f1338b51e3fa14849be547d5d ] \
  || { echo "freeglut-3.8.0 is not the pinned commit 3db1649c" >&2; exit 1; }
cd freeglut-3.8.0
git checkout -q -- . && git clean -qfd src
git apply "$R/patches/freeglut/freeglut-3.8.0-riscos.patch"
cp -r "$R/glut/riscos" src/riscos

DEFS="-DHAVE_SYS_TYPES_H -DHAVE_UNISTD_H -DHAVE_SYS_TIME_H -DHAVE_STDINT_H
 -DHAVE_FCNTL_H -DHAVE_ERRNO_H -DHAVE_VFPRINTF -DNEED_XPARSEGEOMETRY_IMPL
 -DFREEGLUT_PRINT_ERRORS"
# (no FREEGLUT_PRINT_WARNINGS: text on stderr opens a command window in the desktop)
GENERIC="callbacks cursor display ext font_data gamemode geometry gl2 init
 input_devices joystick main misc overlay spaceball state stroke_mono_roman
 stroke_roman structure teapot videoresize window"
BACKEND="init main window state menu"

build_lib() {   # lib-name extra-cflags extra-sources...
  local name=$1 extra=$2; shift 2
  local B; B=$(mktemp -d)
  local s f
  for s in $GENERIC; do
    $CC $RO_CFLAGS $extra $DEFS -Iinclude -Isrc -I"$STAGE/include" -c src/fg_$s.c -o "$B/fg_$s.o"
  done
  for f in "$@" src/x11/fg_glutfont_definitions_x11.c src/util/xparsegeometry_repl.c; do
    $CC $RO_CFLAGS $extra $DEFS -Iinclude -Isrc -I"$STAGE/include" -c "$f" -o "$B/$(basename "$f" .c).o"
  done
  for s in $BACKEND; do
    $CC $RO_CFLAGS -Wall $extra $DEFS -Iinclude -Isrc -I"$STAGE/include" \
      -c src/riscos/fg_${s}_riscos.c -o "$B/fg_${s}_riscos.o"
  done
  rm -f "$STAGE/lib/lib$name.a"
  $AR rcs "$STAGE/lib/lib$name.a" "$B"/*.o
  rm -rf "$B"
}
build_lib glut "" src/fg_font.c src/fg_menu.c
build_lib freeglut-gles "-DFREEGLUT_GLES" src/riscos/fg_gles_riscos.c

mkdir -p "$STAGE/include/GL"
cp include/GL/freeglut.h include/GL/freeglut_std.h include/GL/freeglut_ext.h \
   include/GL/freeglut_ucall.h include/GL/glut.h "$STAGE/include/GL/"
cp COPYING "$STAGE/LICENCE-freeglut.txt" 2>/dev/null || true
ls -la "$STAGE/lib/libglut.a" "$STAGE/lib/libfreeglut-gles.a"
