#!/bin/bash
# OpenAL Soft 1.19.1 -> $STAGE/lib/libopenal.a, $STAGE/include/AL/*.h and
# $STAGE/lib/pkgconfig/openal.pc. Needs build-sdl2.sh first.
#
# 1.19.1 is the last OpenAL Soft written in C (1.20 and later are C++11
# with std::thread). It mixes in software and plays through SDL2's audio
# (its SDL2 backend, the only one built besides Null), so on RISC OS the
# sound goes SDL -> the overlay's RISC OS audio driver -> SharedSoundBuffer.
# Nothing RISC OS-specific is needed below SDL.
# patches/openal: two small build fixes (see patches/openal/README).
#
# Apps link: -lopenal -lSDL2 -lEGL -lOSMesa ... (plus what SDL2 needs, as for SDL apps)
# OpenAL mixes in SDL's audio thread, so programs need UnixLib 5.0.1 or later
# (the pthread ticker fix) and should load the PThreadTicker module
# (devkit/riscos/PThrTicker) from !Run.
set -euo pipefail
source "$(dirname "$0")/env.sh"
R=$(cd "$(dirname "$0")/.." && pwd)
V=$OPENAL_V
cd "$SRC"
# The GitHub tag archive; Ubuntu's orig tarball is the same file.
fetch_verified "$OPENAL_URL" "$OPENAL_SHA256" openal-soft-$V.tar.gz \
  || fetch_verified "$OPENAL_URL2" "$OPENAL_SHA256" openal-soft-$V.tar.gz
rm -rf openal-soft-$V
mkdir openal-soft-$V
tar xzf openal-soft-$V.tar.gz -C openal-soft-$V --strip-components=1
patch -s -p1 -d openal-soft-$V < "$R/patches/openal/openal-soft-$V-riscos.patch"

mkdir -p openal-soft-$V/build-ro && cd openal-soft-$V/build-ro
# -D_POSIX_C_SOURCE/_XOPEN_SOURCE: OpenAL builds with -std=c11, and UnixLib
# only declares nanosleep() (and others) when asked for POSIX.
BACKENDS_OFF=""
for b in ALSA OSS SOLARIS SNDIO QSA WINMM DSOUND WASAPI PORTAUDIO PULSEAUDIO \
         JACK COREAUDIO OPENSL WAVE; do
  BACKENDS_OFF="$BACKENDS_OFF -DALSOFT_BACKEND_$b=OFF"
done
cmake .. -DCMAKE_TOOLCHAIN_FILE="$R/build/riscos.cmake" \
  -DCMAKE_INSTALL_PREFIX="$STAGE" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="$RO_CFLAGS -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700" \
  -DCMAKE_C_FLAGS_RELEASE="-DNDEBUG" \
  -DLIBTYPE=STATIC -DALSOFT_UTILS=OFF -DALSOFT_NO_CONFIG_UTIL=ON \
  -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_DLOPEN=OFF \
  -DALSOFT_EMBED_HRTF_DATA=OFF -DALSOFT_HRTF_DEFS=OFF -DALSOFT_AMBDEC_PRESETS=OFF \
  -DALSOFT_CONFIG=OFF -DALSOFT_INSTALL=ON -DALSOFT_CPUEXT_NEON=OFF \
  -DALSOFT_BACKEND_SDL2=ON -DALSOFT_REQUIRE_SDL2=ON $BACKENDS_OFF \
  -DSDL2_INCLUDE_DIR="$STAGE/include" -DSDL2_LIBRARY="$STAGE/lib/libSDL2.a"
# OpenAL builds two table generators (bin2h, bsincgen) for the build
# machine in a sub-build that inherits CC/CFLAGS from the environment:
# env.sh's cross compiler must not reach it (the library's compiler comes
# from riscos.cmake, which the sub-build doesn't use).
env -u CC -u CXX -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CPPFLAGS -u AR -u RANLIB \
  sh -c "make -j$(nproc) && make install"

grep -q "^#define HAVE_SDL2" config.h \
  || { echo "OpenAL's SDL2 backend is NOT enabled" >&2; exit 1; }
cp ../COPYING "$STAGE/LICENCE-openal.txt"
ls -la "$STAGE/lib/libopenal.a"
