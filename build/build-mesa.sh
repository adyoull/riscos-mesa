#!/bin/bash
# Cross-build Mesa 20.3.5 classic OSMesa as ONE static libOSMesa.a for RISC OS.
set -euo pipefail
source "$(dirname "$0")/env.sh"
HERE=$(cd "$(dirname "$0")/.." && pwd)
V=20.3.5
cd "$SRC"
if [ ! -d mesa-$V ]; then
  # freedesktop.org is often unreachable from build boxes; this GitHub mirror
  # carries the release tags. Swap for archive.mesa3d.org if you prefer.
  curl -fsSL https://codeload.github.com/chaotic-cx/mesa-mirror/tar.gz/refs/tags/mesa-$V | tar xz
  mv mesa-mirror-mesa-$V mesa-$V
  (cd mesa-$V && patch -p1 < "$HERE/patches/mesa/mesa-$V-riscos.patch")
fi
cd mesa-$V
sed "s#@GCCSDK_ENV@#$GCCSDK_ENV#g" "$HERE/build/meson-riscos.txt.in" > riscos-cross.txt
[ -d build-ro ] || meson setup build-ro --cross-file riscos-cross.txt \
  --prefix="$STAGE" -Ddefault_library=static \
  -Dosmesa=classic -Ddri-drivers= -Dgallium-drivers= -Dvulkan-drivers= -Dplatforms= \
  -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dgles1=disabled -Dgles2=disabled \
  -Dllvm=disabled -Dshader-cache=disabled -Dzstd=disabled -Dlibunwind=disabled \
  -Dvalgrind=disabled -Ddri3=disabled -Dglvnd=false -Dbuild-tests=false \
  -Dshared-glapi=disabled -Dselinux=false -Dosmesa-bits=8 -Dbuildtype=release
ninja -C build-ro src/mesa/drivers/osmesa/libOSMesa.a

# Meson doesn't fold the internal static libs into libOSMesa.a; do it here so
# consumers only need: -lOSMesa -lstdc++ -lz -lpthread -lm
cd build-ro
LIBS=$(ninja -t query src/mesa/drivers/osmesa/libOSMesa.a | sed -n '/input:/,/outputs:/p' \
       | grep -oE '[^ |]+\.a$' | grep -v 'libOSMesa.a' || true)
# Fall back to the known list if query output format differs.
# (libglapi_static is link_whole, so meson already put it inside libOSMesa.a.)
[ -n "$LIBS" ] || LIBS="src/compiler/glsl/glcpp/libglcpp.a src/compiler/glsl/libglsl.a
  src/compiler/libcompiler.a src/compiler/nir/libnir.a
  src/mesa/libmesa_classic.a src/mesa/libmesa_common.a src/util/format/libmesa_format.a
  src/util/libmesa_util.a"
rm -f libOSMesa-full.a
{ echo "CREATE libOSMesa-full.a"; echo "ADDLIB src/mesa/drivers/osmesa/libOSMesa.a"
  for a in $LIBS; do echo "ADDLIB $a"; done; echo SAVE; echo END; } | $AR -M
$RANLIB libOSMesa-full.a
mkdir -p "$STAGE/lib" "$STAGE/include/GL" "$STAGE/include/KHR"
cp libOSMesa-full.a "$STAGE/lib/libOSMesa.a"
cp ../include/GL/gl.h ../include/GL/glext.h ../include/GL/osmesa.h "$STAGE/include/GL/"
cp ../include/KHR/khrplatform.h "$STAGE/include/KHR/"
echo "OK: $STAGE/lib/libOSMesa.a ($(du -h "$STAGE/lib/libOSMesa.a" | cut -f1))"
