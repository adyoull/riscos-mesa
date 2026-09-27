#!/bin/bash
# Builds Khronos's EGL conformance tests (dEQP-EGL, from VK-GL-CTS) against
# riscos-mesa's EGL on the host harness's fake RISC OS. See README.md.
#
#   M=<host Mesa tree, patches/mesa applied, built in $M/build>
#   tests/host-harness/deqp/build.sh            -> $OUT/build/modules/egl/deqp-egl
#
#   OUT=<dir>   work directory (default /tmp/riscos-mesa-deqp)
#   CTS=<dir>   an existing VK-GL-CTS checkout (default: fetched into $OUT)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
: "${M:?M=<host Mesa tree with patches/mesa applied and built in \$M/build>}"
: "${OUT:=/tmp/riscos-mesa-deqp}"
CTS_COMMIT=1d3e8178af79e52d5467b379fad58f221fbe3d40     # 2026-09-25
: "${CTS:=$OUT/VK-GL-CTS}"
mkdir -p "$OUT"

if [ ! -d "$CTS/.git" ]; then
    git init -q "$CTS"
    git -C "$CTS" fetch -q --depth 1 https://github.com/KhronosGroup/VK-GL-CTS.git "$CTS_COMMIT"
    git -C "$CTS" checkout -q FETCH_HEAD
fi
# The CTS's external sources at the commits it pins (SPIRV-Tools, glslang...),
# one by one: a download that fails (libpng's archive, say) only matters
# if the build then needs it.
if [ ! -d "$CTS/external/spirv-tools/src" ]; then
    (cd "$CTS/external" && python3 - <<'PY'
import fetch_sources as f
import logging; logging.basicConfig(level=logging.INFO)
for pkg in f.PACKAGES:
    if pkg.baseDir == "vulkan-validationlayers":
        continue
    try:
        pkg.update("https", False)
    except BaseException as e:
        print("could not fetch %s: %s" % (pkg.baseDir, e))
PY
    )
fi
mkdir -p "$CTS/targets/riscos" "$CTS/framework/platform/riscos"
cp "$HERE/riscos.cmake" "$CTS/targets/riscos/"
cp "$HERE"/tcuRiscos*.cpp "$HERE"/tcuRiscos*.hpp "$CTS/framework/platform/riscos/"

# The EGL under test, with the fake RISC OS, as one static library
E=$R/tests/host-harness/egl
mkdir -p "$OUT/egl"
F="-no-pie -fno-pie -O1 -g -w -std=gnu99 -I$E/fake -I$E -I$R/dispmanx -I$R/dispmanx/include -I$R/egl/include -I$M/include"
gcc $F -c "$R/egl/egl_riscos.c" -o "$OUT/egl/egl_riscos.o"
gcc $F -c "$E/fake_riscos.c" -o "$OUT/egl/fake_riscos.o"
rm -f "$OUT/egl/libegl_riscos_fake.a"
ar rcs "$OUT/egl/libegl_riscos_fake.a" "$OUT/egl/egl_riscos.o" "$OUT/egl/fake_riscos.o"

cmake -S "$CTS" -B "$OUT/build" -DDEQP_TARGET=riscos -DCMAKE_BUILD_TYPE=Release \
    -DSELECTED_BUILD_TARGETS=deqp-egl \
    -DRISCOS_EGL_LIB="$OUT/egl/libegl_riscos_fake.a" \
    -DRISCOS_OSMESA_DIR="$M/build/src/mesa/drivers/osmesa" \
    -DRISCOS_FAKE_INCLUDE="$E" > "$OUT/cmake.log"
make -C "$OUT/build" -j"$(nproc)" deqp-egl > "$OUT/make.log"
ls -la "$OUT/build/modules/egl/deqp-egl"
