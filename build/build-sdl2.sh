#!/bin/bash
# SDL 2.26.0 with the RISC OS OSMesa GL context. If you build SDL from your
# own overlay (e.g. the OpenTTD buildkit), apply patches/sdl2/*.patch AFTER
# your own patches and add --enable-video-riscos-osmesa to configure.
set -euo pipefail
source "$(dirname "$0")/env.sh"
HERE=$(cd "$(dirname "$0")/.." && pwd)
cd "$SRC"
if [ ! -d SDL-release-2.26.0 ]; then
  curl -fsSL https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-2.26.0 | tar xz
  (cd SDL-release-2.26.0 && patch -p1 < "$HERE/patches/sdl2/sdl2-riscos-osmesa.patch" && ./autogen.sh)
fi
mkdir -p SDL-release-2.26.0/build-ro && cd SDL-release-2.26.0/build-ro
CFLAGS="$RO_CFLAGS -I$STAGE/include" LDFLAGS="-L$STAGE/lib" \
  ../configure --host=$HOST --prefix="$STAGE" --disable-shared --enable-static \
  --enable-video-riscos-osmesa --disable-video-opengles --disable-video-rpi \
  --disable-video-x11 --disable-video-wayland --disable-video-kmsdrm
grep -q "define SDL_VIDEO_OPENGL_OSMESA 1" include/SDL_config.h || { echo "OSMesa GL NOT enabled"; exit 1; }
grep -q "define SDL_VIDEO_RENDER_OGL 1" include/SDL_config.h && { echo "SDL GL renderer enabled - must stay off"; exit 1; }
make -j"$(nproc)" && make install
