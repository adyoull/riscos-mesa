#!/bin/bash
# Builds ovltest for the host, on the fake RISC OS with the fake
# VideoOverlay module: -> $OUT/ovltest (see run.sh).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../.." && pwd)
H=$R/tests/host-harness/egl
: "${OUT:=/tmp/riscos-mesa-ovl}"
mkdir -p "$OUT"
F="-no-pie -O1 -g -std=gnu99 -Wall -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -I$HERE/fake -I$H -I$H/fake"
gcc $F -Dmain=app_main -c "$R/tests/ovltest.c" -o "$OUT/ovltest.o"
gcc $F -c "$R/tests/hrtime.c" -o "$OUT/hrtime.o"
gcc $F -w -c "$H/fake_riscos.c" -o "$OUT/fake_riscos.o"
gcc $F -c "$HERE/fake_ovl.c" -o "$OUT/fake_ovl.o"
gcc $F -c "$HERE/ovlrun.c" -o "$OUT/ovlrun.o"
gcc -no-pie "$OUT"/ovltest.o "$OUT"/hrtime.o "$OUT"/fake_riscos.o "$OUT"/fake_ovl.o "$OUT"/ovlrun.o \
    -o "$OUT/ovltest" -lpthread -lm
