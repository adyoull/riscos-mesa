# QEMU with RISC OS's alignment rules

RISC OS aborts on an unaligned load or store; Linux, and plain qemu-arm,
don't. This QEMU traps them as RISC OS does, so the ARM checks
(`../mesa/arm/run-arm.sh`) catch an unaligned access in the RISC OS build
of Mesa before it aborts on a Pi.

From the riscos-ffmpeg port's test rig (same owner, same patch):

- `qemu-8.2.2-align-trap.patch`: `QEMU_ARM_ALIGN_TRAP=1` sets SCTLR.A at
  start-up; `QEMU_ARM_ALIGN_IGNORE=lo-hi,...` (hex) exempts code ranges.
- `build-qemu.sh [OUT]`: builds `OUT/qemu-arm` (QEMU 8.2.2 from Ubuntu's
  orig tarball, linux-user ARM only; needs ninja, python3-venv and
  libglib2.0-dev; a few minutes).
- `trapped-ranges.py MAP PATTERN...`: the exempt ranges for a statically
  linked program, from its link map: only code from files matching a
  pattern (libOSMesa.a and the check itself) is trapped; glibc's string
  functions, which use unaligned loads, are exempt.

Use: `QEMU_ALIGN=<OUT>/qemu-arm tests/host-harness/mesa/arm/run-arm.sh`
(`tests/run-all.sh` does this with `ARM=1` when `QEMU_ALIGN` is set). An
unaligned access stops the check with SIGBUS (exit status 135).
