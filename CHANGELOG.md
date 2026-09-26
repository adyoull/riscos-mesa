# Changelog

Releases are numbered after the Mesa version they contain; `-N` is the Nth
riscos-mesa build of it. Each release's full notes are on the GitHub
[Releases](../../releases) page.

## 20.3.5-7: faster rendering, SDL clicks

Rendering is up to 2.8 times as fast as 20.3.5-6, with the same picture,
and SDL programs no longer lose short mouse clicks. The speed figures
were measured on a Raspberry Pi 4.

glbench on the Pi 4, 640x480, 24-bit depth + stencil, milliseconds per
frame, 20.3.5-6 and 20.3.5-7 run in the same session:

| Scene | 20.3.5-6 | 20.3.5-7 | |
| --- | --- | --- | --- |
| lit cube | 5.38 | 3.02 | 1.8x |
| full-screen bilinear texture | 44.04 | 26.13 | 1.7x |
| 12288 lit triangles | 25.79 | 11.03 | 2.3x |
| GLSL per-pixel shaded cube | 108.91 | 38.83 | 2.8x |
| 4 blended full-screen quads | 28.29 | 27.53 | |
| clear | 1.53 | 1.52 | |

### GLSL: a rebuilt shader interpreter

Mesa's software renderer runs GLSL (and ARB) programs in an interpreter.
It has been reworked in two stages. With the other speed-ups below, the
first took the GLSL scene on the Pi from 108.9 to 59.9 ms a frame; the
second took it to 38.8 ms:

- **Programs are decoded once**
  (`patches/mesa/mesa-20.3.5-riscos-glsl-decode.patch`). The interpreter
  used to work out every operand of every instruction again for every
  pixel and every vertex: which register bank it's in, range checks, the
  swizzle and negation. Each program is now decoded once into a compact
  form with those answers ready, and the operand fetch and store are
  inlined (in the speed patch). This speeds up vertex shaders as well
  as fragment shaders. The decoded copy is checked against the program
  at every span and vertex batch, so a shader that is changed, relinked
  or replaced is always picked up.
- **Fragment shaders run 32 pixels at a time**
  (`patches/mesa/mesa-20.3.5-riscos-glsl-batch.patch`). Each instruction
  is dispatched once and applied to up to 32 pixels, with branch-free
  loops for the common case (no negation, indexing or saturation).
  Branching is followed separately for every pixel with lane masks:
  `if`/`else`, loops with `break` and `continue`, `discard`, and `return`
  from `main`. Each pixel has its own address register for indexed
  arrays. Shaders with subroutine calls, and any group that hits Mesa's
  runaway-loop limit, run a pixel at a time as before, so that limit
  still applies to each pixel exactly as it did. The per-pixel arithmetic
  is generated from the interpreter's own code, so results are identical.

### Other speed-ups

In `patches/mesa/mesa-20.3.5-riscos-speed.patch`:

- **Depth testing:** 24-bit depth buffers (what EGL, SDL and GLUT ask
  for) are now as fast as 16-bit ones. Every depth-tested span used to be
  copied to a malloc'd buffer, unpacked, tested, packed and copied back;
  it's now tested in place. OSMesa's fast shaded and flat triangles,
  which were only used with 16-bit depth and `GL_LESS`, now also work
  with 24-bit depth and `GL_LEQUAL`. This is most of the gain for the lit
  cube (1.8x) and the triangle scene (2.3x).
- **Texturing:** `GL_RGBA`/`GL_UNSIGNED_BYTE` textures (the usual kind)
  now use Mesa's integer textured-triangle path when they qualify (one
  texture, no mipmaps, `GL_REPEAT`, power-of-two sizes); it used to skip
  them because their bytes are in a different order from the formats it
  knew. They stay perspective-correct. Everywhere else, texels of the
  common 8-bit formats (RGBA, BGRA, RGB, L, A, LA, I, R, RG) are read
  directly instead of through a call per texel into Mesa's general
  unpacking code. Together: 1.7x for the bilinear texture scene.

And in the build:

- **libOSMesa is no longer position-independent** (meson's default for
  static libraries). GCCSDK's `-fPIC` code reaches every global through
  the shared library tables at &8038; the library is only ever linked
  into programs. GLSL is about 5% faster for it, the rest the same.

### Checking

- **Same picture:** every change was checked image by image against
  20.3.5-6 on a Linux host and on the RISC OS build itself, run under ARM
  emulation: thousands of cases covering every depth function, lighting,
  flat and smooth shading, texture formats, filters, wrap and texture
  environment modes, and GLSL and ARB programs with branches, nested
  loops, per-pixel loop counts, `discard`, early `return`, derivatives,
  `gl_FragDepth`, indexed arrays, and programs changed between draws. The
  only differences: the integer texture path rounds by up to 2/255
  differently, and a shader that reads a variable it never wrote
  (undefined in GLSL) can see a different leftover value.
- Blending is unchanged: its time moves by a couple of milliseconds
  between builds of the benchmark program itself (an A/B test on the Pi
  gave the same time with 20.3.5-6's library and this one).
- The Mesa changes are four new patches in `patches/mesa/`, described in
  `patches/mesa/README`. `build/build-mesa.sh` applies them in order and
  records them in `.riscos-patches-applied`, so an existing Mesa tree
  gets only the ones it lacks.

### Other changes

- **SDL2 mouse clicks in a window are no longer lost:** the window's
  buttons were read once per `SDL_PumpEvents`, so a click pressed and
  released between two frames (easy at the 5-10 frames a second of a busy
  OpenGL game) never reached the program. The Wimp's `Mouse_Click` event
  is now remembered and reported as a press, at the place it was clicked,
  with the release on the next poll. Found with Warzone 2100, whose menus
  ignored normal clicks.
- **Host builds with GCC 13:** `-O3` vectorisation miscompiles one loop in
  Mesa's span code (some smooth-shaded pixels come out wrong), so it's
  turned off for that function. The RISC OS build (GCC 10, no NEON) was
  never affected; the host test harness was.
- **Tests for maintainers:** `tests/run-all.sh` runs every host test in
  one go. New in `tests/host-harness/mesa`: the rendering checks used for
  this release (a hash of every image, compared with `expected/`), the
  same checks on the RISC OS build under qemu-arm, and a performance
  regression check that counts the instructions Mesa executes per frame
  for glbench's scenes. `tools/gen-glsl-batch.py` regenerates the batch
  shader code from the interpreter (and checks it is up to date).
- **Diagnostic EGL attribute removed:** `EGL_FLIP_FIRST_RISCOS` (0x3FFF),
  added to find out whether switching screen banks before or after the
  vsync wait stops tearing on the Pi 4, is gone: banks tear either way, so
  the swap switches after the wait as before. Also gone: egltest's `-s fv`
  option and `tests/egl-banks-fv`. `EGL_SCREEN_BANKS_RISCOS` is unchanged
  (still experimental).
- **For maintainers:**
  - `build/build-all.sh [VERSION]` builds everything in order, logging
    each step to `stage/logs/`.
  - `tools/mesa-branch.sh` turns the Mesa patches into a git branch (one
    commit per patch) and writes them back, so they're edited as source,
    not as patch files. The patch files are now plain git diffs; the
    patched tree is unchanged, and existing Mesa trees are still
    recognised.
  - `egl/egl_riscos.c` is split into `egl/parts/` (screen, buffers,
    validation, configs, API, extensions), still built as one file; the
    compiled library is unchanged.
  - `tools/sdl-overlay-check.sh` compares the SDL overlay with another
    project's copy.
- **Docs:** `build/TOOLCHAIN.md` says to apply riscos-openttd's UnixLib
  patch (without it C++ programs that set up `std::locale` abort with
  "wctype not implemented"), and the porting guide warns against
  `popen()` and `system()`.

## 20.3.5-6: freeglut, SDL sound

All the GLUT demos run on a Raspberry Pi. The SDL sound driver is tested
on a Linux host; Pi reports on sound are welcome.

- **freeglut 3.8.0 with a native RISC OS back end** (`glut/riscos`,
  `build/build-freeglut.sh`, `patches/freeglut`): `libglut.a` (OpenGL) and
  `libfreeglut-gles.a` (OpenGL ES 1.1/2.0), with `GL/glut.h` and
  `GL/freeglut*.h`. GLUT programs usually build unchanged:
  - GLUT windows are Wimp windows and multitask; subwindows (nested to any
    depth) are EGL work area surfaces inside them;
  - GLUT menus are Wimp menus (Menu opens them, Adjust keeps them open),
    also in the ES build;
  - key releases, special keys (and Shift/Ctrl/Alt on their own), mouse
    motion, entry, the scroll wheel, pointer hiding and warping;
  - single-buffered programs are shown after each display callback and
    while idle or timer callbacks draw;
  - full screen and game mode are borderless windows covering the desktop
    in the current screen mode.
- **Worked port and guide:** freeglut's demos (`!Shapes`, `!One`,
  `!Subwin`, `!Lorenz`, `!Fractals`, `!Resizer`, `!View3D`, `!Keyboard`)
  in the new `riscos-mesa-glut` zip; [docs/porting/glut.md](docs/porting/glut.md).
- **EGL:** work area surfaces in a window now stack in creation order
  (later ones on top), and showing one replots those above it. Before,
  the newest was drawn first, so a surface created inside another was
  hidden by it.
- **SDL2 sound:** a RISC OS audio driver (from riscos-openttd, the shared
  SDL overlay), playing through SharedSoundBuffer/StreamManager so SDL
  programs' sound mixes with other programs'. SDL falls back to its `dsp`
  driver (DigitalRenderer) if the modules aren't there. New `!LoopWave`
  (SDL's loopwave test) in the ports zip.
- **Tests:** the EGL host harness has 265 checks (stacking). Its fake Wimp
  can now click, drag, release, turn the wheel and choose from menus.
  `tests/host-harness/glut/run.sh` drives freeglut's demos (23 checks).

## 20.3.5-5: OpenGL ES, porting aids and porting guides

All tests pass on a Raspberry Pi 4.

- **OpenGL ES 1.1 and 2.0** through the native RISC OS EGL types (Wimp
  window, `EGL_RISCOS_SCREEN_WINDOW`, sprite, pbuffer), and through SDL2
  (`SDL_GL_CONTEXT_PROFILE_ES`, major version 1 or 2).
  - It comes from a small patch that gives OSMesa ES profiles.
  - ES 3.x is refused.
- **EGL:**
  - The initial API is now OpenGL ES, as the spec says. Desktop GL code
    must call `eglBindAPI(EGL_OPENGL_API)` (a change from 20.3.5-4).
  - Every config supports GL, ES 1 and ES 2.
- **Mesa patch:** fragment shaders with no default float precision compile
  (mediump, with a warning), as they did on the Pi's VideoCore compiler.
- **DispmanX compatibility (`libbcm_host`), a porting aid** for existing
  Raspberry Pi 1–3 Khronos code. It isn't the RISC OS API; native EGL
  comes first and is unchanged.
  - DispmanX elements, `EGL_DISPMANX_WINDOW_T`, and empty `libGLESv2` /
    `libvcos` / … so Pi link lines work.
  - Also `eglSaneChooseConfigBRCM` and a minimal `interface/vcos/vcos.h`.
  - **Window mode**, the default in the desktop: the program's "display"
    is a desktop window (640 pixels wide). libEGL plots into it, and the
    library polls the Wimp after each `eglSwapBuffers`, so unchanged Pi
    programs multitask.
  - `<App>$Display` / `DispmanX$Display` choose a size, or `full` for the
    Pi's whole-screen behaviour.
- **The Pi's own examples, rebuilt:** `!HelloTriangle`, `!HelloTriangle2` and
  `!HelloTeapot` (`riscos-mesa-hello_pi` zip).
- **Porting guides** (`docs/porting/`), each with a worked port in `ports/`:
  - Mesa's EGL demos, over a RISC OS back end for eglut: `!EGLGears`,
    `!ES1Gears`, `!ES1Torus`, `!ES2Gears`, `!EGLTri`;
  - the OpenGL ES 2.0 Programming Guide samples (`esUtil_RISCOS.c`; the
    samples are fetched at build time, as they have no licence);
  - SDL 2 GL programs (SDL's `testgl2`, `testgles`, `testgles2`);
  - Raspberry Pi code.

  The Mesa demos and SDL programs are in the new `riscos-mesa-ports` zip.
  `ports/common/riscos_wimpwin.c` is a small Wimp window + loop for your
  own ports.
- **High resolution (EX0 EY0, 180 dpi) desktops:**
  - SDL2 windows are shown 2x, with the mouse scaled to match.
    `SDL$WindowScale` overrides this.
  - EGL window and full screen sprites now match the screen's dpi. They
    were doubled and cropped before.
- **SDL2:** each program's task name, icon bar sprite and menu title come
  from its own application directory (`!App`, or the generic application
  sprite).
  - The global `SDL$IconSprite`, which leaked OpenTTD's icon and name into
    other programs, is no longer read.
  - Every desktop SDL program gets an icon bar icon with a Quit menu.
- **Tests:**
  - New `glestest` (ES on the native types) and `dmxtest` (DispmanX, full
    screen and window mode).
  - The host harness has 262 checks and can run whole Wimp programs from a
    script (`portrun.c`).
- **Licences:** patches keep the licence of the files they change. Ports
  keep their own licences (userland BSD, mesa-demos MIT, SDL zlib).

## 20.3.5-4: EGL extensions

- **Display extensions:**
  - `EGL_KHR_surfaceless_context`;
  - fence, reusable and wait syncs;
  - `EGL_EXT_buffer_age`;
  - swap with damage (KHR and EXT);
  - `EGL_KHR_partial_update`;
  - `EGL_KHR_lock_surface` 1–3;
  - `EGL_KHR_context_flush_control`.
- **Client extensions:** `EGL_EXT_client_extensions`, `EGL_EXT_platform_base`
  with `EGL_RISCOS_platform_wimp`,
  `EGL_KHR_client_get_all_proc_addresses` and `EGL_KHR_debug`.
- **Tests:**
  - `egl-damage`: 507 fps with the middle half shown, against 225 fps for
    the whole window, on a Pi 4.
  - The host harness has 206 checks.
- **Docs:** the EGL guide covers every extension, with worked examples.

## 20.3.5-3: EGL 1.4

- **`libEGL.a`: EGL 1.4 over OSMesa.**
  - The native window is a Wimp window handle, or -1 for the whole screen.
  - Pixmaps are sprites; pbuffers are supported.
  - Work area surfaces and `eglRedrawWindowRISCOS` for the Wimp.
- **Full screen:**
  - By default a sprite is plotted after the vsync wait.
  - Rendering straight into screen memory is optional.
  - Screen banks are opt-in (experimental: they tear on the Pi 4).
- **Window surface sprites** are padded to at least 1 MB: small sprites
  froze on the Pi 4.
- **Tests:** `egltest` and its `egl-*` Obey files. The EGL host harness
  runs against a fake Wimp, screen and SpriteOp.
- **Docs:** the programming guide (`docs/EGL-GUIDE.md`), and `LICENCES.txt`
  with every component's licence.

## 20.3.5-2: stack crash fix, faster build, multitasking SDL

- **Build with `-fstack-clash-protection`:**
  - Mesa's big stack frames were jumping past the ELF stack's guard page,
    which caused crashes that looked random.
  - `tools/check-stack-probes.py` checks a binary for this.
- **Flags:** `-O3 -mtune=cortex-a72 -mfpu=vfpv4`, chosen by benchmark on a
  Pi 4 (NEON gave nothing).
- **SDL driver multitasks:** `SDL_Delay`, `SDL_WaitEvent` and windowed
  vsync yield to the desktop.
- **Tools and tests:** `glbench` and a high resolution timer. Zips store
  RISC OS filetypes (`tools/mkrozip.py`).

## 20.3.5: first release

- **Mesa 20.3.5 classic OSMesa** as one static `libOSMesa.a` (OpenGL 2.1,
  GLSL 1.20), plus GLU 9.0.1.
- **SDL 2.26:** an OpenGL context for its RISC OS driver (overlay shared
  with riscos-openttd). GL windows work as desktop windows and full screen.
- **Build:** cross-built with GCCSDK GCC 10; source downloads are SHA-256
  checked.
- **Tested on a Raspberry Pi 4:** a lit 640x480 cube at about 160 fps in a
  desktop window.
