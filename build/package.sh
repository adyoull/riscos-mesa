#!/bin/bash
# Release zips for RISC OS. Filetypes go in each entry's Acorn extra field
# (tools/mkrozip.py), so SparkFS / the RISC OS unzip give the files their
# real types and plain names; NAME,xxx files in stage/ become NAME + type.
#   build/package.sh VERSION    -> dist/riscos-mesa-tests-VERSION.zip
#                                  dist/riscos-mesa-devkit-VERSION.tgz
set -euo pipefail
source "$(dirname "$0")/env.sh"
HERE=$(cd "$(dirname "$0")/.." && pwd)
V=${1:?usage: package.sh VERSION}
mkdir -p "$HERE/dist"
TMP=$(mktemp -d)
cp -r "$STAGE/tests" "$TMP/riscos-mesa-tests"
cp "$HERE/LICENCES.txt" "$TMP/riscos-mesa-tests/Licences,fff"
# The Raspberry Pi examples rebuilt from source (build-hello-pi.sh)
[ -d "$STAGE/hello_pi" ] && cp -r "$STAGE/hello_pi" "$TMP/riscos-mesa-tests/hello_pi"
rm -f "$HERE/dist/riscos-mesa-tests-$V.zip"
( cd "$TMP" && python3 "$HERE/tools/mkrozip.py" "$HERE/dist/riscos-mesa-tests-$V.zip" riscos-mesa-tests )
rm -rf "$TMP"
( cd "$STAGE/.." && tar czf "$HERE/dist/riscos-mesa-devkit-$V.tgz" \
    --transform "s#^stage#riscos-mesa-devkit-$V#" stage/lib/libOSMesa.a stage/lib/libGLU.a \
    stage/lib/libSDL2.a stage/lib/libSDL2main.a stage/lib/libz.a stage/lib/libEGL.a \
    stage/lib/libbcm_host.a stage/lib/libGLESv2.a stage/lib/libGLESv1_CM.a stage/lib/libvcos.a \
    stage/lib/libvchiq_arm.a stage/include \
    --transform "s#^LICENCES.txt#riscos-mesa-devkit-$V/LICENCES.txt#" -C "$HERE" LICENCES.txt )
ls -la "$HERE/dist"
