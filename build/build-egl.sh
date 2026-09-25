#!/bin/bash
# EGL 1.4 for RISC OS over OSMesa (egl/) -> $STAGE/lib/libEGL.a and
# $STAGE/include/EGL, plus the DispmanX compatibility library (dispmanx/)
# -> $STAGE/lib/libbcm_host.a, its headers, and empty libGLESv2,
# libGLESv1_CM, libvcos and libvchiq_arm so Raspberry Pi link lines work.
# Needs build-mesa.sh first (GL/osmesa.h, libOSMesa.a).
# Apps link: -lEGL -lOSMesa -lstdc++ -lz -lm   (EGL before OSMesa)
# Pi (DispmanX) code: -lbcm_host -lEGL -lOSMesa -lstdc++ -lz -lm
set -euo pipefail
source "$(dirname "$0")/env.sh"
E=$(cd "$(dirname "$0")/../egl" && pwd)
X=$(cd "$(dirname "$0")/../dispmanx" && pwd)
B=$(mktemp -d)
$CC $RO_CFLAGS -Wall -I"$E/include" -I"$X" -I"$STAGE/include" -c "$E/egl_riscos.c" -o "$B/egl_riscos.o"
$CC $RO_CFLAGS -Wall -I"$X/include" -I"$X" -I"$E/include" -I"$STAGE/include" -c "$X/bcm_host.c" -o "$B/bcm_host.o"
echo "/* intentionally empty: see riscos-mesa dispmanx/ */" > "$B/empty.c"
$CC $RO_CFLAGS -c "$B/empty.c" -o "$B/empty.o"
rm -f "$STAGE/lib/libEGL.a" "$STAGE/lib/libbcm_host.a"
$AR rcs "$STAGE/lib/libEGL.a" "$B/egl_riscos.o"
$AR rcs "$STAGE/lib/libbcm_host.a" "$B/bcm_host.o"
for l in GLESv2 GLESv1_CM vcos vchiq_arm; do
  rm -f "$STAGE/lib/lib$l.a"
  $AR rcs "$STAGE/lib/lib$l.a" "$B/empty.o"
done
rm -rf "$B"
mkdir -p "$STAGE/include/EGL" "$STAGE/include/interface/vmcs_host" "$STAGE/include/interface/vctypes" \
  "$STAGE/include/interface/vcos"
cp "$E"/include/EGL/*.h "$STAGE/include/EGL/"
cp "$X/include/bcm_host.h" "$STAGE/include/"
cp "$X/include/EGL/eglext_brcm.h" "$STAGE/include/EGL/"
cp "$X"/include/interface/vmcs_host/*.h "$STAGE/include/interface/vmcs_host/"
cp "$X"/include/interface/vctypes/*.h "$STAGE/include/interface/vctypes/"
cp "$X"/include/interface/vcos/*.h "$STAGE/include/interface/vcos/"
ls -la "$STAGE/lib/libEGL.a" "$STAGE/lib/libbcm_host.a"
