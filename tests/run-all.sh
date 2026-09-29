#!/bin/bash
# Runs all of riscos-mesa's tests that work on a Linux host, and says which
# passed. Run it before committing a change to egl/, dispmanx/, glut/,
# patches/sdl2 or patches/mesa.
#
#   tests/host-setup.sh         (once: sources, host Mesa and GLU)
#   tests/run-all.sh
#
# or, with a host Mesa of your own:
#   M=<mesa-20.3.5 tree, patches/mesa applied, built in $M/build> GLU=<host libGLU.a>
#   tests/run-all.sh
#
# What it runs:
#   gen-glsl-batch  the batch shader code in $M matches the interpreter
#                   (tools/gen-glsl-batch.py --check)
#   mesa            rendering checks: every image the same as expected
#                   (tests/host-harness/mesa/run.sh)
#   perf            performance: instructions per frame for glbench's
#                   scenes, no more than 2% above expected/perf.txt
#                   (tests/host-harness/mesa/perf.sh; needs valgrind)
#   egl             libEGL and libbcm_host against a fake RISC OS
#                   (tests/host-harness/egl, over 400 checks)
#   sdl             SDL's GL glue with emulated SWIs (tests/host-harness)
#   sdl-wimp        SDL's Wimp event handling: desktop quit, close icon,
#                   icon bar menu (tests/host-harness/sdl-wimp)
#   openal          OpenAL Soft (patches/openal) built for the host against
#                   the same SDL, playing tests/altest.c's tones through
#                   SDL's disk audio driver; the recording is checked
#                   (tests/host-harness/openal; the first run builds a host
#                   SDL and OpenAL, about a minute)
#   glut            freeglut's RISC OS back end driving freeglut's demos
#                   (tests/host-harness/glut, 23 checks); needs GLU=<a host
#                   libGLU.a>, otherwise skipped
#   examples        the devkit's example programs (devkit/examples) on the
#                   fake RISC OS: what reaches the screen, and the tune
#                   (tests/host-harness/examples; needs GLU, after glut
#                   and openal)
#   ovl             tests/ovltest.c (the Pi VideoOverlay tests) with scripted
#                   keys on the fake RISC OS and a fake VideoOverlay module
#                   (tests/host-harness/ovl)
#   arm             the rendering checks on the RISC OS build of Mesa under
#                   qemu-arm emulating a Cortex-A8, the oldest CPU supported
#                   (tests/host-harness/mesa/arm); only with ARM=1,
#                   as it takes a few minutes
#
# Not run here, as it takes a long first build: Khronos's dEQP-EGL tests
# (tests/host-harness/deqp/README.md). Run them after changing egl/ or
# patches/mesa.
#
# Other settings (environment):
#   SRC=<dir>    where build/build-sdl2.sh and build-freeglut.sh left the
#                patched SDL and freeglut sources (default: src/ in the repo)
#   STAGE=<dir>  the build's stage directory (default: stage/ in the repo)
#   OUT=<dir>    work directory (default /tmp/riscos-mesa-tests)
#
# Building the host Mesa: see tests/host-harness/egl/README.md. After a
# change to patches/mesa, re-apply the patches to $M and rebuild it first.
set -uo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
H=$R/tests/host-harness
: "${SRC:=$R/src}" "${STAGE:=$R/stage}" "${OUT:=/tmp/riscos-mesa-tests}"
# What tests/host-setup.sh made, unless told otherwise
[ -z "${M:-}" ] && [ -f "$SRC/mesa-20.3.5/build/build.ninja" ] && M=$SRC/mesa-20.3.5
[ -z "${GLU:-}" ] && [ -f "$SRC/glu-9.0.1/build-host/libGLU.a" ] && GLU=$SRC/glu-9.0.1/build-host/libGLU.a
[ ! -f "$STAGE/include/GL/glu.h" ] && [ -d "$SRC/host-stage/include" ] && STAGE=$SRC/host-stage
: "${M:?no host Mesa: run tests/host-setup.sh, or set M=<host Mesa tree built in \$M/build>}"
O=$M/build/src/mesa/drivers/osmesa
mkdir -p "$OUT"
export M SRC STAGE
[ -n "${GLU:-}" ] && export GLU

results=()
step() {   # step <name> <command...>: run it, log to $OUT/<name>.log
    local name=$1; shift
    printf "%-15s " "$name"
    if "$@" > "$OUT/$name.log" 2>&1; then
        echo "ok    $(tail -1 "$OUT/$name.log")"
        results+=("ok $name")
    else
        echo "FAIL  (log: $OUT/$name.log)"
        tail -5 "$OUT/$name.log" | sed 's/^/                    /'
        results+=("FAIL $name")
    fi
}
skip() { printf "%-15s skipped: %s\n" "$1" "$2"; }

gen_check() {
    python3 "$R/tools/gen-glsl-batch.py" --check "$M/src/mesa/program/prog_execute.c"
}

egl_harness() {
    cd "$H/egl" || return 1
    local X=$R/dispmanx
    local F="-no-pie -O1 -w -std=gnu99 -Ifake -I$X/include -I$X -I$R/egl/include -I$M/include"
    gcc $F -D__riscos__ -c harness_es.c -o "$OUT/harness_es.o" &&
    gcc $F -D__riscos__ -c "$X/bcm_host.c" -o "$OUT/bcm_host.o" &&
    gcc -no-pie -O1 -w -std=gnu99 -I"$H/ovl/fake" -I. -Ifake -c "$H/ovl/fake_ovl.c" -o "$OUT/fake_ovl.o" &&
    gcc $F harness.c harness_ovl.c fake_riscos.c "$OUT/fake_ovl.o" "$R/egl/egl_riscos.c" "$OUT/harness_es.o" "$OUT/bcm_host.o" \
        -o "$OUT/egl-harness" -L"$O" -lOSMesa -lpthread -Wl,-rpath,"$O" &&
    "$OUT/egl-harness" | tee "$OUT/egl.out" | tail -1 && grep -q "ALL PASS" "$OUT/egl.out"
}

sdl_harness() {
    local S=$SRC/SDL-release-2.26.0
    [ -f "$S/src/video/riscos/SDL_riscosopengl.c" ] || { echo "no patched SDL in $S (run build/build-sdl2.sh)"; return 1; }
    cd "$H" || return 1
    local SF="-no-pie -O1 -w -std=gnu99 -DSDL_VIDEO_DRIVER_RISCOS=1 -DSDL_VIDEO_OPENGL=1 \
        -DSDL_VIDEO_OPENGL_OSMESA=1 -I$S/include -I$S/src/video/riscos -I$S/src/video \
        -I$S/src -Iegl/fake -Iegl -I$R/egl/include -I$M/include -include $S/src/SDL_internal.h"
    local EF="-no-pie -O1 -w -std=gnu99 -Iegl/fake -I$R/dispmanx/include -I$R/dispmanx -I$R/egl/include -I$M/include"
    gcc $SF -c harness.c -o "$OUT/sdl-harness.o" &&
    gcc $SF -c "$S/src/video/riscos/SDL_riscosopengl.c" -o "$OUT/sdl-gl.o" &&
    gcc $EF -c "$R/egl/egl_riscos.c" -o "$OUT/sdl-egl.o" &&
    gcc $EF -c egl/fake_riscos.c -o "$OUT/sdl-fake.o" &&
    gcc -no-pie -O1 -w -std=gnu99 -Iovl/fake -Iegl -Iegl/fake -c ovl/fake_ovl.c -o "$OUT/sdl-fake-ovl.o" &&
    gcc -no-pie "$OUT/sdl-harness.o" "$OUT/sdl-gl.o" "$OUT/sdl-egl.o" "$OUT/sdl-fake.o" "$OUT/sdl-fake-ovl.o" \
        -o "$OUT/sdl-harness" -L"$O" -lOSMesa -lstdc++ -lz -lm -lpthread -Wl,-rpath,"$O" &&
    "$OUT/sdl-harness" | tee "$OUT/sdl.out" | tail -1 && grep -q "ALL CHECKS PASSED" "$OUT/sdl.out"
}

glut_harness() {
    OUT=$OUT/glut "$H/glut/run.sh" | tee "$OUT/glut.out" | tail -1 && grep -q "^0 failures" "$OUT/glut.out"
}

echo "riscos-mesa host tests, Mesa in $M"
step gen-glsl-batch gen_check
step mesa env OUT="$OUT/mesa" "$H/mesa/run.sh"
if command -v valgrind >/dev/null; then
    step perf env OUT="$OUT/perf" "$H/mesa/perf.sh"
else
    skip perf "needs valgrind"
fi
step egl egl_harness
step sdl sdl_harness
step sdl-wimp env SDL="$SRC/SDL-release-2.26.0" OUT="$OUT/sdl-wimp" "$H/sdl-wimp/run.sh"
step openal env SRC="$SRC" OUT="$OUT/openal" "$H/openal/run.sh"
if [ -n "${GLU:-}" ]; then step glut glut_harness; else skip glut "set GLU=<host libGLU.a>"; fi
if [ -n "${GLU:-}" ]; then
    step examples env OUT="$OUT/examples" GLUT="$OUT/glut/gl/libglut.a" AL="$OUT/openal/host" \
        "$H/examples/run.sh"
else
    skip examples "set GLU=<host libGLU.a>"
fi
step ovl env OUT="$OUT/ovl" "$H/ovl/run.sh"
if [ -n "${ARM:-}" ]; then
    # on an emulated Cortex-A8 (VFPv3), the oldest CPU the build supports
    step arm env OUT="$OUT/arm" QEMU_CPU="${QEMU_CPU:-cortex-a8}" "$H/mesa/arm/run-arm.sh"
else
    skip arm "set ARM=1 (needs qemu-user and an ARM Linux cross compiler)"
fi

if printf '%s\n' "${results[@]}" | grep -q '^FAIL'; then
    echo "SOME TESTS FAILED"
    exit 1
fi
echo "all tests passed"
