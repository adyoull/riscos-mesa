#!/bin/bash
# The Raspberry Pi hello_triangle and hello_triangle2 examples (ports/hello_pi,
# from raspberrypi/userland, BSD) rebuilt against riscos-mesa's EGL, OpenGL ES
# and DispmanX compatibility library -> $STAGE/hello_pi/!HelloTriangle and
# !HelloTriangle2. They are built with the Pi's own link line (plus
# -lOSMesa -lstdc++ -lz -lm) to show that existing Pi code ports unchanged.
# Needs build-mesa.sh and build-egl.sh first.
set -euo pipefail
source "$(dirname "$0")/env.sh"
P=$(cd "$(dirname "$0")/../ports/hello_pi" && pwd)
O="$STAGE/hello_pi"
GL="-I$STAGE/include -L$STAGE/lib -I$P/libs/revision"
PILIBS="-lbcm_host -lEGL -lGLESv2 -lvcos -lvchiq_arm -lOSMesa -lstdc++ -lz -lm"
rm -rf "$O"
mkdir -p "$O"
cp -r "$P/riscos/!HelloTriangle" "$P/riscos/!HelloTriangle2" "$O/"
$CC $RO_CFLAGS $GL -static "$P/hello_triangle/triangle.c" "$P/libs/revision/revision.c" \
    -o "$O/!HelloTriangle/!RunImage,e1f" $PILIBS
$CC $RO_CFLAGS $GL -static "$P/hello_triangle2/triangle2.c" "$P/libs/revision/revision.c" \
    -o "$O/!HelloTriangle2/!RunImage,e1f" $PILIBS
$STRIP "$O/!HelloTriangle/!RunImage,e1f" "$O/!HelloTriangle2/!RunImage,e1f"
# Textures (raw RGB data, filetype Data); RISC OS unzips name.raw as name/raw,
# which is what UnixLib opens for "name.raw".
for t in Djenne Gaudi Lucca; do
  cp "$P/hello_triangle/${t}_128_128.raw" "$O/!HelloTriangle/${t}_128_128.raw,ffd"
done
cp "$P/LICENCE-userland" "$O/!HelloTriangle/Licence,fff"
cp "$P/LICENCE-userland" "$O/!HelloTriangle2/Licence,fff"
ls -la "$O"/*
