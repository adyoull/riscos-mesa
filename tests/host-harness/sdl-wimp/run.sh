#!/bin/bash
# Host test of the SDL RISC OS driver's Wimp event handling
# (src/video/riscos/SDL_riscosevents.c from the patched SDL tree): quitting
# from the desktop (Message_PreQuit / Message_Quit), the close icon, the
# icon bar menu and the keys handed on to the Wimp. See wimp-events.c.
#
#   SDL=<patched SDL 2.26 tree, as build/build-sdl2.sh leaves it>
#   tests/host-harness/sdl-wimp/run.sh        (default SDL: src/SDL-release-2.26.0)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
: "${SDL:=$R/src/SDL-release-2.26.0}" "${OUT:=/tmp/sdl-wimp}"
mkdir -p "$OUT"
gcc -no-pie -w -std=gnu99 -DSDL_VIDEO_DRIVER_RISCOS=1 \
    -I"$SDL/include" -I"$SDL/src/video/riscos" -I"$SDL/src/video" -I"$SDL/src" \
    -I"$HERE/fake" -I"$HERE/../egl/fake" -include "$SDL/src/SDL_internal.h" \
    "$HERE/wimp-events.c" -o "$OUT/wimp-events" -lpthread
"$OUT/wimp-events"
