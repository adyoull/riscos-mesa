# v3d: the GPU library, libMesaGPU

`libMesaGPU` is the part of riscos-mesa that draws on a GPU on native
RISC OS. Today that means the Raspberry Pi 4's V3D, through the
V3DRender module. It is Mesa 26.1.6 built with only its v3d gallium
driver. It sits beside Mesa 20.3.5, which stays the software renderer,
with every one of its names hidden but one (`riscos_mesa_gpu`), so the
two Mesas link into one program without clashing. (This is case 3 of
RISC3D's "two Mesas" plan.)

Released in 20.3.5-20. It is in the devkit (`lib/libMesaGPU.a`,
`lib/shared/libMesaGPU.so`) and in !MesaLibs (`lib.libMesaGPU/so/1`).

## How a program gets the GPU

**Programs don't choose between the GPU and software: RISC3D decides.**

1. At start-up !RISC3D (in `!Boot.Resources`) loads RISC3D, which loads
   the machine's GPU driver: V3DRender on a Pi 4, 400 or CM4. V3DRender
   registers with RISC3D as a *DRM device*: it takes Linux's v3d requests
   (`DRM_IOCTL_V3D_*`) through one ioctl SWI rather than a command
   stream.
2. The first time a program makes a context, libOSMesa's RISC3D switch
   (`osmesa/r3d_switch.c`) asks RISC3D for its driver. It uses the GPU
   library when all three of these hold:
   - RISC3D's driver is a DRM device;
   - the program carries the GPU library;
   - the library has a Mesa driver for that device (by DRM driver name:
     "v3d").

   Otherwise it uses RISC3D's command stream (Linux3D on RISC OS on
   Linux) or riscos-mesa's software renderer, as before. `RISC3D$Disable`
   or `LIBGL_ALWAYS_SOFTWARE` keeps everything in software.
3. EGL and OSMesa then draw with the GPU library's contexts, exactly as
   they draw with RISC3D's on RISC OS on Linux.

The program never calls V3DRender itself, only RISC3D and the ioctl SWI
RISC3D names. A program carries the library by linking it once:

| Program | Link |
|---|---|
| static | `... -lOSMesa -Wl,-u,riscos_mesa_gpu -lMesaGPU -lstdc++ -lz -lm` (`-u`: libOSMesa refers to it only weakly) |
| mixed | `... -lOSMesa -lMesaGPU ...` (`libMesaGPU.so.1` in !MesaLibs: docs/porting/mixed.md) |

Programs linked without it, including everything linked before
20.3.5-20, draw in software on a native Pi 4. RISC OS has no `dlopen`,
so the library can't be loaded on demand. Its cost is about 15 MB in a
static program; a mixed program shares !MesaLibs' copy.

## What it does on the GPU

- **APIs:** OpenGL 2.1 (compatibility profile) and OpenGL ES 1.1 to 3.1,
  through EGL (and so SDL 2, freeglut and DispmanX) and through OSMesa.
- **Window surfaces draw straight into their own memory.** That covers
  the window's sprite, screen memory full screen, and each screen bank.
  V3DRender maps the memory into the GPU (`DRM_IOCTL_RISCOS_IMPORT_MEMORY`,
  0.11), so nothing is read back. Screen memory, which the CPU doesn't
  cache, is marked so (`DRM_RISCOS_IMPORT_UNCACHED`, 0.13), and V3DRender
  then does no cache work on it.
- **Other buffers are read back** from a buffer the CPU sees cached
  (`DRM_RISCOS_BO_CPU_CACHED`, 0.11). That covers pbuffers, OSMesa's own
  buffers, and windows whose memory can't be mapped.
- **On the owner's Pi 4:**
  - a 640x480 window at 365 fps;
  - full screen at the display's 60 fps;
  - glbench's scenes in 1.6–2.8 ms a frame against 9.7–13.6 ms in
    software;
  - ClassiCube, TORCS, Warzone 2100 and YSFlight run on it.
- **Use V3DRender 0.15 or later:** 0.11–0.14 spend most of a windowed
  program's frame on cache maintenance.

## The parts

| Part | What it is |
|---|---|
| `build/build-v3d.sh` | Fetches Mesa 26.1.6 (Debian's copy of the release tarball, sha256-checked), patches it, builds it with meson for RISC OS, and makes `libMesaGPU.a` and the test programs. Needs meson 1.4 or later (`MESON=`; `build/ci.sh` installs one). `RO_PIC=1` makes the position-independent build that `build-shared.sh` turns into `libMesaGPU.so.1`. |
| `patches/mesa-26.1.6-riscos-v3d.patch` | Mesa's changes: RISC OS in meson.build and util (UnixLib has no pthread barriers, open_memstream, mkstemps or sysinfo); the v3d driver's mmap, munmap and close of the GPU module's file and of sync files; BOs opened by handle (imported memory); a global GL dispatch and context (GCCSDK's thread-local variables are emulated), each thread's own once a second thread draws; `V3DM$Why`. |
| `libdrm/` | The part of libdrm that Mesa's v3d driver uses, as requests to the GPU module (`src/drm_riscos.c`, per V3DRender's docs/INTERFACE.md). The module is found through RISC3D: `RISC3D_Info` r4 bit 0 = DRM device, r5 its ioctl SWI, r6 its DRM driver name; `RISC3D_Open` r1 = its file. The module's answers are Linux errno numbers, mapped to UnixLib's. `xf86drm.h`, `xf86drmMode.h`, `drm.h`, `drm_mode.h` are libdrm 2.4.124's, unchanged (MIT). `include/drm_riscos.h` has the RISC OS requests (sync files, cached buffers, imported memory, GPU time). |
| `frontend/` | `v3dm.h`: contexts and frames on the GPU, after Mesa's hgl frontend and its removed OSMesa one. It picks Mesa's driver by the DRM driver name, so another GPU's module that registers with RISC3D needs only its Mesa driver added. |
| `gpu/`, `include/riscos_mesa_gpu.h` | The library's one entry point, `riscos_mesa_gpu()`: a table of functions (device, create, destroy, make current, read back, draw into, finish, the gl entry points) that libOSMesa's RISC3D switch uses. `tools/gpu-functions.py` makes the gl entry point list. |
| `tests/v3dtri.c`, `tests/v3dgles2.c` | Bring-up tests on the GPU without EGL: a fixed-function triangle and quad; OpenGL ES 2.0 with GLSL, a vertex buffer, a texture and blending at 1920x1080. The EGL ones are `gpu-egltest`, `gpu-glestest` and `gpu-glbench`, in the tests-mixed zip. |

The library is linked from all of the above with `ld -r`. Every defined
name is prefixed `mgpu_` and made local except `riscos_mesa_gpu`, and
`.group` sections are dropped. Mesa 20.3.5's and 26.1.6's GLSL compilers
share C++ names whose COMDAT groups would otherwise merge.

## Sharing and threads (from 20.3.5-23)

- **Sharing:** a context can share another's objects (textures, buffers,
  renderbuffers, framebuffers, programs): `eglCreateContext` with a
  share context that is the GPU library's gives another of its contexts,
  through `create_shared` (interface version 3) and Mesa's own sharing.
- **Threads:** contexts can be current in different threads at once, each
  thread with its own (a program can paint in one thread and composite
  in another). Mesa 26's glapi keeps the current dispatch and context in
  plain globals, read directly while one thread draws (GCCSDK's
  thread-local variables are emulated, a call per use); it also keeps each
  thread's own, and reads those instead once a second thread makes a
  context current (`_mesa_glapi_riscos_threads`, as Mesa 20's glapi did
  without TLS). The RISC3D switch keeps its current context per thread too.
  Threaded programs load PThreadTicker, as always.
- **Fences:** `EGL_KHR_fence_sync` does a glFinish when the fence is made,
  so it is signalled at once and safe across threads.
- `gpu-share` (tests-mixed, `mx-gpu-share`) checks all of this: two GLES 2
  contexts on the surfaceless platform, the second sharing the first, both
  current in two threads switched pre-emptively, each seeing only its own
  state, sharing a texture and a framebuffer, and a fence. Under QEMU with
  `V3DM$Fake`: 14 checks pass, with the threads interleaved over 20,000
  rounds.

## Finding problems

All of these are system variables, read when the program starts:

| Variable | What it does |
|---|---|
| `V3DM$Stats` | Every 5 s on stderr: each kind of request to the GPU module with its count and the ms spent in it. Then, with V3DRender 0.13 or later, this program's GPU jobs and their time on the GPU per queue. Shows whether a slow program waits for the GPU or the CPU. |
| `V3DM$Why` | A line per job Mesa submits: why (`pipe flush` for glFlush, glFinish, a swap or a read-back; `reading a resource a job writes`; `writing a resource a job reads`; `a BO a job uses`; `directly` for blits, clears and compute), and two addresses as offsets from `v3d_job_submit`, to look up in the shared build's link map `stage/shared/obj/libMesaGPU.so.1.map` (sections are named after functions). |
| `V3DM$Trace` | Every request to the module, and each frame made, chosen and finished. |
| `V3DM$Dump <file>` | The first jobs (`V3DM$DumpJobs`, default 3) with every buffer they use, for replaying off the Pi; `tools/v3dm-dump.py` prints one. After a GPU hang, V3DRender's `*V3DDump` and its kit's replay are the other half. |
| `V3DM$Fake` | No GPU: the libdrm layer answers as a V3D 4.2 would without running anything (`libdrm/src/drm_riscos_fake.c`), so Mesa's ARM side (start-up, shader compiler, command lists, buffers) runs under RISC OS on Linux in QEMU. Nothing is drawn. |
| `V3DM$NoImport`, `V3DM$Uncached`, `V3DM$Tiled` | Turn off the memory import, the cached read-back buffer, or the linear colour buffer, to compare. |

## Not yet

- A command-line program that crashes keeps its GPU file (and GPU
  memory) until a restart. Desktop programs are cleaned up by the Wimp's
  close-down.
- Under RISC OS on Linux (RISC3D's command stream) sharing and threads
  come from RISC3D client 0.12 with RISC3D/Linux3D 0.06 and rol-3d 0.20;
  with older parts there, contexts don't share and one thread draws.
- Screen banks don't switch in step with the display on the Pi 4 (as in
  software), so they stay opt-in.
- Only V3D has a Mesa driver here. Another GPU's module (an i.MX6's, say)
  would register with RISC3D the same way and need its Mesa driver added
  to the build and the frontend's table.

## Notes

- Mesa 25.2 or later is needed: 25.0's v3d compiler mis-draws lit,
  textured fixed-function scenes (RISC3D, 15 Nov 2026).
- Nothing shared between Mesa and the libdrm layer may use `off_t`: Mesa
  is built with `_FILE_OFFSET_BITS=64` (UnixLib's `off_t` is then 8 bytes,
  else 4).
- What's left thread-local (call_once, qsort_r's arguments, a strtok-like
  buffer) isn't on the per-call path.
- Threaded programs need PThreadTicker, as with the software renderer.
