#!/bin/bash
# Release zips for RISC OS. Filetypes go in each entry's Acorn extra field
# (tools/mkrozip.py), so SparkFS / the RISC OS unzip give the files their
# real types and plain names; NAME,xxx files in stage/ become NAME + type.
#   build/package.sh VERSION    -> dist/riscos-mesa-tests-VERSION.zip
#                                  dist/riscos-mesa-hello_pi-VERSION.zip
#                                  dist/riscos-mesa-ports-VERSION.zip
#                                  dist/riscos-mesa-glut-VERSION.zip
#                                  dist/riscos-mesa-devkit-VERSION.tgz
#                                  dist/riscos-mesa-examples-VERSION.zip
set -euo pipefail
source "$(dirname "$0")/env.sh"
HERE=$(cd "$(dirname "$0")/.." && pwd)
V=${1:?usage: package.sh VERSION}
mkdir -p "$HERE/dist"
TMP=$(mktemp -d)
cp -r "$STAGE/tests" "$TMP/riscos-mesa-tests"
cp "$HERE/LICENCES.txt" "$TMP/riscos-mesa-tests/Licences,fff"
# The ReadMe's first line names the release: stamp it with this one
[ -f "$TMP/riscos-mesa-tests/ReadMe,fff" ] &&
  sed -i "1s/^riscos-mesa [^ ]* /riscos-mesa $V /" "$TMP/riscos-mesa-tests/ReadMe,fff"
rm -f "$HERE/dist/riscos-mesa-tests-$V.zip"
( cd "$TMP" && python3 "$HERE/tools/mkrozip.py" "$HERE/dist/riscos-mesa-tests-$V.zip" riscos-mesa-tests )
rm -rf "$TMP"
# The Raspberry Pi examples rebuilt from source (build-hello-pi.sh), in a
# zip of their own: -> dist/riscos-mesa-hello_pi-VERSION.zip
if [ -d "$STAGE/hello_pi" ]; then
  TMP=$(mktemp -d)
  cp -r "$STAGE/hello_pi" "$TMP/riscos-mesa-hello_pi"
  rm -f "$HERE/dist/riscos-mesa-hello_pi-$V.zip"
  ( cd "$TMP" && python3 "$HERE/tools/mkrozip.py" "$HERE/dist/riscos-mesa-hello_pi-$V.zip" riscos-mesa-hello_pi )
  rm -rf "$TMP"
fi
# Ported example programs (build-ports.sh): Mesa's EGL demos and SDL's GL
# tests. The ES 2.0 book samples have no licence, so they stay out.
if [ -d "$STAGE/ports" ]; then
  TMP=$(mktemp -d)
  mkdir -p "$TMP/riscos-mesa-ports"
  for d in mesa-demos sdl2-tests; do
    [ -d "$STAGE/ports/$d" ] && cp -r "$STAGE/ports/$d" "$TMP/riscos-mesa-ports/$d"
  done
  rm -f "$HERE/dist/riscos-mesa-ports-$V.zip"
  ( cd "$TMP" && python3 "$HERE/tools/mkrozip.py" "$HERE/dist/riscos-mesa-ports-$V.zip" riscos-mesa-ports )
  rm -rf "$TMP"
fi
# freeglut's demos over our freeglut (build-ports.sh), in a zip of their own:
# -> dist/riscos-mesa-glut-VERSION.zip
if [ -d "$STAGE/ports/freeglut" ]; then
  TMP=$(mktemp -d)
  cp -r "$STAGE/ports/freeglut" "$TMP/riscos-mesa-glut"
  rm -f "$HERE/dist/riscos-mesa-glut-$V.zip"
  ( cd "$TMP" && python3 "$HERE/tools/mkrozip.py" "$HERE/dist/riscos-mesa-glut-$V.zip" riscos-mesa-glut )
  rm -rf "$TMP"
fi
# The devkit: libraries and headers, plus the beginner's guide, the
# examples (devkit/examples), sdl2-config, pkg-config files and mkrozip.
TMP=$(mktemp -d); K=$TMP/riscos-mesa-devkit-$V
mkdir -p "$K/lib/pkgconfig" "$K/bin"
for l in libOSMesa libGLU libSDL2 libSDL2main libz libEGL libbcm_host libGLESv2 libGLESv1_CM \
         libvcos libvchiq_arm libglut libfreeglut-gles libopenal; do
  cp "$STAGE/lib/$l.a" "$K/lib/"
done
cp -r "$STAGE/include" "$K/include"
cp "$HERE/LICENCES.txt" "$HERE/devkit/README.md" "$K/"
cp "$HERE/devkit/README.md" "$K/ReadMe,fff"            # the same guide, for RISC OS
cp "$HERE/devkit/pkgconfig/"*.pc "$K/lib/pkgconfig/"
cp "$HERE/devkit/bin/sdl2-config" "$HERE/tools/mkrozip.py" "$K/bin/"   # one mkrozip, in tools/
cp -r "$HERE/devkit/examples" "$K/examples"
cp -r "$HERE/devkit/riscos" "$K/riscos"              # PThreadTicker (UnixLib 5.0.1)
mkdir -p "$K/docs/porting"
cp "$HERE/docs/EGL-GUIDE.md" "$K/docs/"
cp "$HERE/docs/porting/"*.md "$K/docs/porting/"
cp "$HERE/devkit/docs-README.md" "$K/docs/README.md"
rm -rf "$K/examples/build"
echo "riscos-mesa devkit $V" > "$K/VERSION"
rm -f "$HERE/dist/riscos-mesa-devkit-$V.tgz"
( cd "$TMP" && tar czf "$HERE/dist/riscos-mesa-devkit-$V.tgz" "riscos-mesa-devkit-$V" )
# The examples, built from the packaged devkit exactly as a user would
# (make zip): -> dist/riscos-mesa-examples-VERSION.zip
make -s -C "$K/examples" GCCSDK_ENV="$GCCSDK_ENV" zip
cp "$K/examples/build/examples.zip" "$HERE/dist/riscos-mesa-examples-$V.zip"
rm -rf "$TMP"
ls -la "$HERE/dist"
