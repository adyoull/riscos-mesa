#!/bin/bash
# riscos-mesa's EGL with RISC3D's real client and Linux helper, end to end
# on the build machine (r3d_e2e.c). Run by tests/run-all.sh (step risc3d).
#
#   R3D_SRC=<RISC3D source tree, at the pinned tag> tests/host-harness/egl/r3d_e2e.sh
#
# Needs the build machine's Mesa EGL (libEGL.so.1; llvmpipe is fine), gcc,
# python3 and the host Mesa of tests/host-setup.sh (M=). It builds RISC3D's
# helper (rol-3d) and the client with RISC3D's host stand-in for the SWIs
# (here also answering RISC3D_Info with a driver), starts the helper, runs
# the test and stops the helper again.
set -uo pipefail
R=$(cd "$(dirname "$0")/../../.." && pwd)
H=$R/tests/host-harness
: "${R3D_SRC:?set R3D_SRC to a RISC3D source tree}" "${M:?set M to the host Mesa tree}"
: "${OUT:=/tmp/riscos-mesa-r3d}"
O=$M/build/src/mesa/drivers/osmesa
mkdir -p "$OUT/gen" "$OUT/r3dhost"
cd "$OUT" || exit 1

# the stream's generated code, which must be the same as riscos-mesa's copy
python3 -I "$R3D_SRC/proto/gen.py" "$OUT/gen" > /dev/null || exit 1
for f in client_gen.c r3d_gl.h r3d_ops.h r3d_exts.h; do
    cmp -s "$OUT/gen/$f" "$R/risc3d/gen/$f" || { echo "risc3d/gen/$f differs from R3D_SRC's: not the pinned version?"; exit 1; }
done
gcc -O2 -w -I"$R3D_SRC/include" -I"$R3D_SRC/lib" -I"$OUT/gen" -I"$R3D_SRC/proto" \
    -o rol-3d-host "$R3D_SRC/linux/rol-3d.c" -ldl || exit 1

# RISC3D's host stand-in for the SWIs, saying a driver is there
cp "$R3D_SRC/test/host/swis.h" "$R3D_SRC/test/host/kernel.h" r3dhost/
python3 - r3dhost/swis.h <<'EOF' || exit 1
import sys
p = sys.argv[1]; s = open(p).read()
a = '  if (swi == 0xC3D02)\n'
assert a in s, "RISC3D's test/host/swis.h has changed"
s = s.replace(a, '  if (swi == 0xC3D00)          /* RISC3D_Info: r3 -> a driver */\n'
                 '    *va_arg (ap, const char **) = "host";\n  else if (swi == 0xC3D02)\n', 1)
open(p, 'w').write(s)
EOF
gcc -O2 -w -DR3D_HOST -Ir3dhost -I"$R/risc3d/include" -I"$R/risc3d/lib" -I"$R/risc3d/gen" -I"$R/risc3d/proto" \
    -c "$R/risc3d/lib/r3d_gl.c" -o r3d_gl.o || exit 1

# riscos-mesa's EGL against the fake RISC OS, with that client
F="-no-pie -O1 -w -std=gnu99 -DEGL_RISCOS_HOST -I$H/egl/fake -I$H/egl -I$R/dispmanx/include -I$R/dispmanx -I$R/egl/include -I$M/include"
gcc -no-pie -O1 -w -std=gnu99 -I"$H/ovl/fake" -I"$H/egl" -I"$H/egl/fake" -c "$H/ovl/fake_ovl.c" -o fake_ovl.o &&
gcc $F -DFAKE_NO_R3D "$H/egl/r3d_e2e.c" "$H/egl/fake_riscos.c" "$R/egl/egl_riscos.c" fake_ovl.o r3d_gl.o \
    -o r3d-e2e -L"$O" -lOSMesa -lpthread -lm -Wl,-rpath,"$O" || exit 1

# in a process group of its own: it forks a process per context
setsid ./rol-3d-host > helper.log 2>&1 &
helper=$!
sleep 1
timeout 120 ./r3d-e2e | tee r3d-e2e.out | tail -1
kill -- -"$helper" 2>/dev/null
grep -q "ALL PASS" r3d-e2e.out
