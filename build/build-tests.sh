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
# OpenGL ES on the native RISC OS types (Wimp window, full screen, sprite)
[ -f "$STAGE/lib/libEGL.a" ] && $CC $RO_CFLAGS $GL -static "$T/glestest.c" "$T/es_cube.c" "$T/hrtime.c" \
    -o "$STAGE/tests/glestest,e1f" -lEGL $LIBS
[ -f "$STAGE/lib/libbcm_host.a" ] && $CC $RO_CFLAGS $GL -static "$T/dmxtest.c" "$T/es_cube.c" "$T/hrtime.c" \
    -o "$STAGE/tests/dmxtest,e1f" -lbcm_host -lEGL -lGLESv2 -lvcos -lvchiq_arm $LIBS
[ -f "$STAGE/lib/libSDL2.a" ] && $CC $RO_CFLAGS $GL -I"$STAGE/include/SDL2" -static "$T/sdlgltest.c" "$T/hrtime.c" \
    -o "$STAGE/tests/sdlgltest,e1f" -lSDL2 -lEGL $LIBS
# SDL's 2D drawing speed (no window), for comparing SDL builds
[ -f "$STAGE/lib/libSDL2.a" ] && $CC $RO_CFLAGS $GL -I"$STAGE/include/SDL2" -static "$T/sdlblitbench.c" "$T/hrtime.c" \
    -o "$STAGE/tests/sdlblitbench,e1f" -lSDL2 -lEGL $LIBS
# SDL's key events: one press, repeats only at the keyboard's delay and rate
[ -f "$STAGE/lib/libSDL2.a" ] && $CC $RO_CFLAGS $GL -I"$STAGE/include/SDL2" -static "$T/sdlkeys.c" \
    -o "$STAGE/tests/sdlkeys,e1f" -lSDL2 -lEGL $LIBS
# OpenAL (libopenal.a) playing through SDL2's audio
[ -f "$STAGE/lib/libopenal.a" ] && $CC $RO_CFLAGS $GL -I"$STAGE/include/SDL2" -static "$T/altest.c" \
    -o "$STAGE/tests/altest,e1f" -lopenal -lSDL2 -lEGL $LIBS
# Two of the host rendering checks, for the Pi: ETC1 textures in OpenGL ES,
# and OpenGL ES 2.0 code in a desktop GL context (their output should match
# tests/host-harness/mesa/expected/render-etc1.txt and glsl-es2compat.txt)
H="$T/host-harness/mesa"
$CC $RO_CFLAGS $GL -static "$H/render-etc1.c" -o "$STAGE/tests/etc1check,e1f" $LIBS
$CC $RO_CFLAGS $GL -static "$H/glsl-es2compat.c" -o "$STAGE/tests/es2check,e1f" $LIBS
# VideoOverlay (hardware overlay) tests for the Pi: plain C, no GL
$CC $RO_CFLAGS -static "$T/ovltest.c" "$T/hrtime.c" -o "$STAGE/tests/ovltest,e1f"
for f in "$STAGE"/tests/*,e1f; do $STRIP "$f"; done
cp "$T/ReadMe,fff" "$T"/egl-*,feb "$T"/dmx-*,feb "$T"/gles-*,feb "$T"/al-*,feb "$T"/ovl-*,feb "$T"/sdl-*,feb "$T"/gl-*,feb "$STAGE/tests/"
# UnixLib 5.0.1's PThreadTicker module, which al-tone loads for altest's
# threads (OpenAL mixes in SDL's audio thread)
cp "$T/../devkit/riscos/PThrTicker,ffa" "$STAGE/tests/"
# Left as ELF (&E1F), same as the OpenTTD build: needs SharedUnixLibrary
# and ARMEABISupport loaded on the Pi.
ls -la "$STAGE/tests"
