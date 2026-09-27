#!/bin/bash
# Rendering checks for riscos-mesa's Mesa patches, on a Linux host.
#
# Builds the checkers in this directory against a host build of Mesa with
# patches/mesa applied, runs them and compares every image hash with
# expected/*.txt (or with another build of libOSMesa). Run it after any
# change to the Mesa patches: the results must stay the same unless the
# change is meant to alter rendering.
#
#   M=<mesa-20.3.5 tree, patches/mesa applied, built in $M/build as in
#      ../egl/README.md>  tests/host-harness/mesa/run.sh
#
# Options (environment):
#   REF=<dir>   compare with the libOSMesa.so.8 in <dir> instead of
#               expected/ (an A/B test of two builds, e.g. before and after
#               a change). Builds older than 20.3.5-7 differ by design in
#               the "tex" lines of render-fixed: Mesa's integer texture
#               path now also takes RGBA8 textures and rounds by up to
#               2/255 differently.
#   SKIP=<re>   leave out lines matching this extended regex as well, e.g.
#               SKIP='^tex ' for an A/B test against a build before 20.3.5-7.
#   UPDATE=1    rewrite expected/ from this build (only when a change to
#               rendering is intended; say why in the commit).
#   OUT=<dir>   where to build and keep the results (default /tmp/mesa-check)
#
# Lines starting "undefined" are printed for information and not compared
# (a pixel's colour that GLSL leaves undefined).
#
# The same checks can run on the RISC OS build of Mesa under qemu-arm:
# see arm/run-arm.sh.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
: "${M:?M=<host Mesa tree with patches/mesa applied and built in \$M/build>}"
: "${OUT:=/tmp/mesa-check}"
LIB=$M/build/src/mesa/drivers/osmesa
[ -e "$LIB/libOSMesa.so.8" ] || { echo "no $LIB/libOSMesa.so.8: build Mesa first" >&2; exit 1; }
mkdir -p "$OUT"

CHECKS="render-fixed render-rows glsl-basic glsl-control glsl-edge"
for c in $CHECKS; do
    gcc -O2 -w -I"$M/include" "$HERE/$c.c" -o "$OUT/$c" \
        -L"$LIB" -lOSMesa -lm -Wl,-rpath,"$LIB"
done

# run_all <libdir> <results dir>: one results file per checker
run_all() {
    local lib=$1 res=$2
    mkdir -p "$res"
    ( export LD_LIBRARY_PATH=$lib
      for d in "16 0" "24 0" "24 8" "32 0"; do "$OUT/render-fixed" $d; done > "$res/render-fixed.txt"
      "$OUT/render-rows" > "$res/render-rows.txt"
      "$OUT/glsl-basic" > "$res/glsl-basic.txt"
      "$OUT/glsl-control" > "$res/glsl-control.txt"
      "$OUT/glsl-edge" 2>/dev/null > "$res/glsl-edge.txt" )
}

run_all "$LIB" "$OUT/this"

if [ -n "${UPDATE:-}" ]; then
    cp "$OUT"/this/*.txt "$HERE/expected/"
    echo "expected/ updated from $LIB"
    exit 0
fi

if [ -n "${REF:-}" ]; then
    run_all "$REF" "$OUT/ref"
    WANT=$OUT/ref
    echo "comparing $LIB with $REF"
else
    WANT=$HERE/expected
    echo "comparing $LIB with expected/"
fi

# the lines compared: not "undefined" ones, nor any matching $SKIP
keep() { grep -v '^undefined' "$1" | { if [ -n "${SKIP:-}" ]; then grep -Ev "$SKIP"; else cat; fi; } || true; }

fail=0
for c in $CHECKS; do
    keep "$WANT/$c.txt" > "$OUT/want.$c"
    keep "$OUT/this/$c.txt" > "$OUT/got.$c"
    n=$(wc -l < "$OUT/got.$c")
    bad=$( (diff "$OUT/want.$c" "$OUT/got.$c" || true) | grep -c '^>' || true)
    if [ "$bad" = 0 ]; then
        printf "ok    %-13s %5d cases\n" "$c" "$n"
    else
        printf "FAIL  %-13s %5d of %d cases differ\n" "$c" "$bad" "$n"
        (diff "$OUT/want.$c" "$OUT/got.$c" || true) | grep '^>' | head -5 || true
        fail=1
    fi
done
[ $fail = 0 ] && echo "all rendering checks match" || echo "some rendering changed (results in $OUT/this)"
exit $fail
