#!/bin/bash
# Host test of the devkit's OpenAL Soft: the same 1.19.1 source with
# patches/openal, built the same way as build/build-openal.sh (SDL2 backend
# only), on Linux against SDL 2.26 from the same tree the RISC OS build
# uses. tests/altest.c plays its three tones through SDL's "disk" audio
# driver, which writes the sound to a file; check-tones.py then checks the
# recording: each tone at its frequency, in the middle, left and right.
#
#   tests/host-harness/openal/run.sh
#
#   SRC=<dir>  where build/build-sdl2.sh and build-openal.sh left the SDL
#              tree and the OpenAL tarball (default: src/ in the repo)
#   OUT=<dir>  work directory (default /tmp/riscos-mesa-openal); the host
#              SDL and OpenAL builds are kept there and rebuilt when the
#              OpenAL patch changes
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
: "${SRC:=$R/src}" "${OUT:=/tmp/riscos-mesa-openal}"
V=1.19.1
S=$SRC/SDL-release-2.26.0
TGZ=$SRC/openal-soft-$V.tar.gz
PATCH=$R/patches/openal/openal-soft-$V-riscos.patch
[ -f "$S/configure" ] || { echo "no SDL tree in $S (run build/build-sdl2.sh)"; exit 1; }
[ -f "$TGZ" ] || { echo "no $TGZ (run build/build-openal.sh)"; exit 1; }
P=$OUT/host
mkdir -p "$OUT"

# Host SDL 2.26: audio only (disk and dummy drivers), static
if [ ! -f "$P/lib/libSDL2.a" ]; then
    rm -rf "$OUT/sdl-build"; mkdir -p "$OUT/sdl-build"
    (cd "$OUT/sdl-build" && "$S/configure" --prefix="$P" --disable-shared --enable-static \
        --disable-video --disable-render --disable-joystick --disable-haptic \
        --disable-sensor --disable-hidapi --disable-power --disable-alsa \
        --disable-pulseaudio --disable-pipewire --disable-jack --disable-sndio \
        --disable-esd --disable-arts --disable-nas --disable-oss --disable-libsamplerate \
        > "$OUT/sdl-configure.log" &&
     make -j"$(nproc)" > "$OUT/sdl-make.log" && make install > /dev/null)
fi

# Host OpenAL, rebuilt whenever the patch changes
stamp=$(sha256sum "$TGZ" "$PATCH" | sha256sum | cut -c1-16)
if [ "$(cat "$P/openal.stamp" 2>/dev/null)" != "$stamp" ]; then
    rm -rf "$OUT/openal"; mkdir -p "$OUT/openal/build"
    tar xzf "$TGZ" -C "$OUT/openal" --strip-components=1
    patch -s -p1 -d "$OUT/openal" < "$PATCH"
    off=""
    for b in ALSA OSS SOLARIS SNDIO QSA PORTAUDIO PULSEAUDIO JACK WAVE; do off="$off -DALSOFT_BACKEND_$b=OFF"; done
    (cd "$OUT/openal/build" && cmake .. -DCMAKE_INSTALL_PREFIX="$P" -DCMAKE_BUILD_TYPE=Release \
        -DLIBTYPE=STATIC -DALSOFT_UTILS=OFF -DALSOFT_NO_CONFIG_UTIL=ON -DALSOFT_EXAMPLES=OFF \
        -DALSOFT_TESTS=OFF -DALSOFT_DLOPEN=OFF -DALSOFT_EMBED_HRTF_DATA=OFF \
        -DALSOFT_HRTF_DEFS=OFF -DALSOFT_AMBDEC_PRESETS=OFF -DALSOFT_CONFIG=OFF \
        -DALSOFT_BACKEND_SDL2=ON -DALSOFT_REQUIRE_SDL2=ON $off \
        -DSDL2_INCLUDE_DIR="$P/include" -DSDL2_LIBRARY="$P/lib/libSDL2.a" \
        > "$OUT/openal-cmake.log" &&
     make -j"$(nproc)" > "$OUT/openal-make.log" && make install > /dev/null)
    echo "$stamp" > "$P/openal.stamp"
fi

gcc -O2 -I"$P/include" -I"$P/include/SDL2" "$R/tests/altest.c" -o "$OUT/altest" \
    -L"$P/lib" -lopenal -lSDL2 -lm -lpthread -ldl

# Mix 16-bit stereo at 22050 Hz (so the recording's format is known) and
# record it with SDL's disk driver.
cat > "$OUT/alsoft.conf" <<CONF
[general]
channels = stereo
sample-type = int16
frequency = 22050
CONF
rm -f "$OUT/altest.raw"
ALSOFT_CONF="$OUT/alsoft.conf" SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$OUT/altest.raw" \
    "$OUT/altest" | tee "$OUT/altest.out"
python3 "$HERE/check-tones.py" "$OUT/altest.raw" 22050
