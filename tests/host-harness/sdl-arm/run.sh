#!/bin/bash
# SDL's ARM SIMD and NEON blitters (armblit.c), on emulated ARM CPUs.
#
# Builds the patched SDL tree for ARM Linux with the same blitter options
# as build/build-sdl2.sh (--enable-arm-simd --enable-arm-neon), links
# armblit.c with it, and runs it under qemu-arm twice:
#   cortex-a8                          NEON (every ARMv7 RISC OS machine
#                                      the build runs on has it)
#   cortex-a8,neon=off,vfp-d32=off     ARM SIMD only, the CPU check's
#                                      fallback
# Code is built for VFPv3-D16 so that it runs on both.
#
#   SDL=<patched SDL 2.26 tree, as build/build-sdl2.sh leaves it>
#   tests/host-harness/sdl-arm/run.sh     (default SDL: src/SDL-release-2.26.0)
#
# Needs qemu-user (qemu-arm) and an ARM Linux cross compiler
# (arm-linux-gnueabihf-gcc; Debian/Ubuntu: gcc-arm-linux-gnueabihf).
# The first run builds SDL, about a minute; later runs rebuild it only if
# the tree has changed.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
: "${SDL:=$R/src/SDL-release-2.26.0}" "${OUT:=/tmp/sdl-arm}"
CC=arm-linux-gnueabihf-gcc
FLAGS="-O2 -marm -mfpu=vfpv3-d16 -mfloat-abi=hard"
command -v qemu-arm >/dev/null || { echo "needs qemu-arm"; exit 1; }
command -v $CC >/dev/null || { echo "needs $CC"; exit 1; }
mkdir -p "$OUT/build"

# rebuild SDL when a source file is newer than the last build, or when
# the tree, configure or the options here have changed
CONFIG="$SDL $FLAGS $(cat "$SDL/configure.ac" "$0" | sha256sum | cut -c1-16)"
if [ ! -f "$OUT/inst/lib/libSDL2.a" ] ||
   [ "$(cat "$OUT/config" 2>/dev/null)" != "$CONFIG" ] ||
   [ -n "$(find "$SDL/src" "$SDL/include" -newer "$OUT/inst/lib/libSDL2.a" -type f -print -quit)" ]; then
    rm -rf "$OUT/build" "$OUT/inst" "$OUT/config"
    mkdir -p "$OUT/build"
    echo "building SDL for ARM Linux in $OUT/build"
    (cd "$OUT/build" &&
     "$SDL/configure" --host=arm-linux-gnueabihf --prefix="$OUT/inst" \
        --disable-shared --enable-static --disable-audio --disable-joystick \
        --disable-haptic --disable-sensor --disable-hidapi --disable-power \
        --disable-video-x11 --disable-video-wayland --disable-video-kmsdrm \
        --disable-video-opengl --disable-video-opengles --disable-video-vulkan \
        --disable-render --disable-threads --disable-loadso --disable-dbus \
        --disable-ibus --disable-libudev --enable-arm-simd --enable-arm-neon \
        CFLAGS="$FLAGS" > configure.log 2>&1 &&
     make -j"$(nproc)" > make.log 2>&1 && make install > install.log 2>&1) ||
        { echo "SDL build failed (logs in $OUT/build)"; exit 1; }
    echo "$CONFIG" > "$OUT/config"
fi
for d in SDL_ARM_SIMD_BLITTERS SDL_ARM_NEON_BLITTERS; do
    grep -q "#define $d 1" "$OUT/inst/include/SDL2/SDL_config.h" ||
        { echo "$d isn't set in the ARM Linux build"; exit 1; }
done

$CC $FLAGS -static -I"$OUT/inst/include/SDL2" "$HERE/armblit.c" \
    "$OUT/inst/lib/libSDL2.a" -lm -o "$OUT/armblit" 2> "$OUT/link.log" ||
    { cat "$OUT/link.log"; exit 1; }

# Each CPU must also be detected as expected: if SDL's CPU check found
# neither, armblit would only test the C code, and pass.
fail=0
for run in "cortex-a8|CPU: NEON yes, ARM SIMD yes" \
           "cortex-a8,neon=off,vfp-d32=off|CPU: NEON no, ARM SIMD yes"; do
    cpu=${run%%|*} want=${run#*|}
    echo "== $cpu"
    qemu-arm -cpu "$cpu" "$OUT/armblit" | tee "$OUT/armblit.out" || fail=1
    grep -qx "$want" "$OUT/armblit.out" || { echo "FAIL: expected \"$want\""; fail=1; }
done
[ $fail = 0 ] && echo "sdl-arm: all passed"
exit $fail
