#!/bin/bash
# Performance regression check for riscos-mesa's Mesa patches.
#
# Counts the instructions Mesa executes per frame for each of glbench's
# scenes (perf.c, run under valgrind), and for starting a program (loading
# it and creating and binding the first context: "startup", a total, not
# per frame), and compares them with
# expected/perf.txt or with another build. Instruction counts don't vary
# from run to run the way timings do, so a slowdown of a couple of percent
# is caught reliably, even on a busy machine. They do depend on the host
# compiler, so the baseline records the gcc version; on a different
# compiler, compare two builds with REF instead.
#
# This measures the code, not the Pi: the Pi's own figures come from
# glbench in the tests zip. But a change that makes the host count go up
# will almost always make the Pi slower too.
#
#   M=<host Mesa tree, patches/mesa applied, built in $M/build>
#   tests/host-harness/mesa/perf.sh
#
# Options (environment):
#   LIB=<dir>    measure the libOSMesa.so.8 in <dir> instead of $M's build
#   REF=<dir>    compare with the libOSMesa.so.8 in <dir> (A/B) instead of
#                expected/perf.txt
#   TOL=<pct>    how much more work per frame counts as a regression
#                (default 2)
#   UPDATE=1     rewrite expected/perf.txt from this build (after an
#                intended change; say why in the commit)
#   FRAMES=<n>   frames per scene (default 8)
#   OUT=<dir>    work directory (default /tmp/mesa-perf)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
: "${M:?M=<host Mesa tree with patches/mesa applied and built in \$M/build>}"
: "${LIB:=$M/build/src/mesa/drivers/osmesa}" "${TOL:=2}" "${FRAMES:=8}" "${OUT:=/tmp/mesa-perf}"
command -v valgrind >/dev/null || { echo "needs valgrind" >&2; exit 1; }
mkdir -p "$OUT"
SCENES="clear cube tex blend tris glsl fog"
ROWS="startup $SCENES"
GCC=$(gcc -dumpfullversion)

gcc -O2 -w -I"$M/include" -I"$HERE/../.." "$HERE/perf.c" "$HERE/../../hrtime.c" \
    -o "$OUT/perf" -L"$M/build/src/mesa/drivers/osmesa" -lOSMesa -lm

# per_frame <libdir> <scene>: instructions per frame, over $FRAMES frames
# (minus a run with none, which is the start-up and first frame)
count() {
    LD_LIBRARY_PATH=$1 valgrind --tool=callgrind --callgrind-out-file=/dev/null \
        "$OUT/perf" "$2" "$3" 2>&1 | awk '/Collected/ { print $4 }'
}
per_frame() {
    local a b
    a=$(count "$1" "$2" 0)
    b=$(count "$1" "$2" "$FRAMES")
    echo $(( (b - a) / FRAMES ))
}
measure() {   # measure <libdir> <file>
    { echo "# instructions: startup = whole program up to the first context"
      echo "# being current; scenes = per frame, 320x240; gcc $GCC"
      echo "startup $(count "$1" startup 0)"
      for s in $SCENES; do echo "$s $(per_frame "$1" "$s")"; done; } > "$2"
}

measure "$LIB" "$OUT/this.txt"

if [ -n "${UPDATE:-}" ]; then
    cp "$OUT/this.txt" "$HERE/expected/perf.txt"
    echo "expected/perf.txt updated"
    cat "$HERE/expected/perf.txt"
    exit 0
fi

if [ -n "${REF:-}" ]; then
    measure "$REF" "$OUT/ref.txt"
    WANT=$OUT/ref.txt
    echo "instructions: $LIB against $REF"
else
    WANT=$HERE/expected/perf.txt
    base_gcc=$(sed -n 's/.*gcc //p' "$WANT" | head -1)
    echo "instructions: $LIB against expected/perf.txt"
    if [ "$base_gcc" != "$GCC" ]; then
        echo "note: the baseline was made with gcc $base_gcc, this is gcc $GCC;"
        echo "      counts differ between compilers, so compare two builds with REF"
    fi
fi

fail=0
printf "%-8s %14s %14s %8s\n" "" expected this change
for s in $ROWS; do
    want=$(awk -v s=$s '$1 == s { print $2 }' "$WANT")
    got=$(awk -v s=$s '$1 == s { print $2 }' "$OUT/this.txt")
    pct=$(awk -v a="$want" -v b="$got" 'BEGIN { printf "%+.1f", (b - a) * 100.0 / a }')
    verdict=ok
    if awk -v p="$pct" -v t="$TOL" 'BEGIN { exit !(p > t) }'; then verdict=SLOWER; fail=1
    elif awk -v p="$pct" -v t="$TOL" 'BEGIN { exit !(p < -t) }'; then verdict=faster
    fi
    printf "%-8s %14s %14s %7s%%  %s\n" "$s" "$want" "$got" "$pct" "$verdict"
done
if [ $fail = 0 ]; then
    echo "no performance regression (tolerance ${TOL}%)"
else
    echo "PERFORMANCE REGRESSION: more than ${TOL}% more work (see SLOWER above)"
fi
exit $fail
