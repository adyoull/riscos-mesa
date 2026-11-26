#!/bin/bash
# The GPU library, libMesaGPU: Mesa 26.1.6 with only its v3d gallium driver,
# for the Raspberry Pi 4's GPU on native RISC OS (V3DRender, reached only
# through RISC3D), behind riscos-mesa's EGL and OSMesa (RISC3D's "two
# Mesas": Mesa 20.3.5 stays the software renderer). Also the test programs
# v3dtri and v3dgles2.
#
#   MESON=<meson 1.4 or later> build/build-v3d.sh     (after build-zlib.sh)
#   RO_PIC=1 ... build/build-v3d.sh                   (build-shared.sh does this)
#
# - Source: Debian's copy of the Mesa 26.1.6 release tarball, checked by
#   sha256. Mesa 25.2 or later is needed: 25.0's v3d compiler loses
#   fixed-function texture coordinates with lighting (RISC3D, 15 Nov 2026).
# - v3d/patches/mesa-26.1.6-riscos-v3d.patch: RISC OS in meson.build and
#   util (UnixLib: no pthread barriers, open_memstream, mkstemps, sysinfo),
#   the v3d driver's mmap, munmap and close of the GPU module's file and
#   sync files, BOs opened by handle, and a global GL dispatch.
# - v3d/libdrm: the part of libdrm Mesa's v3d driver uses, as GPU module
#   requests (headers xf86drm.h, xf86drmMode.h, drm.h, drm_mode.h from
#   libdrm 2.4.124, unchanged; sys/ioccom.h gives Linux's request numbers).
# - v3d/frontend: a context and a frame on the GPU (v3dm.h).
# - v3d/gpu: libMesaGPU.a: all of the above in one object with every symbol
#   hidden but riscos_mesa_gpu() (v3d/include/riscos_mesa_gpu.h), so that it
#   links beside Mesa 20.3.5; libOSMesa's RISC3D switch uses it when RISC3D
#   lists a GPU module (DRM device).
# Output: $STAGE/lib/libMesaGPU.a. The static build (build-v3d/) also makes
# the tests there (v3dtri,e1f, v3dgles2,e1f); with RO_PIC=1 the
# position-independent one (build-v3d-pic/, into stage-pic/) is what
# build-shared.sh makes libMesaGPU.so from, as for every other library.
set -euo pipefail
B=$(cd "$(dirname "$0")" && pwd)
source "$B/env.sh"
R=$(cd "$B/.." && pwd)
V=26.1.6
URL=https://deb.debian.org/debian/pool/main/m/mesa/mesa_$V.orig.tar.xz
SHA=5296b88a0f1e012e2cb9ada150a2bbadf728ca81e5a4fb2ab43c83a4d2158606
O=${V3D_OUT:-$R/build-v3d$RO_SUFFIX}
M=$R/build-v3d/mesa-$V             # (one patched source for both builds)
MB=$O/build
MESON=${MESON:-meson}         # 1.4 or later
mkdir -p "$O" "$O/pc" "$O/lib" "$O/zinc" "$SRC" "$R/build-v3d"

# ---- source ----
if [ ! -d "$M" ]; then
  [ -f "$SRC/mesa_$V.orig.tar.xz" ] || fetch_verified "$URL" "$SHA" "$SRC/mesa_$V.orig.tar.xz"
  echo "$SHA  $SRC/mesa_$V.orig.tar.xz" | sha256sum -c --status
  tar -xJf "$SRC/mesa_$V.orig.tar.xz" -C "$R/build-v3d"
  (cd "$M" && patch -p1 < "$R/v3d/patches/mesa-$V-riscos-v3d.patch")
fi

# ---- the libdrm layer ----
# (built as Mesa is, with 64-bit off_t, though nothing shared with Mesa
# uses off_t: drm_riscos.h)
for f in drm_riscos drm_riscos_fake drm_riscos_dump; do
  $CC $RO_CFLAGS -D_FILE_OFFSET_BITS=64 -Wall -I"$R/v3d/libdrm/include" -I"$M/include" \
    -c "$R/v3d/libdrm/src/$f.c" -o "$O/$f.o"
done
rm -f "$O/lib/libdrm.a"; $AR rcs "$O/lib/libdrm.a" "$O/drm_riscos.o" "$O/drm_riscos_fake.o" "$O/drm_riscos_dump.o"
cat > "$O/pc/libdrm.pc" <<EOF
Name: libdrm
Description: riscos-mesa's libdrm layer for GPU modules (through RISC3D)
Version: 2.4.124
Cflags: -I$R/v3d/libdrm/include
Libs: -L$O/lib -ldrm
EOF
cp "$STAGE/include/zlib.h" "$STAGE/include/zconf.h" "$O/zinc/"
cat > "$O/pc/zlib.pc" <<EOF
Name: zlib
Description: zlib (riscos-mesa's build)
Version: 1.3.1
Cflags: -I$O/zinc
Libs: -L$STAGE/lib -lz
EOF

# ---- Mesa ----
# The libdrm layer's headers on every file: the v3d driver and util include
# xf86drm.h, and drm.h's sys/ioccom.h must be the layer's.
python3 - "$B/meson-riscos.txt.in" "$O/cross.txt" "$R/v3d/libdrm/include" <<'EOF'
import os, sys
s = open(sys.argv[1]).read()
flags = ['-I' + sys.argv[3]] + os.environ['RO_CFLAGS'].split()
s = s.replace('@GCCSDK_ENV@', os.environ['GCCSDK_ENV']).replace('@RO_CFLAGS_LIST@', ', '.join("'%s'" % f for f in flags))
open(sys.argv[2], 'w').write(s)
EOF
if [ ! -f "$MB/build.ninja" ]; then
  PKG_CONFIG_LIBDIR="$O/pc" $MESON setup "$MB" "$M" --cross-file "$O/cross.txt" --buildtype=release \
    -Db_staticpic=$([ -n "${RO_PIC:-}" ] && echo true || echo false) -Ddefault_library=static -Dplatforms= -Dgallium-drivers=v3d -Dvulkan-drivers= \
    -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dllvm=disabled -Dgles1=enabled -Dgles2=enabled \
    -Dopengl=true -Dshared-glapi=enabled -Dzstd=disabled -Dzlib=enabled -Dexpat=disabled \
    -Dvalgrind=disabled -Dlibunwind=disabled -Dlmsensors=disabled -Dbuild-tests=false -Dtools= \
    -Dshader-cache=disabled -Dxmlconfig=disabled -Dperfetto=false -Dvideo-codecs= -Dgallium-va=disabled
fi
LIBS="src/mesa/libmesa.a src/gallium/drivers/v3d/libv3d.a src/gallium/drivers/v3d/libv3d-v42.a
  src/gallium/drivers/v3d/libv3d-v71.a src/broadcom/compiler/libbroadcom_compiler.a
  src/broadcom/libbroadcom_v3d.a src/broadcom/libbroadcom-v42.a src/broadcom/libbroadcom-v71.a
  src/broadcom/cle/libbroadcom_cle.a src/broadcom/qpu/libbroadcom_qpu.a src/broadcom/libv3d_neon.a
  src/broadcom/perfcntrs/libbroadcom_perfcntrs.a src/broadcom/perfcntrs/libv3d-perfcntrs-v42.a
  src/broadcom/perfcntrs/libv3d-perfcntrs-v71.a src/gallium/auxiliary/libgallium.a
  src/compiler/glsl/libglsl.a src/compiler/glsl/glcpp/libglcpp.a src/compiler/nir/libnir.a
  src/compiler/spirv/libvtn.a src/compiler/libcompiler.a src/mesa/glapi/shared-glapi/libglapi.a
  src/mesa/glapi/glapi/libglapi_bridge.a src/util/libxmlconfig.a src/util/libmesa_util.a
  src/util/libmesa_util_simd.a src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a"
# (ninja runs meson's own Python helpers: MESON_PYTHONPATH, if set, is the
# PYTHONPATH that finds MESON's meson rather than another one: build/ci.sh)
PYTHONPATH="${MESON_PYTHONPATH-${PYTHONPATH:-}}" ninja -C "$MB" $LIBS

# ---- the frontend, with Mesa's own compiler flags (st_manager.c's) ----
FLAGS=$(python3 - "$MB" <<'EOF'
import json, shlex, sys
mb = sys.argv[1]
e = [x for x in json.load(open(mb + '/compile_commands.json')) if x['file'].endswith('state_tracker/st_manager.c')][0]
a = shlex.split(e['command'])[1:]
out, skip = [], False
for x in a:
    if skip: skip = False; continue
    if x in ('-o', '-c', '-MF', '-MQ'): skip = True; continue
    if x == '-MD' or x.endswith('.c'): continue
    if x.startswith('-I') and not x.startswith('-I/'): x = '-I' + mb + '/' + x[2:]
    out.append(x)
print(' '.join(shlex.quote(x) for x in out))
EOF
)
eval "$CC $FLAGS -I$M/src/gallium/drivers -I$R/v3d/frontend -c $R/v3d/frontend/v3dm.c -o $O/v3dm.o"
$CC $RO_CFLAGS -Wall -c "$R/v3d/frontend/v3dm_stubs.c" -o "$O/v3dm_stubs.o"
rm -f "$O/libv3dm.a"; $AR rcs "$O/libv3dm.a" "$O/v3dm.o" "$O/v3dm_stubs.o"

# ---- the GPU library: one object, only riscos_mesa_gpu() global ----
# (UnixLib, libstdc++, libgcc and libm stay undefined: the program has
# them; zlib goes inside, hidden, as in libOSMesa)
python3 "$R/tools/gpu-functions.py" "$HOST-nm" "$MB/src/mesa/glapi/glapi/libglapi_bridge.a" "$O/gpu_functions.c"
$CC $RO_CFLAGS -Wall -I"$R/v3d/include" -c "$O/gpu_functions.c" -o "$O/gpu_functions.o"
$CC $RO_CFLAGS -Wall -I"$R/v3d/include" -I"$R/v3d/frontend" -c "$R/v3d/gpu/riscos_mesa_gpu.c" -o "$O/riscos_mesa_gpu.o"
$HOST-ld -r -o "$O/mesagpu-all.o" "$O/riscos_mesa_gpu.o" "$O/gpu_functions.o" --start-group "$O/libv3dm.a" \
  $(for l in $LIBS; do echo "$MB/$l"; done) "$O/lib/libdrm.a" "$STAGE/lib/libz.a" --end-group
# Every name it defines gets a prefix, all but riscos_mesa_gpu are made
# local, and the COMDAT groups go (-R .group: their sections become plain
# ones; ld -r has already kept one copy of each): C++ inline functions are
# COMDAT groups, and Mesa 20.3.5's GLSL compiler has groups of the same
# names (glsl_symbol_table::..., ir_discard's destructors), which the final
# link would otherwise merge, keeping 20.3.5's code for 26.x's callers.
$HOST-nm -g --defined-only "$O/mesagpu-all.o" | awk '$3 != "riscos_mesa_gpu" {print $3, "mgpu_" $3}' | sort -u > "$O/mesagpu.syms"
$HOST-objcopy -R .group --redefine-syms="$O/mesagpu.syms" --keep-global-symbol=riscos_mesa_gpu \
  "$O/mesagpu-all.o" "$O/mesagpu.o"
leaked=$($HOST-nm -g --defined-only "$O/mesagpu.o" | awk '{print $3}' | grep -vx riscos_mesa_gpu || true)
[ -z "$leaked" ] || { echo "libMesaGPU exports more than riscos_mesa_gpu: $leaked" >&2; exit 1; }
# writable data sections of 4 bytes or more word aligned (riscos-crossdev's
# check-data-align.py: a Pi aborts unaligned word accesses)
python3 "$R/tools/elf-align-data.py" "$O/mesagpu.o"
rm -f "$O/libMesaGPU.a"; $AR rcs "$O/libMesaGPU.a" "$O/mesagpu.o"
mkdir -p "$STAGE/lib"; cp "$O/libMesaGPU.a" "$STAGE/lib/"
ls -la "$STAGE/lib/libMesaGPU.a"
[ -z "${RO_PIC:-}" ] || exit 0         # (the tests are static programs)

# ---- the tests ----
for t in v3dtri v3dgles2; do
  $CC $RO_CFLAGS -Wall -I"$M/include" -I"$R/v3d/frontend" -c "$R/v3d/tests/$t.c" -o "$O/$t.o"
  $CXX $RO_CFLAGS -static -Wl,--gc-sections -o "$O/$t" "$O/$t.o" -Wl,--start-group "$O/libv3dm.a" \
    $(for l in $LIBS; do echo "$MB/$l"; done) "$O/lib/libdrm.a" "$STAGE/lib/libz.a" -Wl,--end-group -lstdc++ -lm
  if [ -n "${RCD_DIR:-}" ]; then
    RISCOS_TOOLCHAIN=$GCCSDK_ENV "$RCD_DIR/checks/check-all.sh" "$O/$t"
  fi
  $STRIP -o "$O/$t,e1f" "$O/$t"
done
ls -la "$O/v3dtri,e1f" "$O/v3dgles2,e1f"
