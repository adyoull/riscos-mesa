# Source this. Point GCCSDK_ENV at the GCCSDK GCC 10 install (the directory
# containing bin/arm-riscos-gnueabihf-gcc), e.g. from the OpenTTD buildkit.
: "${GCCSDK_ENV:=$HOME/gccsdk/env}"
: "${STAGE:=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/stage}"   # install prefix for our libs
: "${SRC:=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/src}"      # downloaded sources
export GCCSDK_ENV STAGE SRC
export PATH="$GCCSDK_ENV/bin:$PATH"
export HOST=arm-riscos-gnueabihf
export CC=$HOST-gcc CXX=$HOST-g++ AR=$HOST-ar RANLIB=$HOST-ranlib STRIP=$HOST-strip
# -fstack-clash-protection is REQUIRED: ARMEABISupport maps the 1 MB stack
# a page at a time as the guard page below it is touched, so any function
# with a frame > 4 KB (Mesa has 19, up to 135 KB) must probe page by page or
# it jumps past the guard page and dies with "abort on data transfer"/SIGEMT.
# (The GCCSDK "NEON builds die with SIGEMT" warning is most likely this.)
# Flags chosen by benchmark on a Pi 4 (glbench): -O3 tuned for Cortex-A72;
# NEON added nothing. VFPv3, not VFPv4 (2026-09-28): the only VFPv4
# instruction GCC used was fused multiply-add, in 2 of Mesa's 18557
# functions (GLSL constant folding), and an interleaved A/B on the Pi 4
# measured every glbench scene within 0.5%. VFPv3 also runs on Cortex-A8/A9
# machines (BeagleBoard-xM, PandaBoard, ARMini, i.MX6); RO_FPU=vfpv4 gives
# the old Pi 2-and-later build.
: "${RO_FPU:=vfpv3}"
export RO_FPU
export RO_CFLAGS="-O3 -mtune=cortex-a72 -mfpu=$RO_FPU -mfloat-abi=hard -fstack-clash-protection"
# Let meson/configure find our libs (zlib etc.) and nothing from the build host.
export PKG_CONFIG_LIBDIR="$STAGE/lib/pkgconfig:$STAGE/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=
mkdir -p "$STAGE" "$SRC"

# Build directories remember the FPU they were configured for: autotools and
# meson don't notice a flags change, so a different RO_FPU starts afresh.
fresh_build_dir() {   # dir
  if [ -d "$1" ] && [ "$(cat "$1/.ro-fpu" 2>/dev/null)" != "$RO_FPU" ]; then
    echo "$1: built for another FPU (or before RO_FPU existed), starting afresh"
    rm -rf "$1"
  fi
  mkdir -p "$1"
  echo "$RO_FPU" > "$1/.ro-fpu"
}

# Pinned sources: each downloaded tarball's version, URL and SHA-256, in one
# place (the build scripts and tools/mesa-branch.sh, sdl-overlay-regen.sh
# use these). freedesktop.org is often unreachable from build boxes, so
# Mesa comes from a GitHub mirror that carries the release tags; swap for
# archive.mesa3d.org if you prefer (the hash is of the mirror's archive).
MESA_V=20.3.5
MESA_URL=https://codeload.github.com/chaotic-cx/mesa-mirror/tar.gz/refs/tags/mesa-$MESA_V
MESA_SHA256=adabbe0161cd8db4f1935fca9e07b7ef86219951a2ac830586de149c1753b828
SDL_V=2.26.0
SDL_URL=https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-$SDL_V
SDL_SHA256=f38367892a6f243e8b4010e9e3d9714dc848e11b1a3f69c4af514dc6e7aca7f0
ZLIB_V=1.3.1
ZLIB_URL=https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v$ZLIB_V
ZLIB_SHA256=17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c
OPENAL_V=1.19.1
OPENAL_URL=https://codeload.github.com/kcat/openal-soft/tar.gz/refs/tags/openal-soft-$OPENAL_V
OPENAL_URL2=http://archive.ubuntu.com/ubuntu/pool/universe/o/openal-soft/openal-soft_$OPENAL_V.orig.tar.gz
OPENAL_SHA256=9f3536ab2bb7781dbafabc6a61e0b34b17edd16bd6c2eaf2ae71bc63078f98c7

# Download a source tarball and refuse to use it unless its SHA-256 matches.
# The Mesa/SDL/zlib tarballs are GitHub-generated archives; GitHub has very
# occasionally changed those bytes. If a check fails, compare the unpacked
# tree with the upstream release before updating the pinned hash.
fetch_verified() {   # url sha256 outfile
  local url=$1 sum=$2 out=$3
  if [ ! -f "$out" ]; then
    curl -fsSL "$url" -o "$out.part" || { rm -f "$out.part"; return 1; }
    mv "$out.part" "$out"
  fi
  if ! echo "$sum  $out" | sha256sum -c --status; then
    echo "CHECKSUM MISMATCH for $out (from $url)" >&2
    echo "  expected $sum" >&2
    echo "  got      $(sha256sum "$out" | cut -d' ' -f1)" >&2
    rm -f "$out"; return 1
  fi
}
