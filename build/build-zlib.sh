#!/bin/bash
# zlib 1.3.1 static (Mesa needs crc32). Skip if your GCCSDK env already has libz.
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$SRC"
[ -d zlib-1.3.1 ] || curl -fsSL https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.3.1 | tar xz
cd zlib-1.3.1
CHOST=$HOST CFLAGS="$RO_CFLAGS" ./configure --static --prefix="$STAGE"
make -j"$(nproc)" libz.a && make install
