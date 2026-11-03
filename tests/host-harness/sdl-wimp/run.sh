#!/bin/bash
# Host test of the SDL RISC OS driver's Wimp event handling
# (src/video/riscos/SDL_riscosevents.c from the patched SDL tree): quitting
# from the desktop (Message_PreQuit / Message_Quit), the close icon, the
# icon bar menu, the keys handed on to the Wimp, and key presses and
# auto-repeat read from the keyboard. See wimp-events.c.
#
#   SDL=<patched SDL 2 tree, as build/build-sdl2.sh leaves it>
#   tests/host-harness/sdl-wimp/run.sh        (default SDL: src/SDL-release-<SDL_V>)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
SDL_V=$(sed -n 's/^SDL_V=//p' "$R/build/env.sh")   # the SDL 2 version the devkit builds
: "${SDL:=$R/src/SDL-release-$SDL_V}" "${OUT:=/tmp/sdl-wimp}"
mkdir -p "$OUT"
gcc -no-pie -w -std=gnu99 -DSDL_VIDEO_DRIVER_RISCOS=1 \
    -I"$SDL/include" -I"$SDL/src/video/riscos" -I"$SDL/src/video" -I"$SDL/src" \
    -I"$HERE/fake" -I"$HERE/../egl/fake" -include "$SDL/src/SDL_internal.h" \
    "$HERE/wimp-events.c" -o "$OUT/wimp-events" -lpthread
"$OUT/wimp-events"
