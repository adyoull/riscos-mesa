#!/bin/bash
# Host test of the SDL RISC OS audio driver (src/audio/riscos/
# SDL_riscosaudio.c from the patched SDL tree): how much sound it keeps
# queued (SDL_RISCOS_AUDIO_QUEUE_MS), against a fake SharedSoundBuffer /
# StreamManager that plays in real time on a fake clock. See audio-queue.c.
#
#   SDL=<patched SDL 2 tree, as build/build-sdl2.sh leaves it>
#   tests/host-harness/sdl-audio/run.sh       (default SDL: src/SDL-release-<SDL_V>)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
SDL_V=$(sed -n 's/^SDL_V=//p' "$R/build/env.sh")   # the SDL 2 version the devkit builds
: "${SDL:=$R/src/SDL-release-$SDL_V}" "${OUT:=/tmp/sdl-audio}"
mkdir -p "$OUT"
gcc -no-pie -w -std=gnu99 -DSDL_AUDIO_DRIVER_RISCOS=1 \
    -I"$SDL/include" -I"$SDL/src/audio/riscos" -I"$SDL/src/audio" -I"$SDL/src" \
    -I"$HERE/fake" -I"$HERE/../egl/fake" -include "$SDL/src/SDL_internal.h" \
    "$HERE/audio-queue.c" -o "$OUT/audio-queue"
"$OUT/audio-queue"
