#!/bin/bash
# zlib ($ZLIB_V in env.sh) static (Mesa needs crc32). Skip if your GCCSDK env already has libz.
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$SRC"
if [ ! -d zlib-$ZLIB_V ]; then
  fetch_verified "$ZLIB_URL" "$ZLIB_SHA256" zlib-$ZLIB_V.tgz
  tar xzf zlib-$ZLIB_V.tgz
fi
cd zlib-$ZLIB_V
CHOST=$HOST CFLAGS="$RO_CFLAGS" ./configure --static --prefix="$STAGE"
make -j"$(nproc)" libz.a && make install
