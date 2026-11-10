#!/bin/bash
# zlib ($ZLIB_V in env.sh) static (Mesa needs crc32). Skip if your GCCSDK env already has libz.
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$SRC"
if [ ! -d zlib-$ZLIB_V ]; then
  fetch_verified "$ZLIB_URL" "$ZLIB_SHA256" zlib-$ZLIB_V.tgz
  tar xzf zlib-$ZLIB_V.tgz
fi
# The RO_PIC build compiles in its own copy of the tree (configure and make
# work in the source directory)
if [ -n "$RO_SUFFIX" ] && [ ! -d zlib-$ZLIB_V$RO_SUFFIX ]; then
  cp -a zlib-$ZLIB_V zlib-$ZLIB_V$RO_SUFFIX
  make -C zlib-$ZLIB_V$RO_SUFFIX distclean >/dev/null 2>&1 || true
fi
cd zlib-$ZLIB_V$RO_SUFFIX
CHOST=$HOST CFLAGS="$RO_CFLAGS" ./configure --static --prefix="$STAGE"
make -j"$(nproc)" libz.a && make install
