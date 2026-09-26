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
  fetch_verified https://codeload.github.com/chaotic-cx/mesa-mirror/tar.gz/refs/tags/mesa-$V \
    adabbe0161cd8db4f1935fca9e07b7ef86219951a2ac830586de149c1753b828 mesa-$V.tgz
  tar xzf mesa-$V.tgz
  mv mesa-mirror-mesa-$V mesa-$V
fi
cd mesa-$V
# The port, then the speed-ups, then a workaround for newer host GCCs
# (see patches/mesa/README). Each is applied once: a tree extracted by an
# earlier version of this script gets the ones it lacks.
for p in riscos riscos-speed riscos-glsl-decode gcc13-vectorizer; do
  f="$HERE/patches/mesa/mesa-$V-$p.patch"
  if ! patch -p1 -R -s -f --dry-run < "$f" >/dev/null 2>&1; then
    patch -p1 < "$f"
  fi
done
sed "s#@GCCSDK_ENV@#$GCCSDK_ENV#g" "$HERE/build/meson-riscos.txt.in" > riscos-cross.txt
[ -d build-ro ] || meson setup build-ro --cross-file riscos-cross.txt \
  --prefix="$STAGE" -Ddefault_library=static \
  -Dosmesa=classic -Ddri-drivers= -Dgallium-drivers= -Dvulkan-drivers= -Dplatforms= \
  -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dgles1=disabled -Dgles2=disabled \
  -Dllvm=disabled -Dshader-cache=disabled -Dzstd=disabled -Dlibunwind=disabled \
  -Dvalgrind=disabled -Ddri3=disabled -Dglvnd=false -Dbuild-tests=false \
  -Dshared-glapi=disabled -Dselinux=false -Dosmesa-bits=8 -Dbuildtype=release \
  -Db_staticpic=false
# Not position-independent: GCCSDK's -fPIC code reaches every global through
# the RISC OS shared library tables (&8038), which a static library linked
# into a program doesn't need. (Also sets it on build dirs made before this.)
meson configure build-ro -Db_staticpic=false
# The internal libraries Mesa's shared libOSMesa would link. A static
# library doesn't pull its link_with deps into the build, so name them all.
# (libglapi_static is link_whole, so meson already put it inside libOSMesa.a.)
LIBS="src/compiler/glsl/glcpp/libglcpp.a src/compiler/glsl/libglsl.a
  src/compiler/libcompiler.a src/compiler/nir/libnir.a
  src/mesa/libmesa_classic.a src/mesa/libmesa_common.a
  src/util/format/libmesa_format.a src/util/libmesa_util.a"
ninja -C build-ro src/mesa/drivers/osmesa/libOSMesa.a $LIBS

# Fold everything into ONE libOSMesa.a so consumers only need:
#   -lOSMesa -lstdc++ -lz -lm
cd build-ro
rm -f libOSMesa-full.a
{ echo "CREATE libOSMesa-full.a"; echo "ADDLIB src/mesa/drivers/osmesa/libOSMesa.a"
  for a in $LIBS; do echo "ADDLIB $a"; done; echo SAVE; echo END; } | $AR -M
$RANLIB libOSMesa-full.a
mkdir -p "$STAGE/lib" "$STAGE/include/GL" "$STAGE/include/KHR"
cp libOSMesa-full.a "$STAGE/lib/libOSMesa.a"
cp ../include/GL/gl.h ../include/GL/glext.h ../include/GL/osmesa.h "$STAGE/include/GL/"
cp ../include/KHR/khrplatform.h "$STAGE/include/KHR/"
# OpenGL ES 1.1 and 2.0 (OSMESA_ES1_PROFILE / OSMESA_ES2_PROFILE, our patch):
# the entry points are already in libOSMesa.
mkdir -p "$STAGE/include/GLES" "$STAGE/include/GLES2"
cp ../include/GLES/gl.h ../include/GLES/glext.h ../include/GLES/glplatform.h "$STAGE/include/GLES/"
cp ../include/GLES2/gl2.h ../include/GLES2/gl2ext.h ../include/GLES2/gl2platform.h "$STAGE/include/GLES2/"
echo "OK: $STAGE/lib/libOSMesa.a ($(du -h "$STAGE/lib/libOSMesa.a" | cut -f1))"
