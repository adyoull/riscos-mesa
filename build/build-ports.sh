#!/bin/bash
# Example programs ported to riscos-mesa, as worked examples for the porting
# guides in docs/porting/:
#   ports/mesa-demos  mesa-demos 9.0.0 EGL demos, unchanged, over our eglut
#                     back end (eglut_riscos.c)     -> $STAGE/ports/mesa-demos
#   ports/sdl2-tests  SDL 2.26's testgl2/testgles/testgles2 and loopwave (sound),
#                     unchanged
#                                                    -> $STAGE/ports/sdl2-tests
#   ports/esbook      the OpenGL ES 2.0 Programming Guide samples over our
#                     esUtil_RISCOS.c                -> $STAGE/ports/esbook
#   ports/freeglut    freeglut 3.8.0's demos, unchanged, over our freeglut
#                     (RISC OS back end in glut/)    -> $STAGE/ports/freeglut
# The book's samples have no licence, so they aren't in this repository or
# in release zips: they are fetched at a pinned commit and built for your
# own use (skipped if they can't be fetched).
# Needs build-mesa.sh, build-glu.sh, build-egl.sh, build-sdl2.sh and
# build-freeglut.sh first.
set -euo pipefail
source "$(dirname "$0")/env.sh"
HERE=$(cd "$(dirname "$0")/.." && pwd)
P="$HERE/ports"
OUT="$STAGE/ports"
CF="$RO_CFLAGS -D_GNU_SOURCE -I$STAGE/include -I$P/common"
EGLLIBS="-L$STAGE/lib -lEGL -lOSMesa -lstdc++ -lz -lm"
rm -rf "$OUT"
mkdir -p "$OUT"

# --- mesa-demos: eglut.c + eglut_riscos.c + riscos_wimpwin.c + the demo ---
D="$P/mesa-demos"
mkdir -p "$OUT/mesa-demos"
EGLUT="$D/upstream/eglut/eglut.c $D/eglut_riscos.c $P/common/riscos_wimpwin.c"
while read -r app src; do
  [ -n "$app" ] || continue
  cp -r "$D/riscos/!$app" "$OUT/mesa-demos/"
  extra=""
  [ "$src" = opengles2/es2gears ] && extra="$D/upstream/util/matrix.c"
  $CC $CF -I"$D/upstream/eglut" -I"$D/upstream/util" -static $EGLUT "$D/upstream/$src.c" $extra \
      -o "$OUT/mesa-demos/!$app/!RunImage,e1f" $EGLLIBS
  $STRIP "$OUT/mesa-demos/!$app/!RunImage,e1f"
  cp "$D/LICENCE-mesa-demos" "$OUT/mesa-demos/!$app/Licence,fff"
done < "$D/apps.txt"
cp "$D/riscos/ReadMe,fff" "$OUT/mesa-demos/"

# --- SDL 2.26 test programs, built as SDL's test/configure would ---
T="$SRC/SDL-release-2.26.0/test"
mkdir -p "$OUT/sdl2-tests"
for spec in "TestGL2 testgl2 HAVE_OPENGL" "TestGLES testgles HAVE_OPENGLES" "TestGLES2 testgles2 HAVE_OPENGLES2" \
            "LoopWave loopwave HAVE_AUDIO"; do
  set -- $spec
  cp -r "$P/sdl2-tests/riscos/!$1" "$OUT/sdl2-tests/"
  $CC $CF -D$3 -DOUTPUT_VAR="\"$1\$Output\"" -I"$STAGE/include/SDL2" -static "$T/$2.c" "$T/testutils.c" \
      "$P/sdl2-tests/riscos_output.c" -o "$OUT/sdl2-tests/!$1/!RunImage,e1f" \
      -L"$STAGE/lib" -lSDL2_test -lSDL2 -lOSMesa -lstdc++ -lz -lm
  $STRIP "$OUT/sdl2-tests/!$1/!RunImage,e1f"
  cp "$SRC/SDL-release-2.26.0/LICENSE.txt" "$OUT/sdl2-tests/!$1/Licence,fff"
done
cp "$T/sample.wav" "$OUT/sdl2-tests/!LoopWave/sample.wav,fb1"
cp "$P/sdl2-tests/riscos/ReadMe,fff" "$OUT/sdl2-tests/"

# --- freeglut 3.8.0's demos (progs/demos in the source build-freeglut.sh
#     fetched), unchanged; riscos_output.c sends their text to a file ---
G="$P/freeglut"; GD="$SRC/freeglut-3.8.0/progs/demos"
mkdir -p "$OUT/freeglut"
while read -r app srcs; do
  case "$app" in ''|'#'*) continue;; esac
  cp -r "$G/riscos/!$app" "$OUT/freeglut/"
  files=""; for f in $srcs; do files="$files $GD/$f"; done
  $CC $CF -DOUTPUT_VAR="\"$app\$Output\"" -static $files "$P/sdl2-tests/riscos_output.c" \
      -o "$OUT/freeglut/!$app/!RunImage,e1f" -L"$STAGE/lib" -lglut -lGLU -lEGL -lOSMesa -lstdc++ -lz -lm
  $STRIP "$OUT/freeglut/!$app/!RunImage,e1f"
  cp "$SRC/freeglut-3.8.0/COPYING" "$OUT/freeglut/!$app/Licence,fff"
done < "$G/apps.txt"
cp "$GD/Fractals/fractals.dat" "$OUT/freeglut/!Fractals/fractals.dat,fff"
cp "$G/riscos/ReadMe,fff" "$OUT/freeglut/"

# --- OpenGL ES 2.0 Programming Guide samples (fetched, not redistributed) ---
BOOK_COMMIT=604a02cc84f9cc4369f7efe93d2a1d7f2cab2ba7
B="$SRC/opengles-book-samples"
if [ ! -d "$B" ]; then
  git clone -q https://github.com/danginsburg/opengles-book-samples "$B" || true
fi
if [ -d "$B/.git" ] && git -C "$B" checkout -q "$BOOK_COMMIT"; then
  L="$B/LinuxX11"
  COMMON="$P/esbook/esUtil_RISCOS.c $L/Common/esShader.c $L/Common/esShapes.c $L/Common/esTransform.c $P/common/riscos_wimpwin.c"
  mkdir -p "$OUT/esbook"
  for s in Chapter_2/Hello_Triangle Chapter_8/Simple_VertexShader Chapter_9/Simple_Texture2D \
           Chapter_9/MipMap2D Chapter_9/Simple_TextureCubemap Chapter_9/TextureWrap \
           Chapter_10/MultiTexture Chapter_11/Multisample Chapter_11/Stencil_Test \
           Chapter_13/ParticleSystem; do
    n=$(basename "$s"); app="$OUT/esbook/!$n"
    mkdir -p "$app"
    sed "s/@APP@/$n/g" "$P/esbook/Run.template" > "$app/!Run,feb"
    $CC $CF -I"$L/Common" -static $COMMON "$L/$s/$n.c" -o "$app/!RunImage,e1f" $EGLLIBS
    $STRIP "$app/!RunImage,e1f"
    for t in "$L/$s"/*.tga; do [ -f "$t" ] && cp "$t" "$app/$(basename "$t"),ffd"; done
  done
else
  echo "Skipping the ES 2.0 book samples (couldn't fetch $BOOK_COMMIT)"
fi
find "$OUT" -maxdepth 2 -name '!*' | sort
