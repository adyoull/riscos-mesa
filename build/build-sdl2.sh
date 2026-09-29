#!/bin/bash
# SDL 2.26.0 + the RISC OS overlay (patches/sdl2/*.p, the authoritative
# copy; riscos-openttd takes its copy from here with
# tools/sdl-overlay-export.sh). The same .p files drop into the GCCSDK
# autobuilder libsdl2 recipe. We build WITH --enable-video-riscos-osmesa
# (GL via OSMesa); riscos-openttd builds the same files without it (no GL).
set -euo pipefail
source "$(dirname "$0")/env.sh"
HERE=$(cd "$(dirname "$0")/.." && pwd)
cd "$SRC"
# The overlay's fingerprint: tells whether patches/sdl2 changed since the
# tree was made.
overlay_sum() { cat "$HERE"/patches/sdl2/*.p | sha256sum | cut -d' ' -f1; }
if [ ! -d SDL-release-$SDL_V ]; then
  fetch_verified "$SDL_URL" "$SDL_SHA256" SDL-$SDL_V.tgz
  tar xzf SDL-$SDL_V.tgz
  (cd SDL-release-$SDL_V && for p in "$HERE"/patches/sdl2/*.p; do patch -s -p0 < "$p"; done && ./autogen.sh)
  overlay_sum > SDL-release-$SDL_V/.overlay-sum
elif [ "$(cat SDL-release-$SDL_V/.overlay-sum 2>/dev/null)" != "$(overlay_sum)" ]; then
  # patches/sdl2 changed (a git pull, say) since this tree was made. Edits
  # made in the tree itself are fine (that's where the overlay is edited),
  # but the tree must now match the .p files, or it would build old code.
  if "$HERE"/tools/sdl-overlay-regen.sh --check; then
    overlay_sum > SDL-release-$SDL_V/.overlay-sum
  else
    echo "src/SDL-release-$SDL_V doesn't match patches/sdl2: remove it to build the"
    echo "overlay as it is now, or run tools/sdl-overlay-regen.sh to keep the tree's edits."
    exit 1
  fi
fi
[ -n "${SOURCES_ONLY:-}" ] && exit 0      # tests/host-setup.sh: the patched source is all it needs
fresh_build_dir SDL-release-$SDL_V/build-ro && cd SDL-release-$SDL_V/build-ro
# -I egl/include: SDL's GL windows are EGL window surfaces (programs then
# link -lEGL too: -lSDL2 -lGLU -lEGL -lOSMesa ...)
CFLAGS="$RO_CFLAGS -I$HERE/egl/include -I$STAGE/include" LDFLAGS="-L$STAGE/lib" \
  ../configure --host=$HOST --prefix="$STAGE" --disable-shared --enable-static \
  --enable-video-riscos-osmesa --disable-video-opengles --disable-video-rpi
grep -q "define SDL_VIDEO_OPENGL_OSMESA 1" include/SDL_config.h || { echo "OSMesa GL NOT enabled"; exit 1; }
grep -q "define SDL_VIDEO_RENDER_OGL 1" include/SDL_config.h && { echo "SDL GL renderer enabled - must stay off"; exit 1; }
make -j"$(nproc)" && make install
