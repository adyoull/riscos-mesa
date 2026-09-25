#!/bin/bash
# EGL 1.4 for RISC OS over OSMesa (egl/) -> $STAGE/lib/libEGL.a and
# $STAGE/include/EGL. Needs build-mesa.sh first (GL/osmesa.h, libOSMesa.a).
# Apps link: -lEGL -lOSMesa -lstdc++ -lz -lm   (EGL before OSMesa)
set -euo pipefail
source "$(dirname "$0")/env.sh"
E=$(cd "$(dirname "$0")/../egl" && pwd)
B=$(mktemp -d)
$CC $RO_CFLAGS -Wall -I"$E/include" -I"$STAGE/include" -c "$E/egl_riscos.c" -o "$B/egl_riscos.o"
rm -f "$STAGE/lib/libEGL.a"
$AR rcs "$STAGE/lib/libEGL.a" "$B/egl_riscos.o"
rm -rf "$B"
mkdir -p "$STAGE/include/EGL"
cp "$E"/include/EGL/*.h "$STAGE/include/EGL/"
ls -la "$STAGE/lib/libEGL.a" "$STAGE/include/EGL"
