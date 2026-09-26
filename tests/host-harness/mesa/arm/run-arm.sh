#!/bin/bash
# Runs the rendering checks (../*.c) on the RISC OS build of Mesa itself,
# under qemu-arm on a Linux host, and compares with ../expected/*.txt.
#
# The RISC OS libOSMesa.a is linked into a static ARM Linux program with
# shim.c standing in for the few UnixLib symbols Mesa uses (see shim.c).
# So this runs exactly the code GCCSDK compiled for the Pi, which catches
# anything the host build can't: compiler differences, ARM floating point.
# It must be the normal, not position-independent, library: -fPIC code
# looks for the RISC OS shared-library tables at &8038.
#
# Needs qemu-user and an ARM Linux cross compiler (Debian/Ubuntu:
# qemu-user gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf).
#
#   STAGE=<riscos-mesa stage dir with lib/libOSMesa.a, lib/libz.a and
#          include/>  tests/host-harness/mesa/arm/run-arm.sh
# OUT=<dir> sets the work directory (default /tmp/mesa-check-arm).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
: "${STAGE:?STAGE=<riscos-mesa stage directory>}"
: "${OUT:=/tmp/mesa-check-arm}"
CC=arm-linux-gnueabihf-gcc
CXX=arm-linux-gnueabihf-g++
command -v qemu-arm >/dev/null && command -v $CC >/dev/null && command -v $CXX >/dev/null || {
    echo "needs qemu-arm, $CC and $CXX" >&2; exit 1; }
mkdir -p "$OUT/this"

# errno is thread-local in glibc: point Mesa's references at the shim's
cp "$STAGE/lib/libOSMesa.a" "$OUT/libOSMesa.a"
arm-linux-gnueabihf-objcopy --redefine-sym errno=ro_errno_shim "$OUT/libOSMesa.a"
$CC -c -O2 -mfloat-abi=hard "$HERE/shim.c" -o "$OUT/shim.o"

CHECKS="render-fixed glsl-basic glsl-control glsl-edge"
for c in $CHECKS; do
    $CC -c -O2 -w -mfpu=vfpv4 -mfloat-abi=hard -I"$STAGE/include" "$HERE/../$c.c" -o "$OUT/$c.o"
    $CXX -static -o "$OUT/$c" "$OUT/$c.o" "$OUT/shim.o" "$OUT/libOSMesa.a" \
        "$STAGE/lib/libz.a" -lm -lpthread 2>&1 | grep -v "warning:\|NOTE:" || true
done

# qemu-arm with retries: glibc's pthread_mutex_lock sometimes aborts on
# UnixLib-initialised mutexes (see shim.c); that's the harness, not Mesa.
run() {
    local i
    for i in 1 2 3 4 5 6 7 8; do
        if ( qemu-arm "$@" > "$OUT/run.tmp" ) 2>/dev/null; then cat "$OUT/run.tmp"; return 0; fi
    done
    echo "$* kept failing" >&2
    return 1
}
for d in "16 0" "24 0" "24 8" "32 0"; do run "$OUT/render-fixed" $d; done > "$OUT/this/render-fixed.txt"
run "$OUT/glsl-basic" > "$OUT/this/glsl-basic.txt"
run "$OUT/glsl-control" > "$OUT/this/glsl-control.txt"
run "$OUT/glsl-edge" > "$OUT/this/glsl-edge.txt"

fail=0
for c in $CHECKS; do
    n=$(grep -vc '^undefined' "$OUT/this/$c.txt" || true)
    bad=$( (diff <(grep -v '^undefined' "$HERE/../expected/$c.txt") \
                 <(grep -v '^undefined' "$OUT/this/$c.txt") || true) | grep -c '^>' || true)
    if [ "$bad" = 0 ]; then
        printf "ok    %-13s %5d cases (ARM)\n" "$c" "$n"
    else
        printf "FAIL  %-13s %5d of %d cases differ (ARM)\n" "$c" "$bad" "$n"
        fail=1
    fi
done
[ $fail = 0 ] && echo "the RISC OS build renders exactly as expected" || echo "results in $OUT/this"
exit $fail
