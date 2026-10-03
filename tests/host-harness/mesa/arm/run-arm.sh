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
# Set RO_FPU as for the build (default vfpv3). QEMU_CPU=cortex-a8 checks
# that a VFPv3 build really runs on a Cortex-A8.
# QEMU_ALIGN=<a qemu-arm built by ../../qemu/build-qemu.sh> runs the checks
# with RISC OS's alignment rules: an unaligned load or store in Mesa (or in
# a check) stops it with SIGBUS, as it would abort on RISC OS. glibc's
# code, which never runs on RISC OS, is exempt (../../qemu/README.md).
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
# and UnixLib's mutexes are laid out differently from glibc's: the checks
# run in one thread, so Mesa's mutex calls go to do-nothing shims
arm-linux-gnueabihf-objcopy --redefine-sym errno=ro_errno_shim \
    $(for f in mutex_init mutex_lock mutex_unlock mutex_destroy mutexattr_init mutexattr_settype \
               mutexattr_destroy; do echo "--redefine-sym pthread_$f=ro_pthread_${f}_shim"; done) \
    "$OUT/libOSMesa.a"
$CC -c -O2 -mfloat-abi=hard "$HERE/shim.c" -o "$OUT/shim.o"

CHECKS="render-fixed render-rows render-tex render-image render-matrix glsl-basic glsl-control glsl-edge"
for c in $CHECKS; do
    $CC -c -O2 -w -mfpu=${RO_FPU:-vfpv3} -mfloat-abi=hard -I"$STAGE/include" "$HERE/../$c.c" -o "$OUT/$c.o"
    $CXX -static -o "$OUT/$c" "$OUT/$c.o" "$OUT/shim.o" "$OUT/libOSMesa.a" \
        "$STAGE/lib/libz.a" -lm -lpthread -Wl,-Map="$OUT/$c.map" 2>&1 | grep -v "warning:\|NOTE:" || true
    if [ -n "${QEMU_ALIGN:-}" ]; then      # the code to trap: Mesa's and the check's
        python3 "$HERE/../../qemu/trapped-ranges.py" "$OUT/$c.map" "libOSMesa.a(" "/$c.o" > "$OUT/$c.ignore"
    fi
done
QEMU=qemu-arm
if [ -n "${QEMU_ALIGN:-}" ]; then
    QEMU=$QEMU_ALIGN
    export QEMU_ARM_ALIGN_TRAP=1
    # render-rows' byte-misaligned colour buffer is only a host check: on
    # RISC OS colour buffers are word aligned (see render-rows.c)
    export RENDER_ROWS_ALIGNED_ONLY=1
    echo "alignment faults trapped as on RISC OS ($QEMU_ALIGN)"
fi

# qemu-arm with retries, in case qemu itself fails (it used to be glibc
# aborting on UnixLib-initialised mutexes; see shim.c).
run() {
    local i st
    for i in 1 2 3 4 5 6 7 8; do
        st=0
        ( [ -n "${QEMU_ALIGN:-}" ] && export QEMU_ARM_ALIGN_IGNORE=$(cat "$1.ignore")
          "$QEMU" "$@" > "$OUT/run.tmp" ) 2>/dev/null || st=$?
        [ $st = 0 ] && { cat "$OUT/run.tmp"; return 0; }
        if [ $st = 135 ]; then
            echo "$1: unaligned access (SIGBUS), which would abort on RISC OS;" \
                 "run it under $QEMU -g 1234 and attach gdb-multiarch to find it" >&2
            return 1
        fi
    done
    echo "$* kept failing" >&2
    return 1
}
for d in "16 0" "24 0" "24 8" "32 0"; do run "$OUT/render-fixed" $d; done > "$OUT/this/render-fixed.txt"
run "$OUT/render-rows" > "$OUT/this/render-rows.txt"
run "$OUT/render-tex" > "$OUT/this/render-tex.txt"
run "$OUT/render-matrix" > "$OUT/this/render-matrix.txt"
run "$OUT/render-image" > "$OUT/this/render-image.txt" || true
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
