#!/bin/bash
# zlib 1.3.1 static (Mesa needs crc32). Skip if your GCCSDK env already has libz.
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$SRC"
if [ ! -d zlib-1.3.1 ]; then
  fetch_verified https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.3.1 \
    17e88863f3600672ab49182f217281b6fc4d3c762bde361935e436a95214d05c zlib-1.3.1.tgz
  tar xzf zlib-1.3.1.tgz
fi
cd zlib-1.3.1
CHOST=$HOST CFLAGS="$RO_CFLAGS" ./configure --static --prefix="$STAGE"
make -j"$(nproc)" libz.a && make install
