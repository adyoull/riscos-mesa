# Source this. Point GCCSDK_ENV at the GCCSDK GCC 10 install (the directory
# containing bin/arm-riscos-gnueabihf-gcc), e.g. from the OpenTTD buildkit.
: "${GCCSDK_ENV:=$HOME/gccsdk/env}"
: "${STAGE:=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/stage}"   # install prefix for our libs
: "${SRC:=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/src}"      # downloaded sources
export GCCSDK_ENV STAGE SRC
export PATH="$GCCSDK_ENV/bin:$PATH"
export HOST=arm-riscos-gnueabihf
export CC=$HOST-gcc CXX=$HOST-g++ AR=$HOST-ar RANLIB=$HOST-ranlib STRIP=$HOST-strip
# VFP but no NEON: GCCSDK README reports NEON builds can die with SIGEMT.
export RO_CFLAGS="-O2 -mfpu=vfpv3 -mfloat-abi=hard"
# Let meson/configure find our libs (zlib etc.) and nothing from the build host.
export PKG_CONFIG_LIBDIR="$STAGE/lib/pkgconfig:$STAGE/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=
mkdir -p "$STAGE" "$SRC"

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
