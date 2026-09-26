#!/bin/bash
# Copies the SDL 2.26 RISC OS overlay (patches/sdl2, the authoritative
# copy) into another project's overlay directory, e.g. riscos-openttd's.
#
#   tools/sdl-overlay-export.sh DEST_DIR
#
# DEST_DIR's *.p files are replaced by this repo's (files there
# that are no longer in the overlay are removed), README.md is copied as
# README-overlay.md, and SOURCE records which riscos-mesa commit they came
# from. Run it from a clean riscos-mesa checkout at the commit you want;
# afterwards `tools/sdl-overlay-check.sh DEST_DIR` reports "identical".
#
# Changes to the overlay are made here, in riscos-mesa, never in a copy:
# a copy that has drifted is reported by sdl-overlay-check.sh.
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
A=$R/patches/sdl2
D=${1:?usage: tools/sdl-overlay-export.sh DEST_DIR}
[ -d "$D" ] || { echo "$D: not a directory" >&2; exit 2; }

rev=$(git -C "$R" rev-parse --short HEAD 2>/dev/null || echo unknown)
dirty=""
if git -C "$R" rev-parse -q HEAD >/dev/null 2>&1 &&
   [ -n "$(git -C "$R" status --porcelain -- patches/sdl2)" ]; then
    dirty=" (with uncommitted changes)"
    echo "warning: patches/sdl2 has uncommitted changes" >&2
fi

shopt -s nullglob
for f in "$D"/*.p; do
    [ -f "$A/$(basename "$f")" ] || { rm -f "$f"; echo "removed $(basename "$f")"; }
done
n=0
for f in "$A"/*.p; do cp "$f" "$D/"; n=$((n + 1)); done
cp "$A/README.md" "$D/README-overlay.md"
cat > "$D/SOURCE" <<EOF
SDL 2.26.0 RISC OS overlay, copied from riscos-mesa patches/sdl2
(the authoritative copy) at commit $rev$dirty on $(date -u +%Y-%m-%d).
Don't edit these files here: change them in riscos-mesa and copy them
again with riscos-mesa/tools/sdl-overlay-export.sh.
EOF
echo "copied $n files from riscos-mesa $rev$dirty to $D"
