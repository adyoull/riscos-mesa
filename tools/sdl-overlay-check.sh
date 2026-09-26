#!/bin/bash
# Compares patches/sdl2 with another project's copy of the same SDL 2.26
# RISC OS overlay (riscos-openttd keeps one too), so the two can't drift
# apart unnoticed. Read-only: it never changes either copy.
#
#   tools/sdl-overlay-check.sh OTHER_DIR
#
# OTHER_DIR is the other project's overlay directory (the folder holding
# its src.video.riscos.*.p files). Exit 0 when every .p file is the
# same in both; otherwise it lists what differs and exits 1. README.md is
# not compared (each project describes the overlay in its own words).
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
A=$R/patches/sdl2
B=${1:?usage: tools/sdl-overlay-check.sh OTHER_DIR}
[ -d "$B" ] || { echo "$B: not a directory" >&2; exit 2; }

list() { (cd "$1" && ls -1 | grep -E '\.p$' | sort); }
bad=0
while read -r f; do
    if [ ! -f "$A/$f" ]; then echo "only in $B: $f"; bad=1
    elif [ ! -f "$B/$f" ]; then echo "only in riscos-mesa: $f"; bad=1
    elif ! cmp -s "$A/$f" "$B/$f"; then echo "differs: $f"; bad=1
    fi
done < <(sort -u <(list "$A") <(list "$B"))
[ $bad = 0 ] && echo "SDL overlay: identical ($(list "$A" | wc -l) files)"
exit $bad
