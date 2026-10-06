#!/bin/bash
# Cross-build Mesa 20.3.5 classic OSMesa as ONE static libOSMesa.a for RISC OS.
set -euo pipefail
source "$(dirname "$0")/env.sh"
HERE=$(cd "$(dirname "$0")/.." && pwd)
V=$MESA_V
cd "$SRC"
if [ ! -d mesa-$V ]; then
  fetch_verified "$MESA_URL" "$MESA_SHA256" mesa-$V.tgz
  tar xzf mesa-$V.tgz
  mv mesa-mirror-mesa-$V mesa-$V
  : > mesa-$V/.riscos-patches-applied
fi
cd mesa-$V
# The port, then the speed-ups, then a workaround for newer host GCCs
# (see patches/mesa/README), in this order. .riscos-patches-applied in the
# tree records which are in, so each is applied once, and a new patch
# added to the end of the list is applied to an existing tree. (A patch
# that is changed isn't re-applied: remove src/mesa-20.3.5 to start
# again, or rebase with tools/mesa-branch.sh.)
PATCHES="riscos riscos-speed riscos-glsl-decode riscos-glsl-batch gcc13-vectorizer riscos-startup riscos-size-limit riscos-span-speed riscos-direct-rows riscos-uncompressed riscos-fast-tex riscos-fast-fog riscos-fastest riscos-eglimage riscos-osmesa-buffers riscos-push-flush riscos-fog-span riscos-audit-fixes riscos-exact-speed riscos-glsl-wmask riscos-glsl-neon riscos-es-extras"
STAMP=.riscos-patches-applied
if [ ! -f $STAMP ]; then
  echo "src/mesa-$V has no $STAMP (made by a much older build script):" >&2
  echo "remove it and run this again to start from a fresh tree." >&2
  exit 1
fi
for p in $PATCHES; do
  grep -qx "$p" $STAMP && continue
  patch -p1 < "$HERE/patches/mesa/mesa-$V-$p.patch"
  echo $p >> $STAMP
done
[ -n "${SOURCES_ONLY:-}" ] && exit 0      # tests/host-setup.sh: the patched source is all it needs
flags=$(printf "'%s', " $RO_CFLAGS); flags=${flags%, }        # meson list: '-O3', '-mtune=...', ...
sed "s#@GCCSDK_ENV@#$GCCSDK_ENV#g; s#@RO_CFLAGS_LIST@#$flags#g" "$HERE/build/meson-riscos.txt.in" > riscos-cross.txt
fresh_build_dir build-ro
[ -f build-ro/build.ninja ] || meson setup build-ro --cross-file riscos-cross.txt \
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
