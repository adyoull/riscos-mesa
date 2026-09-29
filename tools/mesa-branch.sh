#!/bin/bash
# Work on riscos-mesa's Mesa changes as a git branch, not as patch files.
#
# patches/mesa/*.patch are what the build applies, in the order given in
# build/build-mesa.sh. Editing them by hand gets hard as they grow; with a
# git branch you edit Mesa's source, commit, rebase onto another Mesa
# release, and so on, and then write the patch files back out.
#
#   tools/mesa-branch.sh make DIR
#       Makes DIR a git repository of pristine Mesa 20.3.5 (commit
#       "upstream"), with one commit per patch on top, in build order.
#       Each commit's message is the patch's section of patches/mesa/README
#       and ends with a "Patch: <name>" line naming its file.
#       Pristine Mesa comes from MESA_SRC=<a Mesa 20.3.5 source tree or
#       tarball>, or else is downloaded (and SHA-256 checked) as
#       build/build-mesa.sh does.
#
#   tools/mesa-branch.sh export DIR
#       Writes each commit on top of "upstream" back to
#       patches/mesa/mesa-20.3.5-<name>.patch, using its "Patch:" line
#       (commits without one are refused, so nothing is lost). To add a
#       patch, commit with a new "Patch: <name>" line, add <name> to
#       PATCHES in build/build-mesa.sh and describe it in patches/mesa/README.
#
# After exporting: apply the patches to a host Mesa tree, rebuild it and
# run tests/run-all.sh (rendering and performance checks).
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
V=20.3.5
cmd=${1:-}; DIR=${2:-}
[ -n "$cmd" ] && [ -n "$DIR" ] || { sed -n '2,27p' "$0"; exit 1; }
PATCHES=$(sed -n 's/^PATCHES="\(.*\)"/\1/p' "$R/build/build-mesa.sh")

# The patch's section of patches/mesa/README, as a commit message
describe() {
    awk -v f="mesa-$V-$1.patch" '
        $0 == f { on = 1; next }
        on && /^mesa-/ { exit }
        on && /^[^ ]/ && NF { exit }
        on { sub(/^    /, ""); print }' "$R/patches/mesa/README" | sed '/./,$!d'
}

case $cmd in
make)
    [ ! -e "$DIR" ] || { echo "$DIR exists" >&2; exit 1; }
    mkdir -p "$DIR"
    if [ -n "${MESA_SRC:-}" ]; then
        if [ -d "$MESA_SRC" ]; then
            (cd "$MESA_SRC" && tar cf - --exclude=./.git --exclude-tag-all=build.ninja .) | (cd "$DIR" && tar xf -)
        else
            tar xzf "$MESA_SRC" -C "$DIR" --strip-components=1
        fi
    else
        source "$R/build/env.sh"
        T=$(mktemp -d)
        (cd "$T" && fetch_verified "$MESA_URL" "$MESA_SHA256" mesa-$V.tgz)
        tar xzf "$T/mesa-$V.tgz" -C "$DIR" --strip-components=1
        rm -rf "$T"
    fi
    cd "$DIR"
    git init -q
    git add -A
    git -c user.name=upstream -c user.email=upstream@mesa3d.org commit -q -m "Mesa $V"
    git branch -m riscos
    git tag upstream
    for p in $PATCHES; do
        patch -p1 -s --no-backup-if-mismatch < "$R/patches/mesa/mesa-$V-$p.patch"
        git add -A
        { echo "riscos-mesa: $p"; echo; describe "$p"; echo; echo "Patch: $p"; } > .git/msg
        git commit -q -F .git/msg
        echo "committed $p"
    done
    rm -f .git/msg
    echo "$DIR: branch riscos, $(echo $PATCHES | wc -w) commits on tag upstream"
    ;;
export)
    cd "$DIR"
    git rev-parse -q --verify upstream >/dev/null || { echo "$DIR: no upstream tag" >&2; exit 1; }
    [ -z "$(git status --porcelain)" ] || { echo "$DIR: commit your changes first" >&2; exit 1; }
    for c in $(git rev-list --reverse upstream..HEAD); do
        name=$(git log -1 --format=%B "$c" | sed -n 's/^Patch: *//p' | tail -1)
        [ -n "$name" ] || { echo "commit $c has no 'Patch: <name>' line" >&2; exit 1; }
        git diff --no-color --no-renames "$c^" "$c" > "$R/patches/mesa/mesa-$V-$name.patch"
        echo "wrote patches/mesa/mesa-$V-$name.patch"
        case " $PATCHES " in *" $name "*) ;; *)
            echo "note: add $name to PATCHES in build/build-mesa.sh and describe it in patches/mesa/README";;
        esac
    done
    ;;
*)
    sed -n '2,27p' "$0"; exit 1;;
esac
