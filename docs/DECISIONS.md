# Why riscos-mesa is the way it is

The decisions behind the project, with the reasons, so they don't have to
be worked out again. Each says what would have to change to revisit it.

## Mesa 20.3.5 and classic OSMesa

- **20.3.5 is the last Mesa with the classic software renderer (swrast)
  behind OSMesa.** 21.0 and later have only the Gallium drivers. OSMesa
  itself went in 25.2 (25.1.x is the last release with it).
- **Classic swrast suits a single slow core.** It's plain C (Mesa itself
  has no JIT there; from 20.3.5-14 riscos-mesa adds a small shader JIT and
  NEON versions of the commonest spans, converted from Mesa's C code and
  optimised, see below), and it has fast paths for the fixed-function GL most RISC OS programs
  and older games use. Most of the project's speed work is in those paths
  (`patches/mesa`).
- **What it gives: OpenGL 2.1 (compatibility), OpenGL ES 1.1 and 2.0.**
  Requests for GL 3.x, core profiles or ES 3.x are refused (checked by
  `tests/prof.c`).
- **ES 3.0 (WebGL 2) isn't a small step.** Classic Mesa's shader back end
  (Mesa IR and `prog_execute.c`) has no integers: GLSL ES 3.00 needs
  native integers, uniform buffers, `texelFetch`, integer textures and
  transform feedback, none of which swrast has.
- **Moving to a newer Mesa means Gallium softpipe or llvmpipe behind the
  same EGL.** It would be a new renderer, not a rebase: most of
  `patches/mesa` is speed work on classic swrast code that softpipe
  doesn't have (see `patches/mesa/README`). For comparison, measured on
  an x86 host on one thread (glbench, 640x480, ms per frame):

  | Scene | riscos-mesa 20.3.5-7 | Mesa 26 softpipe | Mesa 26 llvmpipe |
  | --- | --- | --- | --- |
  | lit cube | 0.44 | 5.34 | 0.54 |
  | bilinear texture | 6.12 | 34.45 | 2.17 |
  | 12288 lit triangles | 2.73 | 17.13 | 4.29 |
  | GLSL shaded cube | 15.83 | 24.56 | 0.81 |

  Softpipe is several times slower. llvmpipe is faster but needs LLVM's
  JIT, which RISC OS doesn't have.
- **No TinyGL.** Its feature set is too small for the programs worth
  porting.
- **No hardware GL.** The Pi 1 to 3's VideoCore IV GLES (the Khronos
  module) is Pi 1–3 only. The Pi 4 and later have a different GPU with no
  RISC OS driver, so rendering is on the CPU.

## Native RISC OS EGL is the API

- **New programs use EGL with RISC OS types:** a Wimp window handle, `-1`
  for the whole screen, sprites as pixmaps. EGL is the standard way
  programs on phones, the Pi and Linux set up GL, so code written for
  those ports easily, and the API would stay the same behind a future
  hardware driver.
- **The program keeps its own Wimp_Poll loop.** EGL never polls: on a
  cooperatively multitasking desktop only the task can answer its redraw
  requests. `eglRedrawWindowRISCOS` does the redraw loop for it.
- **DispmanX compatibility (`libbcm_host`) is a porting aid only**, so
  existing Pi 1–3 Khronos code runs unchanged. It is never presented as
  another way to write programs, and it never changes native behaviour
  (native window types are checked first).
- **The RISC OS extensions' enum values are provisional** (0x3FF0 to
  0x3FF8) until registered with Khronos; the registration is ready in
  `docs/khronos/`. The library will keep accepting the old values.

## Never stop the desktop

RISC OS multitasks cooperatively: a program that waits stops every other
program.

- **In a window, `eglSwapBuffers` doesn't wait for vsync**, whatever the
  swap interval. Programs pace themselves with `Wimp_PollIdle`. (Through a
  hardware overlay it can wait up to one vsync, as writing sooner would
  tear; `eglSwapWouldWaitRISCOS` lets a program avoid even that.)
- **Full screen programs own the machine**, so there the swap interval is
  honoured.
- **SDL's driver yields**: `SDL_Delay` and `SDL_WaitEvent` wait in
  `Wimp_PollIdle`, and a frame that would have to wait for a vsync is
  held and shown from the event loop.
- **Full screen can multitask too.** SDL's desktop-size full screen is a
  borderless screen-sized Wimp window that keeps polling (RDPClient's
  "full window"), so a game's server in a TaskWindow keeps running. Only
  full screen with a mode change owns the machine, because it's faster.

## Opt-in, not automatic

- **Hardware overlays are opt-in** (`EGL_OVERLAY_RISCOS`, or the user's
  `EGL$Overlay`). An overlay sits over everything on the Pi, covers menus
  over a window that has stopped drawing, is missed by screen grabs and
  uses GPU memory, so a program asks for it knowing that. A paused
  program goes back to plotting; every failure falls back to plotting.
- **SDL's GL windows keep their original route (the window's sprite) by
  default.** The EGL route (render size, overlay) is chosen with hints, so
  existing SDL programs behave exactly as before.
- **SDL's GL *renderer* (`SDL_VIDEO_RENDER_OGL`) stays off**: it would
  make every SDL program use software GL instead of SDL's faster software
  renderer. `build/build-sdl2.sh` checks this.

## One SDL overlay, kept here

`patches/sdl2` is the one copy of SDL 2.26's RISC OS driver changes. The
riscos-openttd port uses the same files (`tools/sdl-overlay-export.sh`,
`tools/sdl-overlay-check.sh`), built without GL, which must leave no GL
code in its build. Every change is made here, and then riscos-openttd
re-exports. How to change it: `patches/sdl2/README.md`.

## Building

- **GCCSDK GCC 10 (`arm-riscos-gnueabihf`), static libraries only, for
  now.** It's the simplest thing that works everywhere, and each program
  gets its own GL state. The cost: every program carries its own copy of
  Mesa (about 9 MB). The EGL API is the standard one, so the library
  could later move into SOManager shared libraries or a module without
  changing programs' source (`egl/README.md`, "Design notes"). One
  `libOSMesa.a` holds all of Mesa's internal libraries, so programs link
  `-lOSMesa -lstdc++ -lz -lm`.
- **`-fstack-clash-protection` is required** for everything, programs
  included. The stack is mapped a page at a time as a guard page is
  touched; a function with a frame over 4 KB that doesn't probe jumps past
  the guard page and crashes at random (Mesa has 19 such functions).
  `tools/check-stack-probes.py` checks a binary.
- **`-O3 -mtune=cortex-a72 -mfpu=vfpv3`, no NEON.** Chosen by benchmark on
  a Pi 4: NEON gained nothing, and VFPv3 is as fast as VFPv4 there
  (every glbench scene within 0.5%) while also running on Cortex-A8/A9
  machines. Not the Pi 1 or Zero: they're ARMv6.
- **NEON only where it is checked at run time and gives the same
  answer** (from 20.3.5-12): the GLSL interpreter's NEON code is one
  function compiled with `#pragma GCC target("fpu=neon")`, used when
  VFPSupport_Features reports Advanced SIMD; everything else stays VFPv3.
  NEON flushes denormals and gives the default NaN, so wherever an
  operand is tiny or a result is a NaN the C code redoes the instruction:
  output stays bit-identical (`glsl-special` checks it with and without
  NEON under qemu-arm). 7% on glbench's GLSL scene on a Pi 4.
- **A shader JIT, with "no visible difference" as its rule rather than
  bit-identical output** (the owner's decision, 2026-10-06; in
  development for 20.3.5-14). Straight-line runs of fragment-shader
  arithmetic are compiled to NEON code, four fragments at a time
  (`patches/mesa` riscos-glsl-jit, `program/ro_jit_arm.c`). Keeping every
  bit the same would have meant libm's exact sinf/expf/logf/powf and
  VFP's denormals, which leave little to gain, so the JIT uses its own
  approximations (about 1e-7 relative) and NEON's flush-to-zero. Its
  check (`glsl-jit`) allows 2 in 255 on typical shaders and counts
  pixels on random ones. Our own JIT rather than llvmpipe: llvmpipe's
  shader compiler is tied to Gallium and LLVM (20-40 MB more per program,
  compile stalls), and its approximations would change pictures anyway.
  Everything else stays bit-identical; MESA_NO_JIT turns the JIT off.
- **SDL's ARM NEON and SIMD blitters are on** (from 20.3.5-10), with
  SDL's run-time CPU check, so a machine without NEON uses the ARMv6 SIMD
  ones or the C code. Their per-pixel alpha routines leave the
  destination's alpha alone where SDL's C code blends it, so riscos-mesa
  uses them only for destinations without alpha (such as the window
  surface), where they are more accurate than the C code
  (`tests/host-harness/sdl-arm`). To use them for alpha destinations too,
  SDL's behaviour for those would change: check with SDL upstream first.
- **Not position-independent** (`-Db_staticpic=false`): GCCSDK's `-fPIC`
  code reaches every global through the shared library tables, which a
  static library doesn't need.
- **Threads need UnixLib 5.0.1 and the PThreadTicker module.** Before
  5.0.1 the thread switcher's code lived in the program, and could run
  while another program was paged in and crash it. Everything is linked
  with 5.0.1 and the devkit ships the module.
- **Sources are pinned** by version and SHA-256 (`build/env.sh`), or by
  git commit (GLU, freeglut). On a mismatch, find out why before changing
  a pinned value.

## Workarounds for the Pi 4

- **Window surface sprites are padded to at least 1 MB.** Small sprites
  plotted repeatedly kept showing their first image on a Pi 4, even though
  their memory had changed; cleaning the cache didn't help, padding did.
- **VideoOverlay's Vet call fails for every format** (VideoOverlay 0.02),
  so EGL probes by creating an overlay. Its "Basic" overlays sit over
  everything, so EGL hides them while anything overlaps the window.
- **Overlay memory is slow to read** (about 150 MB/s, against 2-3 GB/s
  to write), so GL renders into ordinary memory and each frame is copied
  in.
