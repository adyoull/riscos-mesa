#!/bin/bash
# OpenAL Soft's NEON mixers against its C ones, on an emulated ARM CPU.
#
# The devkit builds OpenAL with the NEON mixers (Alc/mixer/mixer_neon.c),
# used when the CPU has NEON. This builds the same source (with
# patches/openal) and SDL 2.26's audio for ARM Linux, VFPv3 with only the
# mixer file NEON as in build/build-openal.sh, and mixes
# almix.c's scene (three looping tones at 11, 22 and 44 kHz, panned, with a
# gain change) through OpenAL's loopback device, so there's no timing in it,
# under qemu-arm (Cortex-A8: it has NEON) twice: with the NEON mixers and
# with them turned off (disable-cpu-exts). The two are compared sample for
# sample: 16-bit and 32-bit float output (float shows differences 16 bits
# would hide), with the linear, bsinc and point resamplers.
#
#   tests/host-harness/openal/neon.sh
#
#   SRC=<dir>  where the SDL tree and the OpenAL tarball are (default src/)
#   OUT=<dir>  work directory (default /tmp/riscos-mesa-openal-neon)
#   QEMU_CPU   (default cortex-a8)
#
# Needs arm-linux-gnueabihf-gcc and qemu-arm (run-all.sh runs it with ARM=1).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
: "${SRC:=$R/src}" "${OUT:=/tmp/riscos-mesa-openal-neon}" "${QEMU_CPU:=cortex-a8}"
V=1.19.1
S=$SRC/SDL-release-2.26.0
TGZ=$SRC/openal-soft-$V.tar.gz
PATCH=$R/patches/openal/openal-soft-$V-riscos.patch
CC=arm-linux-gnueabihf-gcc
FLAGS="-O2 -mfpu=vfpv3 -mfloat-abi=hard -DRO_NEON_EXACT_ONLY"   # as build/build-openal.sh
[ -f "$S/configure" ] || { echo "no SDL tree in $S (run build/build-sdl2.sh)"; exit 1; }
[ -f "$TGZ" ] || { echo "no $TGZ (run build/build-openal.sh)"; exit 1; }
P=$OUT/arm
mkdir -p "$OUT"

# SDL 2.26 for ARM Linux: audio only (disk driver), static
if [ ! -f "$P/lib/libSDL2.a" ]; then
    rm -rf "$OUT/sdl-build"; mkdir -p "$OUT/sdl-build"
    (cd "$OUT/sdl-build" && CC=$CC CFLAGS="$FLAGS" "$S/configure" --host=arm-linux-gnueabihf \
        --prefix="$P" --disable-shared --enable-static \
        --disable-video --disable-render --disable-joystick --disable-haptic \
        --disable-sensor --disable-hidapi --disable-power --disable-alsa \
        --disable-pulseaudio --disable-pipewire --disable-jack --disable-sndio \
        --disable-esd --disable-arts --disable-nas --disable-oss --disable-libsamplerate \
        --disable-loadso --disable-video-x11 --disable-video-wayland \
        > "$OUT/sdl-configure.log" &&
     make -j"$(nproc)" > "$OUT/sdl-make.log" && make install > /dev/null)
fi

# OpenAL for ARM Linux with the NEON mixers, rebuilt whenever the patch changes
stamp=$(sha256sum "$TGZ" "$PATCH" | sha256sum | cut -c1-16)
if [ "$(cat "$P/openal.stamp" 2>/dev/null)" != "$stamp" ]; then
    rm -rf "$OUT/openal"; mkdir -p "$OUT/openal/build"
    tar xzf "$TGZ" -C "$OUT/openal" --strip-components=1
    patch -s -p1 -d "$OUT/openal" < "$PATCH"
    off=""
    for b in ALSA OSS SOLARIS SNDIO QSA PORTAUDIO PULSEAUDIO JACK WAVE; do off="$off -DALSOFT_BACKEND_$b=OFF"; done
    # (the table generators bin2h and bsincgen are built for this machine
    # in a sub-build that takes CC from the environment: keep it unset)
    (cd "$OUT/openal/build" && env -u CC cmake .. -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=arm \
        -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++ \
        -DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_INSTALL_PREFIX="$P" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_FIND_ROOT_PATH="$P" -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
        -DLIBTYPE=STATIC -DALSOFT_UTILS=OFF -DALSOFT_NO_CONFIG_UTIL=ON -DALSOFT_EXAMPLES=OFF \
        -DALSOFT_TESTS=OFF -DALSOFT_DLOPEN=OFF -DALSOFT_EMBED_HRTF_DATA=OFF \
        -DALSOFT_HRTF_DEFS=OFF -DALSOFT_AMBDEC_PRESETS=OFF -DALSOFT_CONFIG=OFF \
        -DALSOFT_CPUEXT_NEON=ON -DALSOFT_REQUIRE_NEON=ON \
        -DALSOFT_BACKEND_SDL2=ON -DALSOFT_REQUIRE_SDL2=ON $off \
        -DSDL2_INCLUDE_DIR="$P/include" -DSDL2_LIBRARY="$P/lib/libSDL2.a" \
        > "$OUT/openal-cmake.log" &&
     env -u CC make -j"$(nproc)" > "$OUT/openal-make.log" && make install > /dev/null)
    echo "$stamp" > "$P/openal.stamp"
fi
syms=$(arm-linux-gnueabihf-nm "$P/lib/libopenal.a")
grep -q " T Mix_Neon$" <<< "$syms" || { echo "FAIL: libopenal.a has no NEON mixers"; exit 1; }

$CC $FLAGS -static -I"$P/include" "$HERE/almix.c" -o "$OUT/almix" \
    -L"$P/lib" -lopenal -lSDL2 -lm -lpthread -ldl 2> "$OUT/almix-link.log"

# one mix: $1 = name, $2 = sample type, $3 = rate, $4 = resampler, $5 = "neon" or "c"
mix() {
    local conf=$OUT/$1-$5.conf
    {
        echo "[general]"
        echo "resampler = $4"
        [ "$5" = c ] && echo "disable-cpu-exts = neon"
    } > "$conf"
    ALSOFT_CONF="$conf" ALSOFT_LOGLEVEL=3 ALSOFT_LOGFILE="$OUT/$1-$5.log" \
        qemu-arm -cpu "$QEMU_CPU" "$OUT/almix" "$2" "$3" 2 "$OUT/$1-$5.raw"
}

fail=0
for run in "s16-44100-linear int16 44100 linear" "f32-44100-linear float32 44100 linear" \
           "f32-48000-bsinc12 float32 48000 bsinc12" "s16-48000-bsinc24 int16 48000 bsinc24" \
           "s16-22050-point int16 22050 point"; do
    set -- $run
    mix "$1" "$2" "$3" "$4" neon
    mix "$1" "$2" "$3" "$4" c
    grep -q "Extensions: +NEON" "$OUT/$1-neon.log" || { echo "FAIL $1: the NEON mixers weren't used"; fail=1; }
    grep -q "Extensions: -none-" "$OUT/$1-c.log" || { echo "FAIL $1: NEON wasn't turned off"; fail=1; }
    echo "$1: NEON mixers against C:"
    python3 "$HERE/compare-raw.py" "$OUT/$1-neon.raw" "$OUT/$1-c.raw" "$2" || fail=1
done
[ $fail = 0 ] && echo "openal-neon: all passed" || { echo "openal-neon: FAILED"; exit 1; }
