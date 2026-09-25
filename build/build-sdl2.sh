#!/bin/bash
# SDL 2.26.0 + the shared RISC OS overlay (patches/sdl2/*.p: OpenTTD Wimp
# driver, fix 13, OSMesa OpenGL). The same .p files drop into the GCCSDK
# autobuilder libsdl2 recipe; add --enable-video-riscos-osmesa for GL.
set -euo pipefail
source "$(dirname "$0")/env.sh"
HERE=$(cd "$(dirname "$0")/.." && pwd)
cd "$SRC"
if [ ! -d SDL-release-2.26.0 ]; then
  fetch_verified https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-2.26.0 \
    f38367892a6f243e8b4010e9e3d9714dc848e11b1a3f69c4af514dc6e7aca7f0 SDL-2.26.0.tgz
  tar xzf SDL-2.26.0.tgz
  (cd SDL-release-2.26.0 && for p in "$HERE"/patches/sdl2/*.p; do patch -s -p0 < "$p"; done && ./autogen.sh)
fi
mkdir -p SDL-release-2.26.0/build-ro && cd SDL-release-2.26.0/build-ro
CFLAGS="$RO_CFLAGS -I$STAGE/include" LDFLAGS="-L$STAGE/lib" \
  ../configure --host=$HOST --prefix="$STAGE" --disable-shared --enable-static \
  --enable-video-riscos-osmesa --disable-video-opengles --disable-video-rpi
grep -q "define SDL_VIDEO_OPENGL_OSMESA 1" include/SDL_config.h || { echo "OSMesa GL NOT enabled"; exit 1; }
grep -q "define SDL_VIDEO_RENDER_OGL 1" include/SDL_config.h && { echo "SDL GL renderer enabled - must stay off"; exit 1; }
make -j"$(nproc)" && make install
