#!/bin/bash
# Keeps patches/sdl2/*.p (the SDL 2.26 RISC OS overlay) and a working SDL
# tree in step.
#
#   tools/sdl-overlay-regen.sh           write each .p from the working tree
#   tools/sdl-overlay-regen.sh --check   only check: does pristine SDL plus
#                                        the .p files give the working tree?
#
# The working tree is src/SDL-release-2.26.0 (as build/build-sdl2.sh leaves
# it), or SDL_TREE=<dir>. Pristine SDL comes from the pinned tarball
# (build/env.sh), downloaded into $SRC the first time.
#
# How to change the overlay: edit the C files in the working tree (never
# inside a .p file), rebuild and run the host tests, then run this script
# to rewrite the .p files, and commit them. `git diff patches/sdl2` shows
# what changed. Every .p file patches one source file, and is named after
# it (src/video/riscos/SDL_riscoswindow.c -> src.video.riscos.SDL_riscoswindow.c.p).
# The one exception is configure.ac, which four .p files patch in turn
# (sdl2-configure.ac.*.p, applied in name order): this script only checks
# those; edit them by hand, and --check confirms the result.
#
# Afterwards, tell riscos-openttd to re-export (tools/sdl-overlay-export.sh)
# if any file its build compiles changed.
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
P=$R/patches/sdl2
source "$R/build/env.sh"
T=${SDL_TREE:-$SRC/SDL-release-$SDL_V}
CHECK=0
[ "${1:-}" = --check ] && CHECK=1
[ -d "$T" ] || { echo "no SDL tree at $T (run build/build-sdl2.sh first)" >&2; exit 2; }

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
PRISTINE=$W/SDL-release-$SDL_V
# Pristine SDL: the pinned tarball, or, offline, the working tree's own git
# history if it has one (a tree whose first commit is pristine SDL).
unpack_pristine() {
    rm -rf "$PRISTINE"
    if (cd "$SRC" && fetch_verified "$SDL_URL" "$SDL_SHA256" SDL-$SDL_V.tgz) 2>/dev/null; then
        tar xzf "$SRC/SDL-$SDL_V.tgz" -C "$W"
    elif git -C "$T" rev-parse -q --verify HEAD >/dev/null 2>&1; then
        mkdir -p "$PRISTINE"
        git -C "$T" archive "$(git -C "$T" rev-list --max-parents=0 HEAD)" | tar x -C "$PRISTINE"
    else
        echo "can't get pristine SDL $SDL_V ($SDL_URL)" >&2
        exit 2
    fi
}
unpack_pristine

# The file each .p patches (its "+++" line)
target() { sed -n 's/^+++ \([^[:space:]]*\).*/\1/p' "$1" | head -1; }

bad=0
if [ $CHECK = 0 ]; then
    # Rewrite each single-file .p as a git diff of pristine -> working tree,
    # without "index" lines (they change on every edit and add noise).
    git -C "$PRISTINE" init -q
    git -C "$PRISTINE" add -A
    git -C "$PRISTINE" -c user.name=x -c user.email=x commit -q -m pristine
    for p in "$P"/*.p; do
        f=$(target "$p")
        case "$f" in configure.ac) continue ;; esac
        mkdir -p "$(dirname "$PRISTINE/$f")"
        cp "$T/$f" "$PRISTINE/$f"
        git -C "$PRISTINE" add -N "$f"
        git -C "$PRISTINE" diff --no-prefix -- "$f" | grep -v '^index ' > "$p.new"
        if cmp -s "$p" "$p.new"; then rm "$p.new"; else mv "$p.new" "$p"; echo "updated $(basename "$p")"; fi
    done
    # A clean pristine tree again for the check below
    unpack_pristine
fi

# Check: pristine + every .p (in the order the build applies them) must
# equal the working tree, file by file.
for p in "$P"/*.p; do
    (cd "$PRISTINE" && patch -s -p0 < "$p") || { echo "does not apply: $(basename "$p")"; bad=1; }
done
for f in $(for p in "$P"/*.p; do target "$p"; done | sort -u); do
    if ! cmp -s "$PRISTINE/$f" "$T/$f"; then
        echo "differs from the working tree: $f"
        bad=1
    fi
done
if [ $bad = 0 ]; then
    echo "SDL overlay: the .p files match the working tree ($(ls "$P"/*.p | wc -l) files)"
else
    echo "SDL overlay: out of step (see above)" >&2
fi
exit $bad
