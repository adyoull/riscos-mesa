#!/bin/bash
# GLU 9.0.1 (SGI GLU, as shipped by Mesa) against our OSMesa.
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$SRC"
[ -d glu-9.0.1 ] || git clone -q --depth 1 --branch glu-9.0.1 https://github.com/cpp-pm/glu.git glu-9.0.1
# Pin the exact commit of the glu-9.0.1 tag (mirror of freedesktop mesa/glu)
[ "$(git -C glu-9.0.1 rev-parse HEAD)" = dd4e18eb7557a31a3c8318d6612801329877c745 ] \
  || { echo "glu-9.0.1 is not the pinned commit dd4e18eb" >&2; exit 1; }
cd glu-9.0.1
[ -x configure ] || NOCONFIGURE=1 ./autogen.sh
mkdir -p build-ro && cd build-ro
# -L/-l (not a path to the .a) so libtool doesn't copy OSMesa into libGLU.a
OSMESA_CFLAGS="-I$STAGE/include" OSMESA_LIBS="-L$STAGE/lib -lOSMesa -lstdc++ -lz -lm" \
CFLAGS="$RO_CFLAGS" CXXFLAGS="$RO_CFLAGS" \
  ../configure --host=$HOST --prefix="$STAGE" --enable-osmesa --disable-shared --enable-static
make -j"$(nproc)" && make install
