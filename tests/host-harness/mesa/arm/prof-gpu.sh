#!/bin/bash
# The GPU library's CPU cost, measured and profiled under qemu-arm.
#
# Links the RISC OS builds of libOSMesa.a and libMesaGPU.a (Mesa 26's v3d
# driver) into a static ARM Linux program (tests/drawtest.c, or PROG=<a C
# file using OSMesa>), with shim.c and shim_gpu.c for UnixLib's symbols,
# and runs it with V3DM$Fake: Mesa does all its own work as on a Pi 4, the
# GPU's jobs finish at once. So what's measured is the driver's CPU work.
#
#   STAGE=<riscos-mesa stage dir> tests/host-harness/mesa/arm/prof-gpu.sh [args]
#
# With QEMU_PLUGIN=<qemu's libhotblocks.so or another plugin> it's passed
# to qemu (-plugin), and the output goes to $OUT/plugin.txt; with
# HOTBLOCKS=1 (and a qemu built with plugins as QEMU=) the hot blocks are
# added up by function (tools/hotfuncs.py against the map file).
# OUT=<dir> (default /tmp/mesa-prof-gpu).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$HERE/../../../.." && pwd)
: "${STAGE:?STAGE=<riscos-mesa stage directory>}"
: "${OUT:=/tmp/mesa-prof-gpu}"
: "${PROG:=$R/tests/drawtest.c}"
CC=arm-linux-gnueabihf-gcc
CXX=arm-linux-gnueabihf-g++
mkdir -p "$OUT"
PT="cond_broadcast cond_destroy cond_init cond_signal cond_timedwait cond_wait condattr_destroy
    condattr_init condattr_setclock create detach equal exit join self sigmask mutex_trylock
    key_create key_delete getspecific setspecific once mutex_init mutex_lock mutex_unlock
    mutex_destroy mutexattr_init mutexattr_settype mutexattr_destroy"
REN="--redefine-sym errno=ro_errno_shim $(for f in $PT; do echo "--redefine-sym pthread_$f=ro_pthread_${f}_shim"; done)"
for l in libOSMesa libMesaGPU; do
    cp "$STAGE/lib/$l.a" "$OUT/$l.a"
    arm-linux-gnueabihf-objcopy $REN "$OUT/$l.a"
done
# shim.c's mutex shims are shim_gpu.c's too: only one of each
$CC -c -O2 -mfloat-abi=hard "$HERE/shim.c" -o "$OUT/shim.o"
arm-linux-gnueabihf-objcopy --redefine-sym _kernel_swi=ro_kernel_swi_shim \
    $(for f in mutex_init mutex_lock mutex_unlock mutex_destroy mutexattr_init mutexattr_settype mutexattr_destroy; do
        echo "--weaken-symbol ro_pthread_${f}_shim"; done) "$OUT/shim.o"
$CC -c -O2 -mfloat-abi=hard "$HERE/shim_gpu.c" -o "$OUT/shim_gpu.o"
cat > "$OUT/mutex.c" <<'EOF'
int ro_pthread_mutex_init_shim(void *m, const void *a) { (void) m; (void) a; return 0; }
int ro_pthread_mutex_lock_shim(void *m) { (void) m; return 0; }
int ro_pthread_mutex_unlock_shim(void *m) { (void) m; return 0; }
int ro_pthread_mutex_destroy_shim(void *m) { (void) m; return 0; }
int ro_pthread_mutexattr_init_shim(void *a) { (void) a; return 0; }
int ro_pthread_mutexattr_settype_shim(void *a, int t) { (void) a; (void) t; return 0; }
int ro_pthread_mutexattr_destroy_shim(void *a) { (void) a; return 0; }
EOF
$CC -c -O2 -mfloat-abi=hard "$OUT/mutex.c" -o "$OUT/mutex.o"
$CC -c -O2 -g -w -mfpu=${RO_FPU:-vfpv3} -mfloat-abi=hard -I"$STAGE/include" -I"$R/tests" "$PROG" -o "$OUT/prog.o"
$CC -c -O2 -w -mfloat-abi=hard -I"$R/tests" "$R/tests/hrtime.c" -o "$OUT/hrtime.o" 2>/dev/null || true
$CXX -static -o "$OUT/prog" "$OUT/prog.o" $( [ -f "$OUT/hrtime.o" ] && echo "$OUT/hrtime.o") \
    "$OUT/shim.o" "$OUT/shim_gpu.o" "$OUT/mutex.o" \
    -Wl,-u,riscos_mesa_gpu "$OUT/libOSMesa.a" "$OUT/libMesaGPU.a" "$OUT/libOSMesa.a" \
    "$STAGE/lib/libz.a" -lm -lpthread -Wl,-Map="$OUT/prog.map" 2>&1 | grep -v "warning:\|NOTE:" || true
QEMU=${QEMU:-qemu-arm}
if [ -n "${QEMU_PLUGIN:-}" ]; then
    env 'V3DM$Fake=1' "$QEMU" -cpu cortex-a15 -plugin "$QEMU_PLUGIN" -d plugin -D "$OUT/plugin.txt" "$OUT/prog" "$@"
else
    env 'V3DM$Fake=1' "$QEMU" -cpu cortex-a15 "$OUT/prog" "$@"
fi
