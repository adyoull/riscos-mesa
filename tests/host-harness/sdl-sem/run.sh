#!/bin/bash
# SDL_SemWaitTimeout from the RISC OS overlay's SDL_syssem.c, on Linux, with
# the C library's sem_timedwait and with one that answers as UnixLib's old
# stub (ENOSYS): see semtest.c.
#
#   tests/host-harness/sdl-sem/run.sh
#
#   SDL=<dir>  the patched SDL tree (default src/SDL-release-<SDL_V>)
#   OUT=<dir>  work directory (default /tmp/riscos-mesa-sdl-sem); the host
#              SDL build is kept there, one per SDL version
#
# The host SDL is built without the RISC OS drivers; only SDL_syssem.c is
# compiled again with __riscos__ defined, so the overlay's fallback is in
# it, and linked ahead of libSDL2.a (whose own SDL_syssem.o is then left
# out).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
SDL_V=$(sed -n 's/^SDL_V=//p' "$R/build/env.sh")   # the SDL 2 version the devkit builds
: "${SDL:=$R/src/SDL-release-$SDL_V}" "${OUT:=/tmp/riscos-mesa-sdl-sem}"
[ -f "$SDL/configure" ] || { echo "no SDL tree in $SDL (run build/build-sdl2.sh)"; exit 1; }
P=$OUT/host-sdl$SDL_V
mkdir -p "$OUT"

# Host SDL 2 with threads and timers only, static
if [ ! -f "$P/lib/libSDL2.a" ]; then
    rm -rf "$OUT/sdl-build"; mkdir -p "$OUT/sdl-build"
    (cd "$OUT/sdl-build" && "$SDL/configure" --prefix="$P" --disable-shared --enable-static \
        --disable-video --disable-render --disable-audio --disable-joystick --disable-haptic \
        --disable-sensor --disable-hidapi --disable-power --disable-loadso \
        > "$OUT/sdl-configure.log" 2>&1 &&
     make -j"$(nproc)" > "$OUT/sdl-make.log" 2>&1 && make install > /dev/null)
fi
grep -q "define HAVE_SEM_TIMEDWAIT 1" "$P/include/SDL2/SDL_config.h" ||
    { echo "FAIL: the host SDL doesn't use sem_timedwait, so there's nothing to test"; exit 1; }

gcc -O1 -w -D__riscos__ -I"$P/include/SDL2" -I"$SDL/src" -I"$OUT/sdl-build/include" \
    -c "$SDL/src/thread/pthread/SDL_syssem.c" -o "$OUT/syssem-riscos.o"
# On Linux SDL has its dynamic API, so inside libSDL2.a these functions are
# SDL_SemWait_REAL and so on; on RISC OS (as compiled here) it hasn't.
# Give them the Linux names so they replace libSDL2.a's own SDL_syssem.o.
for f in CreateSemaphore DestroySemaphore SemWait SemTryWait SemWaitTimeout SemPost SemValue; do
    echo "SDL_$f SDL_${f}_REAL"
done > "$OUT/syssem.syms"
objcopy --redefine-syms="$OUT/syssem.syms" "$OUT/syssem-riscos.o"
gcc -O1 -I"$P/include/SDL2" "$HERE/semtest.c" "$OUT/syssem-riscos.o" -o "$OUT/semtest" \
    -Wl,--wrap=sem_timedwait "$P/lib/libSDL2.a" -lm -lpthread -ldl

fail=0
"$OUT/semtest" || fail=1
SEMTEST_STUB=1 "$OUT/semtest" || fail=1
[ $fail = 0 ] && echo "sdl-sem: all passed" || { echo "sdl-sem: FAILED"; exit 1; }
