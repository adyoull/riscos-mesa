#!/bin/bash
# zlib 1.3.1 static (Mesa needs crc32). Skip if your GCCSDK env already has libz.
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$SRC"
if [ ! -d zlib-1.3.1 ]; then
  fetch_verified "$ZLIB_URL" "$ZLIB_SHA256" zlib-$ZLIB_V.tgz
  tar xzf zlib-1.3.1.tgz
fi
cd zlib-1.3.1
CHOST=$HOST CFLAGS="$RO_CFLAGS" ./configure --static --prefix="$STAGE"
make -j"$(nproc)" libz.a && make install
