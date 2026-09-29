#!/bin/bash
# Prepares everything tests/run-all.sh needs on a Linux x86-64 host, with no
# RISC OS toolchain: the patched sources (Mesa, SDL, OpenAL, freeglut, GLU,
# fetched and patched by the build scripts in their sources-only mode), a
# host build of Mesa's OSMesa and a host libGLU.a. Then run the tests:
#
#   tests/host-setup.sh        # the first time about 10 minutes
#   tests/run-all.sh           # finds what this made
#
# Needs: gcc, g++, meson, ninja, python3 with mako, bison, flex, patch,
# autoconf, automake, libtool, cmake, git, curl (valgrind for the perf
# check). On Ubuntu 24.04:
#   sudo apt install build-essential meson ninja-build python3-mako bison \
#        flex autoconf automake libtool cmake git curl valgrind zlib1g-dev
#
# SRC=<dir> as for the build scripts (default: src/ in the repo). The host
# Mesa build goes in $SRC/mesa-20.3.5/build (the RISC OS one is build-ro),
# the host GLU in $SRC/glu-9.0.1/build-host.
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
source "$R/build/env.sh"
# env.sh points CC and friends at the RISC OS cross compiler; the host
# builds below use the host's own (the build scripts set theirs again).
unset CC CXX AR RANLIB STRIP
fail() { echo "$1 failed: see $2" >&2; exit 1; }

echo "== patched sources"
for s in mesa sdl2 openal glu freeglut; do
    SOURCES_ONLY=1 "$R/build/build-$s.sh" > "$SRC/sources-$s.log" 2>&1 \
        || fail "build/build-$s.sh (sources only)" "$SRC/sources-$s.log"
done

echo "== host Mesa (OSMesa, as the tests expect: debugoptimized)"
M=$SRC/mesa-$MESA_V
if [ ! -f "$M/build/build.ninja" ]; then
    meson setup "$M/build" "$M" -Dosmesa=classic -Ddri-drivers= -Dgallium-drivers= \
        -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dplatforms= \
        -Dgles1=disabled -Dgles2=disabled -Dshared-glapi=disabled -Dllvm=disabled \
        -Dzstd=disabled -Dbuildtype=debugoptimized > "$SRC/host-mesa-setup.log" 2>&1 \
        || { rm -rf "$M/build"; fail "meson setup" "$SRC/host-mesa-setup.log"; }
fi
ninja -C "$M/build" > "$SRC/host-mesa-build.log" 2>&1 \
    || fail "the host Mesa build" "$SRC/host-mesa-build.log"

echo "== host GLU"
G=$SRC/glu-9.0.1
B=$G/build-host
mkdir -p "$B/o"
sources=$(sed -n '/^libGLU_la_SOURCES/,/^$/p' "$G/Makefile.am" | grep -o 'src/[^ \\]*\.cc\?')
for f in $sources; do
    o=$B/o/$(echo "$f" | tr / _).o
    [ "$o" -nt "$G/$f" ] && continue
    case $f in *.cc) cc=g++ ;; *) cc=gcc ;; esac
    $cc -O2 -w -DLIBRARYBUILD -I"$G/include" -I"$G/src/include" -I"$G/src/libnurbs/internals" \
        -I"$G/src/libnurbs/interface" -I"$G/src/libnurbs/nurbtess" -I"$M/include" -c "$G/$f" -o "$o"
done
rm -f "$B/libGLU.a"
ar rcs "$B/libGLU.a" "$B"/o/*.o

echo "== headers the GLUT and example tests take from a stage"
H=$SRC/host-stage/include
mkdir -p "$H/GL"
cp "$G/include/GL/glu.h" "$H/GL/"
cp -r "$M/include/GLES" "$M/include/GLES2" "$M/include/KHR" "$H/"

cat <<EOF

Ready. Run the host tests with:
  tests/run-all.sh
(it finds M=$M, GLU=$B/libGLU.a
and STAGE=$SRC/host-stage by itself when there's no RISC OS build)
EOF
