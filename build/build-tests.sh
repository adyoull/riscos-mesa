#!/bin/bash
# RISC OS test binaries -> $STAGE/tests/*,e1f (copy to the Pi, run in a TaskWindow
# or from the command line: needs WimpSlot ~16M for 640x480).
set -euo pipefail
source "$(dirname "$0")/env.sh"
T=$(cd "$(dirname "$0")/../tests" && pwd)
mkdir -p "$STAGE/tests"
GL="-I$STAGE/include -L$STAGE/lib"
LIBS="-lOSMesa -lstdc++ -lz -lm"
$CC $RO_CFLAGS $GL -static "$T/osmesatest.c" -o "$STAGE/tests/osmesatest,e1f" $LIBS
$CC $RO_CFLAGS $GL -static "$T/glbench.c" "$T/hrtime.c" -o "$STAGE/tests/glbench,e1f" $LIBS
$CC $RO_CFLAGS $GL -static "$T/orient.c"     -o "$STAGE/tests/orient,e1f"     $LIBS
$CC $RO_CFLAGS $GL -static "$T/prof.c"       -o "$STAGE/tests/prof,e1f"       $LIBS
[ -f "$STAGE/lib/libGLU.a" ] && $CC $RO_CFLAGS $GL -static "$T/glutest.c" -o "$STAGE/tests/glutest,e1f" -lGLU $LIBS
[ -f "$STAGE/lib/libEGL.a" ] && $CC $RO_CFLAGS $GL -static "$T/egltest.c" "$T/hrtime.c" \
    -o "$STAGE/tests/egltest,e1f" -lEGL $LIBS
# Raspberry Pi style (DispmanX) program, linked the way Pi makefiles do
[ -f "$STAGE/lib/libbcm_host.a" ] && $CC $RO_CFLAGS $GL -static "$T/dmxtest.c" "$T/hrtime.c" \
    -o "$STAGE/tests/dmxtest,e1f" -lbcm_host -lEGL -lGLESv2 -lvcos -lvchiq_arm $LIBS
[ -f "$STAGE/lib/libSDL2.a" ] && $CC $RO_CFLAGS $GL -I"$STAGE/include/SDL2" -static "$T/sdlgltest.c" "$T/hrtime.c" \
    -o "$STAGE/tests/sdlgltest,e1f" -lSDL2 $LIBS
for f in "$STAGE"/tests/*,e1f; do $STRIP "$f"; done
cp "$T/ReadMe,fff" "$T"/egl-*,feb "$T"/dmx-*,feb "$STAGE/tests/"
# Left as ELF (&E1F), same as the OpenTTD build: needs SharedUnixLibrary
# and ARMEABISupport loaded on the Pi.
ls -la "$STAGE/tests"
