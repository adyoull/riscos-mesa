#!/bin/bash
# Builds a qemu-arm that traps unaligned accesses as RISC OS does: QEMU
# 8.2.2 (Ubuntu's orig tarball) with qemu-8.2.2-align-trap.patch,
# linux-user ARM only. Result: OUT/qemu-arm. From the riscos-ffmpeg port.
# Needs build-essential, ninja, python3-venv, libglib2.0-dev.
# Usage: tests/host-harness/qemu/build-qemu.sh [OUT]   (default ~/qemu-aligntrap)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$HOME/qemu-aligntrap}
URL=https://archive.ubuntu.com/ubuntu/pool/main/q/qemu/qemu_8.2.2+ds.orig.tar.xz
mkdir -p "$OUT" && cd "$OUT"
[ -f qemu_8.2.2+ds.orig.tar.xz ] || curl -sSfLO "$URL"
rm -rf qemu-8.2.2 && tar xf qemu_8.2.2+ds.orig.tar.xz
cd qemu-8.2.2
patch -p1 < "$HERE/qemu-8.2.2-align-trap.patch"
mkdir build && cd build
../configure --target-list=arm-linux-user --disable-system --disable-docs \
  --disable-tools --disable-werror --disable-install-blobs >/dev/null
ninja qemu-arm >/dev/null
cp qemu-arm "$OUT/qemu-arm"
echo "$OUT/qemu-arm"
