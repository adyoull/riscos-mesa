# v3d: Mesa's v3d driver on native RISC OS (prototype)

Case 3 of RISC3D's "two Mesas" plan: on a Raspberry Pi 4 running native
RISC OS, a newer Mesa (26.1.6, its v3d gallium driver only) runs in the
program and sends its work to V3DRender, the native GPU driver. Under
RISC OS on Linux the GPU is reached through RISC3D instead, as now.

**Status:** prototype on branch `v3d`. It builds and links into a static
test program (`v3d/tests/v3dtri.c`); the first Pi test is
`riscos-mesa-v3d-test1.zip`. Nothing here is in !MesaLibs or the devkit.
When it draws, it goes behind riscos-mesa's EGL, with its symbols hidden
from Mesa 20.3.5's (the software renderer stays 20.3.5).

| Part | What it is |
|---|---|
| `build/build-v3d.sh` | Fetches Mesa 26.1.6 (Debian's copy of the release tarball, sha256-checked), patches it, builds it with meson for RISC OS, and links the test. Needs meson 1.4 or later (`MESON=`). |
| `patches/mesa-26.1.6-riscos-v3d.patch` | RISC OS in meson.build and util (UnixLib has no pthread barriers, open_memstream, mkstemps or sysinfo); the v3d driver's mmap, munmap and close of V3DRender's file and of sync files; native fences off. |
| `libdrm/` | The part of libdrm that Mesa's v3d driver uses, as V3DRender_Ioctl requests (`src/drm_riscos.c`), per V3DRender's docs/INTERFACE.md. `xf86drm.h`, `xf86drmMode.h`, `drm.h`, `drm_mode.h` are libdrm 2.4.124's, unchanged (MIT); `sys/ioccom.h` gives Linux's request numbers; `libsync.h` maps sync files to V3DRender's requests. |
| `frontend/` | `v3dm.h`: a context and an off-screen frame on the GPU, after Mesa's hgl frontend and its removed OSMesa one. |
| `tests/v3dtri.c` | Clear, a fixed-function triangle and quad, read back, a sprite of the frame, 100 frames timed. |

**Without a V3D:** set `V3DM$Fake` and the libdrm layer answers as a
V3D 4.2 would without running anything (`libdrm/src/drm_riscos_fake.c`),
so Mesa's ARM side (start-up, shader compiler, command lists, buffers)
can be run under ROL in QEMU. Nothing is drawn. `V3DM$Trace` prints each
request to stderr. `V3DM$Dump <file>` writes the first jobs (SUBMIT_CL
and every buffer they use; `V3DM$DumpJobs`, default 3) to a file for
replaying off the Pi; `tools/v3dm-dump.py` prints one.

**Notes for later:**
- Mesa 25.2 or later: 25.0's v3d compiler mis-draws lit, textured
  fixed-function scenes (RISC3D, 15 Nov 2026).
- GCCSDK's GCC has emulated TLS: glapi's current dispatch is read through
  `__emutls_get_address` on every GL call. Fine for a prototype; the EGL
  version should use a global dispatch, as 20.3.5's does.
- Nothing shared between Mesa and the libdrm layer may use off_t: Mesa is
  built with _FILE_OFFSET_BITS=64 (UnixLib's off_t is then 8 bytes, else
  4). test1 crashed on the Pi from exactly that (drm_riscos_mmap).
- Programs are UnixLib programs and need PThreadTicker 0.04.
- A command-line program that dies leaves its V3DRender file (and GPU
  memory) open until a restart; the EGL version must close it from
  riscos-mesa's fatal-signal path too.
