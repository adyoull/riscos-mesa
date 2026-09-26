#!/bin/bash
# Builds everything, in the right order, into $STAGE:
#   zlib, Mesa (slow the first time), GLU, SDL2, libEGL/libbcm_host,
#   freeglut, then the test programs, the Pi examples and the ports.
#
#   GCCSDK_ENV=/path/to/gccsdk/env build/build-all.sh [VERSION]
#
# With a VERSION it also makes the release zips and devkit in dist/
# (build/package.sh VERSION). Each step's output goes to
# $STAGE/logs/<step>.log; the first failure stops the build and shows the
# end of its log. Steps can still be run on their own (see README.md), e.g.
# after changing only egl/, build-egl.sh and the programs after it.
# SKIP_ZLIB=1 leaves out zlib when the GCCSDK environment already has it.
set -euo pipefail
B=$(cd "$(dirname "$0")" && pwd)
source "$B/env.sh"
mkdir -p "$STAGE/logs"

STEPS="zlib mesa glu sdl2 egl freeglut tests hello-pi ports"
[ -n "${SKIP_ZLIB:-}" ] && STEPS=${STEPS#zlib }

for s in $STEPS; do
    printf "%-10s " "$s"
    start=$(date +%s)
    if "$B/build-$s.sh" > "$STAGE/logs/$s.log" 2>&1; then
        echo "ok ($(( $(date +%s) - start )) s)"
    else
        echo "FAILED - end of $STAGE/logs/$s.log:"
        tail -20 "$STAGE/logs/$s.log"
        exit 1
    fi
done

if [ $# -ge 1 ]; then
    printf "%-10s " package
    "$B/package.sh" "$1" > "$STAGE/logs/package.log" 2>&1 && echo "ok: dist/*-$1.*" || {
        echo "FAILED - see $STAGE/logs/package.log"; exit 1; }
fi
echo "everything built into $STAGE"
